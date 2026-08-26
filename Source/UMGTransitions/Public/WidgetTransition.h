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
struct UMGTRANSITIONS_API FWidgetTransitionValue
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

/** Cached access to a transition property on a widget. */
USTRUCT()
struct UMGTRANSITIONS_API FWidgetTransitionPropertyBinding
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
	float SpringSpeed = 0.65f;
	float SpringBounce = 0.45f;
	int32 RepeatCount = 0;
	/** Seconds between Updated callbacks; zero preserves per-tick updates. */
	float UpdateInterval = 0.0f;
	/** Elapsed seconds since the previous Updated callback. */
	float UpdateElapsed = 0.0f;
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
	/** Whether this transition has a Started callback stored in the subsystem. */
	uint16 bHasStartedCallback : 1 = false;
	/** Whether this transition has an Updated callback stored in the subsystem. */
	uint16 bHasUpdatedCallback : 1 = false;
	/** Whether this transition has a Finished callback stored in the subsystem. */
	uint16 bHasFinishedCallback : 1 = false;
	/** Whether this transition writes its sampled value to a widget property. */
	uint16 bBound : 1 = false;
	/** Stable key for rare callbacks in UWidgetTransitionSubsystem::Callbacks. */
	uint64 TransitionId = 0;
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
	UFUNCTION(BlueprintPure, Category = "Widget Transition", meta = (DisplayName = "Create Widget Transition", UMGTransitionsBinding = "Combined", ReturnDisplayName = "Transition"))
	static FWidgetTransition CreateWidgetTransition(UWidget* Widget, UPARAM(meta = (UMGTransitionsRole = "WidgetProperty")) FName WidgetProperty, FWidgetTransitionValue ToValue, float Time = 0.2f, float Delay = 0.0f);
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
class UMGTRANSITIONS_API UWidgetTransitionAsyncAction final : public UBlueprintAsyncActionBase
{
	GENERATED_BODY()

public:
	UPROPERTY(BlueprintAssignable)
	FOnWidgetTransitionAsyncEvent Started;
	UPROPERTY(BlueprintAssignable)
	FOnWidgetTransitionAsyncUpdate Updated;
	UPROPERTY(BlueprintAssignable)
	FOnWidgetTransitionAsyncEvent Finished;

	UFUNCTION(BlueprintCallable, meta = (BlueprintInternalUseOnly = "true", WorldContext = "WorldContextObject", DisplayName = "Add Widget Transition Async", AdvancedDisplay = "UpdateInterval"), Category = "Widget Transition")
	static UWidgetTransitionAsyncAction* AddWidgetTransition(const UObject* WorldContextObject, FWidgetTransition Transition, UPARAM(meta = (ClampMin = "0.0", UIMin = "0.0", UIMax = "1.0")) float UpdateInterval = 0.033f);

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
	bool bBroadcastUpdateValue = false;

#if WITH_DEV_AUTOMATION_TESTS
public:
	/** Initializes the update output path without a UWorld for automation benchmarks. */
	bool InitializeUpdateForTesting(FWidgetTransition Transition);
	/** Dispatches one update through the same async output handler used at runtime. */
	void DispatchUpdatedForTesting(UWidget* Widget, float NormalizedProgress, float EasedProgress);
#endif
};

UCLASS()
class UMGTRANSITIONS_API UWidgetTransitionSubsystem final : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual ETickableTickType GetTickableTickType() const override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override { RETURN_QUICK_DECLARE_CYCLE_STAT(UWidgetTransitionSubsystem, STATGROUP_Tickables); }
	virtual bool IsTickableInEditor() const override { return true; }

	/** Dense transition records; removal uses RemoveAtSwap and repairs spring-owner indices. */
	TArray<FWidgetTransition> Transitions;
	/** Dense spring simulation pass, stored separately from transition records. */
	TArray<FWidgetTransitionSpring> Springs;
	/** Owning transition array index for every entry in Springs. */
	TArray<int32> SpringTransitionIndices;
	/** Callbacks for the small subset of transitions that bind lifecycle or update events. */
	TMap<uint64, FWidgetTransitionCallbacks> Callbacks;
	/** Monotonic key source; array indices are unstable after RemoveAtSwap. */
	uint64 NextTransitionId = 1;

#if WITH_DEV_AUTOMATION_TESTS
	/** Invokes the transition hot path without requiring an initialized UWorld. */
	void TickTransitionsForTesting(float DeltaTime);
	/** Clears transitions through the same removal path used by Clear All Widget Transitions. */
	void ClearTransitionsForTesting(UWidget* Widget);
#endif
};
