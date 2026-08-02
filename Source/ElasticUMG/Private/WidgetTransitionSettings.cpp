#include "WidgetTransitionSettings.h"

UWidgetTransitionSettings::UWidgetTransitionSettings()
{
	const auto AddDefaultEasing = [this](FName Name, FVector2D ControlPoint1, FVector2D ControlPoint2)
	{
		FWidgetTransitionEasing& Easing = EasingFunctions.AddDefaulted_GetRef();
		Easing.Name = Name;
		Easing.ControlPoint1 = ControlPoint1;
		Easing.ControlPoint2 = ControlPoint2;
	};

	AddDefaultEasing(TEXT("Ease"), FVector2D(0.25f, 0.1f), FVector2D(0.25f, 1.0f));
	AddDefaultEasing(TEXT("EaseIn"), FVector2D(0.42f, 0.0f), FVector2D(1.0f, 1.0f));
	AddDefaultEasing(TEXT("EaseOut"), FVector2D(0.0f, 0.0f), FVector2D(0.58f, 1.0f));
	AddDefaultEasing(TEXT("EaseInOut"), FVector2D(0.42f, 0.0f), FVector2D(0.58f, 1.0f));
}

float FWidgetTransitionEasing::Evaluate(float Alpha) const
{
	return EvaluateCubicBezier(ControlPoint1, ControlPoint2, Alpha);
}

float FWidgetTransitionEasing::EvaluateCubicBezier(FVector2D InControlPoint1, FVector2D InControlPoint2, float Alpha)
{
	const auto Cubic = [](float P1, float P2, float T)
	{
		const float OneMinusT = 1.0f - T;
		return 3.0f * OneMinusT * OneMinusT * T * P1 + 3.0f * OneMinusT * T * T * P2 + T * T * T;
	};
	Alpha = FMath::Clamp(Alpha, 0.0f, 1.0f);
	float Low = 0.0f;
	float High = 1.0f;
	for (int32 Iteration = 0; Iteration < 12; ++Iteration)
	{
		const float T = (Low + High) * 0.5f;
		if (Cubic(FMath::Clamp(InControlPoint1.X, 0.0f, 1.0f), FMath::Clamp(InControlPoint2.X, 0.0f, 1.0f), T) < Alpha) Low = T;
		else High = T;
	}
	return Cubic(InControlPoint1.Y, InControlPoint2.Y, (Low + High) * 0.5f);
}

const FWidgetTransitionEasing* UWidgetTransitionSettings::FindEasing(FName Name) const
{
	return Name.IsNone() ? nullptr : EasingFunctions.FindByPredicate([Name](const FWidgetTransitionEasing& Easing) { return Easing.Name == Name; });
}

float UWidgetTransitionSettings::EvaluateEasing(FName Name, float Alpha) const
{
	const FWidgetTransitionEasing* Easing = FindEasing(Name);
	if (!Easing) return Alpha;

	return Easing->Evaluate(Alpha);
}
