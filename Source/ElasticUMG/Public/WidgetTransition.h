#pragma once

#include "CoreMinimal.h"
#include "Binding/DynamicPropertyPath.h"
#include "Engine/CurveTable.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Kismet/BlueprintAsyncActionBase.h"
#include "Spring.h"
#include "Subsystems/WorldSubsystem.h"

#include "WidgetTransition.generated.h"

class UWidget;
class UMaterialInstanceDynamic;
struct FRealCurve;

DECLARE_DYNAMIC_DELEGATE_ThreeParams(FOnWidgetTransitionUpdate, UWidget*, Widget, float, NormalizedProgress, float, EasedProgress);
DECLARE_DYNAMIC_DELEGATE_OneParam(FOnWidgetTransitionEvent, UWidget*, Widget);

/** Rare transition callbacks stored by the subsystem rather than each active transition. */
struct FWidgetTransitionCallbacks
{
	FOnWidgetTransitionEvent OnStarted;
	FOnWidgetTransitionEvent OnFinished;
	FOnWidgetTransitionUpdate OnUpdated;

	bool HasBoundCallbacks() const
	{
		return OnStarted.IsBound() || OnFinished.IsBound() || OnUpdated.IsBound();
	}
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
	MaterialScalar,
	MaterialVector,
	RenderOpacity,
	RenderTransformTranslation,
	RenderTransformScale,
	RenderTransformShear,
	RenderTransformAngle,
	RenderTransformPivot,
};

/**
 * Type-tagged Blueprint endpoint. Runtime code expands this value to the channel
 * count of the selected binding before the transition starts.
 */
USTRUCT(BlueprintType)
struct ELASTICUMG_API FWidgetTransitionValue
{
	GENERATED_BODY()

	/** Normalized transition channels. Split this pin to edit its FVector4f directly. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Widget Transition")
	FVector4f Channels = FVector4f::Zero();

	/** Semantic type selected by Make Transition Value. Kept internal so split pins stay compact. */
	EWidgetTransitionValueType Type = EWidgetTransitionValueType::Float;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnWidgetTransitionAsyncEvent, FWidgetTransitionValue, Value);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOnWidgetTransitionAsyncUpdate, FWidgetTransitionValue, Value, float, NormalizedProgress, float, EasedProgress);

/** Heap-allocated spring state; only present for a spring transition. */
struct FWidgetTransitionSpringState
{
	FWidgetTransitionSpringState(float SpringFactor, float DampingFactor)
		: Spring(SpringFactor, DampingFactor)
	{
	}

	FSpringVector4f Spring;
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
 * Blueprint-facing transition definition and runtime instance. Pure functions
 * construct a copy; Add Widget Transition normalizes that copy for ticking.
 */
USTRUCT(BlueprintType)
struct ELASTICUMG_API FWidgetTransition
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
	float SpringSpeed = 0.65f;
	float SpringBounce = 0.45f;
	int32 RepeatCount = 0;
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
	/** Whether this transition has rare callbacks stored in the subsystem. */
	uint16 bHasCallbacks : 1 = false;
	/** Whether this transition writes its sampled value to a widget property. */
	uint16 bBound : 1 = false;
	/** Stable key for rare callbacks in UWidgetTransitionSubsystem::Callbacks. */
	uint64 TransitionId = 0;
	TSharedPtr<FWidgetTransitionSpringState> SpringState;
	/** Resolved once when the transition is added, avoiding a CurveTable lookup every tick. */
	const FRealCurve* EasingCurve = nullptr;
};

UCLASS()
class ELASTICUMG_API UWidgetTransitionFunctionLibrary final : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** Creates a transition bound to a widget property or a Material.Parameter entry. */
	UFUNCTION(BlueprintPure, Category = "Widget Transition", meta = (DisplayName = "Create Widget Transition", ElasticUMGTransitionBinding = "Combined", ReturnDisplayName = "Transition"))
	static FWidgetTransition CreateWidgetTransition(UWidget* Widget, UPARAM(meta = (ElasticUMGRole = "WidgetProperty")) FName WidgetProperty, FWidgetTransitionValue ToValue, float Time = 0.2f, float Delay = 0.0f);
	UFUNCTION(BlueprintPure, Category = "Widget Transition", meta = (CPP_Default_bUseFrom = "true", ReturnDisplayName = "Transition"))
	static FWidgetTransition From(FWidgetTransition Transition, bool bUseFrom, FWidgetTransitionValue FromValue);
	/** Configures optional transition behavior. */
	UFUNCTION(BlueprintPure, Category = "Widget Transition", meta = (ReturnDisplayName = "Transition"))
	static FWidgetTransition Options(FWidgetTransition Transition, UPARAM(meta = (ToolTip = "Defers applying From Value until the transition starts after Delay.")) bool bDeferFromValue = false, bool bIgnoreDelayOnRepeat = false);

	UFUNCTION(BlueprintPure, Category = "Widget Transition", meta = (DataTablePin = "CurveTable", ReturnDisplayName = "Transition"))
	static FWidgetTransition Easing(FWidgetTransition Transition, UCurveTable* CurveTable, FName RowName);
	UFUNCTION(BlueprintPure, Category = "Widget Transition", meta = (ReturnDisplayName = "Transition"))
	static FWidgetTransition Repeat(FWidgetTransition Transition, int32 RepeatCount);
	UFUNCTION(BlueprintPure, Category = "Widget Transition", meta = (DisplayName = "Yo Yo", ReturnDisplayName = "Transition"))
	static FWidgetTransition YoYo(FWidgetTransition Transition, bool bYoYo = true);
	UFUNCTION(BlueprintPure, Category = "Widget Transition", meta = (ReturnDisplayName = "Transition"))
	static FWidgetTransition Spring(FWidgetTransition Transition, float SpringSpeed = 0.65f, float SpringBounce = 0.45f, bool bFitSimulationToTime = false);
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
	void RefreshEventValue(UWidget* Widget);

	FWidgetTransition PendingTransition;
	FWidgetTransitionValue EventValue;
	FWidgetTransitionValue EventStartValue;
	FWidgetTransitionValue EventTargetValue;
	FWidgetTransitionPropertyBinding EventBinding;
	FWidgetTransitionCallbacks Callbacks;
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

	TSparseArray<FWidgetTransition> Transitions;
	/** Callbacks for the small subset of transitions that bind lifecycle or update events. */
	TMap<uint64, FWidgetTransitionCallbacks> Callbacks;
	/** Monotonic key source; a sparse-array index cannot be used because indices are reused. */
	uint64 NextTransitionId = 1;

#if WITH_DEV_AUTOMATION_TESTS
	/** Invokes the transition hot path without requiring an initialized UWorld. */
	void TickTransitionsForTesting(float DeltaTime);
#endif
};
