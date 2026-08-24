#include "WidgetTransition.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Curves/RichCurve.h"
#include "HAL/PlatformTime.h"
#include "Misc/AutomationTest.h"

namespace
{
	FString FormatMicroseconds(double Seconds)
	{
		return FString::Printf(TEXT("%.3f us"), Seconds * 1000000.0);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWidgetTransitionModeIndicesExperiment, "ElasticUMG.WidgetTransition.Experiments.ModeIndices", EAutomationTestFlags::EditorContext | EAutomationTestFlags::PerfFilter)
bool FWidgetTransitionModeIndicesExperiment::RunTest(const FString&)
{
	constexpr int32 TransitionCount = 500;
	constexpr int32 FrameCount = 300;
	const FName CurveRow(TEXT("ModeIndices"));
	UCurveTable* CurveTable = NewObject<UCurveTable>(GetTransientPackage());
	FRichCurve& Curve = CurveTable->AddRichCurve(CurveRow);
	Curve.AddKey(0.0f, 0.0f);
	Curve.AddKey(0.5f, 0.2f);
	Curve.AddKey(1.0f, 1.0f);
	TArray<FWidgetTransition> Transitions;
	TArray<FWidgetTransitionSpring> Springs;
	TArray<int32> LinearTransitionIndices;
	TArray<int32> EasingTransitionIndices;
	TArray<int32> SpringTransitionIndices;
	Transitions.Reserve(TransitionCount);
	Springs.Reserve(TransitionCount / 3);
	LinearTransitionIndices.Reserve(TransitionCount / 3);
	EasingTransitionIndices.Reserve(TransitionCount / 3);
	SpringTransitionIndices.Reserve(TransitionCount / 3);
	for (int32 Index = 0; Index < TransitionCount; ++Index)
	{
		FWidgetTransition Transition;
		Transition.FromValue.Channels = FVector4f::Zero();
		Transition.ToValue.Channels = FVector4f(100.0f, -50.0f, 25.0f, 1.0f);
		Transition.Time = 1.0f;
		Transition.CurrentTime = 0.35f;
		const int32 TransitionIndex = Transitions.Add(MoveTemp(Transition));
		FWidgetTransition& StoredTransition = Transitions[TransitionIndex];
		switch (Index % 3)
		{
		case 0:
		{
			LinearTransitionIndices.Add(TransitionIndex);
			break;
		}
		case 1:
		{
			StoredTransition.Easing.CurveTable = CurveTable;
			StoredTransition.Easing.RowName = CurveRow;
			StoredTransition.EasingCurve = &Curve;
			EasingTransitionIndices.Add(TransitionIndex);
			break;
		}
		default:
		{
			StoredTransition.bUseSpring = true;
			StoredTransition.SpringIndex = Springs.Emplace(1.0f, 1.0f);
			Springs[StoredTransition.SpringIndex].Start(StoredTransition.FromValue.Channels, StoredTransition.ToValue.Channels);
			SpringTransitionIndices.Add(TransitionIndex);
			break;
		}
		}
	}

	volatile float Sink = 0.0f;
	auto EvaluateTransition = [&Springs](const FWidgetTransition& Transition)
	{
		const float Alpha = FMath::Clamp(Transition.CurrentTime / Transition.Time, 0.0f, 1.0f);
		if (Transition.bUseSpring)
		{
			return Springs[Transition.SpringIndex].GetValue();
		}
		const float EasedAlpha = Transition.EasingCurve ? Transition.EasingCurve->Eval(Alpha) : Alpha;
		return FMath::Lerp(Transition.FromValue.Channels, Transition.ToValue.Channels, EasedAlpha);
	};
	auto EvaluateLinear = [](const FWidgetTransition& Transition)
	{
		const float Alpha = FMath::Clamp(Transition.CurrentTime / Transition.Time, 0.0f, 1.0f);
		return FMath::Lerp(Transition.FromValue.Channels, Transition.ToValue.Channels, Alpha);
	};
	auto EvaluateEasing = [](const FWidgetTransition& Transition)
	{
		const float Alpha = FMath::Clamp(Transition.CurrentTime / Transition.Time, 0.0f, 1.0f);
		const float EasedAlpha = Transition.EasingCurve->Eval(Alpha);
		return FMath::Lerp(Transition.FromValue.Channels, Transition.ToValue.Channels, EasedAlpha);
	};
	auto EvaluateSpring = [&Springs](const FWidgetTransition& Transition)
	{
		return Springs[Transition.SpringIndex].GetValue();
	};
	auto Measure = [this, &Sink](const TCHAR* Name, auto&& Traverse)
	{
		const double StartTime = FPlatformTime::Seconds();
		for (int32 FrameIndex = 0; FrameIndex < FrameCount; ++FrameIndex)
		{
			Traverse();
		}
		const double ElapsedSeconds = FPlatformTime::Seconds() - StartTime;
		AddInfo(FString::Printf(TEXT("%s: %s / frame, %s / transition (%d transitions, %d frames)"), Name, *FormatMicroseconds(ElapsedSeconds / FrameCount), *FormatMicroseconds(ElapsedSeconds / (FrameCount * TransitionCount)), TransitionCount, FrameCount));
	};

	Measure(TEXT("Single mixed transition pass"), [&Transitions, &EvaluateTransition, &Sink]()
	{
		for (const FWidgetTransition& Transition : Transitions)
		{
			Sink += EvaluateTransition(Transition).X;
		}
	});
	Measure(TEXT("Specialized mode index passes"), [&Transitions, &LinearTransitionIndices, &EasingTransitionIndices, &SpringTransitionIndices, &EvaluateLinear, &EvaluateEasing, &EvaluateSpring, &Sink]()
	{
		for (const int32 TransitionIndex : LinearTransitionIndices)
		{
			Sink += EvaluateLinear(Transitions[TransitionIndex]).X;
		}
		for (const int32 TransitionIndex : EasingTransitionIndices)
		{
			Sink += EvaluateEasing(Transitions[TransitionIndex]).X;
		}
		for (const int32 TransitionIndex : SpringTransitionIndices)
		{
			Sink += EvaluateSpring(Transitions[TransitionIndex]).X;
		}
	});
	TestTrue(TEXT("Mode index benchmark executed"), Sink > 0.0f);
	return true;
}

#endif
