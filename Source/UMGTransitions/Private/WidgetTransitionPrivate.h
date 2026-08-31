#pragma once

#include "Curves/RealCurve.h"
#include "WidgetTransition.h"

namespace WidgetTransitionPrivate
{
	struct FSample
	{
		FVector4f Value = FVector4f::Zero();
		float NormalizedProgress = 0.0f;
		float EasedProgress = 0.0f;
		bool bCompleted = false;
	};

	bool IsMaterialBinding(FName WidgetProperty);
	FName GetMaterialParameter(FName WidgetProperty);
	FORCEINLINE FSample SampleTransition(const UWidgetTransitionSubsystem& Subsystem, const FWidgetTransition& Transition)
	{
		FSample Sample;
		Sample.bCompleted = !Transition.bUseSpring && (Transition.Time <= 0.0f || Transition.CurrentTime >= Transition.Delay + Transition.Time);
		Sample.NormalizedProgress = Transition.Time <= 0.0f ? 1.0f : FMath::Clamp((Transition.CurrentTime - Transition.Delay) / Transition.Time, 0.0f, 1.0f);
		Sample.EasedProgress = Transition.EasingCurve ? Transition.EasingCurve->Eval(Sample.NormalizedProgress) : Sample.NormalizedProgress;
		Sample.Value = FMath::Lerp(Transition.FromValue.Channels, Transition.ToValue.Channels, Sample.EasedProgress);
		const bool bReachedSpringDeadline = Transition.bUseSpring && Transition.bFitSpringToTime && Transition.CurrentTime >= Transition.Delay + Transition.Time;
		if (Transition.bUseSpring && Subsystem.Springs.IsValidIndex(Transition.SpringIndex) && !bReachedSpringDeadline)
		{
			const FWidgetTransitionSpring& Spring = Subsystem.Springs[Transition.SpringIndex];
			Sample.Value = Spring.GetValue();
			Sample.bCompleted = Spring.IsCompleted();
		}
		if (bReachedSpringDeadline)
		{
			Sample.Value = Transition.ToValue.Channels;
			Sample.bCompleted = true;
		}
		return Sample;
	}
	void StartTransition(const UObject* WorldContextObject, FWidgetTransition Transition, FWidgetTransitionCallbacks Callbacks = {});
}
