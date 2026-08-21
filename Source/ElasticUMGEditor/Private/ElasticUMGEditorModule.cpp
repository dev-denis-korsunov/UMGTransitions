#include "EdGraphUtilities.h"
#include "WidgetTransition.h"

#include "Blueprint/WidgetTree.h"
#include "Components/PanelSlot.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphSchema_K2.h"
#include "K2Node_CallFunction.h"
#include "K2Node_VariableGet.h"
#include "KismetPins/SGraphPinString.h"
#include "Styling/AppStyle.h"
#include "WidgetBlueprint.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Text/STextBlock.h"

namespace ElasticUMGEditor
{
	struct FPropertyOption
	{
		FString Label;
		FString Path;
		FLinearColor TypeColor = FLinearColor::White;
		bool bHeader = false;
	};

	static bool IsFloatProperty(const FProperty* Property) { return Property && (Property->IsA<FFloatProperty>() || Property->IsA<FDoubleProperty>()); }
	static bool IsBindableProperty(const FProperty* Property)
	{
		const FStructProperty* Struct = CastField<FStructProperty>(Property);
		return IsFloatProperty(Property) || (Struct && (Struct->Struct == TBaseStructure<FVector2D>::Get() || Struct->Struct == TBaseStructure<FLinearColor>::Get()));
	}
	static bool HasBindableDescendant(const UStruct* Struct, int32 Depth)
	{
		if (!Struct || Depth > 8) return false;
		for (TFieldIterator<FProperty> It(Struct, EFieldIterationFlags::None); It; ++It)
		{
			if (IsBindableProperty(*It)) return true;
			if (const FStructProperty* Nested = CastField<FStructProperty>(*It); Nested && HasBindableDescendant(Nested->Struct, Depth + 1)) return true;
		}
		return false;
	}
	static FLinearColor GetTypeColor(const FProperty* Property)
	{
		FEdGraphPinType PinType;
		const UEdGraphSchema_K2* Schema = GetDefault<UEdGraphSchema_K2>();
		return Property && Schema && Schema->ConvertPropertyToPinType(Property, PinType) ? Schema->GetPinTypeColor(PinType) : FLinearColor::White;
	}
	static void AddProperties(const UStruct* Struct, const FString& Prefix, int32 Depth, TArray<TSharedPtr<FPropertyOption>>& OutOptions)
	{
		if (!Struct || Depth > 8) return;
		for (TFieldIterator<FProperty> It(Struct, EFieldIterationFlags::None); It; ++It)
		{
			const FProperty* Property = *It;
			const FString Path = Prefix + Property->GetName();
			if (IsBindableProperty(Property)) OutOptions.Add(MakeShared<FPropertyOption>(FPropertyOption{ Path, Path, GetTypeColor(Property) }));
			if (const FStructProperty* Nested = CastField<FStructProperty>(Property); Nested && HasBindableDescendant(Nested->Struct, Depth + 1))
			{
				if (!IsBindableProperty(Property)) OutOptions.Add(MakeShared<FPropertyOption>(FPropertyOption{ Path, FString(), FLinearColor::White, true }));
				AddProperties(Nested->Struct, Path + TEXT("."), Depth + 1, OutOptions);
			}
		}
	}
	/** Enumerate inherited UWidget fields explicitly: the per-struct walker intentionally does not include super fields. */
	static void AddProperties(UClass* Class, const FString& Prefix, int32 Depth, TArray<TSharedPtr<FPropertyOption>>& OutOptions)
	{
		TArray<UClass*> Hierarchy;
		for (UClass* Current = Class; Current && Current != UObject::StaticClass(); Current = Current->GetSuperClass())
		{
			Hierarchy.Insert(Current, 0);
		}
		for (UClass* Current : Hierarchy)
		{
			AddProperties(static_cast<const UStruct*>(Current), Prefix, Depth, OutOptions);
		}
	}
	static bool IsBindingPropertyPin(const UEdGraphPin* Pin)
	{
		const UK2Node_CallFunction* Node = Pin ? Cast<UK2Node_CallFunction>(Pin->GetOwningNode()) : nullptr;
		const UFunction* Function = Node ? Node->GetTargetFunction() : nullptr;
		return Function && Function->HasMetaData(TEXT("ElasticUMGTransitionBinding")) && Pin->PinName == TEXT("WidgetProperty");
	}
	static UEdGraphPin* GetWidgetSource(const UEdGraphPin* PropertyPin)
	{
		UEdGraphPin* WidgetPin = PropertyPin && PropertyPin->GetOwningNode() ? PropertyPin->GetOwningNode()->FindPin(TEXT("Widget")) : nullptr;
		return WidgetPin && !WidgetPin->LinkedTo.IsEmpty() ? FEdGraphUtilities::GetNetFromPin(WidgetPin->LinkedTo[0]) : nullptr;
	}
	static UWidget* GetDesignerWidget(const UEdGraphPin* PropertyPin)
	{
		UEdGraphPin* Source = GetWidgetSource(PropertyPin);
		const UK2Node_VariableGet* Get = Source ? Cast<UK2Node_VariableGet>(Source->GetOwningNode()) : nullptr;
		UWidgetBlueprint* Blueprint = Source && Source->GetOwningNode() ? Source->GetOwningNode()->GetTypedOuter<UWidgetBlueprint>() : nullptr;
		return Get && Blueprint && Blueprint->WidgetTree ? Blueprint->WidgetTree->FindWidget(Get->GetVarName()) : nullptr;
	}
	static UClass* GetWidgetClassForPin(const UEdGraphPin* PropertyPin)
	{
		if (UEdGraphPin* Source = GetWidgetSource(PropertyPin)) return Cast<UClass>(Source->PinType.PinSubCategoryObject.Get());
		return UWidget::StaticClass();
	}

	class SWidgetPropertyPathPin final : public SGraphPinString
	{
	public:
		SLATE_BEGIN_ARGS(SWidgetPropertyPathPin) {} SLATE_END_ARGS()
		void Construct(const FArguments&, UEdGraphPin* Pin) { SGraphPinString::Construct(SGraphPinString::FArguments(), Pin); }
	protected:
		virtual TSharedRef<SWidget> GetDefaultValueWidget() override
		{
			RefreshOptions();
			return SNew(SComboBox<TSharedPtr<FPropertyOption>>).OptionsSource(&Options).OnComboBoxOpening(this, &SWidgetPropertyPathPin::RefreshOptions)
				.OnGenerateWidget(this, &SWidgetPropertyPathPin::MakeOption).OnSelectionChanged(this, &SWidgetPropertyPathPin::SelectOption)
				.Content()[SNew(STextBlock).Text(this, &SWidgetPropertyPathPin::GetCurrentValue).Font(FAppStyle::GetFontStyle("PropertyWindow.NormalFont"))];
		}
	private:
		void RefreshOptions()
		{
			Options.Reset();
			AddProperties(GetWidgetClassForPin(GraphPinObj), FString(), 0, Options);
			if (UWidget* Widget = GetDesignerWidget(GraphPinObj); Widget && Widget->Slot)
			{
				Options.Add(MakeShared<FPropertyOption>(FPropertyOption{ TEXT("Slot"), FString(), FLinearColor::White, true }));
				AddProperties(Widget->Slot->GetClass(), TEXT("Slot."), 0, Options);
			}
		}
		TSharedRef<SWidget> MakeOption(TSharedPtr<FPropertyOption> Option) const
		{
			if (!Option.IsValid() || Option->bHeader) return SNew(STextBlock).Text(FText::FromString(Option.IsValid() ? Option->Label : FString())).Font(FAppStyle::GetFontStyle("PropertyWindow.BoldFont"));
			return SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().FillWidth(1.0f)[SNew(STextBlock).Text(FText::FromString(Option->Label))]
				+ SHorizontalBox::Slot().AutoWidth().Padding(8.0f, 0.0f, 2.0f, 0.0f)[SNew(SImage).Image(FAppStyle::GetBrush("Kismet.VariableList.TypeIcon")).ColorAndOpacity(Option->TypeColor)];
		}
		void SelectOption(TSharedPtr<FPropertyOption> Option, ESelectInfo::Type)
		{
			if (!Option.IsValid() || Option->bHeader || GraphPinObj->GetDefaultAsString() == Option->Path) return;
			GraphPinObj->Modify();
			GraphPinObj->GetSchema()->TrySetDefaultValue(*GraphPinObj, Option->Path);
		}
		FText GetCurrentValue() const { const FString Value = GraphPinObj->GetDefaultAsString(); return Value.IsEmpty() ? NSLOCTEXT("ElasticUMG", "SelectWidgetProperty", "Select widget property") : FText::FromString(Value); }
		TArray<TSharedPtr<FPropertyOption>> Options;
	};

	class FTransitionPinFactory final : public FGraphPanelPinFactory
	{
	public:
		virtual TSharedPtr<SGraphPin> CreatePin(UEdGraphPin* Pin) const override
		{
			if (IsBindingPropertyPin(Pin)) return SNew(SWidgetPropertyPathPin, Pin);
			return nullptr;
		}
	};
}

class FElasticUMGEditorModule final : public IModuleInterface
{
public:
	virtual void StartupModule() override { PinFactory = MakeShared<ElasticUMGEditor::FTransitionPinFactory>(); FEdGraphUtilities::RegisterVisualPinFactory(PinFactory); }
	virtual void ShutdownModule() override { if (PinFactory.IsValid()) { FEdGraphUtilities::UnregisterVisualPinFactory(PinFactory); PinFactory.Reset(); } }
private:
	TSharedPtr<FGraphPanelPinFactory> PinFactory;
};

IMPLEMENT_MODULE(FElasticUMGEditorModule, ElasticUMGEditor)
