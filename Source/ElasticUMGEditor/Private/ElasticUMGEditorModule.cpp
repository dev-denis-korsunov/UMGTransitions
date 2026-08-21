#include "EdGraphUtilities.h"
#include "WidgetTransition.h"
#include "WidgetTransitionSettings.h"

#include "Blueprint/WidgetTree.h"
#include "Components/PanelSlot.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphSchema_K2.h"
#include "Framework/Application/SlateApplication.h"
#include "IPropertyTypeCustomization.h"
#include "K2Node_CallFunction.h"
#include "K2Node_VariableGet.h"
#include "KismetPins/SGraphPinString.h"
#include "Modules/ModuleManager.h"
#include "PropertyEditorModule.h"
#include "Rendering/DrawElements.h"
#include "ScopedTransaction.h"
#include "ISettingsModule.h"
#include "Styling/AppStyle.h"
#include "WidgetBlueprint.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SLeafWidget.h"
#include "Widgets/Text/STextBlock.h"

namespace ElasticUMGEditor
{
	struct FWidgetPropertyPickerOption
	{
		FString Label;
		FString Path;
		FLinearColor Color = FLinearColor::White;
		bool bHeader = false;
	};

	static bool IsFloat(const FProperty* Property) { return Property && (Property->IsA<FFloatProperty>() || Property->IsA<FDoubleProperty>()); }
	static bool IsBindable(const FProperty* Property)
	{
		const FStructProperty* Struct = CastField<FStructProperty>(Property);
		return IsFloat(Property) || (Struct && (Struct->Struct == TBaseStructure<FVector2D>::Get() || Struct->Struct == TBaseStructure<FLinearColor>::Get()));
	}
	static bool HasBindableDescendant(const UStruct* Struct, int32 Depth)
	{
		if (!Struct || Depth > 8) return false;
		for (TFieldIterator<FProperty> It(Struct, EFieldIterationFlags::None); It; ++It)
		{
			if (IsBindable(*It)) return true;
			if (const FStructProperty* Nested = CastField<FStructProperty>(*It)) if (HasBindableDescendant(Nested->Struct, Depth + 1)) return true;
		}
		return false;
	}
	static FLinearColor PinColor(const FProperty* Property)
	{
		FEdGraphPinType Type;
		const UEdGraphSchema_K2* Schema = GetDefault<UEdGraphSchema_K2>();
		return Property && Schema && Schema->ConvertPropertyToPinType(Property, Type) ? Schema->GetPinTypeColor(Type) : FLinearColor::White;
	}
	static void AddOptions(const UStruct* Struct, const FString& Prefix, int32 Depth, TArray<TSharedPtr<FWidgetPropertyPickerOption>>& Options)
	{
		if (!Struct || Depth > 8) return;
		for (TFieldIterator<FProperty> It(Struct, EFieldIterationFlags::None); It; ++It)
		{
			const FProperty* Property = *It;
			const FString Path = Prefix + Property->GetName();
			FString Label = Property->GetName();
			if (IsBindable(Property)) Options.Add(MakeShared<FWidgetPropertyPickerOption>(FWidgetPropertyPickerOption{ Prefix + Label, Path, PinColor(Property) }));
			if (const FStructProperty* Nested = CastField<FStructProperty>(Property); Nested && HasBindableDescendant(Nested->Struct, Depth + 1))
			{
				if (!IsBindable(Property)) Options.Add(MakeShared<FWidgetPropertyPickerOption>(FWidgetPropertyPickerOption{ Prefix + Label, FString(), FLinearColor::White, true }));
				AddOptions(Nested->Struct, Path + TEXT("."), Depth + 1, Options);
			}
		}
	}
	static const UFunction* GetFunction(const UEdGraphPin* Pin)
	{
		const UK2Node_CallFunction* Call = Pin ? Cast<UK2Node_CallFunction>(Pin->GetOwningNode()) : nullptr;
		return Call ? Call->GetTargetFunction() : nullptr;
	}
	static bool IsBindingPin(const UEdGraphPin* Pin)
	{
		const UFunction* Function = GetFunction(Pin);
		return Function && Function->HasMetaData(TEXT("ElasticUMGTransitionBinding")) && Pin->PinName == TEXT("WidgetProperty");
	}
	static bool IsEasingPin(const UEdGraphPin* Pin)
	{
		const UFunction* Function = GetFunction(Pin);
		return Function && Function->GetFName() == GET_FUNCTION_NAME_CHECKED(UWidgetTransitionFunctionLibrary, WithEasing) && Pin->PinName == TEXT("Easing");
	}
	static UEdGraphPin* GetWidgetSourcePin(const UEdGraphPin* PropertyPin)
	{
		UEdGraphPin* WidgetPin = PropertyPin && PropertyPin->GetOwningNode() ? PropertyPin->GetOwningNode()->FindPin(TEXT("Widget")) : nullptr;
		return WidgetPin && !WidgetPin->LinkedTo.IsEmpty() ? FEdGraphUtilities::GetNetFromPin(WidgetPin->LinkedTo[0]) : nullptr;
	}
	static UWidget* GetDesignerWidget(const UEdGraphPin* PropertyPin)
	{
		UEdGraphPin* Source = GetWidgetSourcePin(PropertyPin);
		const UK2Node_VariableGet* Get = Source ? Cast<UK2Node_VariableGet>(Source->GetOwningNode()) : nullptr;
		UWidgetBlueprint* Blueprint = Source && Source->GetOwningNode() ? Source->GetOwningNode()->GetTypedOuter<UWidgetBlueprint>() : nullptr;
		return Get && Blueprint && Blueprint->WidgetTree ? Blueprint->WidgetTree->FindWidget(Get->GetVarName()) : nullptr;
	}
	static UClass* GetWidgetClass(const UEdGraphPin* PropertyPin)
	{
		if (UEdGraphPin* Source = GetWidgetSourcePin(PropertyPin)) return Cast<UClass>(Source->PinType.PinSubCategoryObject.Get());
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
			Refresh();
			return SNew(SComboBox<TSharedPtr<FWidgetPropertyPickerOption>>).OptionsSource(&Options).OnComboBoxOpening(this, &SWidgetPropertyPathPin::Refresh)
				.OnGenerateWidget(this, &SWidgetPropertyPathPin::MakeOption).OnSelectionChanged(this, &SWidgetPropertyPathPin::Select)
				.Content()[SNew(STextBlock).Text(this, &SWidgetPropertyPathPin::CurrentText).Font(FAppStyle::GetFontStyle("PropertyWindow.NormalFont"))];
		}
	private:
		void Refresh()
		{
			Options.Reset();
			AddOptions(ElasticUMGEditor::GetWidgetClass(GraphPinObj), FString(), 0, Options);
			if (UWidget* Widget = GetDesignerWidget(GraphPinObj); Widget && Widget->Slot)
			{
				Options.Add(MakeShared<FWidgetPropertyPickerOption>(FWidgetPropertyPickerOption{ TEXT("Slot"), FString(), FLinearColor::White, true }));
				AddOptions(Widget->Slot->GetClass(), TEXT("Slot."), 0, Options);
			}
		}
		TSharedRef<SWidget> MakeOption(TSharedPtr<FWidgetPropertyPickerOption> Option) const
		{
			if (!Option.IsValid() || Option->bHeader) return SNew(STextBlock).Text(FText::FromString(Option.IsValid() ? Option->Label : FString())).Font(FAppStyle::GetFontStyle("PropertyWindow.BoldFont"));
			return SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().FillWidth(1.0f)[SNew(STextBlock).Text(FText::FromString(Option->Label))]
				+ SHorizontalBox::Slot().AutoWidth().Padding(8.0f, 0.0f, 2.0f, 0.0f)[SNew(SImage).Image(FAppStyle::GetBrush("Kismet.VariableList.TypeIcon")).ColorAndOpacity(Option->Color)];
		}
		void Select(TSharedPtr<FWidgetPropertyPickerOption> Option, ESelectInfo::Type)
		{
			if (!Option.IsValid() || Option->bHeader || GraphPinObj->GetDefaultAsString() == Option->Path) return;
			const FScopedTransaction Transaction(NSLOCTEXT("ElasticUMG", "SetWidgetPropertyPath", "Set Widget Property Path"));
			GraphPinObj->Modify();
			GraphPinObj->GetSchema()->TrySetDefaultValue(*GraphPinObj, Option->Path);
		}
		FText CurrentText() const { const FString Value = GraphPinObj->GetDefaultAsString(); return Value.IsEmpty() ? NSLOCTEXT("ElasticUMG", "SelectWidgetProperty", "Select widget property") : FText::FromString(Value); }
		TArray<TSharedPtr<FWidgetPropertyPickerOption>> Options;
	};

	class SEasingGraph final : public SLeafWidget
	{
	public:
		SLATE_BEGIN_ARGS(SEasingGraph) {} SLATE_END_ARGS()
		void Construct(const FArguments&, UEdGraphPin* Pin) { EasingPin = Pin; }
		virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D(720.0f, 224.0f); }
		virtual int32 OnPaint(const FPaintArgs&, const FGeometry& Geometry, const FSlateRect&, FSlateWindowElementList& Elements, int32 Layer, const FWidgetStyle&, bool) const override
		{
			FWidgetTransitionEasingValue Value; Read(Value);
			const float Side = Size(Geometry); const FVector2D Origin(4.0f, 4.0f);
			const auto ToPoint = [Origin, Side](FVector2D Point) { return FVector2D(Origin.X + Point.X * Side, Origin.Y + (1.3f - Point.Y) * Side); };
			const FVector2D Start = ToPoint(FVector2D::ZeroVector), End = ToPoint(FVector2D(1.0f, 1.0f));
			const FVector2D Handle1 = VisualHandle(Start, ToPoint(Value.ControlPoint1) - Start, Side / 3.0f, FVector2D(1.0f, 0.0f));
			const FVector2D Handle2 = VisualHandle(End, ToPoint(Value.ControlPoint2) - End, Side / 3.0f, FVector2D(-1.0f, 0.0f));
			const FVector2D Outer = Origin + FVector2D(Side, Side * 1.6f);
			FSlateDrawElement::MakeLines(Elements, Layer, Geometry.ToPaintGeometry(), { Origin, FVector2D(Outer.X, Origin.Y), Outer, FVector2D(Origin.X, Outer.Y), Origin }, ESlateDrawEffect::None, FLinearColor(0.30f, 0.30f, 0.30f), true, 1.0f);
			FSlateDrawElement::MakeLines(Elements, Layer, Geometry.ToPaintGeometry(), { Start, FVector2D(End.X, Start.Y), End, FVector2D(Start.X, End.Y), Start }, ESlateDrawEffect::None, FLinearColor(0.42f, 0.42f, 0.42f), true, 1.0f);
			FSlateDrawElement::MakeLines(Elements, Layer + 1, Geometry.ToPaintGeometry(), { Start, Handle1 }, ESlateDrawEffect::None, FLinearColor(0.45f, 0.55f, 0.65f), true, 1.0f);
			FSlateDrawElement::MakeLines(Elements, Layer + 1, Geometry.ToPaintGeometry(), { End, Handle2 }, ESlateDrawEffect::None, FLinearColor(0.45f, 0.55f, 0.65f), true, 1.0f);
			const FVector2D Marker(5.0f, 5.0f);
			FSlateDrawElement::MakeBox(Elements, Layer + 2, Geometry.ToPaintGeometry(Handle1 - Marker, Marker * 2.0f), FAppStyle::GetBrush("Icons.Circle"), ESlateDrawEffect::None, FLinearColor(0.35f, 0.75f, 1.0f));
			FSlateDrawElement::MakeBox(Elements, Layer + 2, Geometry.ToPaintGeometry(Handle2 - Marker, Marker * 2.0f), FAppStyle::GetBrush("Icons.Circle"), ESlateDrawEffect::None, FLinearColor(0.35f, 0.75f, 1.0f));
			TArray<FVector2D> Curve; Curve.Reserve(33);
			for (int32 Index = 0; Index <= 32; ++Index) { const float T = Index / 32.0f, U = 1.0f - T; Curve.Add(ToPoint(3.0f * U * U * T * Value.ControlPoint1 + 3.0f * U * T * T * Value.ControlPoint2 + T * T * T * FVector2D(1.0f, 1.0f))); }
			FSlateDrawElement::MakeLines(Elements, Layer + 2, Geometry.ToPaintGeometry(), Curve, ESlateDrawEffect::None, bHovering ? FLinearColor(1.0f, 0.45f, 0.05f) : FLinearColor(0.25f, 0.65f, 1.0f), true, bHovering ? 4.0f : 2.0f);
			return Layer + 2;
		}
		virtual void OnMouseEnter(const FGeometry& G, const FPointerEvent& E) override { bHovering = true; SLeafWidget::OnMouseEnter(G, E); }
		virtual void OnMouseLeave(const FPointerEvent& E) override { if (!bDragging) bHovering = false; SLeafWidget::OnMouseLeave(E); }
		virtual FReply OnMouseButtonDown(const FGeometry&, const FPointerEvent& Event) override
		{
			if (Event.GetEffectingButton() != EKeys::LeftMouseButton || !EasingPin) return FReply::Unhandled();
			Transaction = MakeUnique<FScopedTransaction>(NSLOCTEXT("ElasticUMG", "SculptTransitionEasing", "Sculpt Transition Easing"));
			EasingPin->Modify(); bDragging = true; return FReply::Handled().CaptureMouse(AsShared());
		}
		virtual FReply OnMouseMove(const FGeometry& Geometry, const FPointerEvent& Event) override
		{
			if (!bDragging || !EasingPin) return FReply::Unhandled();
			const float Side = Size(Geometry); if (Side <= UE_SMALL_NUMBER) return FReply::Handled();
			const FVector2D Delta(Event.GetCursorDelta().X / Side, -Event.GetCursorDelta().Y / Side); if (Delta.IsNearlyZero()) return FReply::Handled();
			FWidgetTransitionEasingValue Value; Read(Value);
			const FVector2D Local = Geometry.AbsoluteToLocal(Event.GetScreenSpacePosition());
			const FVector2D Origin(4.0f, 4.0f); const auto ToPoint = [Origin, Side](FVector2D Point) { return FVector2D(Origin.X + Point.X * Side, Origin.Y + (1.3f - Point.Y) * Side); };
			const FVector2D H1 = VisualHandle(ToPoint(FVector2D::ZeroVector), ToPoint(Value.ControlPoint1) - ToPoint(FVector2D::ZeroVector), Side / 3.0f, FVector2D(1.0f, 0.0f));
			const FVector2D H2 = VisualHandle(ToPoint(FVector2D(1.0f, 1.0f)), ToPoint(Value.ControlPoint2) - ToPoint(FVector2D(1.0f, 1.0f)), Side / 3.0f, FVector2D(-1.0f, 0.0f));
			const float Radius = Side * .75f;
			const auto Influence = [Radius, Local](FVector2D Handle) { return FMath::Clamp((1.0f - FVector2D::Distance(Local, Handle) / Radius) * 1.5f, 0.0f, 1.0f); };
			const float I1 = Influence(H1), I2 = Influence(H2);
			Value.ControlPoint1 += Delta * I1 * FMath::Max(Value.ControlPoint1.Size() * 3.0f, 1.0f);
			Value.ControlPoint2 += Delta * I2 * FMath::Max((Value.ControlPoint2 - FVector2D(1.0f, 1.0f)).Size() * 3.0f, 1.0f);
			Write(Value); return FReply::Handled();
		}
		virtual FReply OnMouseButtonUp(const FGeometry&, const FPointerEvent& Event) override { if (!bDragging || Event.GetEffectingButton() != EKeys::LeftMouseButton) return FReply::Unhandled(); bDragging = false; Transaction.Reset(); return FReply::Handled().ReleaseMouseCapture(); }
		virtual void OnMouseCaptureLost(const FCaptureLostEvent& Event) override { bDragging = false; bHovering = false; Transaction.Reset(); SLeafWidget::OnMouseCaptureLost(Event); }
	private:
		static float Size(const FGeometry& Geometry) { const FVector2D Size = Geometry.GetLocalSize(); return FMath::Min(Size.X - 8.0f, (Size.Y - 8.0f) / 1.6f); }
		static FVector2D VisualHandle(FVector2D Start, FVector2D Direction, float Length, FVector2D Fallback) { const FVector2D Normal = Direction.GetSafeNormal(); return Start + (Normal.IsNearlyZero() ? Fallback : Normal) * Length; }
		void Read(FWidgetTransitionEasingValue& Value) const { if (EasingPin) FWidgetTransitionEasingValue::StaticStruct()->ImportText(*EasingPin->GetDefaultAsString(), &Value, nullptr, PPF_None, nullptr, TEXT("Easing")); }
		void Write(FWidgetTransitionEasingValue Value) const
		{
			Value.ControlPoint1.X = FMath::Clamp(Value.ControlPoint1.X, 0.0f, 1.0f); Value.ControlPoint1.Y = FMath::Clamp(Value.ControlPoint1.Y, -2.0f, 2.0f);
			Value.ControlPoint2.X = FMath::Max(0.0f, Value.ControlPoint2.X); Value.ControlPoint2.Y = FMath::Clamp(Value.ControlPoint2.Y, -2.0f, 2.0f);
			EasingPin->GetSchema()->TrySetDefaultValue(*EasingPin, FString::Printf(TEXT("(ControlPoint1=(X=%g,Y=%g),ControlPoint2=(X=%g,Y=%g))"), Value.ControlPoint1.X, Value.ControlPoint1.Y, Value.ControlPoint2.X, Value.ControlPoint2.Y));
		}
		UEdGraphPin* EasingPin = nullptr; TUniquePtr<FScopedTransaction> Transaction; bool bDragging = false; bool bHovering = false;
	};

	class SEasingPin final : public SGraphPin
	{
	public:
		SLATE_BEGIN_ARGS(SEasingPin) {} SLATE_END_ARGS()
		void Construct(const FArguments&, UEdGraphPin* Pin) { SGraphPin::Construct(SGraphPin::FArguments(), Pin); }
	protected:
		virtual TSharedRef<SWidget> GetDefaultValueWidget() override
		{
			return SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight()[SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().FillWidth(1.0f)[SNew(STextBlock).Text(NSLOCTEXT("ElasticUMG", "InlineEasing", "Custom cubic Bézier"))]
					+ SHorizontalBox::Slot().AutoWidth().Padding(4.0f, 0.0f)[SNew(SButton).ButtonStyle(FAppStyle::Get(), "HoverHintOnly").ToolTipText(NSLOCTEXT("ElasticUMG", "OpenEasingSettings", "Open Elastic UMG easing settings")).OnClicked_Lambda([] { FModuleManager::LoadModuleChecked<ISettingsModule>("Settings").ShowViewer(TEXT("Project"), TEXT("Plugins"), TEXT("ElasticUMG")); return FReply::Handled(); })[SNew(SImage).Image(FAppStyle::GetBrush("Icons.Settings"))]]]
				+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 4.0f)[SNew(SEasingGraph, GraphPinObj)];
		}
	};

	class FTransitionPinFactory final : public FGraphPanelPinFactory
	{
	public:
		virtual TSharedPtr<SGraphPin> CreatePin(UEdGraphPin* Pin) const override
		{
			if (IsBindingPin(Pin)) return SNew(SWidgetPropertyPathPin, Pin);
			if (IsEasingPin(Pin)) return SNew(SEasingPin, Pin);
			return nullptr;
		}
	};
}

class FElasticUMGEditorModule final : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		PinFactory = MakeShared<ElasticUMGEditor::FTransitionPinFactory>();
		FEdGraphUtilities::RegisterVisualPinFactory(PinFactory);
	}
	virtual void ShutdownModule() override
	{
		if (PinFactory.IsValid()) { FEdGraphUtilities::UnregisterVisualPinFactory(PinFactory); PinFactory.Reset(); }
	}
private:
	TSharedPtr<FGraphPanelPinFactory> PinFactory;
};

IMPLEMENT_MODULE(FElasticUMGEditorModule, ElasticUMGEditor)
