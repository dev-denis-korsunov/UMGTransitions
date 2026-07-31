#include "WidgetTransitionSettings.h"

UWidgetTransitionSettings::UWidgetTransitionSettings()
{
	const auto AddDefaultEasing = [this](FName Name, float StartTangent, float EndTangent)
	{
		FWidgetTransitionEasing& Easing = EasingFunctions.AddDefaulted_GetRef();
		Easing.Name = Name;
		FRichCurve* RichCurve = Easing.Curve.GetRichCurve();
		const FKeyHandle StartKey = RichCurve->AddKey(0.0f, 0.0f, false);
		const FKeyHandle EndKey = RichCurve->AddKey(1.0f, 1.0f, false);
		RichCurve->SetKeyInterpMode(StartKey, RCIM_Cubic);
		RichCurve->SetKeyInterpMode(EndKey, RCIM_Cubic);
		RichCurve->SetKeyTangentMode(StartKey, RCTM_User);
		RichCurve->SetKeyTangentMode(EndKey, RCTM_User);
		RichCurve->GetKey(StartKey).LeaveTangent = StartTangent;
		RichCurve->GetKey(EndKey).ArriveTangent = EndTangent;
	};

	AddDefaultEasing(TEXT("Ease"), 0.4f, 0.0f);
	AddDefaultEasing(TEXT("EaseIn"), 0.0f, 2.0f);
	AddDefaultEasing(TEXT("EaseOut"), 2.0f, 0.0f);
	AddDefaultEasing(TEXT("EaseInOut"), 0.0f, 0.0f);
}

const FWidgetTransitionEasing* UWidgetTransitionSettings::FindEasing(FName Name) const
{
	return Name.IsNone() ? nullptr : EasingFunctions.FindByPredicate([Name](const FWidgetTransitionEasing& Easing) { return Easing.Name == Name; });
}

float UWidgetTransitionSettings::EvaluateEasing(FName Name, float Alpha) const
{
	const FWidgetTransitionEasing* Easing = FindEasing(Name);
	if (!Easing) return Alpha;

	return Easing->Curve.GetRichCurveConst()->Eval(FMath::Clamp(Alpha, 0.0f, 1.0f));
}
