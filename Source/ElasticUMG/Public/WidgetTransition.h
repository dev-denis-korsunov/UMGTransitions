#pragma once

#include "CoreMinimal.h"
#include "Binding/DynamicPropertyPath.h"
#include "Engine/CurveTable.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Kismet/BlueprintAsyncActionBase.h"
#include "Misc/TVariant.h"
#include "Spring.h"
#include "Subsystems/WorldSubsystem.h"
#include "WidgetTransitionSettings.h"

#include "WidgetTransition.generated.h"

class UWidget;
class UMaterialInstanceDynamic;

DECLARE_DYNAMIC_DELEGATE_ThreeParams(FOnWidgetTransitionUpdate, UWidget*, Widget, float, NormalizedProgress, float, EasedProgress);
DECLARE_DYNAMIC_DELEGATE_OneParam(FOnWidgetTransitionEvent, UWidget*, Widget);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnWidgetTransitionAsyncEvent, UWidget*, Widget);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOnWidgetTransitionAsyncUpdate, UWidget*, Widget, float, NormalizedProgress, float, EasedProgress);

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

enum class EWidgetTransitionBindingKind : uint8
{
	Property,
	Material,
	MaterialScalar,
	MaterialVector,
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
	/** Resolved adapter kind. */
	EWidgetTransitionBindingKind Kind = EWidgetTransitionBindingKind::Property;
	/** Cached dynamic material for a virtual material channel. */
	TWeakObjectPtr<UMaterialInstanceDynamic> MaterialInstance;
	/** Material parameter addressed by the cached material adapter. */
	FName MaterialParameter;

	/** Resolves a widget property and caches the resulting property path. */
	bool Resolve(UWidget* InWidget, const FString& InPropertyPath);
	/** Resolves and determines the channel layout of a material parameter for a supported widget adapter. */
	bool ResolveMaterial(UWidget* InWidget, FName InParameter);
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
	EWidgetTransitionBindingKind BindingKind = EWidgetTransitionBindingKind::Property;
	FName MaterialParameter;
	FWidgetTransitionValue FromValue;
	FWidgetTransitionValue ToValue;
	FCurveTableRowHandle Easing;
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
	/** Derives spring frequency from Time so the simulation settles within its requested duration. */
	bool bFitSpringToTime = false;
};

	/** Runtime transition with normalized 1, 2, or 4 channel data. */
struct FActiveWidgetTransition
{
	TWeakObjectPtr<UWidget> Widget;
	FName WidgetProperty;
	FWidgetTransitionPropertyBinding PropertyBinding;
	FVector4f FromValue = FVector4f::Zero();
	FVector4f ToValue = FVector4f::Zero();
	FCurveTableRowHandle Easing;
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
	uint16 bFitSpringToTime : 1 = false;
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
	/** Sets the target widget and its transitionable property path. */
	UFUNCTION(BlueprintPure, Category = "Widget Transition", meta = (DisplayName = "Bind", ElasticUMGTransitionBinding = "true"))
	static FWidgetTransition Bind(FWidgetTransition Transition, UWidget* Widget, UPARAM(meta = (ElasticUMGRole = "WidgetProperty")) FName WidgetProperty);
	/** Binds a material parameter on an Image or Border brush material. The adapter determines its channel layout. */
	UFUNCTION(BlueprintPure, Category = "Widget Transition")
	static FWidgetTransition BindMaterialParameter(FWidgetTransition Transition, UWidget* Widget, FName ParameterName);

	UFUNCTION(BlueprintPure, Category = "Widget Transition")
	static FWidgetTransition Delay(FWidgetTransition Transition, float Delay, bool bApplyValueBeforeDelay = true);
	UFUNCTION(BlueprintPure, Category = "Widget Transition")
	static FWidgetTransition Easing(FWidgetTransition Transition, FCurveTableRowHandle Easing);
	UFUNCTION(BlueprintPure, Category = "Widget Transition")
	static FWidgetTransition Repeat(FWidgetTransition Transition, int32 RepeatCount, bool bYoYo = false);
	UFUNCTION(BlueprintPure, Category = "Widget Transition")
	static FWidgetTransition Spring(FWidgetTransition Transition, float SpringSpeed = 0.65f, float SpringBounce = 0.45f, bool bFitSimulationToTime = false);
	UFUNCTION(BlueprintPure, Category = "Widget Transition")
	static FWidgetTransition RemoveFromParent(FWidgetTransition Transition, bool bRemoveFromParent = true);
	UFUNCTION(BlueprintPure, Category = "Widget Transition")
	static FWidgetTransition Events(FWidgetTransition Transition, FOnWidgetTransitionEvent OnStarted, FOnWidgetTransitionEvent OnFinished);
	UFUNCTION(BlueprintPure, Category = "Widget Transition", meta = (DisplayName = "On Start"))
	static FWidgetTransition OnStart(FWidgetTransition Transition, FOnWidgetTransitionEvent OnStarted);
	UFUNCTION(BlueprintPure, Category = "Widget Transition", meta = (DisplayName = "On Finish"))
	static FWidgetTransition OnFinish(FWidgetTransition Transition, FOnWidgetTransitionEvent OnFinished);
	UFUNCTION(BlueprintPure, Category = "Widget Transition")
	static FWidgetTransition OnUpdate(FWidgetTransition Transition, FOnWidgetTransitionUpdate OnUpdate);

	/** Converts a transition description to compact runtime data and starts it. */
	UFUNCTION(BlueprintCallable, Category = "Widget Transition", meta = (DisplayName = "Add Widget Transition", WorldContext = "WorldContextObject"))
	static void StartWidgetTransition(const UObject* WorldContextObject, FWidgetTransition Transition);

	UFUNCTION(BlueprintCallable, Category = "Widget Transition", meta = (WorldContext = "WorldContextObject"))
	static void ClearAllWidgetTransitions(const UObject* WorldContextObject, UWidget* Widget);

private:
	/** Internal compiler targets for the two dynamic custom nodes. */
	UFUNCTION(BlueprintPure, meta = (BlueprintInternalUseOnly = "true"))
	static FWidgetTransition CreateFloatWidgetTransition(float ToValue, float Delay = 0.0f, float Time = 0.2f);
	UFUNCTION(BlueprintPure, meta = (BlueprintInternalUseOnly = "true"))
	static FWidgetTransition CreateVectorWidgetTransition(FVector2D ToValue, float Delay = 0.0f, float Time = 0.2f);
	UFUNCTION(BlueprintPure, meta = (BlueprintInternalUseOnly = "true"))
	static FWidgetTransition CreateColorWidgetTransition(FLinearColor ToValue, float Delay = 0.0f, float Time = 0.2f);
	UFUNCTION(BlueprintPure, meta = (BlueprintInternalUseOnly = "true"))
	static FWidgetTransition FromFloat(FWidgetTransition Transition, float FromValue);
	UFUNCTION(BlueprintPure, meta = (BlueprintInternalUseOnly = "true"))
	static FWidgetTransition FromVector(FWidgetTransition Transition, FVector2D FromValue);
	UFUNCTION(BlueprintPure, meta = (BlueprintInternalUseOnly = "true"))
	static FWidgetTransition FromColor(FWidgetTransition Transition, FLinearColor FromValue);
};

/** Blueprint async action which exposes transition lifecycle callbacks as execution outputs. */
UCLASS()
class ELASTICUMG_API UWidgetTransitionAsyncAction final : public UBlueprintAsyncActionBase
{
	GENERATED_BODY()

public:
	UPROPERTY(BlueprintAssignable)
	FOnWidgetTransitionAsyncEvent Started;
	UPROPERTY(BlueprintAssignable)
	FOnWidgetTransitionAsyncUpdate Updated;
	UPROPERTY(BlueprintAssignable)
	FOnWidgetTransitionAsyncEvent Finished;

	UFUNCTION(BlueprintCallable, meta = (BlueprintInternalUseOnly = "true", WorldContext = "WorldContextObject", DisplayName = "Add Widget Transition Async"), Category = "Widget Transition")
	static UWidgetTransitionAsyncAction* AddWidgetTransition(const UObject* WorldContextObject, FWidgetTransition Transition);

	virtual void Activate() override;

private:
	UFUNCTION()
	void HandleStarted(UWidget* Widget);
	UFUNCTION()
	void HandleUpdated(UWidget* Widget, float NormalizedProgress, float EasedProgress);
	UFUNCTION()
	void HandleFinished(UWidget* Widget);

	FWidgetTransition PendingTransition;
	TWeakObjectPtr<const UObject> WorldContextObject;
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
