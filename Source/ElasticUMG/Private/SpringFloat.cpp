#include "SpringFloat.h"

FSpringFloat::FSpringFloat(const float SpringFactor, const float DampingFactor, const float MaxVelocity, const float CompleteTolerance)
	: SpringFactor(SpringFactor)
	, DampingFactor(DampingFactor)
	, MaxVelocity(MaxVelocity)
	, CompleteTolerance(CompleteTolerance)
	, bStarted(false)
	, bCompleted(false)
{
}

void FSpringFloat::Start(float InStartValue, float InTargetValue)
{
	CurrentValue = InStartValue;
	TargetValue = InTargetValue;
	Velocity = 0.0f;
	bStarted = true;
	bCompleted = false;
}

void FSpringFloat::Tick(float DeltaTime)
{
	// If the motion hasn't started or is already completed, no further calculations are needed.
	if (!bStarted || bCompleted)
	{
		return;
	}

	// Limit DeltaTime to prevent large jumps at low FPS.
	DeltaTime = FMath::Min(DeltaTime, 1.0f / 20.0f);

	// Calculate the distance between the current and target values (spring displacement).
	const float springDistance = TargetValue - CurrentValue;

	// Calculate the damping force, which slows down the motion.
	// The force is proportional to the current velocity and the damping factor.
	const float dampingForce = -1 * Velocity * DampingFactor;

	// Calculate the spring force, which pulls the current value towards the target.
	// The force is proportional to the distance and the spring factor.
	const float springForce = springDistance * SpringFactor;

	// The resultant force is the sum of the spring force and the damping force.
	const float resultForce = springForce + dampingForce;

	// Update velocity based on the resultant force and elapsed time.
	Velocity = Velocity + resultForce * DeltaTime;

	// Clamp the velocity to ensure it doesn't exceed the maximum in either direction (positive or negative).
	Velocity = FMath::Clamp(Velocity, -1 * MaxVelocity, MaxVelocity);

	// Update the current value based on the velocity and elapsed time.
	CurrentValue += Velocity * DeltaTime;

	// Check if the resultant force is close enough to zero to consider the motion completed.
	bCompleted = FMath::IsNearlyZero(resultForce, CompleteTolerance);

	if (bCompleted)
	{
		CurrentValue = TargetValue;
	}
}
