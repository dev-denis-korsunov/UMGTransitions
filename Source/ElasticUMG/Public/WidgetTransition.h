#pragma once

#include "CoreMinimal.h"
#include "Binding/DynamicPropertyPath.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Misc/TVariant.h"
#include "Spring.h"
#include "Subsystems/WorldSubsystem.h"
#include "WidgetTransitionSettings.h"

#include "WidgetTransition.generated.h"

class UWidget;

DECLARE_DYNAMIC_DELEGATE_ThreeParams(FOnFloatWidgetTransitionUpdate, UWidget*, Widget, float, Value, float, Alpha);
DECLARE_DYNAMIC_DELEGATE_ThreeParams(FOnBoolWidgetTransitionUpdate, UWidget*, Widget, bool, Value, float, Alpha);
DECLARE_DYNAMIC_DELEGATE_ThreeParams(FOnVectorWidgetTransitionUpdate, UWidget*, Widget, FVector2D, Value, float, Alpha);
DECLARE_DYNAMIC_DELEGATE_ThreeParams(FOnColorWidgetTransitionUpdate, UWidget*, Widget, FLinearColor, Value, float, Alpha);
DECLARE_DYNAMIC_DELEGATE_OneParam(FOnWidgetTransitionEvent, UWidget*, Widget);

/** Rare lifecycle callbacks stored by the transition subsystem rather than each active transition. */
struct FWidgetTransitionEvents
{
	FOnWidgetTransitionEvent OnStarted;
	FOnWidgetTransitionEvent OnFinished;

	bool HasBoundEvents() const { return OnStarted.IsBound() || OnFinished.IsBound(); }
};

UENUM(BlueprintType)
enum class EWidgetTransitionValueType : uint8
{
	Float,
	Bool,
	Vector2D,
	LinearColor,
};

/** The two endpoints of a transition always use the same alternative. */
using FTransitionValue = TVariant<float, FVector2D, bool, FLinearColor>;

/** The one typed update callback used by a transition, allocated only when the Up pin is bound. */
using FWidgetTransitionUpdateCallback = TVariant<FEmptyVariantState, FOnFloatWidgetTransitionUpdate, FOnBoolWidgetTransitionUpdate, FOnVectorWidgetTransitionUpdate, FOnColorWidgetTransitionUpdate>;

/** Heap-allocated spring state; only present for a float or Vector2D spring transition. */
struct FWidgetTransitionSpringState
{
	TVariant<FEmptyVariantState, FSpringFloat, FSpringVector2D> Spring;
};

/** Cached access to a transition property on a widget. */
USTRUCT()
struct ELASTICUMG_API FWidgetTransitionPropertyBinding
{
	GENERATED_BODY()

	/** Resolved property path, retained to avoid resolving it each tick. */
	FDynamicPropertyPath CachedPropertyPath;
	/** Whether the property path has been successfully resolved. */
	bool bResolved = false;
	/** Whether a floating-point property uses double precision storage. */
	bool bUsesDouble = false;
	/** Value type used to read and write the resolved property. */
	EWidgetTransitionValueType ValueType = EWidgetTransitionValueType::Float;

	/** Resolves a widget property and caches the resulting property path. */
	bool Resolve(UWidget* InWidget, const FString& InPropertyPath);
	/** Clears the cached property path and resolution state. */
	void Invalidate();
	/** Writes a transition value to the resolved property. */
	bool Apply(UWidget* Widget, const FTransitionValue& Value) const;
	/** Reads the current value of the resolved property. */
	bool Read(UWidget* Widget, FTransitionValue& OutValue) const;
};

/**
 * Blueprint-facing transition description. It is intentionally cold data: it is
 * assembled by pure functions and converted into a compact runtime transition
 * only when StartWidgetTransition is called.
 */
USTRUCT(BlueprintType)
struct ELASTICUMG_API FWidgetTransition
{
	GENERATED_BODY()

	TWeakObjectPtr<UWidget> Widget;
	FName WidgetProperty;
	FTransitionValue FromValue;
	FTransitionValue ToValue;
	FWidgetTransitionEasingValue Easing;
	FWidgetTransitionEvents Events;
	FWidgetTransitionUpdateCallback UpdateCallback;
	float Time = 0.2f;
	float Delay = 0.0f;
	float SpringSpeed = 0.65f;
	float SpringBounce = 0.45f;
	int32 RepeatCount = 0;
	EWidgetTransitionValueType ValueType = EWidgetTransitionValueType::Float;
	bool bUseFrom = false;
	bool bApplyValueBeforeDelay = true;
	bool bYoYo = false;
	bool bRemoveFromParent = false;
	bool bUseSpring = false;
};

/** Runtime transition with compact tagged From/To values. */
struct FActiveWidgetTransition
{
	TWeakObjectPtr<UWidget> Widget;
	FName WidgetProperty;
	FWidgetTransitionPropertyBinding PropertyBinding;
	FTransitionValue FromValue;
	FTransitionValue ToValue;
	FWidgetTransitionEasingValue Easing;
	float Time = 0.2f;
	float Delay = 0.0f;
	float CurrentTime = 0.0f;
	int32 RepeatCount = 0;
	int32 CompletedRepeats = 0;
	/** Normalized designer controls. 0..1 maps to physical stiffness and damping ratio. */
	float SpringSpeed = 0.65f;
	float SpringBounce = 0.45f;
	uint16 bFrom : 1 = false;
	uint16 bChangeFromPropertyAfterDelay : 1 = false;
	uint16 bPipe : 1 = false;
	uint16 bFromVisibility : 1 = false;
	uint16 bToVisibility : 1 = false;
	uint16 bRemoveFromParent : 1 = false;
	uint16 bSpring : 1 = false;
	uint16 bYoYo : 1 = false;
	uint16 bStarted : 1 = false;
	/** Stable key for rare lifecycle callbacks in UWidgetTransitionSubsystem::EventCallbacks. */
	uint64 TransitionId = 0;
	TUniquePtr<FWidgetTransitionSpringState> SpringState;
	TUniquePtr<FWidgetTransitionUpdateCallback> UpdateCallback;
};

UCLASS()
class ELASTICUMG_API UWidgetTransitionFunctionLibrary final : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** Typed pure builder selected by the universal Create Widget Transition node. */
	UFUNCTION(BlueprintPure, Category = "Widget Transition", meta = (BlueprintInternalUseOnly = "true", ElasticUMGTransition = "true", ElasticUMGValueType = "Float", ElasticUMGModes = "Interpolation,Spring"))
	static FWidgetTransition CreateFloatWidgetTransition(
		UPARAM(meta = (ElasticUMGTab = "Basic", ElasticUMGOption = "WidgetAndProperty", ElasticUMGLabel = "Wp", ElasticUMGTooltip = "Widget and Property: show or hide both binding inputs")) UWidget* Widget,
		UPARAM(meta = (ElasticUMGRole = "WidgetProperty")) const FString& WidgetProperty, UPARAM(meta = (ElasticUMGRole = "ToValue")) float ToValue,
		UPARAM(meta = (ElasticUMGRole = "UseFrom")) bool bUseFrom = false, UPARAM(meta = (ElasticUMGTab = "Basic", ElasticUMGOption = "From", ElasticUMGLabel = "Fr", ElasticUMGTooltip = "From: use an explicit starting value")) float FromValue = 0.0f);

	UFUNCTION(BlueprintPure, Category = "Widget Transition", meta = (BlueprintInternalUseOnly = "true", ElasticUMGTransition = "true", ElasticUMGValueType = "Bool", ElasticUMGModes = "Interpolation"))
	static FWidgetTransition CreateBoolWidgetTransition(UWidget* Widget, const FString& WidgetProperty, bool ToValue, bool bUseFrom = false, bool FromValue = false);

	UFUNCTION(BlueprintPure, Category = "Widget Transition", meta = (BlueprintInternalUseOnly = "true", ElasticUMGTransition = "true", ElasticUMGValueType = "Vector2D", ElasticUMGModes = "Interpolation,Spring"))
	static FWidgetTransition CreateVectorWidgetTransition(UWidget* Widget, const FString& WidgetProperty, FVector2D ToValue, bool bUseFrom = false, FVector2D FromValue = FVector2D::ZeroVector);

	UFUNCTION(BlueprintPure, Category = "Widget Transition", meta = (BlueprintInternalUseOnly = "true", ElasticUMGTransition = "true", ElasticUMGValueType = "LinearColor", ElasticUMGModes = "Interpolation"))
	static FWidgetTransition CreateColorWidgetTransition(UWidget* Widget, const FString& WidgetProperty, FLinearColor ToValue, bool bUseFrom = false, FLinearColor FromValue = FLinearColor::White);

	UFUNCTION(BlueprintPure, Category = "Widget Transition")
	static FWidgetTransition WithDelay(FWidgetTransition Transition, float Delay, bool bApplyValueBeforeDelay = true);
	UFUNCTION(BlueprintPure, Category = "Widget Transition")
	static FWidgetTransition WithEasing(FWidgetTransition Transition, FWidgetTransitionEasingValue Easing);
	UFUNCTION(BlueprintPure, Category = "Widget Transition")
	static FWidgetTransition WithRepeat(FWidgetTransition Transition, int32 RepeatCount, bool bYoYo = false);
	UFUNCTION(BlueprintPure, Category = "Widget Transition")
	static FWidgetTransition WithSpring(FWidgetTransition Transition, float SpringSpeed = 0.65f, float SpringBounce = 0.45f);
	UFUNCTION(BlueprintPure, Category = "Widget Transition")
	static FWidgetTransition WithRemoveFromParent(FWidgetTransition Transition, bool bRemoveFromParent = true);
	UFUNCTION(BlueprintPure, Category = "Widget Transition")
	static FWidgetTransition WithEvents(FWidgetTransition Transition, FOnWidgetTransitionEvent OnStarted, FOnWidgetTransitionEvent OnFinished);
	UFUNCTION(BlueprintPure, Category = "Widget Transition")
	static FWidgetTransition WithFloatUpdate(FWidgetTransition Transition, FOnFloatWidgetTransitionUpdate OnUpdate);
	UFUNCTION(BlueprintPure, Category = "Widget Transition")
	static FWidgetTransition WithBoolUpdate(FWidgetTransition Transition, FOnBoolWidgetTransitionUpdate OnUpdate);
	UFUNCTION(BlueprintPure, Category = "Widget Transition")
	static FWidgetTransition WithVectorUpdate(FWidgetTransition Transition, FOnVectorWidgetTransitionUpdate OnUpdate);
	UFUNCTION(BlueprintPure, Category = "Widget Transition")
	static FWidgetTransition WithColorUpdate(FWidgetTransition Transition, FOnColorWidgetTransitionUpdate OnUpdate);

	/** Converts a transition description to compact runtime data and starts it. */
	UFUNCTION(BlueprintCallable, Category = "Widget Transition", meta = (WorldContext = "WorldContextObject"))
	static void StartWidgetTransition(const UObject* WorldContextObject, FWidgetTransition Transition);

	UFUNCTION(BlueprintCallable, Category = "Widget Transition", meta = (WorldContext = "WorldContextObject"))
	static void ClearAllWidgetTransitions(const UObject* WorldContextObject, UWidget* Widget);
};

UCLASS()
class ELASTICUMG_API UWidgetTransitionSubsystem final : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual ETickableTickType GetTickableTickType() const override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override { RETURN_QUICK_DECLARE_CYCLE_STAT(UWidgetTransitionSubsystem, STATGROUP_Tickables); }
	virtual bool IsTickableInEditor() const override { return true; }

	TSparseArray<FActiveWidgetTransition> Transitions;
	/** Lifecycle callbacks for the small subset of transitions that bind Start or Finished. */
	TMap<uint64, FWidgetTransitionEvents> EventCallbacks;
	/** Monotonic key source; a sparse-array index cannot be used because indices are reused. */
	uint64 NextTransitionId = 1;
};
