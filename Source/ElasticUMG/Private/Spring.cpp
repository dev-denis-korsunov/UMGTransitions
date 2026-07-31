#include "Spring.h"

FSpringFloat::FSpringFloat(float InSpringFactor, float InDampingFactor)
	: SpringFactor(InSpringFactor)
	, DampingFactor(InDampingFactor)
	, bStarted(false)
	, bCompleted(false)
{
}

void FSpringFloat::Start(float InStartValue, float InTargetValue)
{
	CurrentValue = InStartValue;
	TargetValue = InTargetValue;
	Velocity = 0.0f;
	InitialDisplacement = FMath::Abs(TargetValue - CurrentValue);
	bStarted = true;
	bCompleted = false;
}

void FSpringFloat::Tick(float DeltaTime)
{
	if (!bStarted || bCompleted) return;
	const float Frequency = FMath::Sqrt(SpringFactor);
	const float DampingRatio = DampingFactor / (2.0f * Frequency);
	const float Offset = CurrentValue - TargetValue;
	if (DampingRatio < 1.0f)
	{
		const float DampedFrequency = Frequency * FMath::Sqrt(1.0f - DampingRatio * DampingRatio);
		const float Exponential = FMath::Exp(-DampingRatio * Frequency * DeltaTime);
		const float Cosine = FMath::Cos(DampedFrequency * DeltaTime);
		const float Sine = FMath::Sin(DampedFrequency * DeltaTime);
		const float Coefficient = (Velocity + DampingRatio * Frequency * Offset) / DampedFrequency;
		const float NewOffset = Exponential * (Offset * Cosine + Coefficient * Sine);
		Velocity = Exponential * (-DampingRatio * Frequency * (Offset * Cosine + Coefficient * Sine) - Offset * DampedFrequency * Sine + Coefficient * DampedFrequency * Cosine);
		CurrentValue = TargetValue + NewOffset;
	}
	else
	{
		const float Exponential = FMath::Exp(-Frequency * DeltaTime);
		const float Coefficient = Velocity + Frequency * Offset;
		CurrentValue = TargetValue + Exponential * (Offset + Coefficient * DeltaTime);
		Velocity = Exponential * (Velocity - Frequency * Coefficient * DeltaTime);
	}
	const float Threshold = FMath::Max(0.0001f, InitialDisplacement * 0.001f);
	bCompleted = FMath::Abs(CurrentValue - TargetValue) <= Threshold && FMath::Abs(Velocity) <= Threshold * Frequency;
	if (bCompleted) CurrentValue = TargetValue;
}

FSpringVector2D::FSpringVector2D(float InSpringFactor, float InDampingFactor)
	: SpringFactor(InSpringFactor)
	, DampingFactor(InDampingFactor)
	, bStarted(false)
	, bCompleted(false)
{
}

void FSpringVector2D::Start(FVector2D InStartValue, FVector2D InTargetValue)
{
	CurrentValue = InStartValue;
	TargetValue = InTargetValue;
	Velocity = FVector2D::ZeroVector;
	InitialDisplacement = (TargetValue - CurrentValue).Size();
	bStarted = true;
	bCompleted = false;
}

void FSpringVector2D::Tick(float DeltaTime)
{
	if (!bStarted || bCompleted) return;
	const float Frequency = FMath::Sqrt(SpringFactor);
	const float DampingRatio = DampingFactor / (2.0f * Frequency);
	const FVector2D Offset = CurrentValue - TargetValue;
	if (DampingRatio < 1.0f)
	{
		const float DampedFrequency = Frequency * FMath::Sqrt(1.0f - DampingRatio * DampingRatio);
		const float Exponential = FMath::Exp(-DampingRatio * Frequency * DeltaTime);
		const float Cosine = FMath::Cos(DampedFrequency * DeltaTime);
		const float Sine = FMath::Sin(DampedFrequency * DeltaTime);
		const FVector2D Coefficient = (Velocity + DampingRatio * Frequency * Offset) / DampedFrequency;
		const FVector2D NewOffset = Exponential * (Offset * Cosine + Coefficient * Sine);
		Velocity = Exponential * (-DampingRatio * Frequency * (Offset * Cosine + Coefficient * Sine) - Offset * DampedFrequency * Sine + Coefficient * DampedFrequency * Cosine);
		CurrentValue = TargetValue + NewOffset;
	}
	else
	{
		const float Exponential = FMath::Exp(-Frequency * DeltaTime);
		const FVector2D Coefficient = Velocity + Frequency * Offset;
		CurrentValue = TargetValue + Exponential * (Offset + Coefficient * DeltaTime);
		Velocity = Exponential * (Velocity - Frequency * Coefficient * DeltaTime);
	}
	const float Threshold = FMath::Max(0.0001f, InitialDisplacement * 0.001f);
	bCompleted = (CurrentValue - TargetValue).Size() <= Threshold && Velocity.Size() <= Threshold * Frequency;
	if (bCompleted) CurrentValue = TargetValue;
}
