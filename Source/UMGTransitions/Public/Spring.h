#pragma once

#include "CoreMinimal.h"

namespace WidgetTransitionSpring
{
	/** Relative remaining displacement at which every spring settles. */
	inline constexpr float EndTolerance = 0.001f;
}

/** Four-channel spring state stored in the subsystem's dense spring array. */
struct UMGTRANSITIONS_API FWidgetTransitionSpring
{
	FWidgetTransitionSpring(float SpringFactor, float DampingFactor, float InMaxSpeed = 0.0f);

	void Tick(float DeltaTime);
	void Start(FVector4f InStartValue, FVector4f InTargetValue, float InDelay = 0.0f, FVector4f InInitialVelocity = FVector4f::Zero());
	void SetTarget(FVector4f InTargetValue);

	FORCEINLINE const FVector4f& GetValue() const { return CurrentValue; }
	FORCEINLINE const FVector4f& GetVelocity() const { return Velocity; }
	FORCEINLINE const FVector4f& GetTarget() const { return TargetValue; }
	FORCEINLINE bool IsCompleted() const { return bCompleted; }

private:
	const float SpringFactor = 200.0f;
	const float Frequency = 0.0f;
	const float DampingRatio = 1.0f;
	const float DampedFrequency = 0.0f;
	const float MaxSpeed = 0.0f;
	FVector4f TargetValue = FVector4f::Zero();
	FVector4f CurrentValue = FVector4f::Zero();
	FVector4f Velocity = FVector4f::Zero();
	float Delay = 0.0f;
	float CurrentDelay = 0.0f;
	float CompletionThresholdSquared = 0.0f;
	uint8 bStarted : 1;
	uint8 bCompleted : 1;
	void UpdateCompletionThreshold();
};
