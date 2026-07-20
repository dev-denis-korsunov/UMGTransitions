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
	struct FWidgetPropertyPickerOption
	{
		FString Label;
		FString PropertyPath;
		bool bIsHeader = false;
	};

	static bool IsBindableFloatProperty(const FProperty* Property)
	{
		return Property && (Property->IsA<FFloatProperty>() || Property->IsA<FDoubleProperty>());
	}

	static void AddFloatProperties(const UStruct* Struct, const FString& Prefix, int32 Depth, TArray<FString>& OutPropertyPaths)
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
				OutPropertyPaths.Add(PropertyPath);
			}
			else if (const FStructProperty* StructProperty = CastField<FStructProperty>(Property))
			{
				AddFloatProperties(StructProperty->Struct, PropertyPath + TEXT("."), Depth + 1, OutPropertyPaths);
			}
		}
	}

	static void AddFloatProperties(UClass* Class, const FString& Prefix, TArray<FString>& OutPropertyPaths)
	{
		AddFloatProperties(Class, Prefix, 0, OutPropertyPaths);
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
			return SNew(SComboBox<TSharedPtr<FWidgetPropertyPickerOption>>)
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
			TArray<FString> PropertyPaths;
			AddFloatProperties(ElasticUMGEditor::GetWidgetClass(GraphPinObj), FString(), PropertyPaths);

			if (UWidget* DesignerWidget = GetDesignerWidget(GraphPinObj); DesignerWidget && DesignerWidget->Slot)
			{
				AddFloatProperties(DesignerWidget->Slot->GetClass(), TEXT("Slot."), PropertyPaths);
			}

			TMap<FString, TArray<FString>> GroupedPaths;
			for (const FString& PropertyPath : PropertyPaths)
			{
				FString Group;
				FString PropertyLabel = PropertyPath;
				if (PropertyPath.Split(TEXT("."), &Group, &PropertyLabel))
				{
					// Keep the path relative to the group in the picker, but store the full path.
				}
				else
				{
					Group = TEXT("Widget");
				}

				GroupedPaths.FindOrAdd(Group).Add(PropertyPath);
			}

			TArray<FString> GroupNames;
			GroupedPaths.GetKeys(GroupNames);
			GroupNames.Sort();
			for (const FString& Group : GroupNames)
			{
				const FString GroupLabel = FName::NameToDisplayString(Group, false);
				Options.Add(MakeShared<FWidgetPropertyPickerOption>(FWidgetPropertyPickerOption{ GroupLabel, FString(), true }));
				TArray<FString>& GroupPaths = GroupedPaths.FindChecked(Group);
				GroupPaths.Sort();
				for (const FString& PropertyPath : GroupPaths)
				{
					const FString PropertyLabel = PropertyPath.RightChop(Group == TEXT("Widget") ? 0 : Group.Len() + 1);
					Options.Add(MakeShared<FWidgetPropertyPickerOption>(FWidgetPropertyPickerOption{ PropertyLabel, PropertyPath, false }));
				}
			}
		}

		TSharedRef<SWidget> MakeOptionWidget(TSharedPtr<FWidgetPropertyPickerOption> Option) const
		{
			return SNew(STextBlock)
				.Text(FText::FromString(Option.IsValid() ? Option->Label : FString()))
				.Margin(Option.IsValid() && !Option->bIsHeader ? FMargin(12.0f, 0.0f, 0.0f, 0.0f) : FMargin(0.0f))
				.Font(FAppStyle::GetFontStyle(Option.IsValid() && Option->bIsHeader ? "PropertyWindow.BoldFont" : "PropertyWindow.NormalFont"));
		}

		void SelectOption(TSharedPtr<FWidgetPropertyPickerOption> Option, ESelectInfo::Type)
		{
			if (!Option.IsValid() || Option->bIsHeader || GraphPinObj->GetDefaultAsString() == Option->PropertyPath)
			{
				return;
			}

			const FScopedTransaction Transaction(NSLOCTEXT("ElasticUMG", "SetWidgetPropertyPath", "Set Widget Property Path"));
			GraphPinObj->Modify();
			GraphPinObj->GetSchema()->TrySetDefaultValue(*GraphPinObj, Option->PropertyPath);
		}

		FText GetCurrentValue() const
		{
			const FString Value = GraphPinObj->GetDefaultAsString();
			return Value.IsEmpty() ? NSLOCTEXT("ElasticUMG", "SelectWidgetProperty", "Select widget property") : FText::FromString(Value);
		}

		TArray<TSharedPtr<FWidgetPropertyPickerOption>> Options;
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
