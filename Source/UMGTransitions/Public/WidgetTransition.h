#pragma once

#include "CoreMinimal.h"
#include "Binding/DynamicPropertyPath.h"
#include "Kismet/BlueprintFunctionLibrary.h"

#include "WidgetTransition.generated.h"

class UWidget;
class UMaterialInstanceDynamic;

UENUM(BlueprintType)
enum class EWidgetTransitionColorMix : uint8
{
	RGB UMETA(ToolTip = "Interpolates red, green, blue, and alpha channels directly."),
	HSV UMETA(ToolTip = "Interpolates hue, saturation, value, and alpha."),
	OKLCH UMETA(ToolTip = "Interpolates perceptual OKLCH color channels and alpha."),
};

UENUM(BlueprintType)
enum class EWidgetTransitionAddMode : uint8
{
	Replace UMETA(ToolTip = "Replaces the active transition for this Widget Property."),
	Skip UMETA(ToolTip = "Does not add this transition if the Widget Property already has an active or queued transition."),
	Pipe UMETA(ToolTip = "Queues this transition until the active transition for this Widget Property finishes."),
};

UENUM()
enum class EWidgetTransitionValueType : uint8
{
	Float,
	Vector2D,
	LinearColor,
};

/**
 * Type-tagged Blueprint endpoint. Runtime code expands this value to the channel
 * count of the selected binding before the transition starts.
 */
USTRUCT(BlueprintType)
struct UMGTRANSITIONS_API FWidgetTransitionValue
{
	GENERATED_BODY()

	/** Normalized transition channels. Split this pin to edit its FVector4f directly. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Widget Transition")
	FVector4f Channels = FVector4f::Zero();

	/** Semantic type selected by Make Transition Value. Kept internal so split pins stay compact. */
	UPROPERTY()
	EWidgetTransitionValueType Type = EWidgetTransitionValueType::Float;
};

/**
 * A normalized cubic Bezier timing function. The curve always begins at (0, 0)
 * and finishes at (1, 1); only its two control points are editable.
 */
USTRUCT(BlueprintType)
struct UMGTRANSITIONS_API FWidgetTransitionEasing
{
	GENERATED_BODY()

	/** Handle leaving the start point. Its X coordinate is clamped to the normalized time range. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Widget Transition")
	FVector2D FirstControlPoint = FVector2D(0.25, 0.1);

	/** Handle arriving at the end point. Its X coordinate is clamped to the normalized time range. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Widget Transition")
	FVector2D SecondControlPoint = FVector2D(0.25, 1.0);

	/** Evaluates the easing at normalized progress. */
	float Evaluate(float Progress) const;
	/** Clamps the editable control-point domain while preserving vertical overshoot. */
	void Clamp();
};

enum class EWidgetTransitionBindingKind : uint8
{
	Property,
	PropertyFieldNotify,
	MaterialScalar,
	MaterialVector,
	RenderOpacity,
	RenderTransformTranslation,
	RenderTransformScale,
	RenderTransformShear,
	RenderTransformAngle,
	RenderTransformPivot,
};

/** Cached access to a transition property on a widget. */
struct UMGTRANSITIONS_API FWidgetTransitionPropertyBinding
{
	/** Resolved property path, retained to avoid resolving it each tick. */
	FDynamicPropertyPath CachedPropertyPath;
	/** Whether the property path has been successfully resolved. */
	bool bResolved = false;
	/** Whether a floating-point property uses double precision storage. */
	bool bUsesDouble = false;
	/** Value type used to read and write the resolved property. */
	EWidgetTransitionValueType ValueType = EWidgetTransitionValueType::Float;
	/** Resolved adapter kind. */
	EWidgetTransitionBindingKind Kind = EWidgetTransitionBindingKind::Property;
	/** Cached dynamic material for a virtual material channel. */
	TWeakObjectPtr<UMaterialInstanceDynamic> MaterialInstance;
	/** Material parameter addressed by the cached material adapter or FieldNotify property name. */
	FName MaterialParameter;

	/** Resolves a widget property and caches the resulting property path. */
	bool Resolve(UWidget* InWidget, const FString& InPropertyPath);
	/** Resolves and determines the channel layout of a material parameter for a supported widget adapter. */
	bool ResolveMaterial(UWidget* InWidget, FName InParameter);
	/** Clears the cached property path and resolution state. */
	void Invalidate();
	/** Number of float channels exposed by the resolved property. */
	uint8 ChannelCount = 0;
	/** Whether applying this binding can emit a FieldNotify change notification. */
	bool IsFieldNotify() const;
	/** Writes normalized transition channels to the resolved property. */
	bool Apply(UWidget* Widget, const FVector4f& Value, bool bBroadcastFieldNotify = true) const;
	/** Broadcasts the FieldNotify event after a write performed without notification. */
	void BroadcastFieldNotify(UWidget* Widget) const;
	/** Reads the current value of the resolved property. */
	bool Read(UWidget* Widget, FVector4f& OutValue) const;
};

/**
 * Blueprint-facing transition definition and runtime instance. Pure functions
 * construct a copy; Add Widget Transition normalizes that copy for ticking.
 */
USTRUCT(BlueprintType)
struct UMGTRANSITIONS_API FWidgetTransition
{
	GENERATED_BODY()

	TWeakObjectPtr<UWidget> Widget;
	FName WidgetProperty;
	FWidgetTransitionValue FromValue;
	FWidgetTransitionValue ToValue;
	FWidgetTransitionEasing Easing;
	FWidgetTransitionPropertyBinding PropertyBinding;
	float Time = 0.2f;
	float Delay = 0.0f;
	float CurrentTime = 0.0f;
	/** Spring stiffness coefficient. 160 preserves the default spring response; larger values make it faster. */
	float SpringForce = 160.0f;
	float SpringDamping = 0.45f;
	float SpringMaxSpeed = 0.0f;
	int32 RepeatCount = 0;
	/** Seconds between async-node Update callbacks and FieldNotify broadcasts; zero preserves per-tick events. */
	float EventInterval = 0.033f;
	/** Color mixing method used when interpolating color transition values. */
	EWidgetTransitionColorMix ColorMix = EWidgetTransitionColorMix::RGB;
	/** Determines whether a transition replaces, skips, or queues behind an active property transition. */
	EWidgetTransitionAddMode AddMode = EWidgetTransitionAddMode::Replace;
	uint16 bUseFrom : 1 = false;
	/** Whether to evaluate the cubic Bezier easing instead of linear progress. */
	uint16 bUseEasing : 1 = false;
	/** Applies From Value immediately, before the initial delay. */
	uint16 bIgnoreDelay : 1 = true;
	/** Applies Delay again at the start of every repeated cycle. */
	uint16 bRepeatDelay : 1 = false;
	uint16 bYoYo : 1 = false;
	uint16 bUseSpring : 1 = false;
	/** Runtime phase flag: true while the reverse half of a Yo Yo cycle is active. */
	uint16 bYoYoReverse : 1 = false;
	uint16 bStarted : 1 = false;
	/** Derives spring frequency from Time so the simulation settles within its requested duration. */
	uint16 bFitToTime : 1 = false;
	/** Whether this transition writes its sampled value to a widget property. */
	uint16 bBound : 1 = false;
	/** Index into UWidgetTransitionSubsystem::Springs when bUseSpring is enabled. */
	int32 SpringIndex = INDEX_NONE;
};

UCLASS()
class UMGTRANSITIONS_API UWidgetTransitionFunctionLibrary final : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** Creates a transition bound to a widget property or a Material.Parameter entry. */
	UFUNCTION(BlueprintPure, Category = "Widget Transition", meta = (DisplayName = "Create Widget Transition", DefaultToSelf = "Widget", UMGTransitionsBinding = "Combined", ReturnDisplayName = "Transition", AdvancedDisplay = "bYoYo,RepeatCount,bRepeatDelay,AddMode,ColorMix,EventInterval"))
	static FWidgetTransition CreateWidgetTransition(UWidget* Widget, UPARAM(meta = (UMGTransitionsRole = "WidgetProperty")) FName WidgetProperty, FWidgetTransitionValue ToValue, float Time = 0.2f, float Delay = 0.0f, bool bYoYo = false, UPARAM(meta = (ClampMin = "-1", ToolTip = "Number of additional cycles. Minus one repeats indefinitely.")) int32 RepeatCount = 0, UPARAM(meta = (ToolTip = "Applies Delay again at the start of every repeated cycle.")) bool bRepeatDelay = false, UPARAM(meta = (UMGTransitionsSegmentedControl, ToolTip = "Replace replaces, Skip ignores this transition when the property is busy, and Pipe queues behind the active transition.")) EWidgetTransitionAddMode AddMode = EWidgetTransitionAddMode::Replace, UPARAM(meta = (DisplayName = "ColorMix", UMGTransitionsSegmentedControl, ToolTip = "Color interpolation method: RGB, HSV, or OKLCH.")) EWidgetTransitionColorMix ColorMix = EWidgetTransitionColorMix::RGB, UPARAM(meta = (ClampMin = "0.0", UIMin = "0.0", UIMax = "1.0", ToolTip = "Seconds between async-node Update callbacks and FieldNotify broadcasts. Zero dispatches events every tick.")) float EventInterval = 0.033f);
	UFUNCTION(BlueprintPure, Category = "Widget Transition", meta = (CPP_Default_bUseFrom = "true", CPP_Default_bIgnoreDelay = "true", ReturnDisplayName = "Transition"))
	static FWidgetTransition From(FWidgetTransition Transition, bool bUseFrom, FWidgetTransitionValue FromValue, UPARAM(meta = (ToolTip = "Applies From Value immediately, before the transition delay.")) bool bIgnoreDelay = true);

	/** Applies a normalized cubic Bezier timing function to this transition. */
	UFUNCTION(BlueprintPure, Category = "Widget Transition", meta = (ReturnDisplayName = "Transition"))
	static FWidgetTransition Easing(FWidgetTransition Transition, FWidgetTransitionEasing Easing);
	UFUNCTION(BlueprintPure, Category = "Widget Transition", meta = (ReturnDisplayName = "Transition"))
	static FWidgetTransition Spring(FWidgetTransition Transition, UPARAM(meta = (ClampMin = "1.0", ToolTip = "Spring stiffness coefficient. 160 matches the default response; larger values make the spring faster. It does not affect Fit To Time.")) float SpringForce = 160.0f, float SpringDamping = 0.45f, UPARAM(meta = (AdvancedDisplay, ClampMin = "0.0")) float SpringMaxSpeed = 0.0f, UPARAM(meta = (AdvancedDisplay, ToolTip = "Derives spring frequency from Time and Damping so the transition completes at Time. Spring Force does not affect this mode.")) bool bFitToTime = false);
	/** Makes a scalar transition endpoint. */
	UFUNCTION(BlueprintPure, Category = "Widget Transition", meta = (BlueprintAutocast, DisplayName = "Make Float Transition Value", ReturnDisplayName = "Transition Value"))
	static FWidgetTransitionValue MakeFloatTransitionValue(float Value);
	/** Makes a two-dimensional transition endpoint. */
	UFUNCTION(BlueprintPure, Category = "Widget Transition", meta = (BlueprintAutocast, DisplayName = "Make Vector2D Transition Value", ReturnDisplayName = "Transition Value"))
	static FWidgetTransitionValue MakeVectorTransitionValue(FVector2D Value);
	/** Makes a color transition endpoint. */
	UFUNCTION(BlueprintPure, Category = "Widget Transition", meta = (BlueprintAutocast, DisplayName = "Make Color Transition Value", ReturnDisplayName = "Transition Value"))
	static FWidgetTransitionValue MakeColorTransitionValue(FLinearColor Value);
	/** Returns the first channel of a transition endpoint. */
	UFUNCTION(BlueprintPure, Category = "Widget Transition", meta = (DisplayName = "As Float"))
	static float AsFloat(FWidgetTransitionValue Value);
	/** Returns the first two channels of a transition endpoint. */
	UFUNCTION(BlueprintPure, Category = "Widget Transition", meta = (DisplayName = "As Vector2D"))
	static FVector2D AsVector2D(FWidgetTransitionValue Value);
	/** Returns all four channels of a transition endpoint as a linear color. */
	UFUNCTION(BlueprintPure, Category = "Widget Transition", meta = (DisplayName = "As Color"))
	static FLinearColor AsColor(FWidgetTransitionValue Value);

	/** Starts one transition. */
	UFUNCTION(BlueprintCallable, Category = "Widget Transition", meta = (DisplayName = "Add Widget Transition", WorldContext = "WorldContextObject"))
	static void AddWidgetTransition(const UObject* WorldContextObject, FWidgetTransition Transition);
	/** Starts every transition in the array. */
	UFUNCTION(BlueprintCallable, Category = "Widget Transition", meta = (DisplayName = "Add Widget Transition Array", WorldContext = "WorldContextObject", AutoCreateRefTerm = "Transitions"))
	static void AddWidgetTransitionArray(const UObject* WorldContextObject, TArray<FWidgetTransition> Transitions);

	UFUNCTION(BlueprintCallable, Category = "Widget Transition", meta = (WorldContext = "WorldContextObject"))
	static void ClearAllWidgetTransitions(const UObject* WorldContextObject, UWidget* Widget);
};
