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
#include "Widgets/Input/SNumericEntryBox.h"
#include "Widgets/Text/STextBlock.h"

namespace ElasticUMGEditor
{
	struct FWidgetPropertyPickerOption
	{
		FString Label;
		FString PropertyPath;
		bool bIsHeader = false;
	};

	struct FWidgetPropertyPath
	{
		FString Path;
		int32 SortOrder = 0;
	};

	struct FWidgetPropertyPickerGroup
	{
		int32 SortOrder = MAX_int32;
		TArray<FWidgetPropertyPath> Properties;
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

	static bool IsBindableTypedProperty(const FProperty* Property)
	{
		const FStructProperty* StructProperty = CastField<FStructProperty>(Property);
		return IsBindableFloatProperty(Property)
			|| Property->IsA<FBoolProperty>()
			|| (StructProperty && (StructProperty->Struct == TBaseStructure<FVector2D>::Get() || StructProperty->Struct == TBaseStructure<FLinearColor>::Get()));
	}

	static void AddFloatProperties(const UStruct* Struct, const FString& Prefix, int32 Depth, int32& InOutSortOrder, TArray<FWidgetPropertyPath>& OutPropertyPaths, bool bIncludeTypedProperties)
	{
		if (!Struct || Depth > 8)
		{
			return;
		}

		for (TFieldIterator<FProperty> It(Struct, EFieldIterationFlags::None); It; ++It)
		{
			const FProperty* Property = *It;
			const FString PropertyPath = Prefix + Property->GetName();
			if (IsBindableFloatProperty(Property) || (bIncludeTypedProperties && IsBindableTypedProperty(Property)))
			{
				OutPropertyPaths.Add(FWidgetPropertyPath{ PropertyPath, InOutSortOrder++ });
			}
			else if (const FStructProperty* StructProperty = CastField<FStructProperty>(Property))
			{
				AddFloatProperties(StructProperty->Struct, PropertyPath + TEXT("."), Depth + 1, InOutSortOrder, OutPropertyPaths, bIncludeTypedProperties);
			}
		}
	}

	static void AddFloatProperties(UClass* Class, const FString& Prefix, TArray<FWidgetPropertyPath>& OutPropertyPaths, bool bIncludeTypedProperties)
	{
		TArray<UClass*> ClassHierarchy;
		for (UClass* CurrentClass = Class; CurrentClass && CurrentClass != UObject::StaticClass(); CurrentClass = CurrentClass->GetSuperClass())
		{
			ClassHierarchy.Insert(CurrentClass, 0);
		}

		int32 SortOrder = 0;
		for (UClass* CurrentClass : ClassHierarchy)
		{
			AddFloatProperties(CurrentClass, Prefix, 0, SortOrder, OutPropertyPaths, bIncludeTypedProperties);
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
			if (!Property)
			{
				break;
			}

			if (Index + 1 < Segments.Num())
			{
				const FStructProperty* StructProperty = CastField<FStructProperty>(Property);
				CurrentStruct = StructProperty ? StructProperty->Struct : nullptr;
				continue;
			}

			if (IsBindableFloatProperty(Property))
			{
				return EWidgetTransitionValueType::Float;
			}
			if (Property->IsA<FBoolProperty>())
			{
				return EWidgetTransitionValueType::Bool;
			}
			if (const FStructProperty* StructProperty = CastField<FStructProperty>(Property))
			{
				if (StructProperty->Struct == TBaseStructure<FVector2D>::Get())
				{
					return EWidgetTransitionValueType::Vector2D;
				}
				if (StructProperty->Struct == TBaseStructure<FLinearColor>::Get())
				{
					return EWidgetTransitionValueType::LinearColor;
				}
			}
		}

		return EWidgetTransitionValueType::Float;
	}

	static int32 GetPropertySortOrder(const FWidgetPropertyPath& Property)
	{
		if (Property.Path.StartsWith(TEXT("RenderTransform.")))
		{
			return 0;
		}
		if (Property.Path == TEXT("RenderOpacity"))
		{
			return 1;
		}
		if (Property.Path.StartsWith(TEXT("RenderTransformPivot.")))
		{
			return 2;
		}

		return 100 + Property.SortOrder;
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
			TArray<FWidgetPropertyPath> PropertyPaths;
			const bool bIncludeTypedProperties = IsTypedTransitionNode(GraphPinObj);
			AddFloatProperties(ElasticUMGEditor::GetWidgetClass(GraphPinObj), FString(), PropertyPaths, bIncludeTypedProperties);

			if (UWidget* DesignerWidget = GetDesignerWidget(GraphPinObj); DesignerWidget && DesignerWidget->Slot)
			{
				AddFloatProperties(DesignerWidget->Slot->GetClass(), TEXT("Slot."), PropertyPaths, bIncludeTypedProperties);
			}

			TMap<FString, FWidgetPropertyPickerGroup> GroupedPaths;
			for (const FWidgetPropertyPath& Property : PropertyPaths)
			{
				FString Group;
				FString PropertyLabel = Property.Path;
				if (Property.Path.Split(TEXT("."), &Group, &PropertyLabel))
				{
					// Keep the path relative to the group in the picker, but store the full path.
				}
				else
				{
					Group = TEXT("Widget");
				}

				FWidgetPropertyPickerGroup& GroupedProperties = GroupedPaths.FindOrAdd(Group);
				const int32 SortOrder = GetPropertySortOrder(Property);
				GroupedProperties.SortOrder = FMath::Min(GroupedProperties.SortOrder, SortOrder);
				GroupedProperties.Properties.Add(FWidgetPropertyPath{ Property.Path, SortOrder });
			}

			TArray<FString> GroupNames;
			GroupedPaths.GetKeys(GroupNames);
			GroupNames.Sort([&GroupedPaths](const FString& Left, const FString& Right)
			{
				const int32 LeftOrder = GroupedPaths.FindChecked(Left).SortOrder;
				const int32 RightOrder = GroupedPaths.FindChecked(Right).SortOrder;
				return LeftOrder == RightOrder ? Left < Right : LeftOrder < RightOrder;
			});
			for (const FString& Group : GroupNames)
			{
				const FString GroupLabel = FName::NameToDisplayString(Group, false);
				Options.Add(MakeShared<FWidgetPropertyPickerOption>(FWidgetPropertyPickerOption{ GroupLabel, FString(), true }));
				TArray<FWidgetPropertyPath>& GroupPaths = GroupedPaths.FindChecked(Group).Properties;
				GroupPaths.Sort([](const FWidgetPropertyPath& Left, const FWidgetPropertyPath& Right)
				{
					return Left.SortOrder == Right.SortOrder ? Left.Path < Right.Path : Left.SortOrder < Right.SortOrder;
				});
				for (const FWidgetPropertyPath& Property : GroupPaths)
				{
					const FString PropertyLabel = Property.Path.RightChop(Group == TEXT("Widget") ? 0 : Group.Len() + 1);
					Options.Add(MakeShared<FWidgetPropertyPickerOption>(FWidgetPropertyPickerOption{ PropertyLabel, Property.Path, false }));
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

	class FWidgetPropertyPathPinFactory final : public FGraphPanelPinFactory
	{
	public:
		virtual TSharedPtr<SGraphPin> CreatePin(UEdGraphPin* Pin) const override
		{
			const bool bIsTypedTransitionNode = IsTypedTransitionNode(Pin);
			const UK2Node_CallFunction* CallNode = Pin ? Cast<UK2Node_CallFunction>(Pin->GetOwningNode()) : nullptr;
			const UFunction* Function = CallNode ? CallNode->GetTargetFunction() : nullptr;
			const bool bIsCreateWidgetTransition = Function
				&& Function->GetOuterUClass() == UWidgetTransitionFunctionLibrary::StaticClass()
				&& Function->GetFName() == GET_FUNCTION_NAME_CHECKED(UWidgetTransitionFunctionLibrary, CreateWidgetTransition);

			if ((bIsCreateWidgetTransition || bIsTypedTransitionNode) && Pin->PinName == TEXT("WidgetProperty"))
			{
				return SNew(SWidgetPropertyPathGraphPin, Pin);
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
