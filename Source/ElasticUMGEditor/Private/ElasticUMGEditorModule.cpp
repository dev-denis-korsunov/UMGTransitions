#include "EdGraphUtilities.h"
#include "K2Node_WidgetTransition.h"
#include "WidgetTransitionSettings.h"
#include "K2Node_VariableGet.h"
#include "EdGraphSchema_K2.h"
#include "KismetPins/SGraphPinString.h"
#include "KismetNodes/SGraphNodeK2Default.h"
#include "Styling/AppStyle.h"
#include "WidgetBlueprint.h"
#include "Blueprint/WidgetTree.h"
#include "Components/PanelSlot.h"
#include "EdGraph/EdGraphPin.h"
#include "ScopedTransaction.h"
#include "ISettingsModule.h"
#include "Modules/ModuleManager.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

namespace ElasticUMGEditor
{
	struct FWidgetPropertyPickerOption
	{
		FString Label;
		FString PropertyPath;
		FLinearColor TypeColor = FLinearColor::White;
		bool bIsHeader = false;
	};

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

	static FLinearColor GetPropertyTypeColor(const FProperty* Property)
	{
		FEdGraphPinType PinType;
		const UEdGraphSchema_K2* Schema = GetDefault<UEdGraphSchema_K2>();
		return Property && Schema && Schema->ConvertPropertyToPinType(Property, PinType)
			? Schema->GetPinTypeColor(PinType)
			: FLinearColor::White;
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
			FString DisplayName = Property->GetName();
			if (Property->IsA<FBoolProperty>() && DisplayName.Len() > 1 && DisplayName[0] == TEXT('b') && FChar::IsUpper(DisplayName[1]))
			{
				DisplayName.RightChopInline(1);
			}
			if (IsTransitionBindableProperty(Property))
			{
				OutOptions.Add(MakeShared<FWidgetPropertyPickerOption>(FWidgetPropertyPickerOption{ Prefix + DisplayName, PropertyPath, GetPropertyTypeColor(Property), false }));
			}
			if (const FStructProperty* StructProperty = CastField<FStructProperty>(Property))
			{
				if (HasTransitionBindableDescendant(StructProperty->Struct, Depth + 1))
				{
					if (!IsTransitionBindableProperty(Property))
					{
						OutOptions.Add(MakeShared<FWidgetPropertyPickerOption>(FWidgetPropertyPickerOption{ Prefix + DisplayName, FString(), FLinearColor::White, true }));
					}
					AddBindableProperties(StructProperty->Struct, PropertyPath + TEXT("."), Depth + 1, OutOptions);
				}
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
			UClass* WidgetClass = ElasticUMGEditor::GetWidgetClass(GraphPinObj);
			UWidget* DesignerWidget = GetDesignerWidget(GraphPinObj);
			UClass* SlotClass = DesignerWidget && DesignerWidget->Slot ? DesignerWidget->Slot->GetClass() : nullptr;
			const FString CacheKey = FString::Printf(TEXT("%u:%u"), WidgetClass ? WidgetClass->GetUniqueID() : 0, SlotClass ? SlotClass->GetUniqueID() : 0);
			static TMap<FString, TArray<TSharedPtr<FWidgetPropertyPickerOption>>> CachedOptions;
			if (const TArray<TSharedPtr<FWidgetPropertyPickerOption>>* Cached = CachedOptions.Find(CacheKey))
			{
				Options = *Cached;
				return;
			}

			AddBindableProperties(WidgetClass, FString(), 0, Options);
			if (SlotClass)
			{
				Options.Add(MakeShared<FWidgetPropertyPickerOption>(FWidgetPropertyPickerOption{ TEXT("Slot"), FString(), FLinearColor::White, true }));
				AddBindableProperties(SlotClass, TEXT("Slot."), 0, Options);
			}
			CachedOptions.Add(CacheKey, Options);
		}

		TSharedRef<SWidget> MakeOptionWidget(TSharedPtr<FWidgetPropertyPickerOption> Option) const
		{
			if (!Option.IsValid() || Option->bIsHeader)
			{
				return SNew(SBorder)
					.BorderImage(FAppStyle::GetBrush("NoBorder"))
					.Cursor(EMouseCursor::Default)
					.OnMouseButtonDown_Lambda([](const FGeometry&, const FPointerEvent&) { return FReply::Handled(); })
					[
						SNew(STextBlock)
						.Text(FText::FromString(Option.IsValid() ? Option->Label : FString()))
						.Font(FAppStyle::GetFontStyle("PropertyWindow.BoldFont"))
					];
			}
			return SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.FillWidth(1.0f)
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Text(FText::FromString(Option.IsValid() ? Option->Label : FString()))
					.Font(FAppStyle::GetFontStyle("PropertyWindow.NormalFont"))
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(FMargin(8.0f, 0.0f, 2.0f, 0.0f))
				[
					SNew(SImage)
					.Image(FAppStyle::GetBrush("Kismet.VariableList.TypeIcon"))
					.ColorAndOpacity(Option.IsValid() ? Option->TypeColor : FLinearColor::White)
				];
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
				TransitionNode->SetPropertyValueType(GetPropertyValueType(GraphPinObj, Option->PropertyPath));
			}
		}

		FText GetCurrentValue() const
		{
			const FString Value = GraphPinObj->GetDefaultAsString();
			return Value.IsEmpty() ? NSLOCTEXT("ElasticUMG", "SelectWidgetProperty", "Select widget property") : FText::FromString(Value);
		}

		TArray<TSharedPtr<FWidgetPropertyPickerOption>> Options;
	};

	class SWidgetTransitionEasingGraphPin final : public SGraphPin
	{
	public:
		SLATE_BEGIN_ARGS(SWidgetTransitionEasingGraphPin) {}
		SLATE_END_ARGS()

		void Construct(const FArguments& InArgs, UEdGraphPin* InGraphPinObj)
		{
			SGraphPin::Construct(SGraphPin::FArguments(), InGraphPinObj);
		}

	protected:
		virtual TSharedRef<SWidget> GetDefaultValueWidget() override
		{
			RefreshOptions();
			return SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.FillWidth(1.0f)
				[
					SNew(SComboBox<TSharedPtr<FString>>)
					.OptionsSource(&Options)
					.OnComboBoxOpening(this, &SWidgetTransitionEasingGraphPin::RefreshOptions)
					.OnGenerateWidget(this, &SWidgetTransitionEasingGraphPin::MakeOptionWidget)
					.OnSelectionChanged(this, &SWidgetTransitionEasingGraphPin::SelectOption)
					.IsEnabled(this, &SGraphPin::GetDefaultValueIsEditable)
					.Content()
					[
						SNew(STextBlock)
						.Text(this, &SWidgetTransitionEasingGraphPin::GetCurrentValue)
						.Font(FAppStyle::GetFontStyle("PropertyWindow.NormalFont"))
					]
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(FMargin(4.0f, 0.0f, 0.0f, 0.0f))
				[
					SNew(SButton)
					.ButtonStyle(FAppStyle::Get(), "HoverHintOnly")
					.ToolTipText(NSLOCTEXT("ElasticUMG", "OpenEasingSettings", "Open Elastic UMG easing settings"))
					.OnClicked_Lambda([]()
					{
						FModuleManager::LoadModuleChecked<ISettingsModule>("Settings").ShowViewer(TEXT("Project"), TEXT("Plugins"), TEXT("ElasticUMG"));
						return FReply::Handled();
					})
					[
						SNew(SImage).Image(FAppStyle::GetBrush("Icons.Settings"))
					]
				];
		}

	private:
		void RefreshOptions()
		{
			Options.Reset();
			Options.Add(MakeShared<FString>(TEXT("None")));
			for (const FWidgetTransitionEasing& Easing : GetDefault<UWidgetTransitionSettings>()->EasingFunctions)
			{
				if (!Easing.Name.IsNone()) Options.Add(MakeShared<FString>(Easing.Name.ToString()));
			}
		}

		TSharedRef<SWidget> MakeOptionWidget(TSharedPtr<FString> Option) const
		{
			return SNew(STextBlock).Text(FText::FromString(Option.IsValid() ? *Option : FString()));
		}

		void SelectOption(TSharedPtr<FString> Option, ESelectInfo::Type)
		{
			if (!Option.IsValid()) return;
			const FString NewValue = *Option == TEXT("None") ? FString() : *Option;
			if (GraphPinObj->GetDefaultAsString() == NewValue) return;
			const FScopedTransaction Transaction(NSLOCTEXT("ElasticUMG", "SetTransitionEasing", "Set Transition Easing"));
			GraphPinObj->Modify();
			GraphPinObj->GetSchema()->TrySetDefaultValue(*GraphPinObj, NewValue);
		}

		FText GetCurrentValue() const
		{
			const FString Value = GraphPinObj->GetDefaultAsString();
			return FText::FromString(Value.IsEmpty() ? TEXT("None") : Value);
		}

		TArray<TSharedPtr<FString>> Options;
	};

	class SWidgetTransitionOptionalPins final : public SCompoundWidget
	{
	public:
		SLATE_BEGIN_ARGS(SWidgetTransitionOptionalPins) {}
			SLATE_ARGUMENT(UK2Node_WidgetTransition*, TransitionNode)
		SLATE_END_ARGS()

		void Construct(const FArguments& InArgs)
		{
			TransitionNode = InArgs._TransitionNode;
			TSharedRef<SHorizontalBox> Buttons = SNew(SHorizontalBox);
			for (const FPinOption& Option : GetOptions())
			{
				Buttons->AddSlot().AutoWidth().Padding(FMargin(0.0f, 0.0f, 2.0f, 0.0f))
				[
					SNew(SCheckBox)
					.Style(FAppStyle::Get(), "ToggleButtonCheckbox")
					.Cursor(EMouseCursor::Hand)
					.ToolTipText(FText::FromString(Option.Tooltip))
					.IsChecked(this, &SWidgetTransitionOptionalPins::GetOptionState, Option.Pin)
					.OnCheckStateChanged(this, &SWidgetTransitionOptionalPins::SetOptionState, Option.Pin)
					.Padding(FMargin(4.0f, 5.0f))
					[
						SNew(STextBlock).Text(FText::FromString(Option.Label))
					]
				];
			}
			Buttons->AddSlot().AutoWidth().Padding(FMargin(0.0f, 0.0f, 2.0f, 0.0f))
			[
				SNew(SCheckBox)
				.Style(FAppStyle::Get(), "ToggleButtonCheckbox")
				.Cursor(EMouseCursor::Hand)
				.ToolTipText(NSLOCTEXT("ElasticUMG", "EasingOption", "Easing: select a configured cubic Bezier curve"))
				.IsChecked_Lambda([this]() { return TransitionNode.IsValid() && TransitionNode->IsEasingVisible() ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
				.OnCheckStateChanged_Lambda([this](ECheckBoxState State) { if (TransitionNode.IsValid()) TransitionNode->SetEasingVisible(State == ECheckBoxState::Checked); })
				.Padding(FMargin(4.0f, 5.0f))
				[
					SNew(STextBlock).Text(NSLOCTEXT("ElasticUMG", "EasingOptionLabel", "Es"))
				]
			];
			ChildSlot [ Buttons ];
		}

	private:
		struct FPinOption { EWidgetTransitionOptionalPin Pin; const TCHAR* Label; const TCHAR* Tooltip; };

		static const TArray<FPinOption>& GetOptions()
		{
			static const TArray<FPinOption> Options =
			{
				{ EWidgetTransitionOptionalPin::WidgetAndProperty, TEXT("Wp"), TEXT("Widget and Property: show or hide both binding inputs") },
				{ EWidgetTransitionOptionalPin::From, TEXT("Fr"), TEXT("From: use an explicit starting value") },
				{ EWidgetTransitionOptionalPin::Delay, TEXT("Dl"), TEXT("Delay: wait before starting the transition") },
				{ EWidgetTransitionOptionalPin::Repeat, TEXT("Rp"), TEXT("Repeat Count: number of additional repeats; -1 repeats forever") },
				{ EWidgetTransitionOptionalPin::YoYo, TEXT("Yo"), TEXT("Yo Yo: reverse From and To on every repeat") },
				{ EWidgetTransitionOptionalPin::RemoveFromParent, TEXT("Rm"), TEXT("Remove From Parent after the final transition") },
				{ EWidgetTransitionOptionalPin::Spring, TEXT("Sp"), TEXT("Spring: use spring motion instead of linear interpolation") },
				{ EWidgetTransitionOptionalPin::OnUpdate, TEXT("Up"), TEXT("On Update: expose the per-frame update delegate") },
			};
			return Options;
		}

		ECheckBoxState GetOptionState(EWidgetTransitionOptionalPin Option) const
		{
			return TransitionNode.IsValid() && TransitionNode->IsOptionalPinVisible(Option) ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
		}

		void SetOptionState(ECheckBoxState State, EWidgetTransitionOptionalPin Option)
		{
			if (TransitionNode.IsValid())
			{
				const FScopedTransaction Transaction(NSLOCTEXT("ElasticUMG", "ToggleTransitionOptionalPin", "Toggle Transition Optional Pin"));
				UK2Node_WidgetTransition* Node = TransitionNode.Get();
				const int32 NewOptionalPins = State == ECheckBoxState::Checked
					? Node->GetOptionalPins() | static_cast<int32>(Option)
					: Node->GetOptionalPins() & ~static_cast<int32>(Option);
				Node->SetOptionalPins(NewOptionalPins);
			}
		}

		TWeakObjectPtr<UK2Node_WidgetTransition> TransitionNode;
	};

	class SWidgetTransitionGraphNode final : public SGraphNodeK2Default
	{
	public:
		SLATE_BEGIN_ARGS(SWidgetTransitionGraphNode) {}
		SLATE_END_ARGS()

		void Construct(const FArguments& InArgs, UK2Node_WidgetTransition* InNode)
		{
			TransitionNode = InNode;
			SGraphNodeK2Default::Construct(SGraphNodeK2Default::FArguments(), InNode);
		}

	protected:
		virtual TSharedRef<SWidget> CreateNodeContentArea() override
		{
			return SNew(SBorder)
				.BorderImage(FAppStyle::GetBrush("NoBorder"))
				.HAlign(HAlign_Fill)
				.VAlign(VAlign_Fill)
				.Padding(FMargin(0.0f, 3.0f))
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot()
					.AutoHeight()
					.HAlign(HAlign_Center)
					.Padding(FMargin(4.0f, 0.0f, 4.0f, 2.0f))
					[
						SNew(SWidgetTransitionOptionalPins).TransitionNode(TransitionNode.Get())
					]
					+ SVerticalBox::Slot()
					.AutoHeight()
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot()
						.HAlign(HAlign_Left)
						.FillWidth(1.0f)
						[
							SAssignNew(LeftNodeBox, SVerticalBox)
						]
						+ SHorizontalBox::Slot()
						.AutoWidth()
						.HAlign(HAlign_Right)
						[
							SAssignNew(RightNodeBox, SVerticalBox)
						]
					]
				];
		}

	private:
		TWeakObjectPtr<UK2Node_WidgetTransition> TransitionNode;
	};

	class FWidgetPropertyPathPinFactory final : public FGraphPanelPinFactory
	{
	public:
		virtual TSharedPtr<SGraphPin> CreatePin(UEdGraphPin* Pin) const override
		{
			const bool bIsTypedTransitionNode = IsTypedTransitionNode(Pin);
			if (bIsTypedTransitionNode && Pin->PinName == TEXT("WidgetProperty"))
			{
				return SNew(SWidgetPropertyPathGraphPin, Pin);
			}
			if (bIsTypedTransitionNode && Pin->PinName == TEXT("Easing"))
			{
				return SNew(SWidgetTransitionEasingGraphPin, Pin);
			}
			return nullptr;
		}
	};

	class FWidgetTransitionNodeFactory final : public FGraphPanelNodeFactory
	{
	public:
		virtual TSharedPtr<SGraphNode> CreateNode(UEdGraphNode* Node) const override
		{
			if (UK2Node_WidgetTransition* TransitionNode = Cast<UK2Node_WidgetTransition>(Node)) return SNew(SWidgetTransitionGraphNode, TransitionNode);
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
		NodeFactory = MakeShared<ElasticUMGEditor::FWidgetTransitionNodeFactory>();
		FEdGraphUtilities::RegisterVisualNodeFactory(NodeFactory);
	}

	virtual void ShutdownModule() override
	{
		if (PinFactory.IsValid())
		{
			FEdGraphUtilities::UnregisterVisualPinFactory(PinFactory);
			PinFactory.Reset();
		}
		if (NodeFactory.IsValid())
		{
			FEdGraphUtilities::UnregisterVisualNodeFactory(NodeFactory);
			NodeFactory.Reset();
		}
	}

private:
	TSharedPtr<FGraphPanelPinFactory> PinFactory;
	TSharedPtr<FGraphPanelNodeFactory> NodeFactory;
};

IMPLEMENT_MODULE(FElasticUMGEditorModule, ElasticUMGEditor)
