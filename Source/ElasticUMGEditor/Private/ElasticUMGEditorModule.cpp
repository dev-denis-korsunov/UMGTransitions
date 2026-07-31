#include "EdGraphUtilities.h"
#include "K2Node_CallFunction.h"
#include "K2Node_WidgetTransition.h"
#include "K2Node_VariableGet.h"
#include "EdGraphSchema_K2.h"
#include "KismetPins/SGraphPinNum.h"
#include "KismetPins/SGraphPinString.h"
#include "Styling/AppStyle.h"
#include "WidgetBlueprint.h"
#include "WidgetTransition.h"
#include "Blueprint/WidgetTree.h"
#include "Components/PanelSlot.h"
#include "EdGraph/EdGraphPin.h"
#include "ScopedTransaction.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SNumericEntryBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

namespace ElasticUMGEditor
{
	struct FWidgetPropertyPickerOption
	{
		FString Label;
		FString PropertyPath;
		bool bIsHeader = false;
		int32 Depth = 0;
	};

	struct FTransitionFloatSliderConfig
	{
		float MinSliderValue;
		float MaxSliderValue;
		float Delta;
	};

	static TOptional<FTransitionFloatSliderConfig> GetTransitionFloatSliderConfig(FName PinName)
	{
		if (PinName == TEXT("Time") || PinName == TEXT("Delay"))
		{
			return FTransitionFloatSliderConfig{ 0.0f, 2.0f, 0.05f };
		}
		if (PinName == TEXT("TargetValue") || PinName == TEXT("FromValue"))
		{
			return FTransitionFloatSliderConfig{ -100.0f, 100.0f, 0.1f };
		}
		if (PinName == TEXT("SpringFactor"))
		{
			return FTransitionFloatSliderConfig{ 0.0f, 500.0f, 1.0f };
		}
		if (PinName == TEXT("DampingFactor"))
		{
			return FTransitionFloatSliderConfig{ 0.0f, 100.0f, 0.1f };
		}
		if (PinName == TEXT("MaxVelocity"))
		{
			return FTransitionFloatSliderConfig{ 0.0f, 5000.0f, 10.0f };
		}
		if (PinName == TEXT("CompleteTolerance"))
		{
			return FTransitionFloatSliderConfig{ 0.0001f, 1.0f, 0.001f };
		}

		return {};
	}

	static bool IsBindableFloatProperty(const FProperty* Property)
	{
		return Property && (Property->IsA<FFloatProperty>() || Property->IsA<FDoubleProperty>());
	}

	static bool IsTypedTransitionNode(const UEdGraphPin* Pin)
	{
		return Pin && Cast<UK2Node_WidgetTransition>(Pin->GetOwningNode());
	}

	static UWidget* GetDesignerWidget(const UEdGraphPin* PropertyPathPin);
	static UClass* GetWidgetClass(const UEdGraphPin* PropertyPathPin);

	static bool IsTransitionBindableProperty(const FProperty* Property)
	{
		const FStructProperty* StructProperty = CastField<FStructProperty>(Property);
		return IsBindableFloatProperty(Property)
			|| (Property && Property->IsA<FBoolProperty>())
			|| (StructProperty && (StructProperty->Struct == TBaseStructure<FVector2D>::Get() || StructProperty->Struct == TBaseStructure<FLinearColor>::Get()));
	}

	static bool HasTransitionBindableDescendant(const UStruct* Struct, int32 Depth)
	{
		if (!Struct || Depth > 8) return false;
		for (TFieldIterator<FProperty> It(Struct, EFieldIterationFlags::None); It; ++It)
		{
			const FProperty* Property = *It;
			if (IsTransitionBindableProperty(Property)) return true;
			if (const FStructProperty* StructProperty = CastField<FStructProperty>(Property))
			{
				if (HasTransitionBindableDescendant(StructProperty->Struct, Depth + 1)) return true;
			}
		}
		return false;
	}

	static void AddBindableProperties(const UStruct* Struct, const FString& Prefix, int32 Depth, TArray<TSharedPtr<FWidgetPropertyPickerOption>>& OutOptions)
	{
		if (!Struct || Depth > 8) return;
		for (TFieldIterator<FProperty> It(Struct, EFieldIterationFlags::None); It; ++It)
		{
			const FProperty* Property = *It;
			const FString PropertyPath = Prefix + Property->GetName();
			const bool bIsBindable = IsTransitionBindableProperty(Property);
			const FStructProperty* StructProperty = CastField<FStructProperty>(Property);
			const bool bHasBindableChildren = StructProperty && HasTransitionBindableDescendant(StructProperty->Struct, Depth + 1);
			const FString DisplayName = FName::NameToDisplayString(Property->GetFName().ToString(), false);

			// A supported struct (e.g. Scale: FVector2D) remains selectable, and its
			// components are shown directly below it.
			if (bIsBindable)
			{
				OutOptions.Add(MakeShared<FWidgetPropertyPickerOption>(FWidgetPropertyPickerOption{ DisplayName, PropertyPath, false, Depth }));
			}
			if (bHasBindableChildren)
			{
				if (!bIsBindable)
				{
					OutOptions.Add(MakeShared<FWidgetPropertyPickerOption>(FWidgetPropertyPickerOption{ DisplayName, FString(), true, Depth }));
				}
				AddBindableProperties(StructProperty->Struct, PropertyPath + TEXT("."), Depth + 1, OutOptions);
			}
		}
	}

	static void AddBindableProperties(UClass* Class, const FString& Prefix, int32 Depth, TArray<TSharedPtr<FWidgetPropertyPickerOption>>& OutOptions)
	{
		TArray<UClass*> ClassHierarchy;
		for (UClass* CurrentClass = Class; CurrentClass && CurrentClass != UObject::StaticClass(); CurrentClass = CurrentClass->GetSuperClass())
		{
			ClassHierarchy.Insert(CurrentClass, 0);
		}

		for (UClass* CurrentClass : ClassHierarchy)
		{
			AddBindableProperties(static_cast<const UStruct*>(CurrentClass), Prefix, Depth, OutOptions);
		}
	}

	static EWidgetTransitionValueType GetPropertyValueType(const UEdGraphPin* PropertyPathPin, const FString& PropertyPath)
	{
		const UWidget* DesignerWidget = GetDesignerWidget(PropertyPathPin);
		const UStruct* CurrentStruct = GetWidgetClass(PropertyPathPin);
		FString RelativePath = PropertyPath;
		if (PropertyPath.StartsWith(TEXT("Slot.")))
		{
			CurrentStruct = DesignerWidget && DesignerWidget->Slot ? DesignerWidget->Slot->GetClass() : nullptr;
			RelativePath = PropertyPath.RightChop(5);
		}

		TArray<FString> Segments;
		RelativePath.ParseIntoArray(Segments, TEXT("."), true);
		for (int32 Index = 0; CurrentStruct && Index < Segments.Num(); ++Index)
		{
			const FProperty* Property = FindFProperty<FProperty>(CurrentStruct, *Segments[Index]);
			if (!Property) break;
			if (Index + 1 < Segments.Num())
			{
				const FStructProperty* StructProperty = CastField<FStructProperty>(Property);
				CurrentStruct = StructProperty ? StructProperty->Struct : nullptr;
				continue;
			}
			if (IsBindableFloatProperty(Property)) return EWidgetTransitionValueType::Float;
			if (Property->IsA<FBoolProperty>()) return EWidgetTransitionValueType::Bool;
			if (const FStructProperty* StructProperty = CastField<FStructProperty>(Property))
			{
				if (StructProperty->Struct == TBaseStructure<FVector2D>::Get()) return EWidgetTransitionValueType::Vector2D;
				if (StructProperty->Struct == TBaseStructure<FLinearColor>::Get()) return EWidgetTransitionValueType::LinearColor;
			}
		}
		return EWidgetTransitionValueType::Float;
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
			const UK2Node_WidgetTransition* TransitionNode = Cast<UK2Node_WidgetTransition>(GraphPinObj->GetOwningNode());
			if (!TransitionNode)
			{
				return;
			}
			AddBindableProperties(ElasticUMGEditor::GetWidgetClass(GraphPinObj), FString(), 0, Options);
			if (UWidget* DesignerWidget = GetDesignerWidget(GraphPinObj); DesignerWidget && DesignerWidget->Slot)
			{
				Options.Add(MakeShared<FWidgetPropertyPickerOption>(FWidgetPropertyPickerOption{ TEXT("Slot"), FString(), true, 0 }));
				AddBindableProperties(DesignerWidget->Slot->GetClass(), TEXT("Slot."), 1, Options);
			}
		}

		TSharedRef<SWidget> MakeOptionWidget(TSharedPtr<FWidgetPropertyPickerOption> Option) const
		{
			return SNew(STextBlock)
				.Text(FText::FromString(Option.IsValid() ? Option->Label : FString()))
				.Margin(Option.IsValid() ? FMargin(12.0f * Option->Depth, 0.0f, 0.0f, 0.0f) : FMargin(0.0f))
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
			if (UK2Node_WidgetTransition* TransitionNode = Cast<UK2Node_WidgetTransition>(GraphPinObj->GetOwningNode()))
			{
				TransitionNode->SetValueType(GetPropertyValueType(GraphPinObj, Option->PropertyPath));
			}
		}

		FText GetCurrentValue() const
		{
			const FString Value = GraphPinObj->GetDefaultAsString();
			return Value.IsEmpty() ? NSLOCTEXT("ElasticUMG", "SelectWidgetProperty", "Select widget property") : FText::FromString(Value);
		}

		TArray<TSharedPtr<FWidgetPropertyPickerOption>> Options;
	};

	class SWidgetTransitionFloatGraphPin final : public SGraphPin
	{
	public:
		SLATE_BEGIN_ARGS(SWidgetTransitionFloatGraphPin) {}
		SLATE_END_ARGS()

		void Construct(const FArguments& InArgs, UEdGraphPin* InGraphPinObj, FTransitionFloatSliderConfig InConfig)
		{
			Config = InConfig;
			SGraphPin::Construct(SGraphPin::FArguments(), InGraphPinObj);
		}

	protected:
		virtual TSharedRef<SWidget> GetDefaultValueWidget() override
		{
			return SNew(SBox)
				.MinDesiredWidth(94.0f)
				.MaxDesiredWidth(400.0f)
				[
					SNew(SNumericEntryBox<float>)
					.EditableTextBoxStyle(FAppStyle::Get(), "Graph.EditableTextBox")
					.BorderForegroundColor(FSlateColor::UseForeground())
					.Visibility(this, &SGraphPin::GetDefaultValueVisibility)
					.IsEnabled(this, &SGraphPin::GetDefaultValueIsEditable)
					.Value(this, &SWidgetTransitionFloatGraphPin::GetValue)
					.AllowSpin(true)
					.MinSliderValue(Config.MinSliderValue)
					.MaxSliderValue(Config.MaxSliderValue)
					.Delta(Config.Delta)
					.OnBeginSliderMovement(this, &SWidgetTransitionFloatGraphPin::BeginSliderTransaction)
					.OnValueChanged(this, &SWidgetTransitionFloatGraphPin::SetSliderValue)
					.OnEndSliderMovement(this, &SWidgetTransitionFloatGraphPin::EndSliderTransaction)
					.OnValueCommitted(this, &SWidgetTransitionFloatGraphPin::CommitValue)
				];
		}

	private:
		TOptional<float> GetValue() const
		{
			float Value = 0.0f;
			LexFromString(Value, *GraphPinObj->GetDefaultAsString());
			return Value;
		}

		void SetDefaultValue(float Value)
		{
			if (!FMath::IsNearlyEqual(GetValue().GetValue(), Value))
			{
				GraphPinObj->GetSchema()->TrySetDefaultValue(*GraphPinObj, LexToString(Value));
			}
		}

		void BeginSliderTransaction()
		{
			SliderTransaction = MakeUnique<FScopedTransaction>(NSLOCTEXT("ElasticUMG", "ChangeTransitionFloat", "Change Transition Value"));
			GraphPinObj->Modify();
		}

		void SetSliderValue(float Value)
		{
			if (SliderTransaction.IsValid())
			{
				SetDefaultValue(Value);
			}
		}

		void EndSliderTransaction(float Value)
		{
			SetSliderValue(Value);
			SliderTransaction.Reset();
		}

		void CommitValue(float Value, ETextCommit::Type)
		{
			if (SliderTransaction.IsValid())
			{
				return;
			}

			const FScopedTransaction Transaction(NSLOCTEXT("ElasticUMG", "CommitTransitionFloat", "Set Transition Value"));
			GraphPinObj->Modify();
			SetDefaultValue(Value);
		}

		FTransitionFloatSliderConfig Config{ 0.0f, 1.0f, 0.1f };
		TUniquePtr<FScopedTransaction> SliderTransaction;
	};

	class SWidgetTransitionOptionalPinsGraphPin final : public SGraphPin
	{
	public:
		SLATE_BEGIN_ARGS(SWidgetTransitionOptionalPinsGraphPin) {}
		SLATE_END_ARGS()

		void Construct(const FArguments& InArgs, UEdGraphPin* InGraphPinObj)
		{
			SGraphPin::Construct(SGraphPin::FArguments(), InGraphPinObj);
		}

	protected:
		virtual TSharedRef<SWidget> GetDefaultValueWidget() override
		{
			TSharedRef<SHorizontalBox> Buttons = SNew(SHorizontalBox);
			for (const FPinOption& Option : GetOptions())
			{
				Buttons->AddSlot().AutoWidth().Padding(0.0f)
				[
					SNew(SCheckBox)
					.Style(FAppStyle::Get(), "ToggleButtonCheckbox")
					.IsChecked(this, &SWidgetTransitionOptionalPinsGraphPin::GetOptionState, Option.Pin)
					.OnCheckStateChanged(this, &SWidgetTransitionOptionalPinsGraphPin::SetOptionState, Option.Pin)
					.Padding(FMargin(3.0f, 1.0f))
					[
						SNew(STextBlock).Text(FText::FromString(Option.Label))
					]
				];
			}
			return Buttons;
		}

	private:
		struct FPinOption { EWidgetTransitionOptionalPin Pin; const TCHAR* Label; };

		static const TArray<FPinOption>& GetOptions()
		{
			static const TArray<FPinOption> Options =
			{
				{ EWidgetTransitionOptionalPin::From, TEXT("Fr") },
				{ EWidgetTransitionOptionalPin::Time, TEXT("Tm") },
				{ EWidgetTransitionOptionalPin::Delay, TEXT("Dl") },
				{ EWidgetTransitionOptionalPin::Repeat, TEXT("Rp") },
				{ EWidgetTransitionOptionalPin::YoYo, TEXT("Yo") },
				{ EWidgetTransitionOptionalPin::RemoveFromParent, TEXT("Rm") },
				{ EWidgetTransitionOptionalPin::Spring, TEXT("Sp") },
				{ EWidgetTransitionOptionalPin::OnUpdate, TEXT("Up") },
			};
			return Options;
		}

		ECheckBoxState GetOptionState(EWidgetTransitionOptionalPin Option) const
		{
			if (!GraphPinObj) return ECheckBoxState::Unchecked;
			const UK2Node_WidgetTransition* Node = Cast<UK2Node_WidgetTransition>(GraphPinObj->GetOwningNodeUnchecked());
			return Node && Node->IsOptionalPinVisible(Option) ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
		}

		void SetOptionState(ECheckBoxState State, EWidgetTransitionOptionalPin Option)
		{
			if (!GraphPinObj) return;
			if (UK2Node_WidgetTransition* Node = Cast<UK2Node_WidgetTransition>(GraphPinObj->GetOwningNodeUnchecked()))
			{
				const FScopedTransaction Transaction(NSLOCTEXT("ElasticUMG", "ToggleTransitionOptionalPin", "Toggle Transition Optional Pin"));
				const int32 NewOptionalPins = State == ECheckBoxState::Checked
					? Node->GetOptionalPins() | static_cast<int32>(Option)
					: Node->GetOptionalPins() & ~static_cast<int32>(Option);
				Node->SetOptionalPins(NewOptionalPins);
			}
		}
	};

	class FWidgetPropertyPathPinFactory final : public FGraphPanelPinFactory
	{
	public:
		virtual TSharedPtr<SGraphPin> CreatePin(UEdGraphPin* Pin) const override
		{
			const bool bIsTypedTransitionNode = IsTypedTransitionNode(Pin);
			const UK2Node_CallFunction* CallNode = Pin ? Cast<UK2Node_CallFunction>(Pin->GetOwningNode()) : nullptr;
			const UFunction* Function = CallNode ? CallNode->GetTargetFunction() : nullptr;
			if (bIsTypedTransitionNode && Pin->PinName == TEXT("WidgetProperty"))
			{
				return SNew(SWidgetPropertyPathGraphPin, Pin);
			}
			if (bIsTypedTransitionNode && Pin->PinName == TEXT("OptionalPins"))
			{
				return SNew(SWidgetTransitionOptionalPinsGraphPin, Pin);
			}

			const bool bIsTransitionFunction = (Function
				&& Function->GetOuterUClass() == UWidgetTransitionFunctionLibrary::StaticClass()
				&& Pin->Direction == EGPD_Input
				&& Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Real
				&& Pin->PinType.PinSubCategory == UEdGraphSchema_K2::PC_Float)
				|| (bIsTypedTransitionNode
					&& Pin->Direction == EGPD_Input
					&& Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Real
					&& Pin->PinType.PinSubCategory == UEdGraphSchema_K2::PC_Float);
			if (bIsTransitionFunction)
			{
				if (const TOptional<FTransitionFloatSliderConfig> Config = GetTransitionFloatSliderConfig(Pin->PinName))
				{
					return SNew(SWidgetTransitionFloatGraphPin, Pin, Config.GetValue());
				}
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
