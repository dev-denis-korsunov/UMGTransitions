#include "EdGraphUtilities.h"
#include "WidgetTransition.h"

#include "Blueprint/WidgetTree.h"
#include "Components/PanelSlot.h"
#include "Components/Border.h"
#include "Components/Image.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphSchema_K2.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "K2Node_CallFunction.h"
#include "K2Node_Self.h"
#include "SGraphPin.h"
#include "Framework/Application/SlateApplication.h"
#include "KismetPins/SVector2DTextBox.h"
#include "K2Node_VariableGet.h"
#include "Materials/MaterialInterface.h"
#include "Misc/OutputDeviceNull.h"
#include "ScopedTransaction.h"
#include "Styling/AppStyle.h"
#include "Styling/CoreStyle.h"
#include "Styling/ToolBarStyle.h"
#include "WidgetBlueprint.h"
#include "Widgets/Input/SNumericEntryBox.h"
#include "Widgets/Input/SSegmentedControl.h"
#include "Widgets/Colors/SColorBlock.h"
#include "Widgets/Colors/SColorPicker.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/SLeafWidget.h"
#include "Widgets/SBoxPanel.h"
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
	DECLARE_DELEGATE(FOnCubicBezierEasingEdit);

	/** A named cubic-Bezier shape that fits the transition easing's two editable control points. */
	struct FEasingTemplate
	{
		FEasingTemplate(const TCHAR* InName, FVector2D FirstControlPoint, FVector2D SecondControlPoint)
			: Name(InName)
		{
			Easing.FirstControlPoint = FirstControlPoint;
			Easing.SecondControlPoint = SecondControlPoint;
		}

		FString Name;
		FWidgetTransitionEasing Easing;
	};

	static void AddEasingTemplate(TArray<TSharedPtr<FEasingTemplate>>& Templates, const TCHAR* Name, double FirstX, double FirstY, double SecondX, double SecondY)
	{
		Templates.Add(MakeShared<FEasingTemplate>(Name, FVector2D(FirstX, FirstY), FVector2D(SecondX, SecondY)));
	}

	/**
	 * Common named curves that are expressible by one cubic Bezier segment.
	 * Bounce and Elastic deliberately do not appear: their multiple oscillations need more than two control points.
	 */
	static TArray<TSharedPtr<FEasingTemplate>> MakeEasingTemplates()
	{
		TArray<TSharedPtr<FEasingTemplate>> Templates;
		Templates.Reserve(29);

		AddEasingTemplate(Templates, TEXT("Linear"), 0.0, 0.0, 1.0, 1.0);
		AddEasingTemplate(Templates, TEXT("Ease"), 0.25, 0.1, 0.25, 1.0);
		AddEasingTemplate(Templates, TEXT("Ease In"), 0.42, 0.0, 1.0, 1.0);
		AddEasingTemplate(Templates, TEXT("Ease Out"), 0.0, 0.0, 0.58, 1.0);
		AddEasingTemplate(Templates, TEXT("Ease In Out"), 0.42, 0.0, 0.58, 1.0);

		AddEasingTemplate(Templates, TEXT("Sine In"), 0.12, 0.0, 0.39, 0.0);
		AddEasingTemplate(Templates, TEXT("Sine Out"), 0.61, 1.0, 0.88, 1.0);
		AddEasingTemplate(Templates, TEXT("Sine In Out"), 0.37, 0.0, 0.63, 1.0);
		AddEasingTemplate(Templates, TEXT("Quad In"), 0.11, 0.0, 0.50, 0.0);
		AddEasingTemplate(Templates, TEXT("Quad Out"), 0.50, 1.0, 0.89, 1.0);
		AddEasingTemplate(Templates, TEXT("Quad In Out"), 0.45, 0.0, 0.55, 1.0);
		AddEasingTemplate(Templates, TEXT("Cubic In"), 0.32, 0.0, 0.67, 0.0);
		AddEasingTemplate(Templates, TEXT("Cubic Out"), 0.33, 1.0, 0.68, 1.0);
		AddEasingTemplate(Templates, TEXT("Cubic In Out"), 0.65, 0.0, 0.35, 1.0);
		AddEasingTemplate(Templates, TEXT("Quart In"), 0.50, 0.0, 0.75, 0.0);
		AddEasingTemplate(Templates, TEXT("Quart Out"), 0.25, 1.0, 0.50, 1.0);
		AddEasingTemplate(Templates, TEXT("Quart In Out"), 0.76, 0.0, 0.24, 1.0);
		AddEasingTemplate(Templates, TEXT("Quint In"), 0.64, 0.0, 0.78, 0.0);
		AddEasingTemplate(Templates, TEXT("Quint Out"), 0.22, 1.0, 0.36, 1.0);
		AddEasingTemplate(Templates, TEXT("Quint In Out"), 0.83, 0.0, 0.17, 1.0);
		AddEasingTemplate(Templates, TEXT("Expo In"), 0.70, 0.0, 0.84, 0.0);
		AddEasingTemplate(Templates, TEXT("Expo Out"), 0.16, 1.0, 0.30, 1.0);
		AddEasingTemplate(Templates, TEXT("Expo In Out"), 0.87, 0.0, 0.13, 1.0);
		AddEasingTemplate(Templates, TEXT("Circ In"), 0.55, 0.0, 1.0, 0.45);
		AddEasingTemplate(Templates, TEXT("Circ Out"), 0.0, 0.55, 0.45, 1.0);
		AddEasingTemplate(Templates, TEXT("Circ In Out"), 0.85, 0.0, 0.15, 1.0);
		AddEasingTemplate(Templates, TEXT("Back In"), 0.36, 0.0, 0.66, -0.5);
		AddEasingTemplate(Templates, TEXT("Back Out"), 0.34, 1.5, 0.64, 1.0);
		AddEasingTemplate(Templates, TEXT("Back In Out"), 0.68, -0.5, 0.32, 1.5);
		return Templates;
	}

	/** Compact direct-manipulation editor for the two control points of a cubic Bezier easing. */
	class SCubicBezierEasingEditor final : public SLeafWidget
	{
	public:
		SLATE_BEGIN_ARGS(SCubicBezierEasingEditor) {}
			SLATE_ATTRIBUTE(FWidgetTransitionEasing, Value)
			SLATE_EVENT(FOnCubicBezierEasingChanged, OnValueChanged)
			SLATE_EVENT(FOnCubicBezierEasingEdit, OnEditStarted)
			SLATE_EVENT(FOnCubicBezierEasingEdit, OnEditFinished)
		SLATE_END_ARGS()

		void Construct(const FArguments& InArgs)
		{
			ValueAttribute = InArgs._Value;
			OnValueChanged = InArgs._OnValueChanged;
			OnEditStarted = InArgs._OnEditStarted;
			OnEditFinished = InArgs._OnEditFinished;
			SetClipping(EWidgetClipping::ClipToBounds);
		}

		virtual FVector2D ComputeDesiredSize(float) const override
		{
			return FVector2D(VisualCanvasWidth + AnchorSize, VisualCanvasHeight + AnchorSize);
		}

		virtual int32 OnPaint(const FPaintArgs&, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle&, bool) const override
		{
			const FSlateBrush* WhiteBrush = FCoreStyle::Get().GetBrush("WhiteBrush");
			const FSlateRect Canvas = GetCanvasRect(AllottedGeometry);
			FSlateDrawElement::MakeBox(OutDrawElements, LayerId, AllottedGeometry.ToPaintGeometry(Canvas.GetSize(), FSlateLayoutTransform(FVector2D(Canvas.Left, Canvas.Top))), WhiteBrush, ESlateDrawEffect::None, FLinearColor(0.012f, 0.014f, 0.015f, 1.0f));

			const FSlateRect Plot = GetPlotRect(AllottedGeometry);
			const FPaintGeometry PaintGeometry = AllottedGeometry.ToPaintGeometry();
			const FLinearColor GridColor(0.048f, 0.053f, 0.056f, 0.9f);
			DrawAlignedGrid(OutDrawElements, LayerId + 1, PaintGeometry, Canvas, Plot, 2, 2, GridColor);

			const FWidgetTransitionEasing Easing = ValueAttribute.Get();
			const FVector2D Start = ToScreen(Plot, FVector2D::Zero());
			const FVector2D End = ToScreen(Plot, FVector2D(1.0, 1.0));
			const FVector2D MathematicalFirst = ToScreen(Plot, Easing.FirstControlPoint);
			const FVector2D MathematicalSecond = ToScreen(Plot, Easing.SecondControlPoint);
			const FVector2D First = GetVisualHandlePosition(Start, MathematicalFirst);
			const FVector2D Second = GetVisualHandlePosition(End, MathematicalSecond);
			const FLinearColor HandleColor(0.48f, 0.55f, 0.62f, 0.9f);
			const FLinearColor HoveredHandleColor(0.34f, 0.90f, 0.12f, 1.0f);
			const FLinearColor FirstHandleColor = HoveredHandle == EHandle::First ? HoveredHandleColor : HandleColor;
			const FLinearColor SecondHandleColor = HoveredHandle == EHandle::Second ? HoveredHandleColor : HandleColor;
			FSlateDrawElement::MakeLines(OutDrawElements, LayerId + 2, PaintGeometry, { FVector2f(Start), FVector2f(First) }, ESlateDrawEffect::None, FirstHandleColor, true, 1.0f);
			FSlateDrawElement::MakeLines(OutDrawElements, LayerId + 2, PaintGeometry, { FVector2f(Second), FVector2f(End) }, ESlateDrawEffect::None, SecondHandleColor, true, 1.0f);
			const FLinearColor AxisColor(0.16f, 0.17f, 0.18f, 0.95f);
			FSlateDrawElement::MakeLines(OutDrawElements, LayerId + 2, PaintGeometry, { FVector2f(Plot.Left, Start.Y), FVector2f(Plot.Right, Start.Y) }, ESlateDrawEffect::None, AxisColor, true, 1.0f);
			FSlateDrawElement::MakeLines(OutDrawElements, LayerId + 2, PaintGeometry, { FVector2f(Plot.Left, End.Y), FVector2f(Plot.Right, End.Y) }, ESlateDrawEffect::None, AxisColor, true, 1.0f);
			const FLinearColor CurveColor(0.30f, 0.72f, 1.0f, 1.0f);
			FSlateDrawElement::MakeCubicBezierSpline(OutDrawElements, LayerId + 3, PaintGeometry, FVector2f(Start), FVector2f(MathematicalFirst), FVector2f(MathematicalSecond), FVector2f(End), 2.5f, ESlateDrawEffect::None, CurveColor);
			DrawAnchorPoint(OutDrawElements, LayerId + 4, AllottedGeometry, Start, CurveColor);
			DrawAnchorPoint(OutDrawElements, LayerId + 4, AllottedGeometry, End, CurveColor);
			DrawHandle(OutDrawElements, LayerId + 5, AllottedGeometry, First, FirstHandleColor);
			DrawHandle(OutDrawElements, LayerId + 5, AllottedGeometry, Second, SecondHandleColor);
			return LayerId + 5;
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
			const FVector2D FirstHandle = GetVisualHandlePosition(Start, ToScreen(Plot, Easing.FirstControlPoint));
			const FVector2D SecondHandle = GetVisualHandlePosition(End, ToScreen(Plot, Easing.SecondControlPoint));
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
			OnEditStarted.ExecuteIfBound();
			return FReply::Handled().CaptureMouse(AsShared());
		}

		virtual FReply OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override
		{
			if (!HasMouseCapture())
			{
				UpdateHoveredHandle(MyGeometry, MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition()));
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
				Easing.FirstControlPoint += GetConstrainedHandleValueDelta(Plot, FVector2D::Zero(), Easing.FirstControlPoint, ScreenDelta);
				if (MouseEvent.IsShiftDown())
				{
					Easing.FirstControlPoint = SnapControlPointToVisualGrid(Plot, FVector2D::Zero(), Easing.FirstControlPoint);
				}
				break;
			}
			case EDragMode::SecondHandle:
			{
				Easing.SecondControlPoint += GetConstrainedHandleValueDelta(Plot, FVector2D(1.0, 1.0), Easing.SecondControlPoint, ScreenDelta);
				if (MouseEvent.IsShiftDown())
				{
					Easing.SecondControlPoint = SnapControlPointToVisualGrid(Plot, FVector2D(1.0, 1.0), Easing.SecondControlPoint);
				}
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
				OnEditFinished.ExecuteIfBound();
				return FReply::Handled().ReleaseMouseCapture();
			}
			return FReply::Unhandled();
		}

		virtual void OnMouseLeave(const FPointerEvent& MouseEvent) override
		{
			SLeafWidget::OnMouseLeave(MouseEvent);
			HoveredHandle = EHandle::None;
			Invalidate(EInvalidateWidgetReason::Paint);
		}

		private:
		enum class EHandle : uint8
		{
			None,
			First,
			Second,
		};

		enum class EDragMode : uint8
		{
			None,
			FirstHandle,
			SecondHandle,
			Sculpt,
		};

		static constexpr float HandleSize = 8.0f;
		static constexpr float AnchorSize = 10.0f;
		static constexpr float HandleHitRadius = 10.0f;
		static constexpr double VisualCanvasWidth = 165.0;
		static constexpr double VisualCanvasHeight = 154.6875;
		static constexpr double VisualPlotHeight = 82.5;
		/** Visual handle length relative to the mathematical control vector. */
		static constexpr double HandleVisualScale = 0.5;
		/** Brush radius expressed as normalized curve length. */
		static constexpr double SculptRadius = 1.0 / 3.0;
		/** The two handles reach their maximum geometric influence near these curve positions. */
		static constexpr double FirstHandleCurvePosition = 1.0 / 3.0;
		static constexpr double SecondHandleCurvePosition = 2.0 / 3.0;

		FSlateRect GetPlotRect(const FGeometry& Geometry) const
		{
			const FSlateRect Canvas = GetCanvasRect(Geometry);
			const double WorkAreaHeight = FMath::Min(VisualPlotHeight, Canvas.GetSize().Y);
			const double VerticalPadding = (Canvas.GetSize().Y - WorkAreaHeight) * 0.5;
			return FSlateRect(Canvas.Left, Canvas.Top + VerticalPadding, Canvas.Right, Canvas.Top + VerticalPadding + WorkAreaHeight);
		}

		/** Keeps half a handle of transparent hit area around every side of the visual canvas. */
		FSlateRect GetCanvasRect(const FGeometry& Geometry) const
		{
			const FVector2D Size = Geometry.GetLocalSize();
			const FVector2D CanvasSize(FMath::Min(VisualCanvasWidth, Size.X - AnchorSize), FMath::Min(VisualCanvasHeight, Size.Y - AnchorSize));
			const FVector2D Offset = (Size - CanvasSize) * 0.5;
			return FSlateRect(Offset.X, Offset.Y, Offset.X + CanvasSize.X, Offset.Y + CanvasSize.Y);
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

		static FVector2D GetVisualHandlePosition(FVector2D Anchor, FVector2D MathematicalHandle)
		{
			return FMath::Lerp(Anchor, MathematicalHandle, HandleVisualScale);
		}

		static FVector2D GetConstrainedHandleValueDelta(const FSlateRect& Plot, FVector2D AnchorValue, FVector2D MathematicalHandle, FVector2D ScreenDelta)
		{
			const FVector2D Anchor = ToScreen(Plot, AnchorValue);
			const FVector2D VisualHandle = GetVisualHandlePosition(Anchor, ToScreen(Plot, MathematicalHandle));
			FVector2D ConstrainedHandle = VisualHandle + ScreenDelta;
			ConstrainedHandle.X = FMath::Clamp(ConstrainedHandle.X, static_cast<double>(Plot.Left), static_cast<double>(Plot.Right));
			const FVector2D EffectiveScreenDelta = ConstrainedHandle - VisualHandle;
			return FVector2D(EffectiveScreenDelta.X / Plot.GetSize().X, -EffectiveScreenDelta.Y / Plot.GetSize().Y) / HandleVisualScale;
		}

		static FVector2D SnapControlPointToVisualGrid(const FSlateRect& Plot, FVector2D AnchorValue, FVector2D MathematicalHandle)
		{
			constexpr double SnapStep = 0.05;
			const FVector2D Anchor = ToScreen(Plot, AnchorValue);
			const FVector2D VisualHandle = GetVisualHandlePosition(Anchor, ToScreen(Plot, MathematicalHandle));
			FVector2D VisualValue(
				(VisualHandle.X - Plot.Left) / Plot.GetSize().X,
				(Plot.Bottom - VisualHandle.Y) / Plot.GetSize().Y);
			VisualValue.X = FMath::GridSnap(VisualValue.X, SnapStep);
			VisualValue.Y = FMath::GridSnap(VisualValue.Y, SnapStep);
			const FVector2D SnappedVisual = ToScreen(Plot, VisualValue);
			const FVector2D SnappedMathematical = Anchor + (SnappedVisual - Anchor) / HandleVisualScale;
			return FVector2D(
				( SnappedMathematical.X - Plot.Left) / Plot.GetSize().X,
				(Plot.Bottom - SnappedMathematical.Y) / Plot.GetSize().Y);
		}

		void UpdateHoveredHandle(const FGeometry& Geometry, FVector2D LocalPosition)
		{
			const FSlateRect Plot = GetPlotRect(Geometry);
			const FWidgetTransitionEasing Easing = ValueAttribute.Get();
			const FVector2D FirstHandle = GetVisualHandlePosition(ToScreen(Plot, FVector2D::Zero()), ToScreen(Plot, Easing.FirstControlPoint));
			const FVector2D SecondHandle = GetVisualHandlePosition(ToScreen(Plot, FVector2D(1.0, 1.0)), ToScreen(Plot, Easing.SecondControlPoint));
			const EHandle NewHoveredHandle = (LocalPosition - FirstHandle).SizeSquared() <= HandleHitRadius * HandleHitRadius ? EHandle::First : (LocalPosition - SecondHandle).SizeSquared() <= HandleHitRadius * HandleHitRadius ? EHandle::Second : EHandle::None;
			if (HoveredHandle != NewHoveredHandle)
			{
				HoveredHandle = NewHoveredHandle;
				Invalidate(EInvalidateWidgetReason::Paint);
			}
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

		/** Draws a grid whose cells are defined by the working plot and continued through the visual canvas. */
		static void DrawAlignedGrid(FSlateWindowElementList& OutDrawElements, int32 LayerId, const FPaintGeometry& PaintGeometry, const FSlateRect& Canvas, const FSlateRect& Plot, int32 Columns, int32 Rows, FLinearColor Color)
		{
			const FVector2D CellSize(Plot.GetSize().X / Columns, Plot.GetSize().Y / Rows);
			const int32 FirstColumn = FMath::CeilToInt((Canvas.Left - Plot.Left) / CellSize.X);
			const int32 LastColumn = FMath::FloorToInt((Canvas.Right - Plot.Left) / CellSize.X);
			for (int32 Column = FirstColumn; Column <= LastColumn; ++Column)
			{
				const float X = static_cast<float>(Plot.Left + Column * CellSize.X);
				FSlateDrawElement::MakeLines(OutDrawElements, LayerId, PaintGeometry, { FVector2f(X, Canvas.Top), FVector2f(X, Canvas.Bottom) }, ESlateDrawEffect::None, Color, true, 1.0f);
			}

			const int32 FirstRow = FMath::CeilToInt((Canvas.Top - Plot.Top) / CellSize.Y);
			const int32 LastRow = FMath::FloorToInt((Canvas.Bottom - Plot.Top) / CellSize.Y);
			for (int32 Row = FirstRow; Row <= LastRow; ++Row)
			{
				const float Y = static_cast<float>(Plot.Top + Row * CellSize.Y);
				FSlateDrawElement::MakeLines(OutDrawElements, LayerId, PaintGeometry, { FVector2f(Canvas.Left, Y), FVector2f(Canvas.Right, Y) }, ESlateDrawEffect::None, Color, true, 1.0f);
			}

			FSlateDrawElement::MakeLines(OutDrawElements, LayerId, PaintGeometry, { FVector2f(Canvas.Left, Canvas.Top), FVector2f(Canvas.Right, Canvas.Top), FVector2f(Canvas.Right, Canvas.Bottom), FVector2f(Canvas.Left, Canvas.Bottom), FVector2f(Canvas.Left, Canvas.Top) }, ESlateDrawEffect::None, Color, true, 1.0f);
		}

		static void DrawHandle(FSlateWindowElementList& OutDrawElements, int32 LayerId, const FGeometry& Geometry, FVector2D Position, FLinearColor Color)
		{
			const FVector2D HandleVisualSize(HandleSize, HandleSize);
			FSlateDrawElement::MakeBox(OutDrawElements, LayerId, Geometry.ToPaintGeometry(HandleVisualSize, FSlateLayoutTransform(Position - HandleVisualSize * 0.5)), FCoreStyle::Get().GetBrush("WhiteBrush"), ESlateDrawEffect::None, Color);
		}

		static void DrawAnchorPoint(FSlateWindowElementList& OutDrawElements, int32 LayerId, const FGeometry& Geometry, FVector2D Position, FLinearColor Color)
		{
			const FVector2D PointSize(10.0, 10.0);
			FSlateDrawElement::MakeBox(OutDrawElements, LayerId, Geometry.ToPaintGeometry(PointSize, FSlateLayoutTransform(Position - PointSize * 0.5)), FAppStyle::Get().GetBrush("Graph.Pin.Connected"), ESlateDrawEffect::None, Color);
		}

		TAttribute<FWidgetTransitionEasing> ValueAttribute;
		FOnCubicBezierEasingChanged OnValueChanged;
		FOnCubicBezierEasingEdit OnEditStarted;
		FOnCubicBezierEasingEdit OnEditFinished;
		EDragMode DragMode = EDragMode::None;
		FVector2D LastLocalPosition = FVector2D::ZeroVector;
		double SculptT = 0.5;
		EHandle HoveredHandle = EHandle::None;
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
			const FScopedTransaction Transaction(NSLOCTEXT("UMGTransitions", "ChangeWidgetPropertyPin", "Change Widget Property Pin"));
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
				const FString NewValue = Enum->GetNameStringByValue(Value);
				if (GraphPinObj->GetDefaultAsString() != NewValue)
				{
					const FScopedTransaction Transaction(NSLOCTEXT("UMGTransitions", "ChangeSegmentedEnumPin", "Change Segmented Enum Pin"));
					GraphPinObj->Modify();
					GraphPinObj->GetSchema()->TrySetDefaultValue(*GraphPinObj, NewValue);
				}
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
				.Clipping(EWidgetClipping::ClipToBounds)
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
			if (GraphPinObj->GetDefaultAsString() == Text)
			{
				return;
			}
			const FScopedTransaction Transaction(NSLOCTEXT("UMGTransitions", "ChangeTransitionValuePin", "Change Transition Value Pin"));
			GraphPinObj->Modify();
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
			Templates = MakeEasingTemplates();
			SGraphPin::Construct(SGraphPin::FArguments(), Pin);
		}

	protected:
		virtual TSharedRef<SWidget> GetDefaultValueWidget() override
		{
			const FToolBarStyle& ToolBarStyle = FAppStyle::Get().GetWidgetStyle<FToolBarStyle>("EditorViewportToolBar");
			return SNew(SBox)
				.Visibility(this, &SGraphPin::GetDefaultValueVisibility)
				.IsEnabled(this, &SGraphPin::GetDefaultValueIsEditable)
				.Clipping(EWidgetClipping::ClipToBounds)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.0f, 0.0f, 0.0f, 3.0f)
				[
					SNew(SComboButton)
					.ButtonStyle(&ToolBarStyle.ButtonStyle)
					.ContentPadding(ToolBarStyle.ButtonPadding)
					.OnGetMenuContent(this, &STransitionEasingPin::GetTemplateMenuContent)
					.ButtonContent()
					[
						SNew(STextBlock)
						.Text(NSLOCTEXT("UMGTransitions", "EasingTemplate", "Template"))
						.TextStyle(&ToolBarStyle.LabelStyle)
						.ColorAndOpacity(FSlateColor::UseForeground())
					]
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					SNew(SCubicBezierEasingEditor)
					.Value(this, &STransitionEasingPin::GetValue)
					.OnValueChanged(FOnCubicBezierEasingChanged::CreateSP(this, &STransitionEasingPin::SetValue))
					.OnEditStarted(FOnCubicBezierEasingEdit::CreateSP(this, &STransitionEasingPin::BeginEasingEdit))
					.OnEditFinished(FOnCubicBezierEasingEdit::CreateSP(this, &STransitionEasingPin::EndEasingEdit))
				]
			];
		}

	private:
		TSharedRef<SWidget> GetTemplateMenuContent()
		{
			FMenuBuilder MenuBuilder(true, nullptr);
			for (const TSharedPtr<FEasingTemplate>& Template : Templates)
			{
				if (Template.IsValid())
				{
					MenuBuilder.AddMenuEntry(
						FText::FromString(Template->Name),
						FText::GetEmpty(),
						FSlateIcon(),
						FUIAction(FExecuteAction::CreateSP(this, &STransitionEasingPin::ApplyTemplate, Template)));
				}
			}
			return MenuBuilder.MakeWidget();
		}

		void ApplyTemplate(TSharedPtr<FEasingTemplate> Template)
		{
			if (Template.IsValid())
			{
				SetValue(Template->Easing);
			}
		}

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
			if (GraphPinObj->GetDefaultAsString() == Text)
			{
				return;
			}
			if (!ActiveEasingEditTransaction.IsValid())
			{
				const FScopedTransaction Transaction(NSLOCTEXT("UMGTransitions", "ChangeEasingPinValue", "Change Easing Pin Value"));
				GraphPinObj->Modify();
				GraphPinObj->GetSchema()->TrySetDefaultValue(*GraphPinObj, Text);
				return;
			}
			GraphPinObj->GetSchema()->TrySetDefaultValue(*GraphPinObj, Text);
		}

		void BeginEasingEdit()
		{
			if (!ActiveEasingEditTransaction.IsValid())
			{
				ActiveEasingEditTransaction = MakeUnique<FScopedTransaction>(NSLOCTEXT("UMGTransitions", "EditEasingCurve", "Edit Easing Curve"));
				GraphPinObj->Modify();
			}
		}

		void EndEasingEdit()
		{
			ActiveEasingEditTransaction.Reset();
		}

		TArray<TSharedPtr<FEasingTemplate>> Templates;
		TUniquePtr<FScopedTransaction> ActiveEasingEditTransaction;
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
