#pragma once

#include "CoreMinimal.h"

/** Heap-allocated four-channel spring used by spring transitions. */
struct ELASTICUMG_API FWidgetTransitionSpring
{
	FWidgetTransitionSpring(float SpringFactor, float DampingFactor);

	void Tick(float DeltaTime);
	void Start(FVector4f InStartValue, FVector4f InTargetValue);

	FORCEINLINE const FVector4f& GetValue() const { return CurrentValue; }
	FORCEINLINE bool IsCompleted() const { return bCompleted; }

private:
	const float SpringFactor = 200.0f;
	const float Frequency = 0.0f;
	const float DampingRatio = 1.0f;
	const float DampedFrequency = 0.0f;
	FVector4f TargetValue = FVector4f::Zero();
	FVector4f CurrentValue = FVector4f::Zero();
	FVector4f Velocity = FVector4f::Zero();
	float CompletionThresholdSquared = 0.0f;
	uint8 bStarted : 1;
	uint8 bCompleted : 1;
};
