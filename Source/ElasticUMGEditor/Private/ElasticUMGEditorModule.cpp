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
#include "PropertyEditorModule.h"
#include "IPropertyTypeCustomization.h"
#include "DetailWidgetRow.h"
#include "IDetailChildrenBuilder.h"
#include "PropertyHandle.h"
#include "ISettingsModule.h"
#include "Modules/ModuleManager.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/SLeafWidget.h"
#include "Rendering/DrawElements.h"
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

	class SWidgetTransitionEasingPreview final : public SLeafWidget
	{
	public:
		SLATE_BEGIN_ARGS(SWidgetTransitionEasingPreview) {}
			SLATE_ARGUMENT(FName, EasingName)
			SLATE_ARGUMENT(UEdGraphPin*, EasingPin)
		SLATE_END_ARGS()

		void Construct(const FArguments& InArgs)
		{
			EasingName = InArgs._EasingName;
			EasingPin = InArgs._EasingPin;
			StartTime = FPlatformTime::Seconds();
			RegisterActiveTimer(0.0f, FWidgetActiveTimerDelegate::CreateSP(this, &SWidgetTransitionEasingPreview::AdvancePreview));
		}

		virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D(180.0f, 40.0f); }

		virtual int32 OnPaint(const FPaintArgs&, const FGeometry& AllottedGeometry, const FSlateRect&, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle&, bool) const override
		{
			const FVector2D Size = AllottedGeometry.GetLocalSize();
			const UWidgetTransitionSettings* Settings = GetDefault<UWidgetTransitionSettings>();
			const float Duration = FMath::Max(Settings->PreviewDuration, 0.1f);
			const float Progress = FMath::Fmod(static_cast<float>(FPlatformTime::Seconds() - StartTime), Duration) / Duration;
			float EasedProgress = Settings->EvaluateEasing(EasingName, Progress);
			if (EasingName == TEXT("Custom"))
			{
				if (const UK2Node_WidgetTransition* Node = EasingPin ? Cast<UK2Node_WidgetTransition>(EasingPin->GetOwningNode()) : nullptr)
				{
					FVector2D Point1(0.25f, 0.1f), Point2(0.25f, 1.0f);
					if (const UEdGraphPin* Pin = Node->FindPin(TEXT("EasingControlPoint1"))) Point1.InitFromString(Pin->GetDefaultAsString());
					if (const UEdGraphPin* Pin = Node->FindPin(TEXT("EasingControlPoint2"))) Point2.InitFromString(Pin->GetDefaultAsString());
					EasedProgress = FWidgetTransitionEasing::EvaluateCubicBezier(Point1, Point2, Progress);
				}
			}
			const float Radius = 6.0f;
			const float Left = Radius + 4.0f;
			const float Right = FMath::Max(Left, Size.X - Radius - 4.0f);
			const FVector2D Center(FMath::Lerp(Left, Right, EasedProgress), Size.Y * 0.5f);
			FSlateDrawElement::MakeLines(OutDrawElements, LayerId, AllottedGeometry.ToPaintGeometry(), TArray<FVector2D>{ FVector2D(Left, Center.Y), FVector2D(Right, Center.Y) }, ESlateDrawEffect::None, FLinearColor(0.35f, 0.35f, 0.35f), true, 1.0f);
			FSlateDrawElement::MakeBox(OutDrawElements, LayerId + 1, AllottedGeometry.ToPaintGeometry(Center - FVector2D(Radius), FVector2D(Radius * 2.0f)), FAppStyle::GetBrush("Icons.Circle"), ESlateDrawEffect::None, FLinearColor(0.45f, 0.75f, 1.0f));
			return LayerId + 1;
		}

	private:
		EActiveTimerReturnType AdvancePreview(double, float)
		{
			Invalidate(EInvalidateWidgetReason::Paint);
			return EActiveTimerReturnType::Continue;
		}

		FName EasingName;
		UEdGraphPin* EasingPin = nullptr;
		double StartTime = 0.0;
	};

	class SWidgetTransitionEasingTemplatePreview final : public SLeafWidget
	{
	public:
		SLATE_BEGIN_ARGS(SWidgetTransitionEasingTemplatePreview) {}
			SLATE_ARGUMENT(TSharedPtr<IPropertyHandle>, ControlPoint1)
			SLATE_ARGUMENT(TSharedPtr<IPropertyHandle>, ControlPoint2)
		SLATE_END_ARGS()

		void Construct(const FArguments& InArgs) { ControlPoint1 = InArgs._ControlPoint1; ControlPoint2 = InArgs._ControlPoint2; }
		virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D(130.0f, 96.0f); }

		virtual int32 OnPaint(const FPaintArgs&, const FGeometry& Geometry, const FSlateRect&, FSlateWindowElementList& Elements, int32 LayerId, const FWidgetStyle&, bool) const override
		{
			FVector2D Point1(0.25f, 0.1f), Point2(0.25f, 1.0f);
			if (ControlPoint1.IsValid()) ControlPoint1->GetValue(Point1);
			if (ControlPoint2.IsValid()) ControlPoint2->GetValue(Point2);
			const FVector2D Size = Geometry.GetLocalSize();
			const float GraphSize = FMath::Min(Size.X - 12.0f, Size.Y - 12.0f);
			TArray<FVector2D> Values; Values.Reserve(33);
			float MinY = 0.0f, MaxY = 1.0f;
			for (int32 Index = 0; Index <= 32; ++Index) { const float T = static_cast<float>(Index) / 32.0f; const float U = 1.0f - T; const FVector2D Value = 3.0f * U * U * T * Point1 + 3.0f * U * T * T * Point2 + T * T * T * FVector2D(1.0f, 1.0f); Values.Add(Value); MinY = FMath::Min(MinY, Value.Y); MaxY = FMath::Max(MaxY, Value.Y); }
			const float Padding = (MaxY - MinY) * 0.05f;
			MinY -= Padding; MaxY += Padding;
			const FVector2D Origin(6.0f, 6.0f);
			const auto ToPoint = [Origin, GraphSize, MinY, MaxY](FVector2D Value) { return FVector2D(Origin.X + Value.X * GraphSize, Origin.Y + (1.0f - (Value.Y - MinY) / (MaxY - MinY)) * GraphSize); };
			FSlateDrawElement::MakeLines(Elements, LayerId, Geometry.ToPaintGeometry(), TArray<FVector2D>{ Origin, Origin + FVector2D(GraphSize, 0.0f), Origin + FVector2D(GraphSize, GraphSize), Origin + FVector2D(0.0f, GraphSize), Origin }, ESlateDrawEffect::None, FLinearColor(0.3f, 0.3f, 0.3f), true, 1.0f);
			TArray<FVector2D> Curve; Curve.Reserve(Values.Num()); for (const FVector2D& Value : Values) Curve.Add(ToPoint(Value));
			FSlateDrawElement::MakeLines(Elements, LayerId + 1, Geometry.ToPaintGeometry(), Curve, ESlateDrawEffect::None, FLinearColor(0.25f, 0.65f, 1.0f), true, 2.0f);
			return LayerId + 1;
		}

	private:
		TSharedPtr<IPropertyHandle> ControlPoint1;
		TSharedPtr<IPropertyHandle> ControlPoint2;
	};

	class SWidgetTransitionNodeEasingCurvePreview final : public SLeafWidget
	{
	public:
		SLATE_BEGIN_ARGS(SWidgetTransitionNodeEasingCurvePreview) {}
			SLATE_ARGUMENT(UK2Node_WidgetTransition*, TransitionNode)
		SLATE_END_ARGS()
		void Construct(const FArguments& InArgs) { TransitionNode = InArgs._TransitionNode; }
		virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D(130.0f, 96.0f); }
		virtual int32 OnPaint(const FPaintArgs&, const FGeometry& Geometry, const FSlateRect&, FSlateWindowElementList& Elements, int32 LayerId, const FWidgetStyle&, bool) const override
		{
			const FWidgetTransitionEasingValue Value = TransitionNode.IsValid() ? TransitionNode->GetDisplayedEasingValue() : FWidgetTransitionEasingValue();
			const FVector2D Point1 = Value.ControlPoint1, Point2 = Value.ControlPoint2;
			const FVector2D Size = Geometry.GetLocalSize(); const float GraphSize = FMath::Min(Size.X - 12.0f, (Size.Y - 12.0f) / 1.4f); const FVector2D Origin(6.0f, 6.0f + GraphSize * 0.2f);
			TArray<FVector2D> Values; Values.Reserve(33); float MinY = 0.0f, MaxY = 1.0f;
			for (int32 Index = 0; Index <= 32; ++Index) { const float T = static_cast<float>(Index) / 32.0f; const float U = 1.0f - T; const FVector2D CurveValue = 3.0f * U * U * T * Point1 + 3.0f * U * T * T * Point2 + T * T * T * FVector2D(1.0f, 1.0f); Values.Add(CurveValue); MinY = FMath::Min(MinY, CurveValue.Y); MaxY = FMath::Max(MaxY, CurveValue.Y); }
			const float Padding = FMath::Max((MaxY - MinY) * 0.05f, KINDA_SMALL_NUMBER); MinY -= Padding; MaxY += Padding;
			const auto ToPoint = [Origin, GraphSize, MinY, MaxY](FVector2D CurveValue) { return FVector2D(Origin.X + CurveValue.X * GraphSize, Origin.Y + (1.0f - (CurveValue.Y - MinY) / (MaxY - MinY)) * GraphSize); };
			FSlateDrawElement::MakeLines(Elements, LayerId, Geometry.ToPaintGeometry(), TArray<FVector2D>{ Origin, Origin + FVector2D(GraphSize, 0.0f), Origin + FVector2D(GraphSize, GraphSize), Origin + FVector2D(0.0f, GraphSize), Origin }, ESlateDrawEffect::None, FLinearColor(0.3f, 0.3f, 0.3f), true, 1.0f);
			TArray<FVector2D> Curve; Curve.Reserve(Values.Num()); for (const FVector2D& CurveValue : Values) Curve.Add(ToPoint(CurveValue));
			const bool bEditable = TransitionNode.IsValid() && TransitionNode->IsCustomEasingSelected();
			FSlateDrawElement::MakeLines(Elements, LayerId + 1, Geometry.ToPaintGeometry(), Curve, ESlateDrawEffect::None, bEditable ? FLinearColor(0.25f, 0.65f, 1.0f) : FLinearColor(0.42f, 0.42f, 0.42f), true, 2.0f); return LayerId + 1;
		}
	private: TWeakObjectPtr<UK2Node_WidgetTransition> TransitionNode;
	};

	class FWidgetTransitionEasingCustomization final : public IPropertyTypeCustomization
	{
	public:
		static TSharedRef<IPropertyTypeCustomization> MakeInstance() { return MakeShared<FWidgetTransitionEasingCustomization>(); }

		virtual void CustomizeHeader(TSharedRef<IPropertyHandle> StructHandle, FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils&) override
		{
			HeaderRow.NameContent()[StructHandle->CreatePropertyNameWidget()]
			.ValueContent()[StructHandle->CreatePropertyValueWidget()];
		}

		virtual void CustomizeChildren(TSharedRef<IPropertyHandle> StructHandle, IDetailChildrenBuilder& ChildrenBuilder, IPropertyTypeCustomizationUtils&) override
		{
			TSharedPtr<IPropertyHandle> Name = StructHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FWidgetTransitionEasing, Name));
			TSharedPtr<IPropertyHandle> ControlPoint1 = StructHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FWidgetTransitionEasing, ControlPoint1));
			TSharedPtr<IPropertyHandle> ControlPoint2 = StructHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FWidgetTransitionEasing, ControlPoint2));
			if (Name.IsValid()) ChildrenBuilder.AddProperty(Name.ToSharedRef());
			if (ControlPoint1.IsValid()) ChildrenBuilder.AddProperty(ControlPoint1.ToSharedRef());
			if (ControlPoint2.IsValid()) ChildrenBuilder.AddProperty(ControlPoint2.ToSharedRef());
			ChildrenBuilder.AddCustomRow(NSLOCTEXT("ElasticUMG", "EasingPreview", "Preview"))
			.NameContent()[SNew(STextBlock).Text(NSLOCTEXT("ElasticUMG", "EasingPreview", "Preview"))]
			.ValueContent().MinDesiredWidth(130.0f)[SNew(SWidgetTransitionEasingTemplatePreview).ControlPoint1(ControlPoint1).ControlPoint2(ControlPoint2)];
		}
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
			return SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight()
				[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.FillWidth(1.0f)
				[
					SNew(SComboBox<TSharedPtr<FString>>)
					.OptionsSource(&Options)
					.OnComboBoxOpening(this, &SWidgetTransitionEasingGraphPin::RefreshOptions)
					.OnGenerateWidget(this, &SWidgetTransitionEasingGraphPin::MakeOptionWidget)
					.OnSelectionChanged(this, &SWidgetTransitionEasingGraphPin::SelectOption)
					.IsEnabled(true)
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
				]
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.0f, 4.0f, 0.0f, 0.0f))
				[
					SNew(SWidgetTransitionNodeEasingCurvePreview).TransitionNode(Cast<UK2Node_WidgetTransition>(GraphPinObj->GetOwningNode()))
				];
		}

	private:
		void RefreshOptions()
		{
			Options.Reset();
			Options.Add(MakeShared<FString>(TEXT("None")));
			Options.Add(MakeShared<FString>(TEXT("Custom")));
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
			const FName NewValue = *Option == TEXT("None") ? NAME_None : FName(**Option);
			UK2Node_WidgetTransition* TransitionNode = Cast<UK2Node_WidgetTransition>(GraphPinObj->GetOwningNode());
			if (!TransitionNode || TransitionNode->GetSelectedEasingPreset() == NewValue) return;
			const FScopedTransaction Transaction(NSLOCTEXT("ElasticUMG", "SetTransitionEasing", "Set Transition Easing"));
			TransitionNode->SetSelectedEasingPreset(NewValue);
		}

		FText GetCurrentValue() const
		{
			const UK2Node_WidgetTransition* TransitionNode = Cast<UK2Node_WidgetTransition>(GraphPinObj->GetOwningNode());
			const FName Value = TransitionNode ? TransitionNode->GetSelectedEasingPreset() : NAME_None;
			return FText::FromString(Value.IsNone() ? TEXT("None") : Value.ToString());
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
			TSharedRef<SHorizontalBox> BasicButtons = SNew(SHorizontalBox);
			for (const FPinOption& Option : GetBaseOptions()) AddOption(BasicButtons, Option);
			TSharedRef<SHorizontalBox> ModeButtons = SNew(SHorizontalBox);
			ModeButtons->AddSlot().AutoWidth().Padding(FMargin(0.0f, 0.0f, 2.0f, 0.0f))
			[
				SNew(SCheckBox)
				.Style(FAppStyle::Get(), "ToggleButtonCheckbox")
				.Cursor(EMouseCursor::Hand)
				.ToolTipText(NSLOCTEXT("ElasticUMG", "EasingOption", "Interpolation: select a configured cubic Bezier curve"))
				.IsChecked_Lambda([this]() { return TransitionNode.IsValid() && !TransitionNode->IsOptionalPinVisible(EWidgetTransitionOptionalPin::Spring) ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
				.OnCheckStateChanged_Lambda([this](ECheckBoxState State) { if (TransitionNode.IsValid() && State == ECheckBoxState::Checked) TransitionNode->SetTransitionMode(false); })
				.Padding(FMargin(4.0f, 5.0f))
				[
					SNew(STextBlock).Text(NSLOCTEXT("ElasticUMG", "InterpolationOptionLabel", "In"))
				]
			];
			ModeButtons->AddSlot().AutoWidth()
			[
				SNew(SCheckBox)
				.Style(FAppStyle::Get(), "ToggleButtonCheckbox")
				.Cursor(EMouseCursor::Hand)
				.ToolTipText(NSLOCTEXT("ElasticUMG", "SpringOption", "Spring: use physical spring motion"))
				.IsChecked_Lambda([this]() { return TransitionNode.IsValid() && TransitionNode->IsOptionalPinVisible(EWidgetTransitionOptionalPin::Spring) ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
				.OnCheckStateChanged_Lambda([this](ECheckBoxState State) { if (TransitionNode.IsValid() && State == ECheckBoxState::Checked) TransitionNode->SetTransitionMode(true); })
				.Padding(FMargin(4.0f, 5.0f))
				[
					SNew(STextBlock).Text(NSLOCTEXT("ElasticUMG", "SpringOptionLabel", "Sp"))
				]
			];
			TSharedRef<SHorizontalBox> EventButtons = SNew(SHorizontalBox);
			for (const FPinOption& Option : GetEventOptions()) AddOption(EventButtons, Option);
			ChildSlot
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth()[ModeButtons]
				+ SHorizontalBox::Slot().AutoWidth().Padding(FMargin(5.0f, 2.0f))[CreateGroupSeparator()]
				+ SHorizontalBox::Slot().AutoWidth()[BasicButtons]
				+ SHorizontalBox::Slot().AutoWidth().Padding(FMargin(5.0f, 2.0f))[CreateGroupSeparator()]
				+ SHorizontalBox::Slot().AutoWidth()[EventButtons]
			];
		}

	private:
		struct FPinOption { EWidgetTransitionOptionalPin Pin; const TCHAR* Label; const TCHAR* Tooltip; };

		static TSharedRef<SWidget> CreateGroupSeparator()
		{
			return SNew(SBox)
				.WidthOverride(2.0f)
				[
					SNew(SBorder)
					.BorderImage(FAppStyle::Get().GetBrush("WhiteBrush"))
					.BorderBackgroundColor(FLinearColor(0.12f, 0.12f, 0.12f, 0.9f))
					.Padding(0.0f)
				];
		}

		void AddOption(const TSharedRef<SHorizontalBox>& Buttons, const FPinOption& Option)
		{
			Buttons->AddSlot().AutoWidth().Padding(FMargin(0.0f, 0.0f, 2.0f, 0.0f))
			[
				SNew(SCheckBox).Style(FAppStyle::Get(), "ToggleButtonCheckbox").Cursor(EMouseCursor::Hand).ToolTipText(FText::FromString(Option.Tooltip))
				.IsChecked(this, &SWidgetTransitionOptionalPins::GetOptionState, Option.Pin).OnCheckStateChanged(this, &SWidgetTransitionOptionalPins::SetOptionState, Option.Pin).Padding(FMargin(4.0f, 5.0f))
				[SNew(STextBlock).Text(FText::FromString(Option.Label))]
			];
		}

		static const TArray<FPinOption>& GetBaseOptions()
		{
			static const TArray<FPinOption> Options =
			{
				{ EWidgetTransitionOptionalPin::WidgetAndProperty, TEXT("Wp"), TEXT("Widget and Property: show or hide both binding inputs") },
				{ EWidgetTransitionOptionalPin::From, TEXT("Fr"), TEXT("From: use an explicit starting value") },
				{ EWidgetTransitionOptionalPin::Delay, TEXT("Dl"), TEXT("Delay: wait before starting the transition") },
				{ EWidgetTransitionOptionalPin::Repeat, TEXT("Rp"), TEXT("Repeat Count: number of additional repeats; -1 repeats forever") },
				{ EWidgetTransitionOptionalPin::YoYo, TEXT("Yo"), TEXT("Yo Yo: reverse From and To on every repeat") },
				{ EWidgetTransitionOptionalPin::RemoveFromParent, TEXT("Rm"), TEXT("Remove From Parent after the final transition") },
			};
			return Options;
		}

		static const TArray<FPinOption>& GetEventOptions()
		{
			static const TArray<FPinOption> Options =
			{
				{ EWidgetTransitionOptionalPin::OnStarted, TEXT("St"), TEXT("On Started: called after Delay when the transition begins") },
				{ EWidgetTransitionOptionalPin::OnUpdate, TEXT("Up"), TEXT("On Update: expose the per-frame update delegate") },
				{ EWidgetTransitionOptionalPin::OnFinished, TEXT("En"), TEXT("On Finished: called after the final transition") },
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
					+ SVerticalBox::Slot()
					.AutoHeight()
					.HAlign(HAlign_Center)
					.Padding(FMargin(0.0f, 2.0f, 0.0f, 2.0f))
					[
						SNew(SBox)
						.Visibility_Lambda([Node = TransitionNode]()
						{
							const UEdGraphPin* EasingPin = Node.IsValid() ? Node->FindPin(TEXT("Easing")) : nullptr;
							return EasingPin && EasingPin->SubPins.Num() > 0 ? EVisibility::Visible : EVisibility::Collapsed;
						})
						[
							SNew(SWidgetTransitionNodeEasingCurvePreview).TransitionNode(TransitionNode.Get())
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
		FPropertyEditorModule& PropertyEditor = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");
		PropertyEditor.RegisterCustomPropertyTypeLayout(TEXT("WidgetTransitionEasing"), FOnGetPropertyTypeCustomizationInstance::CreateStatic(&ElasticUMGEditor::FWidgetTransitionEasingCustomization::MakeInstance));
		PropertyEditor.NotifyCustomizationModuleChanged();
		PinFactory = MakeShared<ElasticUMGEditor::FWidgetPropertyPathPinFactory>();
		FEdGraphUtilities::RegisterVisualPinFactory(PinFactory);
		NodeFactory = MakeShared<ElasticUMGEditor::FWidgetTransitionNodeFactory>();
		FEdGraphUtilities::RegisterVisualNodeFactory(NodeFactory);
	}

	virtual void ShutdownModule() override
	{
		if (FModuleManager::Get().IsModuleLoaded("PropertyEditor"))
		{
			FPropertyEditorModule& PropertyEditor = FModuleManager::GetModuleChecked<FPropertyEditorModule>("PropertyEditor");
			PropertyEditor.UnregisterCustomPropertyTypeLayout(TEXT("WidgetTransitionEasing"));
		}
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
