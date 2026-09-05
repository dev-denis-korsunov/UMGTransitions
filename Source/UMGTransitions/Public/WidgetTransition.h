#pragma once

#include "CoreMinimal.h"
#include "Binding/DynamicPropertyPath.h"
#include "Engine/CurveTable.h"
#include "Kismet/BlueprintFunctionLibrary.h"

#include "WidgetTransition.generated.h"

class UWidget;
class UMaterialInstanceDynamic;
struct FRealCurve;

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
	EWidgetTransitionValueType Type = EWidgetTransitionValueType::Float;
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
	/** Original path supplied by the animation editor. */
	FString PropertyPath;
	/** Optional widget callback invoked after a direct property-path write. */
	FName SynchronizationFunction;
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
	FCurveTableRowHandle Easing;
	FWidgetTransitionPropertyBinding PropertyBinding;
	float Time = 0.2f;
	float Delay = 0.0f;
	float CurrentTime = 0.0f;
	float SpringForce = 0.65f;
	float SpringDamping = 0.45f;
	float SpringMaxSpeed = 0.0f;
	int32 RepeatCount = 0;
	/** Seconds between Updated callbacks and FieldNotify broadcasts; zero preserves per-tick updates. */
	float UpdateInterval = 0.033f;
	uint16 bUseFrom : 1 = false;
	/** Defers applying From Value until the transition starts after its delay. */
	uint16 bDeferFromValue : 1 = false;
	/** Skips Delay after the initial cycle when the transition repeats. */
	uint16 bIgnoreDelayOnRepeat : 1 = false;
	uint16 bYoYo : 1 = false;
	uint16 bRemoveFromParent : 1 = false;
	uint16 bUseSpring : 1 = false;
	uint16 bStarted : 1 = false;
	/** Derives spring frequency from Time so the simulation settles within its requested duration. */
	uint16 bFitSpringToTime : 1 = false;
	/** Whether this transition writes its sampled value to a widget property. */
	uint16 bBound : 1 = false;
	/** Interpolates linear-color properties through HSV instead of RGB. */
	uint16 bInterpolateColorInHSV : 1 = false;
	/** Interpolates linear-color properties through OKLCH instead of RGB. */
	uint16 bInterpolateColorInOKLCH : 1 = false;
	/** Index into UWidgetTransitionSubsystem::Springs when bUseSpring is enabled. */
	int32 SpringIndex = INDEX_NONE;
	/** Resolved once when the transition is added, avoiding a CurveTable lookup every tick. */
	const FRealCurve* EasingCurve = nullptr;
};

UCLASS()
class UMGTRANSITIONS_API UWidgetTransitionFunctionLibrary final : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** Creates a transition bound to a widget property or a Material.Parameter entry. */
	UFUNCTION(BlueprintPure, Category = "Widget Transition", meta = (DisplayName = "Create Widget Transition", DefaultToSelf = "Widget", UMGTransitionsBinding = "Combined", ReturnDisplayName = "Transition"))
	static FWidgetTransition CreateWidgetTransition(UWidget* Widget, UPARAM(meta = (UMGTransitionsRole = "WidgetProperty")) FName WidgetProperty, FWidgetTransitionValue ToValue, float Time = 0.2f, float Delay = 0.0f);
	UFUNCTION(BlueprintPure, Category = "Widget Transition", meta = (CPP_Default_bUseFrom = "true", ReturnDisplayName = "Transition"))
	static FWidgetTransition From(FWidgetTransition Transition, bool bUseFrom, FWidgetTransitionValue FromValue);
	/** Configures optional transition behavior and notification frequency. */
	UFUNCTION(BlueprintPure, Category = "Widget Transition", meta = (ReturnDisplayName = "Transition"))
	static FWidgetTransition Options(FWidgetTransition Transition, UPARAM(meta = (ToolTip = "Defers applying From Value until the transition starts after Delay.")) bool bDeferFromValue = false, bool bIgnoreDelayOnRepeat = false, UPARAM(meta = (ClampMin = "0.0", UIMin = "0.0", UIMax = "1.0", ToolTip = "Seconds between Updated callbacks and FieldNotify broadcasts. Zero updates every tick.")) float CallbackUpdateInterval = 0.033f, UPARAM(meta = (ToolTip = "Interpolates color properties through HSV instead of RGB.")) bool bInterpolateColorInHSV = false, UPARAM(meta = (ToolTip = "Interpolates color properties through OKLCH instead of RGB.")) bool bInterpolateColorInOKLCH = false);

	UFUNCTION(BlueprintPure, Category = "Widget Transition", meta = (DataTablePin = "CurveTable", ReturnDisplayName = "Transition"))
	static FWidgetTransition Easing(FWidgetTransition Transition, UCurveTable* CurveTable, FName RowName);
	UFUNCTION(BlueprintPure, Category = "Widget Transition", meta = (ReturnDisplayName = "Transition"))
	static FWidgetTransition Repeat(FWidgetTransition Transition, int32 RepeatCount);
	UFUNCTION(BlueprintPure, Category = "Widget Transition", meta = (DisplayName = "Yo Yo", ReturnDisplayName = "Transition"))
	static FWidgetTransition YoYo(FWidgetTransition Transition, bool bYoYo = true);
	UFUNCTION(BlueprintPure, Category = "Widget Transition", meta = (ReturnDisplayName = "Transition"))
	static FWidgetTransition Spring(FWidgetTransition Transition, float SpringForce = 0.65f, float SpringDamping = 0.45f, float SpringMaxSpeed = 0.0f, bool bFitSimulationToTime = false);
	UFUNCTION(BlueprintPure, Category = "Widget Transition", meta = (ReturnDisplayName = "Transition"))
	static FWidgetTransition RemoveFromParent(FWidgetTransition Transition, bool bRemoveFromParent = true);
	/** Makes a scalar transition endpoint. */
	UFUNCTION(BlueprintPure, Category = "Widget Transition|Transition Value", meta = (DisplayName = "Make Float Transition Value", ReturnDisplayName = "Transition Value"))
	static FWidgetTransitionValue MakeFloatTransitionValue(float Value);
	/** Makes a two-dimensional transition endpoint. */
	UFUNCTION(BlueprintPure, Category = "Widget Transition|Transition Value", meta = (DisplayName = "Make Vector2D Transition Value", ReturnDisplayName = "Transition Value"))
	static FWidgetTransitionValue MakeVectorTransitionValue(FVector2D Value);
	/** Makes a color transition endpoint. */
	UFUNCTION(BlueprintPure, Category = "Widget Transition|Transition Value", meta = (DisplayName = "Make Color Transition Value", ReturnDisplayName = "Transition Value"))
	static FWidgetTransitionValue MakeColorTransitionValue(FLinearColor Value);
	/** Returns the first channel of a transition endpoint. */
	UFUNCTION(BlueprintPure, Category = "Widget Transition|Transition Value", meta = (DisplayName = "As Float"))
	static float AsFloat(FWidgetTransitionValue Value);
	/** Returns the first two channels of a transition endpoint. */
	UFUNCTION(BlueprintPure, Category = "Widget Transition|Transition Value", meta = (DisplayName = "As Vector2D"))
	static FVector2D AsVector2D(FWidgetTransitionValue Value);
	/** Returns all four channels of a transition endpoint as a linear color. */
	UFUNCTION(BlueprintPure, Category = "Widget Transition|Transition Value", meta = (DisplayName = "As Color"))
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
