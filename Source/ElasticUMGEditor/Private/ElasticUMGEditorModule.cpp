#include "EdGraphUtilities.h"
#include "K2Node_CallFunction.h"
#include "K2Node_VariableGet.h"
#include "KismetPins/SGraphPinString.h"
#include "Styling/AppStyle.h"
#include "WidgetBlueprint.h"
#include "WidgetTransition.h"
#include "Blueprint/WidgetTree.h"
#include "Components/PanelSlot.h"
#include "EdGraph/EdGraphPin.h"
#include "ScopedTransaction.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Text/STextBlock.h"

namespace ElasticUMGEditor
{
	static bool IsBindableFloatProperty(const FProperty* Property)
	{
		return Property && Property->IsA<FFloatProperty>();
	}

	static void AddFloatProperties(const UStruct* Struct, const FString& Prefix, int32 Depth, TArray<TSharedPtr<FString>>& OutOptions)
	{
		if (!Struct || Depth > 8)
		{
			return;
		}

		for (TFieldIterator<FProperty> It(Struct, EFieldIterationFlags::IncludeSuper); It; ++It)
		{
			const FProperty* Property = *It;
			const FString PropertyPath = Prefix + Property->GetName();
			if (IsBindableFloatProperty(Property))
			{
				OutOptions.Add(MakeShared<FString>(PropertyPath));
			}
			else if (const FStructProperty* StructProperty = CastField<FStructProperty>(Property))
			{
				AddFloatProperties(StructProperty->Struct, PropertyPath + TEXT("."), Depth + 1, OutOptions);
			}
		}
	}

	static void AddFloatProperties(UClass* Class, const FString& Prefix, TArray<TSharedPtr<FString>>& OutOptions)
	{
		AddFloatProperties(Class, Prefix, 0, OutOptions);
	}

	static UEdGraphPin* GetWidgetSourcePin(const UEdGraphPin* PropertyPathPin)
	{
		if (!PropertyPathPin || !PropertyPathPin->GetOwningNode())
		{
			return nullptr;
		}

		UEdGraphPin* WidgetPin = PropertyPathPin->GetOwningNode()->FindPin(TEXT("Widget"));
		return WidgetPin && !WidgetPin->LinkedTo.IsEmpty() ? FEdGraphUtilities::GetNetFromPin(WidgetPin->LinkedTo[0]) : nullptr;
	}

	static UWidget* GetDesignerWidget(const UEdGraphPin* PropertyPathPin)
	{
		UEdGraphPin* SourcePin = GetWidgetSourcePin(PropertyPathPin);
		const UK2Node_VariableGet* VariableGet = SourcePin ? Cast<UK2Node_VariableGet>(SourcePin->GetOwningNode()) : nullptr;
		UWidgetBlueprint* WidgetBlueprint = SourcePin && SourcePin->GetOwningNode()
			? SourcePin->GetOwningNode()->GetTypedOuter<UWidgetBlueprint>()
			: nullptr;
		return VariableGet && WidgetBlueprint && WidgetBlueprint->WidgetTree
			? WidgetBlueprint->WidgetTree->FindWidget(VariableGet->GetVarName())
			: nullptr;
	}

	static UClass* GetWidgetClass(const UEdGraphPin* PropertyPathPin)
	{
		if (UEdGraphPin* SourcePin = GetWidgetSourcePin(PropertyPathPin))
		{
			return Cast<UClass>(SourcePin->PinType.PinSubCategoryObject.Get());
		}

		return UWidget::StaticClass();
	}

	class SWidgetPropertyPathGraphPin final : public SGraphPinString
	{
	public:
		SLATE_BEGIN_ARGS(SWidgetPropertyPathGraphPin) {}
		SLATE_END_ARGS()

		void Construct(const FArguments& InArgs, UEdGraphPin* InGraphPinObj)
		{
			SGraphPinString::Construct(SGraphPinString::FArguments(), InGraphPinObj);
		}

	protected:
		virtual TSharedRef<SWidget> GetDefaultValueWidget() override
		{
			RefreshOptions();
			return SNew(SComboBox<TSharedPtr<FString>>)
				.OptionsSource(&Options)
				.OnComboBoxOpening(this, &SWidgetPropertyPathGraphPin::RefreshOptions)
				.OnGenerateWidget(this, &SWidgetPropertyPathGraphPin::MakeOptionWidget)
				.OnSelectionChanged(this, &SWidgetPropertyPathGraphPin::SelectOption)
				.IsEnabled(this, &SGraphPin::GetDefaultValueIsEditable)
				.Content()
				[
					SNew(STextBlock)
					.Text(this, &SWidgetPropertyPathGraphPin::GetCurrentValue)
					.Font(FAppStyle::GetFontStyle("PropertyWindow.NormalFont"))
				];
		}

	private:
		void RefreshOptions()
		{
			Options.Reset();
			AddFloatProperties(ElasticUMGEditor::GetWidgetClass(GraphPinObj), FString(), Options);

			if (UWidget* DesignerWidget = GetDesignerWidget(GraphPinObj); DesignerWidget && DesignerWidget->Slot)
			{
				AddFloatProperties(DesignerWidget->Slot->GetClass(), TEXT("Slot."), Options);
			}

			Options.Sort([](const TSharedPtr<FString>& Left, const TSharedPtr<FString>& Right)
			{
				return *Left < *Right;
			});
		}

		TSharedRef<SWidget> MakeOptionWidget(TSharedPtr<FString> Option) const
		{
			return SNew(STextBlock)
				.Text(FText::FromString(Option.IsValid() ? *Option : FString()))
				.Font(FAppStyle::GetFontStyle("PropertyWindow.NormalFont"));
		}

		void SelectOption(TSharedPtr<FString> Option, ESelectInfo::Type)
		{
			if (!Option.IsValid() || GraphPinObj->GetDefaultAsString() == *Option)
			{
				return;
			}

			const FScopedTransaction Transaction(NSLOCTEXT("ElasticUMG", "SetWidgetPropertyPath", "Set Widget Property Path"));
			GraphPinObj->Modify();
			GraphPinObj->GetSchema()->TrySetDefaultValue(*GraphPinObj, *Option);
		}

		FText GetCurrentValue() const
		{
			const FString Value = GraphPinObj->GetDefaultAsString();
			return Value.IsEmpty() ? NSLOCTEXT("ElasticUMG", "SelectWidgetProperty", "Select widget property") : FText::FromString(Value);
		}

		TArray<TSharedPtr<FString>> Options;
	};

	class FWidgetPropertyPathPinFactory final : public FGraphPanelPinFactory
	{
	public:
		virtual TSharedPtr<SGraphPin> CreatePin(UEdGraphPin* Pin) const override
		{
			const UK2Node_CallFunction* CallNode = Pin ? Cast<UK2Node_CallFunction>(Pin->GetOwningNode()) : nullptr;
			const UFunction* Function = CallNode ? CallNode->GetTargetFunction() : nullptr;
			const bool bIsWidgetPropertyBinding = Function
				&& Function->GetOuterUClass() == UWidgetTransitionFunctionLibrary::StaticClass()
				&& Function->GetFName() == GET_FUNCTION_NAME_CHECKED(UWidgetTransitionFunctionLibrary, BindWidgetProperty);

			if (bIsWidgetPropertyBinding && Pin->PinName == TEXT("InPropertyPath"))
			{
				return SNew(SWidgetPropertyPathGraphPin, Pin);
			}

			return nullptr;
		}
	};
}

class FElasticUMGEditorModule final : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		PinFactory = MakeShared<ElasticUMGEditor::FWidgetPropertyPathPinFactory>();
		FEdGraphUtilities::RegisterVisualPinFactory(PinFactory);
	}

	virtual void ShutdownModule() override
	{
		if (PinFactory.IsValid())
		{
			FEdGraphUtilities::UnregisterVisualPinFactory(PinFactory);
			PinFactory.Reset();
		}
	}

private:
	TSharedPtr<FGraphPanelPinFactory> PinFactory;
};

IMPLEMENT_MODULE(FElasticUMGEditorModule, ElasticUMGEditor)
