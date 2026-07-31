#pragma once

/**
* The `FSpringFloat` class simulates an elastic (spring-like) motion for a floating-point value,
* allowing smooth transitions between two values over time. The class applies spring and damping forces
* to progressively move the value from a start position toward a target, with velocity control to avoid abrupt changes.
*
* Features:
* - Elastic Motion Simulation: Utilizes a spring factor and damping factor to simulate natural, decaying oscillations,
*   providing smooth and responsive transitions.
* - Velocity Control: Ensures that the value changes in a controlled manner, respecting a maximum velocity limit
*   to avoid excessively fast transitions.
* - Completion Tracking: Tracks the completion of the motion, allowing queries to determine when the transition
*   has reached its target.
*/
class ELASTICUMG_API FSpringFloat
{
public:
	FSpringFloat(float SpringFactor, float DampingFactor);

	void Tick(float DeltaTime);

	void Start(float InStartValue, float InTargetValue);

	FORCEINLINE float GetValue() const { return CurrentValue; }
	FORCEINLINE bool IsCompleted() const { return bCompleted; }

private:
	/**
	 * Defines how strong the spring force is, determining how quickly the value is pulled toward the target.
	 */
	const float SpringFactor = 200.0f;

	/**
	 * Controls how quickly the motion decays, reducing the oscillation effect over time.
	 */
	const float DampingFactor = 16.0f;

	/** State */
	float TargetValue = 0.0f;
	float CurrentValue = 0.0f;
	float Velocity = 0.0f;
	float InitialDisplacement = 0.0f;
	uint8 bStarted : 1;
	uint8 bCompleted : 1;
};

/** A single physical spring for a 2D value. Velocity and its cap are shared by both axes. */
class ELASTICUMG_API FSpringVector2D
{
public:
	FSpringVector2D(float SpringFactor, float DampingFactor);

	void Tick(float DeltaTime);
	void Start(FVector2D InStartValue, FVector2D InTargetValue);

	FORCEINLINE const FVector2D& GetValue() const { return CurrentValue; }
	FORCEINLINE bool IsCompleted() const { return bCompleted; }

private:
	const float SpringFactor = 200.0f;
	const float DampingFactor = 16.0f;
	FVector2D TargetValue = FVector2D::ZeroVector;
	FVector2D CurrentValue = FVector2D::ZeroVector;
	FVector2D Velocity = FVector2D::ZeroVector;
	float InitialDisplacement = 0.0f;
	uint8 bStarted : 1;
	uint8 bCompleted : 1;
};
