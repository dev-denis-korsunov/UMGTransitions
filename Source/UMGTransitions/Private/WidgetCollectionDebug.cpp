#include "WidgetCollectionLibrary.h"

#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
#include "Application/SlateApplicationBase.h"
#include "Components/Widget.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "HAL/PlatformTime.h"
#include "Fonts/FontMeasure.h"
#include "Rendering/DrawElements.h"
#include "Rendering/SlateRenderer.h"
#include "Styling/CoreStyle.h"
#include "Widgets/SLeafWidget.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Components/Image.h"
#include "Engine/Engine.h"
#include "Input/HittestGrid.h"
#include "Misc/AutomationTest.h"
#include "Types/PaintArgs.h"
#include "Widgets/SWindow.h"
#endif

namespace
{
	FLinearColor MakeFillColor(const FWidgetCollectionItem& Item, bool bByDepth, float Alpha)
	{
		const float Index = static_cast<float>(FMath::Max(0, Item.Index));
		const float IndexFactor = Index / (Index + 4.0f);
		const float Hue = 240.0f * IndexFactor;
		float Saturation = 0.85f;
		if (bByDepth)
		{
			Saturation /= 1.0f + 0.5f * static_cast<float>(FMath::Max(0, Item.Depth));
		}
		FLinearColor Result = FLinearColor(Hue, Saturation, 1.0f).HSVToLinearRGB();
		Result.A = FMath::Clamp(Alpha, 0.0f, 1.0f);
		return Result;
	}

	TArray<FVector2f> MakeOutline(const UWidget& Widget, const FGeometry& Overlay)
	{
		// Paint geometry and the overlay share window space.
		const FGeometry& Target = Widget.GetPaintSpaceGeometry();
		const FVector2D Size = Target.GetLocalSize();
		TArray<FVector2f> Points;
		Points.Reserve(5);
		for (const FVector2D& Corner : {FVector2D::ZeroVector, FVector2D(Size.X, 0), Size, FVector2D(0, Size.Y)})
		{
			Points.Add(FVector2f(Overlay.AbsoluteToLocal(Target.LocalToAbsolute(Corner))));
		}
		Points.Add(FVector2f(Points[0]));
		return Points;
	}

	class SWidgetCollectionDebug final : public SLeafWidget
	{
	public:
		SLATE_BEGIN_ARGS(SWidgetCollectionDebug)
			: _Duration(2.0f), _ShowDepth(false), _ShowIndex(false), _ShowName(false)
			, _Color(0.2f, 1.0f, 0.2f, 1.0f), _ColorByIndex(false), _ColorByDepth(false)
			, _IndexOpacity(0.08f), _DepthOpacity(0.08f) {}
			SLATE_ARGUMENT(FWidgetCollectionItem, Item)
			SLATE_ARGUMENT(float, Duration)
			SLATE_ARGUMENT(bool, ShowDepth)
			SLATE_ARGUMENT(bool, ShowIndex)
			SLATE_ARGUMENT(bool, ShowName)
			SLATE_ARGUMENT(FLinearColor, Color)
			SLATE_ARGUMENT(bool, ColorByIndex)
			SLATE_ARGUMENT(bool, ColorByDepth)
			SLATE_ARGUMENT(float, IndexOpacity)
			SLATE_ARGUMENT(float, DepthOpacity)
			SLATE_ARGUMENT(TWeakObjectPtr<UGameViewportClient>, Viewport)
		SLATE_END_ARGS()

		void Construct(const FArguments& Args)
		{
			Widget = Args._Item.Widget.Get();
			Viewport = Args._Viewport;
			ExpiresAt = FPlatformTime::Seconds() + Args._Duration;
			Color = Args._Color;
			OutlineColor = Color;
			bFill = Args._ColorByIndex || Args._ColorByDepth;
			if (bFill)
			{
				const float Opacity = Args._ColorByDepth ? Args._DepthOpacity : Args._IndexOpacity;
				FillColor = MakeFillColor(Args._Item, Args._ColorByDepth, FMath::Clamp(Opacity, 0.0f, 1.0f) * FMath::Clamp(Color.A, 0.0f, 1.0f));
				OutlineColor = FillColor;
				OutlineColor.A = 1.0f;
			}
			if (Args._ShowName && Widget.IsValid())
			{
				Label = Widget->GetName();
			}
			if (Args._ShowIndex)
			{
				if (!Label.IsEmpty())
				{
					Label += TEXT("  ");
				}
				Label += FString::Printf(TEXT("Index: %d"), Args._Item.Index);
			}
			if (Args._ShowDepth)
			{
				if (!Label.IsEmpty())
				{
					Label += TEXT("  ");
				}
				Label += FString::Printf(TEXT("Depth: %d"), Args._Item.Depth);
			}
			if (!Label.IsEmpty())
			{
				Font = FCoreStyle::GetDefaultFontStyle("Regular", 10);
				LabelSize = FVector2f(FSlateApplicationBase::Get().GetRenderer()->GetFontMeasureService()->Measure(Label, Font)) + FVector2f(12, 4);
			}
			SetVisibility(EVisibility::HitTestInvisible);
			SetPixelSnapping(EWidgetPixelSnapping::SnapToPixel);
			RegisterActiveTimer(0.0f, FWidgetActiveTimerDelegate::CreateSP(this, &SWidgetCollectionDebug::UpdateLifetime));
		}

		virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override
		{
			const UWidget* Target = Widget.Get();
			if (!Target || !Target->IsVisible() || !Target->GetCachedWidget().IsValid() || FPlatformTime::Seconds() >= ExpiresAt)
			{
				return LayerId;
			}
			const FGeometry& Geometry = Target->GetPaintSpaceGeometry();
			if (Geometry.GetLocalSize().X <= 0 || Geometry.GetLocalSize().Y <= 0)
			{
				return LayerId;
			}
			const TArray<FVector2f> Outline = MakeOutline(*Target, AllottedGeometry);
			if (bFill)
			{
				FSlateDrawElement::MakeBox(OutDrawElements, LayerId, Geometry.ToPaintGeometry(), FCoreStyle::Get().GetBrush("WhiteBrush"), ESlateDrawEffect::None, FillColor);
				++LayerId;
			}
			FSlateDrawElement::MakeLines(OutDrawElements, LayerId, AllottedGeometry.ToPaintGeometry(), Outline, ESlateDrawEffect::None, OutlineColor, true, 1.0f);
			if (!Label.IsEmpty())
			{
				FVector2f Position = Outline[0];
				for (const FVector2f& Point : Outline)
				{
					Position.X = FMath::Min(Position.X, Point.X);
					Position.Y = FMath::Min(Position.Y, Point.Y);
				}
				Position.Y -= LabelSize.Y;
				const FVector2f ViewSize(AllottedGeometry.GetLocalSize());
				Position.X = FMath::Clamp(Position.X, 0.0f, FMath::Max(0.0f, ViewSize.X - LabelSize.X));
				Position.Y = FMath::Clamp(Position.Y, 0.0f, FMath::Max(0.0f, ViewSize.Y - LabelSize.Y));
				FSlateDrawElement::MakeBox(OutDrawElements, LayerId + 1, AllottedGeometry.ToPaintGeometry(LabelSize, FSlateLayoutTransform(Position)), FCoreStyle::Get().GetBrush("WhiteBrush"), ESlateDrawEffect::None, OutlineColor);
				FSlateDrawElement::MakeText(OutDrawElements, LayerId + 2, AllottedGeometry.ToPaintGeometry(LabelSize, FSlateLayoutTransform(Position + FVector2f(4, 2))), Label, Font, ESlateDrawEffect::None, FLinearColor::Black);
				return LayerId + 2;
			}
			return LayerId;
		}

	private:
#if WITH_DEV_AUTOMATION_TESTS
		friend class FWidgetCollectionDebugTest;
#endif

		virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D::ZeroVector; }
		virtual bool ComputeVolatility() const override { return true; }

		EActiveTimerReturnType UpdateLifetime(double, float)
		{
			if (!Widget.IsValid() || !Viewport.IsValid() || FPlatformTime::Seconds() >= ExpiresAt)
			{
				if (UGameViewportClient* Client = Viewport.Get())
				{
					Client->RemoveViewportWidgetContent(SharedThis(this));
				}
				return EActiveTimerReturnType::Stop;
			}
			Invalidate(EInvalidateWidgetReason::Paint);
			return EActiveTimerReturnType::Continue;
		}

		TWeakObjectPtr<UWidget> Widget;
		TWeakObjectPtr<UGameViewportClient> Viewport;
		double ExpiresAt = 0;
		FString Label;
		FLinearColor Color = FLinearColor::Green;
		FLinearColor OutlineColor = FLinearColor::Green;
		FLinearColor FillColor = FLinearColor::Transparent;
		bool bFill = false;
		FSlateFontInfo Font;
		FVector2f LabelSize = FVector2f::ZeroVector;
	};

#if WITH_DEV_AUTOMATION_TESTS
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWidgetCollectionDebugTest, "UMGTransitions.WidgetCollection.Runtime.DebugOutline", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
	bool FWidgetCollectionDebugTest::RunTest(const FString&)
	{
		const FLinearColor Warm = MakeFillColor({0, 0, nullptr}, false, 0.08f);
		const FLinearColor Cold = MakeFillColor({20, 0, nullptr}, false, 0.08f);
		TestTrue(TEXT("Index ramp progresses from red to cold with low opacity"), Warm.R > Warm.B && Cold.B > Cold.R && FMath::IsNearlyEqual(Cold.A, 0.08f));
		const FLinearColor BaseHSV = MakeFillColor({20, 0, nullptr}, false, 1).LinearRGBToHSV();
		const FLinearColor DepthHSV = MakeFillColor({20, 2, nullptr}, true, 1).LinearRGBToHSV();
		const FLinearColor DeeperHSV = MakeFillColor({20, 3, nullptr}, true, 1).LinearRGBToHSV();
		TestTrue(TEXT("Depth retains the index hue and progressively reduces saturation"), FMath::IsNearlyEqual(BaseHSV.R, DepthHSV.R, 0.001f) && FMath::IsNearlyEqual(BaseHSV.R, DeeperHSV.R, 0.001f) && BaseHSV.G > DepthHSV.G && DepthHSV.G > DeeperHSV.G);
		TestTrue(TEXT("Depth zero matches the original index color"), MakeFillColor({20, 0, nullptr}, true, 1) == MakeFillColor({20, 0, nullptr}, false, 1));
		TestTrue(TEXT("Depth mode still uses index to select the hue"), MakeFillColor({0, 2, nullptr}, true, 1) != MakeFillColor({20, 2, nullptr}, true, 1));
		const FGeometry Overlay = FGeometry::MakeRoot(FVector2f(800, 600), FSlateLayoutTransform(2.0f, FVector2f(20, 20)));
		const FGeometry Target = FGeometry::MakeRoot(FVector2f(100, 50), FSlateLayoutTransform(2.0f, FVector2f(40, 60)));
		UImage* Widget = NewObject<UImage>();
		const TSharedRef<SWidget> SlateWidget = Widget->TakeWidget();
		const TSharedRef<SWindow> Window = SNew(SWindow);
		FSlateWindowElementList Elements(Window);
		FHittestGrid HitTestGrid;
		HitTestGrid.SetHittestArea(FVector2f::ZeroVector, FVector2f(800, 600));
		for (const FVector2f WindowOffset : {FVector2f::ZeroVector, FVector2f(100, 250), FVector2f(-300, 500)})
		{
			const FPaintArgs Args(nullptr, HitTestGrid, WindowOffset, FPlatformTime::Seconds(), 0);
			SlateWidget->Paint(Args, Target, FSlateRect(-1000, -1000, 2000, 2000), Elements, 0, FWidgetStyle(), true);
			const TArray<FVector2f> Outline = MakeOutline(*Widget, Overlay);
			TestTrue(TEXT("Outline ignores desktop window position and accounts for DPI"), Outline == TArray<FVector2f>({{10, 20}, {110, 20}, {110, 70}, {10, 70}, {10, 20}}));
			TestTrue(TEXT("Cached geometry includes the desktop offset"), FVector2f(Widget->GetCachedGeometry().LocalToAbsolute(FVector2f::ZeroVector)).Equals(FVector2f(40, 60) + WindowOffset, 0.001f));
		}
		const FGeometry Rotated = Target.MakeChild(FSlateRenderTransform(FQuat2D(PI * 0.5f)), FVector2f::ZeroVector);
		const FPaintArgs Args(nullptr, HitTestGrid, FVector2f(100, 250), FPlatformTime::Seconds(), 0);
		SlateWidget->Paint(Args, Rotated, FSlateRect(-1000, -1000, 2000, 2000), Elements, 0, FWidgetStyle(), true);
		const TArray<FVector2f> RotatedOutline = MakeOutline(*Widget, Overlay);
		TestTrue(TEXT("Outline follows widget render rotation"), RotatedOutline[1].Equals(FVector2f(10, 120), 0.001f) && RotatedOutline[2].Equals(FVector2f(-40, 120), 0.001f));
		UGameViewportClient* Viewport = NewObject<UGameViewportClient>(GEngine);
		const TSharedRef<SWidgetCollectionDebug> Debug = SNew(SWidgetCollectionDebug)
			.Item(FWidgetCollectionItem(7, 2, Widget)).Duration(2).ShowDepth(true).ShowIndex(true).ShowName(true).Color(FLinearColor::Blue).ColorByIndex(true).ColorByDepth(true).IndexOpacity(0.2f).DepthOpacity(0.35f).Viewport(Viewport);
		TestTrue(TEXT("Depth opacity wins when both color modes are enabled"), FMath::IsNearlyEqual(Debug->FillColor.A, 0.35f));
		const TSharedRef<SWidgetCollectionDebug> IndexDebug = SNew(SWidgetCollectionDebug)
			.Item(FWidgetCollectionItem(7, 2, Widget)).Duration(2).Color(FLinearColor(1, 1, 1, 0.5f)).ColorByIndex(true).IndexOpacity(0.2f).Viewport(Viewport);
		TestTrue(TEXT("Index opacity is independent and scales by color alpha"), FMath::IsNearlyEqual(IndexDebug->FillColor.A, 0.1f));
		TestEqual(TEXT("Label uses the supplied object's name and item metadata"), Debug->Label, Widget->GetName() + TEXT("  Index: 7  Depth: 2"));
		TestTrue(TEXT("Map outline matches fill RGB with full opacity"), Debug->OutlineColor.R == Debug->FillColor.R && Debug->OutlineColor.G == Debug->FillColor.G && Debug->OutlineColor.B == Debug->FillColor.B && Debug->OutlineColor.A == 1.0f && IndexDebug->OutlineColor.A == 1.0f);
		TestTrue(TEXT("Outline snaps to pixels"), Debug->GetPixelSnapping() == EWidgetPixelSnapping::SnapToPixel);
		TestTrue(TEXT("Debug outline does not intercept input"), Debug->GetVisibility() == EVisibility::HitTestInvisible);
		TestTrue(TEXT("Outline stays active until its deadline"), Debug->UpdateLifetime(0, 0) == EActiveTimerReturnType::Continue);
		Debug->ExpiresAt = FPlatformTime::Seconds() - 1;
		TestTrue(TEXT("Outline stops at its deadline without world ticking"), Debug->UpdateLifetime(0, 0) == EActiveTimerReturnType::Stop);
		Debug->ExpiresAt = FPlatformTime::Seconds() + 10;
		Debug->Widget.Reset();
		TestTrue(TEXT("Destroyed target stops the outline early"), Debug->UpdateLifetime(0, 0) == EActiveTimerReturnType::Stop);
		return true;
	}
#endif
}
#endif

void UWidgetCollectionLibrary::DrawDebugCollectionItem(const FWidgetCollectionItem& Item, float Duration, bool bShowDepth, bool bShowIndex, FLinearColor Color, bool bShowName, bool bColorByIndex, bool bColorByDepth, float IndexOpacity, float DepthOpacity)
{
#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
	if (!IsValid(Item.Widget) || !FMath::IsFinite(Duration) || Duration <= 0)
	{
		return;
	}
	UWorld* World = Item.Widget->GetWorld();
	UGameViewportClient* Viewport = World ? World->GetGameViewport() : nullptr;
	if (!IsValid(Viewport))
	{
		return;
	}
	Viewport->AddViewportWidgetContent(
		SNew(SWidgetCollectionDebug).Item(Item).Duration(Duration).ShowDepth(bShowDepth).ShowIndex(bShowIndex).ShowName(bShowName).Color(Color).ColorByIndex(bColorByIndex).ColorByDepth(bColorByDepth).IndexOpacity(IndexOpacity).DepthOpacity(DepthOpacity).Viewport(Viewport), MAX_int32);
#endif
}
