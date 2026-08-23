#pragma once

/** A single physical spring that integrates all four transition channels together. */
class ELASTICUMG_API FSpringVector4f
{
public:
	FSpringVector4f(float SpringFactor = 200.0f, float DampingFactor = 16.0f);

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
