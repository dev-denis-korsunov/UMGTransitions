#include "Spring.h"

namespace
{
	static float Length(const FVector4f& Value)
	{
		return FMath::Sqrt(Value.X * Value.X + Value.Y * Value.Y + Value.Z * Value.Z + Value.W * Value.W);
	}
}

FSpringVector4f::FSpringVector4f(float InSpringFactor, float InDampingFactor)
	: SpringFactor(InSpringFactor)
	, DampingFactor(InDampingFactor)
	, bStarted(false)
	, bCompleted(false)
{
}

void FSpringVector4f::Start(FVector4f InStartValue, FVector4f InTargetValue)
{
	CurrentValue = InStartValue;
	TargetValue = InTargetValue;
	Velocity = FVector4f::Zero();
	InitialDisplacement = Length(TargetValue - CurrentValue);
	bStarted = true;
	bCompleted = false;
}

void FSpringVector4f::Tick(float DeltaTime)
{
	if (!bStarted || bCompleted)
	{
		return;
	}
	const float Frequency = FMath::Sqrt(SpringFactor);
	const float DampingRatio = DampingFactor / (2.0f * Frequency);
	const FVector4f Offset = CurrentValue - TargetValue;
	if (DampingRatio < 1.0f)
	{
		const float DampedFrequency = Frequency * FMath::Sqrt(1.0f - DampingRatio * DampingRatio);
		const float Exponential = FMath::Exp(-DampingRatio * Frequency * DeltaTime);
		const float Cosine = FMath::Cos(DampedFrequency * DeltaTime);
		const float Sine = FMath::Sin(DampedFrequency * DeltaTime);
		const FVector4f Coefficient = (Velocity + DampingRatio * Frequency * Offset) / DampedFrequency;
		const FVector4f NewOffset = Exponential * (Offset * Cosine + Coefficient * Sine);
		Velocity = Exponential * (-DampingRatio * Frequency * (Offset * Cosine + Coefficient * Sine) - Offset * DampedFrequency * Sine + Coefficient * DampedFrequency * Cosine);
		CurrentValue = TargetValue + NewOffset;
	}
	else
	{
		const float Exponential = FMath::Exp(-Frequency * DeltaTime);
		const FVector4f Coefficient = Velocity + Frequency * Offset;
		CurrentValue = TargetValue + Exponential * (Offset + Coefficient * DeltaTime);
		Velocity = Exponential * (Velocity - Frequency * Coefficient * DeltaTime);
	}
	const float Threshold = FMath::Max(0.0001f, InitialDisplacement * 0.001f);
	bCompleted = Length(CurrentValue - TargetValue) <= Threshold && Length(Velocity) <= Threshold * Frequency;
	if (bCompleted)
	{
		CurrentValue = TargetValue;
	}
}
