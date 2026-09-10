#include "EdGraphUtilities.h"
#include "WidgetTransition.h"

#include "Blueprint/WidgetTree.h"
#include "Components/PanelSlot.h"
#include "Components/Border.h"
#include "Components/Image.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphSchema_K2.h"
#include "K2Node_CallFunction.h"
#include "K2Node_Self.h"
#include "SGraphPin.h"
#include "Framework/Application/SlateApplication.h"
#include "KismetPins/SVector2DTextBox.h"
#include "K2Node_VariableGet.h"
#include "Materials/MaterialInterface.h"
#include "Misc/OutputDeviceNull.h"
#include "Styling/AppStyle.h"
#include "Styling/CoreStyle.h"
#include "WidgetBlueprint.h"
#include "Widgets/Input/SNumericEntryBox.h"
#include "Widgets/Input/SSegmentedControl.h"
#include "Widgets/Colors/SColorBlock.h"
#include "Widgets/Colors/SColorPicker.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/SLeafWidget.h"
#include "Widgets/Text/STextBlock.h"
#include "Rendering/DrawElementTypes.h"

namespace UMGTransitionsEditor
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
		if (!Struct || Depth > 8)
		{
			return false;
		}
		for (TFieldIterator<FProperty> It(Struct, EFieldIterationFlags::None); It; ++It)
		{
			if (IsBindableProperty(*It))
			{
				return true;
			}
			if (const FStructProperty* Nested = CastField<FStructProperty>(*It); Nested && HasBindableDescendant(Nested->Struct, Depth + 1))
			{
				return true;
			}
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
		if (!Struct || Depth > 8)
		{
			return;
		}
		for (TFieldIterator<FProperty> It(Struct, EFieldIterationFlags::None); It; ++It)
		{
			const FProperty* Property = *It;
			const FString Path = Prefix + Property->GetName();
			if (IsBindableProperty(Property))
			{
				OutOptions.Add(MakeShared<FPropertyOption>(FPropertyOption{ Path, Path, GetTypeColor(Property) }));
			}
			if (const FStructProperty* Nested = CastField<FStructProperty>(Property); Nested && HasBindableDescendant(Nested->Struct, Depth + 1))
			{
				if (!IsBindableProperty(Property))
				{
					OutOptions.Add(MakeShared<FPropertyOption>(FPropertyOption{ Path, FString(), FLinearColor::White, true }));
				}
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
		return Function && Function->HasMetaData(TEXT("UMGTransitionsBinding")) && (Pin->PinName == TEXT("WidgetProperty") || Pin->PinName == TEXT("Binding"));
	}
	static bool IsCombinedBindingPin(const UEdGraphPin* Pin)
	{
		const UK2Node_CallFunction* Node = Pin ? Cast<UK2Node_CallFunction>(Pin->GetOwningNode()) : nullptr;
		const UFunction* Function = Node ? Node->GetTargetFunction() : nullptr;
		return Function && Function->GetMetaData(TEXT("UMGTransitionsBinding")) == TEXT("Combined") && (Pin->PinName == TEXT("WidgetProperty") || Pin->PinName == TEXT("Binding"));
	}
	static UEdGraphPin* GetWidgetSource(const UEdGraphPin* PropertyPin)
	{
		UEdGraphPin* WidgetPin = PropertyPin && PropertyPin->GetOwningNode() ? PropertyPin->GetOwningNode()->FindPin(TEXT("Widget")) : nullptr;
		return WidgetPin && !WidgetPin->LinkedTo.IsEmpty() ? FEdGraphUtilities::GetNetFromPin(WidgetPin->LinkedTo[0]) : nullptr;
	}
	static bool UsesDefaultSelfWidget(const UEdGraphPin* PropertyPin)
	{
		const UK2Node_CallFunction* Node = PropertyPin ? Cast<UK2Node_CallFunction>(PropertyPin->GetOwningNode()) : nullptr;
		const UFunction* Function = Node ? Node->GetTargetFunction() : nullptr;
		return Function && Function->GetMetaData(TEXT("DefaultToSelf")) == TEXT("Widget");
	}
	static UWidgetBlueprint* GetWidgetBlueprint(const UEdGraphPin* PropertyPin)
	{
		if (UEdGraphPin* Source = GetWidgetSource(PropertyPin))
		{
			if (UWidgetBlueprint* Blueprint = Source->GetOwningNode()->GetTypedOuter<UWidgetBlueprint>())
			{
				return Blueprint;
			}
		}
		return PropertyPin && PropertyPin->GetOwningNode() ? PropertyPin->GetOwningNode()->GetTypedOuter<UWidgetBlueprint>() : nullptr;
	}
	static UWidget* GetDesignerWidget(const UEdGraphPin* PropertyPin)
	{
		UEdGraphPin* Source = GetWidgetSource(PropertyPin);
		const UK2Node_VariableGet* Get = Source ? Cast<UK2Node_VariableGet>(Source->GetOwningNode()) : nullptr;
		UWidgetBlueprint* Blueprint = GetWidgetBlueprint(PropertyPin);
		return Get && Blueprint && Blueprint->WidgetTree ? Blueprint->WidgetTree->FindWidget(Get->GetVarName()) : nullptr;
	}
	static UClass* GetWidgetClassForPin(const UEdGraphPin* PropertyPin)
	{
		if (UEdGraphPin* Source = GetWidgetSource(PropertyPin))
		{
			if (Source->GetOwningNode()->IsA<UK2Node_Self>())
			{
				if (UWidgetBlueprint* Blueprint = GetWidgetBlueprint(PropertyPin); Blueprint && Blueprint->GeneratedClass && Blueprint->GeneratedClass->IsChildOf(UWidget::StaticClass()))
				{
					return Blueprint->GeneratedClass;
				}
			}
			return Cast<UClass>(Source->PinType.PinSubCategoryObject.Get());
		}
		if (UsesDefaultSelfWidget(PropertyPin))
		{
			if (UWidgetBlueprint* Blueprint = GetWidgetBlueprint(PropertyPin); Blueprint && Blueprint->GeneratedClass && Blueprint->GeneratedClass->IsChildOf(UWidget::StaticClass()))
			{
				return Blueprint->GeneratedClass;
			}
		}
		return UWidget::StaticClass();
	}
	static UMaterialInterface* GetDesignerMaterial(const UEdGraphPin* Pin)
	{
		if (const UImage* Image = Cast<UImage>(GetDesignerWidget(Pin)))
		{
			return Cast<UMaterialInterface>(Image->GetBrush().GetResourceObject());
		}
		if (const UBorder* Border = Cast<UBorder>(GetDesignerWidget(Pin)))
		{
			return Cast<UMaterialInterface>(Border->Background.GetResourceObject());
		}
		return nullptr;
	}

	DECLARE_DELEGATE_OneParam(FOnCubicBezierEasingChanged, FWidgetTransitionEasing);

	/** Compact direct-manipulation editor for the two control points of a cubic Bezier easing. */
	class SCubicBezierEasingEditor final : public SLeafWidget
	{
	public:
		SLATE_BEGIN_ARGS(SCubicBezierEasingEditor) {}
			SLATE_ATTRIBUTE(FWidgetTransitionEasing, Value)
			SLATE_EVENT(FOnCubicBezierEasingChanged, OnValueChanged)
		SLATE_END_ARGS()

		void Construct(const FArguments& InArgs)
		{
			ValueAttribute = InArgs._Value;
			OnValueChanged = InArgs._OnValueChanged;
		}

		virtual FVector2D ComputeDesiredSize(float) const override
		{
			return FVector2D(220.0, 132.0);
		}

		virtual int32 OnPaint(const FPaintArgs&, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle&, bool) const override
		{
			const FSlateBrush* WhiteBrush = FCoreStyle::Get().GetBrush("WhiteBrush");
			FSlateDrawElement::MakeBox(OutDrawElements, LayerId, AllottedGeometry.ToPaintGeometry(), WhiteBrush, ESlateDrawEffect::None, FLinearColor(0.035f, 0.04f, 0.045f, 1.0f));

			const FSlateRect Plot = GetPlotRect(AllottedGeometry);
			const FPaintGeometry PaintGeometry = AllottedGeometry.ToPaintGeometry();
			const FLinearColor GridColor(0.20f, 0.22f, 0.24f, 0.7f);
			for (int32 Index = 0; Index <= 4; ++Index)
			{
				const float Fraction = static_cast<float>(Index) / 4.0f;
				FSlateDrawElement::MakeLines(OutDrawElements, LayerId + 1, PaintGeometry, { FVector2f(Plot.Left + Plot.GetSize().X * Fraction, Plot.Top), FVector2f(Plot.Left + Plot.GetSize().X * Fraction, Plot.Bottom) }, ESlateDrawEffect::None, GridColor, true, 1.0f);
				FSlateDrawElement::MakeLines(OutDrawElements, LayerId + 1, PaintGeometry, { FVector2f(Plot.Left, Plot.Top + Plot.GetSize().Y * Fraction), FVector2f(Plot.Right, Plot.Top + Plot.GetSize().Y * Fraction) }, ESlateDrawEffect::None, GridColor, true, 1.0f);
			}

			const FWidgetTransitionEasing Easing = ValueAttribute.Get();
			const FVector2D Start = ToScreen(Plot, FVector2D::Zero());
			const FVector2D End = ToScreen(Plot, FVector2D(1.0, 1.0));
			const FVector2D MathematicalFirst = ToScreen(Plot, Easing.FirstControlPoint);
			const FVector2D MathematicalSecond = ToScreen(Plot, Easing.SecondControlPoint);
			const FVector2D First = GetVisualHandlePosition(Plot, Start, MathematicalFirst, FVector2D(1.0, 0.0));
			const FVector2D Second = GetVisualHandlePosition(Plot, End, MathematicalSecond, FVector2D(-1.0, 0.0));
			const FLinearColor HandleColor(0.48f, 0.55f, 0.62f, 0.9f);
			FSlateDrawElement::MakeLines(OutDrawElements, LayerId + 2, PaintGeometry, { FVector2f(Start), FVector2f(First) }, ESlateDrawEffect::None, HandleColor, true, 1.0f);
			FSlateDrawElement::MakeLines(OutDrawElements, LayerId + 2, PaintGeometry, { FVector2f(Second), FVector2f(End) }, ESlateDrawEffect::None, HandleColor, true, 1.0f);
			const FLinearColor CurveColor = bHovered ? FLinearColor(1.0f, 0.45f, 0.08f, 1.0f) : FLinearColor(0.30f, 0.72f, 1.0f, 1.0f);
			FSlateDrawElement::MakeCubicBezierSpline(OutDrawElements, LayerId + 3, PaintGeometry, FVector2f(Start), FVector2f(MathematicalFirst), FVector2f(MathematicalSecond), FVector2f(End), bHovered ? 2.5f : 1.5f, ESlateDrawEffect::None, CurveColor);
			DrawHandle(OutDrawElements, LayerId + 4, AllottedGeometry, First, HandleColor);
			DrawHandle(OutDrawElements, LayerId + 4, AllottedGeometry, Second, HandleColor);
			return LayerId + 4;
		}

		virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override
		{
			if (MouseEvent.GetEffectingButton() != EKeys::LeftMouseButton)
			{
				return FReply::Unhandled();
			}
			const FVector2D LocalPosition = MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition());
			const FSlateRect Plot = GetPlotRect(MyGeometry);
			const FWidgetTransitionEasing Easing = ValueAttribute.Get();
			const FVector2D Start = ToScreen(Plot, FVector2D::Zero());
			const FVector2D End = ToScreen(Plot, FVector2D(1.0, 1.0));
			const FVector2D FirstHandle = GetVisualHandlePosition(Plot, Start, ToScreen(Plot, Easing.FirstControlPoint), FVector2D(1.0, 0.0));
			const FVector2D SecondHandle = GetVisualHandlePosition(Plot, End, ToScreen(Plot, Easing.SecondControlPoint), FVector2D(-1.0, 0.0));
			if ((LocalPosition - FirstHandle).SizeSquared() <= HandleHitRadius * HandleHitRadius)
			{
				DragMode = EDragMode::FirstHandle;
			}
			else if ((LocalPosition - SecondHandle).SizeSquared() <= HandleHitRadius * HandleHitRadius)
			{
				DragMode = EDragMode::SecondHandle;
			}
			else
			{
				DragMode = EDragMode::Sculpt;
				SculptT = FindClosestCurveT(Plot, Easing, LocalPosition);
			}
			LastLocalPosition = LocalPosition;
			return FReply::Handled().CaptureMouse(AsShared());
		}

		virtual FReply OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override
		{
			if (!HasMouseCapture())
			{
				return FReply::Unhandled();
			}
			const FVector2D LocalPosition = MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition());
			const FSlateRect Plot = GetPlotRect(MyGeometry);
			const FVector2D ScreenDelta = LocalPosition - LastLocalPosition;
			const FVector2D ValueDelta(ScreenDelta.X / Plot.GetSize().X, -ScreenDelta.Y / Plot.GetSize().Y);
			FWidgetTransitionEasing Easing = ValueAttribute.Get();
			switch (DragMode)
			{
			case EDragMode::FirstHandle:
			{
				Easing.FirstControlPoint += ValueDelta;
				break;
			}
			case EDragMode::SecondHandle:
			{
				Easing.SecondControlPoint += ValueDelta;
				break;
			}
			case EDragMode::Sculpt:
			{
				Easing.FirstControlPoint += ValueDelta * EvaluateSculptFalloff(SculptT, FirstHandleCurvePosition);
				Easing.SecondControlPoint += ValueDelta * EvaluateSculptFalloff(SculptT, SecondHandleCurvePosition);
				break;
			}
			default:
			{
				break;
			}
			}
			Easing.Clamp();
			OnValueChanged.ExecuteIfBound(Easing);
			LastLocalPosition = LocalPosition;
			return FReply::Handled();
		}

		virtual FReply OnMouseButtonUp(const FGeometry&, const FPointerEvent& MouseEvent) override
		{
			if (MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton && HasMouseCapture())
			{
				DragMode = EDragMode::None;
				return FReply::Handled().ReleaseMouseCapture();
			}
			return FReply::Unhandled();
		}

		virtual void OnMouseEnter(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override
		{
			SLeafWidget::OnMouseEnter(MyGeometry, MouseEvent);
			bHovered = true;
			Invalidate(EInvalidateWidgetReason::Paint);
		}

		virtual void OnMouseLeave(const FPointerEvent& MouseEvent) override
		{
			SLeafWidget::OnMouseLeave(MouseEvent);
			bHovered = false;
			Invalidate(EInvalidateWidgetReason::Paint);
		}

	private:
		enum class EDragMode : uint8
		{
			None,
			FirstHandle,
			SecondHandle,
			Sculpt,
		};

		static constexpr float HandleHitRadius = 10.0f;
		/** Brush radius expressed as normalized curve length. */
		static constexpr double SculptRadius = 1.0 / 3.0;
		/** The two handles reach their maximum geometric influence near these curve positions. */
		static constexpr double FirstHandleCurvePosition = 1.0 / 3.0;
		static constexpr double SecondHandleCurvePosition = 2.0 / 3.0;

		static FSlateRect GetPlotRect(const FGeometry& Geometry)
		{
			const FVector2D Size = Geometry.GetLocalSize();
			return FSlateRect(12.0f, 10.0f, FMath::Max(12.0, Size.X - 12.0), FMath::Max(10.0, Size.Y - 10.0));
		}

		static FVector2D ToScreen(const FSlateRect& Plot, FVector2D Value)
		{
			return FVector2D(Plot.Left + Value.X * Plot.GetSize().X, Plot.Bottom - Value.Y * Plot.GetSize().Y);
		}

		static double EvaluateSculptFalloff(double CurvePosition, double HandleCurvePosition)
		{
			const double Distance = FMath::Abs(CurvePosition - HandleCurvePosition);
			if (Distance >= SculptRadius)
			{
				return 0.0;
			}
			const double Sigma = SculptRadius / 1.5;
			const double Gaussian = FMath::Exp(-0.5 * FMath::Square(Distance / Sigma));
			const double EdgeGaussian = FMath::Exp(-0.5 * FMath::Square(SculptRadius / Sigma));
			return (Gaussian - EdgeGaussian) / (1.0 - EdgeGaussian);
		}

		static FVector2D GetVisualHandlePosition(const FSlateRect& Plot, FVector2D Anchor, FVector2D MathematicalHandle, FVector2D FallbackDirection)
		{
			FVector2D Direction = MathematicalHandle - Anchor;
			if (Direction.IsNearlyZero())
			{
				Direction = FallbackDirection;
			}
			return Anchor + Direction.GetSafeNormal() * (FMath::Min(Plot.GetSize().X, Plot.GetSize().Y) / 3.0f);
		}

		static FVector2D EvaluatePoint(const FWidgetTransitionEasing& Easing, double T)
		{
			const double InverseT = 1.0 - T;
			return 3.0 * InverseT * InverseT * T * Easing.FirstControlPoint + 3.0 * InverseT * T * T * Easing.SecondControlPoint + T * T * T * FVector2D(1.0, 1.0);
		}

		static double FindClosestCurveT(const FSlateRect& Plot, const FWidgetTransitionEasing& Easing, FVector2D Position)
		{
			constexpr int32 SampleCount = 64;
			double BestDistanceSquared = TNumericLimits<double>::Max();
			double BestT = 0.5;
			for (int32 Index = 1; Index < SampleCount; ++Index)
			{
				const double T = static_cast<double>(Index) / SampleCount;
				const double DistanceSquared = (Position - ToScreen(Plot, EvaluatePoint(Easing, T))).SizeSquared();
				if (DistanceSquared < BestDistanceSquared)
				{
					BestDistanceSquared = DistanceSquared;
					BestT = T;
				}
			}
			return BestT;
		}

		static void DrawHandle(FSlateWindowElementList& OutDrawElements, int32 LayerId, const FGeometry& Geometry, FVector2D Position, FLinearColor Color)
		{
			const FVector2D HandleSize(8.0, 8.0);
			FSlateDrawElement::MakeBox(OutDrawElements, LayerId, Geometry.ToPaintGeometry(HandleSize, FSlateLayoutTransform(Position - HandleSize * 0.5)), FCoreStyle::Get().GetBrush("WhiteBrush"), ESlateDrawEffect::None, Color);
		}

		TAttribute<FWidgetTransitionEasing> ValueAttribute;
		FOnCubicBezierEasingChanged OnValueChanged;
		EDragMode DragMode = EDragMode::None;
		FVector2D LastLocalPosition = FVector2D::ZeroVector;
		double SculptT = 0.5;
		bool bHovered = false;
	};

	class SWidgetPropertyPathPin final : public SGraphPin
	{
	public:
		SLATE_BEGIN_ARGS(SWidgetPropertyPathPin) {} SLATE_END_ARGS()
		void Construct(const FArguments&, UEdGraphPin* Pin) { bIncludeMaterialParameters = IsCombinedBindingPin(Pin); SGraphPin::Construct(SGraphPin::FArguments(), Pin); }
	protected:
		virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override
		{
			SGraphPin::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);

			const UEdGraphPin* SourcePin = GetWidgetSource(GraphPinObj);
			const bool bUsesDefaultSelf = UsesDefaultSelfWidget(GraphPinObj);
			if (!bWidgetSourceInitialized)
			{
				WidgetSourcePin = SourcePin;
				bWidgetUsesDefaultSelf = bUsesDefaultSelf;
				bWidgetSourceInitialized = true;
				RefreshOptions();
				if (!IsCurrentPropertyAvailable())
				{
					ResetWidgetProperty();
				}
				if (ComboBox.IsValid())
				{
					ComboBox->RefreshOptions();
				}
				return;
			}
			if (WidgetSourcePin != SourcePin || bWidgetUsesDefaultSelf != bUsesDefaultSelf)
			{
				WidgetSourcePin = SourcePin;
				bWidgetUsesDefaultSelf = bUsesDefaultSelf;
				RefreshOptions();
				if (!IsCurrentPropertyAvailable())
				{
					ResetWidgetProperty();
				}
				if (ComboBox.IsValid())
				{
					ComboBox->RefreshOptions();
				}
			}
		}
		virtual TSharedRef<SWidget> GetDefaultValueWidget() override
		{
			RefreshOptions();
			return SAssignNew(ComboBox, SComboBox<TSharedPtr<FPropertyOption>>)
				.Visibility(this, &SGraphPin::GetDefaultValueVisibility)
				.IsEnabled(this, &SGraphPin::GetDefaultValueIsEditable)
				.OptionsSource(&Options)
				.OnComboBoxOpening(this, &SWidgetPropertyPathPin::RefreshOptions)
				.OnGenerateWidget(this, &SWidgetPropertyPathPin::MakeOption)
				.OnSelectionChanged(this, &SWidgetPropertyPathPin::SelectOption)
				.Content()
				[
					SNew(STextBlock)
					.Text(this, &SWidgetPropertyPathPin::GetCurrentValue)
					.Font(FAppStyle::GetFontStyle("PropertyWindow.NormalFont"))
				];
		}
	private:
		bool IsCurrentPropertyAvailable() const
		{
			const FString CurrentProperty = GraphPinObj->GetDefaultAsString();
			return CurrentProperty.IsEmpty() || CurrentProperty == TEXT("None") || Options.ContainsByPredicate([&CurrentProperty](const TSharedPtr<FPropertyOption>& Option)
			{
				return Option.IsValid() && !Option->bHeader && Option->Path == CurrentProperty;
			});
		}
		void ResetWidgetProperty()
		{
			if (GraphPinObj->GetDefaultAsString().IsEmpty() || GraphPinObj->GetDefaultAsString() == TEXT("None"))
			{
				return;
			}
			GraphPinObj->Modify();
			GraphPinObj->GetSchema()->TrySetDefaultValue(*GraphPinObj, TEXT("None"));
		}
		void RefreshOptions()
		{
			Options.Reset();
			if (bIncludeMaterialParameters)
			{
				Options.Add(MakeShared<FPropertyOption>(FPropertyOption{ TEXT("None"), TEXT("None"), FLinearColor(0.55f, 0.55f, 0.55f) }));
			}
			AddProperties(GetWidgetClassForPin(GraphPinObj), FString(), 0, Options);
			if (UWidget* Widget = GetDesignerWidget(GraphPinObj); Widget && Widget->Slot)
			{
				Options.Add(MakeShared<FPropertyOption>(FPropertyOption{ TEXT("Slot"), FString(), FLinearColor::White, true }));
				AddProperties(Widget->Slot->GetClass(), TEXT("Slot."), 0, Options);
			}
			if (bIncludeMaterialParameters)
			{
				UMaterialInterface* Material = GetDesignerMaterial(GraphPinObj);
				if (!Material)
				{
					return;
				}
				Options.Add(MakeShared<FPropertyOption>(FPropertyOption{ TEXT("Material Parameters"), FString(), FLinearColor::White, true }));
				TArray<FMaterialParameterInfo> Parameters;
				TArray<FGuid> Ids;
				const auto AddMaterialParameters = [this, &Material, &Parameters, &Ids](bool bVector)
				{
					Parameters.Reset(); Ids.Reset();
					if (bVector)
					{
						Material->GetAllVectorParameterInfo(Parameters, Ids);
					}
					else
					{
						Material->GetAllScalarParameterInfo(Parameters, Ids);
					}
					const FLinearColor TypeColor = bVector ? FLinearColor(0.25f, 0.65f, 1.0f) : FLinearColor(0.35f, 0.85f, 0.35f);
					for (const FMaterialParameterInfo& Parameter : Parameters)
					{
						if (Parameter.Association == EMaterialParameterAssociation::GlobalParameter)
						{
							Options.Add(MakeShared<FPropertyOption>(FPropertyOption{ Parameter.Name.ToString(), TEXT("Material.") + Parameter.Name.ToString(), TypeColor }));
						}
					}
				};
				AddMaterialParameters(false);
				AddMaterialParameters(true);
			}
		}
		TSharedRef<SWidget> MakeOption(TSharedPtr<FPropertyOption> Option) const
		{
			if (!Option.IsValid() || Option->bHeader)
			{
				return SNew(SBox)
					.IsEnabled(false)
					[
						SNew(STextBlock)
						.Text(FText::FromString(Option.IsValid() ? Option->Label : FString()))
						.Font(FAppStyle::GetFontStyle("PropertyWindow.BoldFont"))
					];
			}
			return SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.FillWidth(1.0f)
				[
					SNew(STextBlock)
					.Text(FText::FromString(Option->Label))
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(8.0f, 0.0f, 2.0f, 0.0f)
				[
					SNew(SImage)
					.Image(FAppStyle::GetBrush("Kismet.VariableList.TypeIcon"))
					.ColorAndOpacity(Option->TypeColor)
				];
		}
		void SelectOption(TSharedPtr<FPropertyOption> Option, ESelectInfo::Type)
		{
			if (!Option.IsValid() || Option->bHeader || GraphPinObj->GetDefaultAsString() == Option->Path)
			{
				return;
			}
			GraphPinObj->Modify();
			GraphPinObj->GetSchema()->TrySetDefaultValue(*GraphPinObj, Option->Path);
		}
		FText GetCurrentValue() const { const FString Value = GraphPinObj->GetDefaultAsString(); return Value.IsEmpty() || Value == TEXT("None") ? NSLOCTEXT("UMGTransitions", "NoBinding", "None") : FText::FromString(Value); }
		TArray<TSharedPtr<FPropertyOption>> Options;
		TSharedPtr<SComboBox<TSharedPtr<FPropertyOption>>> ComboBox;
		bool bIncludeMaterialParameters = false;
		bool bWidgetSourceInitialized = false;
		const UEdGraphPin* WidgetSourcePin = nullptr;
		bool bWidgetUsesDefaultSelf = false;
	};

	static bool IsTransitionValuePin(const UEdGraphPin* Pin)
	{
		return Pin && Pin->Direction == EGPD_Input && Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Struct && Pin->PinType.PinSubCategoryObject == FWidgetTransitionValue::StaticStruct();
	}
	static bool IsTransitionEasingPin(const UEdGraphPin* Pin)
	{
		return Pin && Pin->Direction == EGPD_Input && Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Struct && Pin->PinType.PinSubCategoryObject == FWidgetTransitionEasing::StaticStruct();
	}
	static const FProperty* GetFunctionParameter(const UEdGraphPin* Pin)
	{
		const UK2Node_CallFunction* Node = Pin ? Cast<UK2Node_CallFunction>(Pin->GetOwningNode()) : nullptr;
		const UFunction* Function = Node ? Node->GetTargetFunction() : nullptr;
		return Function ? Function->FindPropertyByName(Pin->PinName) : nullptr;
	}
	static bool IsSegmentedEnumPin(const UEdGraphPin* Pin)
	{
		const FProperty* Parameter = GetFunctionParameter(Pin);
		return Pin && Pin->Direction == EGPD_Input && Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Byte && Cast<UEnum>(Pin->PinType.PinSubCategoryObject.Get()) && Parameter && Parameter->HasMetaData(TEXT("UMGTransitionsSegmentedControl"));
	}

	/** Generic compact enum editor enabled by UMGTransitionsSegmentedControl parameter metadata. */
	class SSegmentedEnumPin final : public SGraphPin
	{
	public:
		SLATE_BEGIN_ARGS(SSegmentedEnumPin) {} SLATE_END_ARGS()
		void Construct(const FArguments&, UEdGraphPin* Pin)
		{
			SGraphPin::Construct(SGraphPin::FArguments(), Pin);
		}

	protected:
		virtual TSharedRef<SWidget> GetDefaultValueWidget() override
		{
			const UEnum* Enum = Cast<UEnum>(GraphPinObj->PinType.PinSubCategoryObject.Get());
			TSharedRef<SSegmentedControl<int64>> SegmentedControl = SNew(SSegmentedControl<int64>)
				.Value(this, &SSegmentedEnumPin::GetValue)
				.OnValueChanged(this, &SSegmentedEnumPin::SetValue);
			if (Enum)
			{
				// UEnum stores its generated _MAX sentinel as the final entry.
				for (int32 Index = 0; Index < Enum->NumEnums() - 1; ++Index)
				{
					if (Enum->HasMetaData(TEXT("Hidden"), Index) || Enum->HasMetaData(TEXT("Spacer"), Index))
					{
						continue;
					}
					const int64 Value = Enum->GetValueByIndex(Index);
					FText ToolTip = Enum->GetToolTipTextByIndex(Index);
					if (ToolTip.IsEmpty())
					{
						ToolTip = Enum->GetDisplayNameTextByIndex(Index);
					}
					SegmentedControl->AddSlot(Value)
					.ToolTip(ToolTip)
					[
						SNew(STextBlock)
						.Text(Enum->GetDisplayNameTextByIndex(Index))
					];
				}
			}
			return SNew(SBox)
				.Visibility(this, &SGraphPin::GetDefaultValueVisibility)
				.IsEnabled(this, &SGraphPin::GetDefaultValueIsEditable)
				[
					SegmentedControl
				];
		}

	private:
		int64 GetValue() const
		{
			const UEnum* Enum = Cast<UEnum>(GraphPinObj->PinType.PinSubCategoryObject.Get());
			if (!Enum)
			{
				return 0;
			}
			const int64 Value = Enum->GetValueByNameString(GraphPinObj->GetDefaultAsString());
			return Value == INDEX_NONE ? Enum->GetValueByIndex(0) : Value;
		}
		void SetValue(int64 Value)
		{
			const UEnum* Enum = Cast<UEnum>(GraphPinObj->PinType.PinSubCategoryObject.Get());
			if (Enum)
			{
				GraphPinObj->GetSchema()->TrySetDefaultValue(*GraphPinObj, Enum->GetNameStringByValue(Value));
			}
		}
	};

	class STransitionValuePin final : public SGraphPin
	{
	public:
		SLATE_BEGIN_ARGS(STransitionValuePin) {} SLATE_END_ARGS()
		void Construct(const FArguments&, UEdGraphPin* Pin)
		{
			SGraphPin::Construct(SGraphPin::FArguments(), Pin);
		}

	protected:
		virtual TSharedRef<SWidget> GetDefaultValueWidget() override
		{
			return SNew(SBox)
				.Visibility(this, &SGraphPin::GetDefaultValueVisibility)
				.IsEnabled(this, &SGraphPin::GetDefaultValueIsEditable)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				[
					MakeFloatEditor()
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				[
					MakeVectorEditor()
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(4.0f, 0.0f, 0.0f, 0.0f)
				.VAlign(VAlign_Center)
				[
					MakeColorEditor()
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(4.0f, 0.0f, 0.0f, 0.0f)
				.VAlign(VAlign_Center)
				[
					SNew(SSegmentedControl<EWidgetTransitionValueType>)
						.Visibility(this, &SGraphPin::GetDefaultValueVisibility)
						.IsEnabled(this, &SGraphPin::GetDefaultValueIsEditable)
						.Value(this, &STransitionValuePin::GetValueType)
						.OnValueChanged(this, &STransitionValuePin::SetValueType)
						+ SSegmentedControl<EWidgetTransitionValueType>::Slot(EWidgetTransitionValueType::Float)
						[
							SNew(STextBlock)
							.Text(FText::FromString(TEXT("F")))
							.ToolTipText(FText::FromString(TEXT("Float")))
						]
						+ SSegmentedControl<EWidgetTransitionValueType>::Slot(EWidgetTransitionValueType::Vector2D)
						[
							SNew(STextBlock)
							.Text(FText::FromString(TEXT("V")))
							.ToolTipText(FText::FromString(TEXT("Vector 2D")))
						]
						+ SSegmentedControl<EWidgetTransitionValueType>::Slot(EWidgetTransitionValueType::LinearColor)
						[
							SNew(STextBlock)
							.Text(FText::FromString(TEXT("C")))
							.ToolTipText(FText::FromString(TEXT("Linear Color")))
						]
				]
			];
		}

	private:
		FWidgetTransitionValue GetValue() const
		{
			FWidgetTransitionValue Value;
			if (!GraphPinObj->GetDefaultAsString().IsEmpty())
			{
				FOutputDeviceNull NullOutput;
				FWidgetTransitionValue::StaticStruct()->ImportText(*GraphPinObj->GetDefaultAsString(), &Value, nullptr, PPF_SerializedAsImportText, &NullOutput, GraphPinObj->PinName.ToString());
			}
			return Value;
		}
		void SetValue(FWidgetTransitionValue Value) const
		{
			FString Text;
			FWidgetTransitionValue::StaticStruct()->ExportText(Text, &Value, nullptr, nullptr, PPF_SerializedAsImportText, nullptr);
			GraphPinObj->GetSchema()->TrySetDefaultValue(*GraphPinObj, Text);
		}
		EWidgetTransitionValueType GetValueType() const { return GetValue().Type; }
		void SetValueType(EWidgetTransitionValueType Type)
		{
			FWidgetTransitionValue Value = GetValue();
			if (Type == EWidgetTransitionValueType::LinearColor && Value.Type != EWidgetTransitionValueType::LinearColor && FMath::IsNearlyZero(Value.Channels.W))
			{
				Value.Channels.W = 1.0f;
			}
			Value.Type = Type;
			SetValue(Value);
		}
		void SetChannel(float NewValue, int32 Channel)
		{
			FWidgetTransitionValue Value = GetValue();
			Value.Channels[Channel] = NewValue;
			SetValue(Value);
		}
		EVisibility GetColorVisibility() const { return GetValueType() == EWidgetTransitionValueType::LinearColor ? EVisibility::Visible : EVisibility::Collapsed; }
		FLinearColor GetColor() const
		{
			const FVector4f Channels = GetValue().Channels;
			return FLinearColor(Channels.X, Channels.Y, Channels.Z, Channels.W);
		}
		FReply OnColorClicked(const FGeometry&, const FPointerEvent& MouseEvent)
		{
			if (MouseEvent.GetEffectingButton() != EKeys::LeftMouseButton)
			{
				return FReply::Unhandled();
			}
			FColorPickerArgs PickerArgs;
			PickerArgs.bIsModal = true;
			PickerArgs.bUseAlpha = true;
			PickerArgs.InitialColor = GetColor();
			PickerArgs.ParentWidget = AsShared();
			PickerArgs.OnColorCommitted = FOnLinearColorValueChanged::CreateSP(this, &STransitionValuePin::OnColorCommitted);
			OpenColorPicker(PickerArgs);
			return FReply::Handled();
		}
		void OnColorCommitted(FLinearColor NewColor)
		{
			FWidgetTransitionValue Value = GetValue();
			Value.Channels = FVector4f(NewColor.R, NewColor.G, NewColor.B, NewColor.A);
			SetValue(Value);
		}
		EVisibility GetFloatVisibility() const { return GetValueType() == EWidgetTransitionValueType::Float ? EVisibility::Visible : EVisibility::Collapsed; }
		EVisibility GetVectorVisibility() const { return GetValueType() == EWidgetTransitionValueType::Vector2D ? EVisibility::Visible : EVisibility::Collapsed; }
		TOptional<float> GetFloatValue() const { return GetValue().Channels.X; }
		void CommitFloat(float NewValue, ETextCommit::Type) { SetChannel(NewValue, 0); }
		FString GetVectorX() const { return FString::Printf(TEXT("%f"), GetValue().Channels.X); }
		FString GetVectorY() const { return FString::Printf(TEXT("%f"), GetValue().Channels.Y); }
		void CommitVectorX(float NewValue, ETextCommit::Type) { SetChannel(NewValue, 0); }
		void CommitVectorY(float NewValue, ETextCommit::Type) { SetChannel(NewValue, 1); }
		TSharedRef<SWidget> MakeFloatEditor()
		{
			return SNew(SBox)
				.Visibility(this, &STransitionValuePin::GetFloatVisibility)
			[
				SNew(SNumericEntryBox<float>)
					.EditableTextBoxStyle(FAppStyle::Get(), "Graph.EditableTextBox")
					.BorderForegroundColor(FSlateColor::UseForeground())
					.Value(this, &STransitionValuePin::GetFloatValue)
					.OnValueCommitted(this, &STransitionValuePin::CommitFloat)
			];
		}
		TSharedRef<SWidget> MakeVectorEditor()
		{
			return SNew(SVector2DTextBox<float>)
				.Visibility(this, &STransitionValuePin::GetVectorVisibility)
				.VisibleText_X(this, &STransitionValuePin::GetVectorX)
				.VisibleText_Y(this, &STransitionValuePin::GetVectorY)
				.OnNumericCommitted_Box_X(this, &STransitionValuePin::CommitVectorX)
				.OnNumericCommitted_Box_Y(this, &STransitionValuePin::CommitVectorY);
		}
		TSharedRef<SWidget> MakeColorEditor()
		{
			return SNew(SBox)
				.Visibility(this, &STransitionValuePin::GetColorVisibility)
			[
				SNew(SColorBlock)
					.Color(this, &STransitionValuePin::GetColor)
					.ShowBackgroundForAlpha(true)
					.OnMouseButtonDown(this, &STransitionValuePin::OnColorClicked)
			];
		}
	};

	class STransitionEasingPin final : public SGraphPin
	{
	public:
		SLATE_BEGIN_ARGS(STransitionEasingPin) {} SLATE_END_ARGS()
		void Construct(const FArguments&, UEdGraphPin* Pin)
		{
			SGraphPin::Construct(SGraphPin::FArguments(), Pin);
		}

	protected:
		virtual TSharedRef<SWidget> GetDefaultValueWidget() override
		{
			return SNew(SBox)
				.Visibility(this, &SGraphPin::GetDefaultValueVisibility)
				.IsEnabled(this, &SGraphPin::GetDefaultValueIsEditable)
			[
				SNew(SCubicBezierEasingEditor)
				.Value(this, &STransitionEasingPin::GetValue)
				.OnValueChanged(FOnCubicBezierEasingChanged::CreateSP(this, &STransitionEasingPin::SetValue))
			];
		}

	private:
		FWidgetTransitionEasing GetValue() const
		{
			FWidgetTransitionEasing Value;
			if (!GraphPinObj->GetDefaultAsString().IsEmpty())
			{
				FOutputDeviceNull NullOutput;
				FWidgetTransitionEasing::StaticStruct()->ImportText(*GraphPinObj->GetDefaultAsString(), &Value, nullptr, PPF_SerializedAsImportText, &NullOutput, GraphPinObj->PinName.ToString());
			}
			return Value;
		}

		void SetValue(FWidgetTransitionEasing Value)
		{
			FString Text;
			FWidgetTransitionEasing::StaticStruct()->ExportText(Text, &Value, nullptr, nullptr, PPF_SerializedAsImportText, nullptr);
			GraphPinObj->GetSchema()->TrySetDefaultValue(*GraphPinObj, Text);
		}
	};

	class FTransitionPinFactory final : public FGraphPanelPinFactory
	{
	public:
		virtual TSharedPtr<SGraphPin> CreatePin(UEdGraphPin* Pin) const override
		{
			if (IsBindingPropertyPin(Pin))
			{
				return SNew(SWidgetPropertyPathPin, Pin);
			}
			if (IsTransitionValuePin(Pin))
			{
				return SNew(STransitionValuePin, Pin);
			}
			if (IsTransitionEasingPin(Pin))
			{
				return SNew(STransitionEasingPin, Pin);
			}
			if (IsSegmentedEnumPin(Pin))
			{
				return SNew(SSegmentedEnumPin, Pin);
			}
			return nullptr;
		}
	};
}

class FUMGTransitionsEditorModule final : public IModuleInterface
{
public:
	virtual void StartupModule() override { PinFactory = MakeShared<UMGTransitionsEditor::FTransitionPinFactory>(); FEdGraphUtilities::RegisterVisualPinFactory(PinFactory); }
	virtual void ShutdownModule() override { if (PinFactory.IsValid()) { FEdGraphUtilities::UnregisterVisualPinFactory(PinFactory); PinFactory.Reset(); } }
private:
	TSharedPtr<FGraphPanelPinFactory> PinFactory;
};

IMPLEMENT_MODULE(FUMGTransitionsEditorModule, UMGTransitionsEditor)
