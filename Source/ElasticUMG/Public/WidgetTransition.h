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

DECLARE_DYNAMIC_DELEGATE_ThreeParams(FOnWidgetTransitionUpdate, UWidget*, Widget, float, NormalizedProgress, float, EasedProgress);
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
	Vector2D,
	LinearColor,
};

/**
 * Type-tagged Blueprint endpoint. Runtime code expands this value to the channel
 * count of the selected binding before the transition starts.
 */
struct FWidgetTransitionValue
{
	FVector4f Channels = FVector4f::Zero();
	EWidgetTransitionValueType Type = EWidgetTransitionValueType::Float;
};

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
	/** Number of float channels exposed by the resolved property. */
	uint8 ChannelCount = 0;
	/** Writes normalized transition channels to the resolved property. */
	bool Apply(UWidget* Widget, const FVector4f& Value) const;
	/** Reads the current value of the resolved property. */
	bool Read(UWidget* Widget, FVector4f& OutValue) const;
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
	FWidgetTransitionValue FromValue;
	FWidgetTransitionValue ToValue;
	FWidgetTransitionEasingValue Easing;
	FWidgetTransitionEvents Events;
	FOnWidgetTransitionUpdate OnUpdate;
	float Time = 0.2f;
	float Delay = 0.0f;
	float SpringSpeed = 0.65f;
	float SpringBounce = 0.45f;
	int32 RepeatCount = 0;
	bool bUseFrom = false;
	bool bApplyValueBeforeDelay = true;
	bool bYoYo = false;
	bool bRemoveFromParent = false;
	bool bUseSpring = false;
};

	/** Runtime transition with normalized 1, 2, or 4 channel data. */
struct FActiveWidgetTransition
{
	TWeakObjectPtr<UWidget> Widget;
	FName WidgetProperty;
	FWidgetTransitionPropertyBinding PropertyBinding;
	FVector4f FromValue = FVector4f::Zero();
	FVector4f ToValue = FVector4f::Zero();
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
	FOnWidgetTransitionUpdate OnUpdate;
};

UCLASS()
class ELASTICUMG_API UWidgetTransitionFunctionLibrary final : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** Starts a one-channel descriptor. Bind it before adding it to the subsystem. */
	UFUNCTION(BlueprintPure, Category = "Widget Transition", meta = (DisplayName = "Create Float Widget Transition", ElasticUMGTransition = "true", ElasticUMGValueType = "Float"))
	static FWidgetTransition CreateFloatWidgetTransition(float ToValue, float Delay = 0.0f, float Time = 0.2f);

	/** Starts a two-channel descriptor. Bind it before adding it to the subsystem. */
	UFUNCTION(BlueprintPure, Category = "Widget Transition", meta = (DisplayName = "Create Vector2D Widget Transition", ElasticUMGTransition = "true", ElasticUMGValueType = "Vector2D"))
	static FWidgetTransition CreateVectorWidgetTransition(FVector2D ToValue, float Delay = 0.0f, float Time = 0.2f);

	/** Starts a four-channel descriptor. Bind it before adding it to the subsystem. */
	UFUNCTION(BlueprintPure, Category = "Widget Transition", meta = (DisplayName = "Create Color Widget Transition", ElasticUMGTransition = "true", ElasticUMGValueType = "LinearColor"))
	static FWidgetTransition CreateColorWidgetTransition(FLinearColor ToValue, float Delay = 0.0f, float Time = 0.2f);

	/** Sets the target widget and its transitionable property path. */
	UFUNCTION(BlueprintPure, Category = "Widget Transition", meta = (DisplayName = "Bind Widget Transition", ElasticUMGTransitionBinding = "true"))
	static FWidgetTransition WithBinding(FWidgetTransition Transition, UWidget* Widget, UPARAM(meta = (ElasticUMGRole = "WidgetProperty")) const FString& WidgetProperty);
	UFUNCTION(BlueprintPure, Category = "Widget Transition", meta = (DisplayName = "From Float"))
	static FWidgetTransition WithFloatFrom(FWidgetTransition Transition, float FromValue);
	UFUNCTION(BlueprintPure, Category = "Widget Transition", meta = (DisplayName = "From Vector2D"))
	static FWidgetTransition WithVectorFrom(FWidgetTransition Transition, FVector2D FromValue);
	UFUNCTION(BlueprintPure, Category = "Widget Transition", meta = (DisplayName = "From Color"))
	static FWidgetTransition WithColorFrom(FWidgetTransition Transition, FLinearColor FromValue);

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
	UFUNCTION(BlueprintPure, Category = "Widget Transition", meta = (DisplayName = "On Start"))
	static FWidgetTransition WithOnStart(FWidgetTransition Transition, FOnWidgetTransitionEvent OnStarted);
	UFUNCTION(BlueprintPure, Category = "Widget Transition", meta = (DisplayName = "On Finish"))
	static FWidgetTransition WithOnFinish(FWidgetTransition Transition, FOnWidgetTransitionEvent OnFinished);
	UFUNCTION(BlueprintPure, Category = "Widget Transition")
	static FWidgetTransition WithUpdate(FWidgetTransition Transition, FOnWidgetTransitionUpdate OnUpdate);

	/** Converts a transition description to compact runtime data and starts it. */
	UFUNCTION(BlueprintCallable, Category = "Widget Transition", meta = (DisplayName = "Add Widget Transition", WorldContext = "WorldContextObject"))
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
