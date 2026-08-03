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

/** Runtime transition with compact tagged From/To values. */
struct FWidgetTransition
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
	/** Internal typed entry point used only when the universal node is compiled. */
	UFUNCTION(BlueprintCallable, Category = "Widget Transition", meta = (WorldContext = "WorldContextObject", BlueprintInternalUseOnly = "true"))
	static void CreateFloatWidgetTransition(const UObject* WorldContextObject, UWidget* Widget, const FString& WidgetProperty, float ToValue, FOnFloatWidgetTransitionUpdate OnUpdate, FOnWidgetTransitionEvent OnStarted, FOnWidgetTransitionEvent OnFinished, float Time = 0.2f, float Delay = 0.0f, bool bApplyValueBeforeDelay = true, FWidgetTransitionEasingValue Easing = FWidgetTransitionEasingValue(), int32 RepeatCount = 0, bool bYoYo = false, bool bRemoveFromParent = false, bool bUseSpring = false, float SpringSpeed = 0.65f, float SpringBounce = 0.45f, bool bUseFrom = false, float FromValue = 0.0f);

	/** Internal typed entry point used only when the universal node is compiled. */
	UFUNCTION(BlueprintCallable, Category = "Widget Transition", meta = (WorldContext = "WorldContextObject", BlueprintInternalUseOnly = "true"))
	static void CreateBoolWidgetTransition(const UObject* WorldContextObject, UWidget* Widget, const FString& WidgetProperty, bool ToValue, FOnBoolWidgetTransitionUpdate OnUpdate, FOnWidgetTransitionEvent OnStarted, FOnWidgetTransitionEvent OnFinished, float Time = 0.2f, float Delay = 0.0f, bool bApplyValueBeforeDelay = true, FWidgetTransitionEasingValue Easing = FWidgetTransitionEasingValue(), int32 RepeatCount = 0, bool bYoYo = false, bool bRemoveFromParent = false, bool bUseFrom = false, bool FromValue = false);

	/** Internal typed entry point used only when the universal node is compiled. */
	UFUNCTION(BlueprintCallable, Category = "Widget Transition", meta = (WorldContext = "WorldContextObject", BlueprintInternalUseOnly = "true"))
	static void CreateVectorWidgetTransition(const UObject* WorldContextObject, UWidget* Widget, const FString& WidgetProperty, FVector2D ToValue, FOnVectorWidgetTransitionUpdate OnUpdate, FOnWidgetTransitionEvent OnStarted, FOnWidgetTransitionEvent OnFinished, float Time = 0.2f, float Delay = 0.0f, bool bApplyValueBeforeDelay = true, FWidgetTransitionEasingValue Easing = FWidgetTransitionEasingValue(), int32 RepeatCount = 0, bool bYoYo = false, bool bRemoveFromParent = false, bool bUseSpring = false, float SpringSpeed = 0.65f, float SpringBounce = 0.45f, bool bUseFrom = false, FVector2D FromValue = FVector2D::ZeroVector);

	/** Internal typed entry point used only when the universal node is compiled. */
	UFUNCTION(BlueprintCallable, Category = "Widget Transition", meta = (WorldContext = "WorldContextObject", BlueprintInternalUseOnly = "true"))
	static void CreateColorWidgetTransition(const UObject* WorldContextObject, UWidget* Widget, const FString& WidgetProperty, FLinearColor ToValue, FOnColorWidgetTransitionUpdate OnUpdate, FOnWidgetTransitionEvent OnStarted, FOnWidgetTransitionEvent OnFinished, float Time = 0.2f, float Delay = 0.0f, bool bApplyValueBeforeDelay = true, FWidgetTransitionEasingValue Easing = FWidgetTransitionEasingValue(), int32 RepeatCount = 0, bool bYoYo = false, bool bRemoveFromParent = false, bool bUseFrom = false, FLinearColor FromValue = FLinearColor::White);

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

	TSparseArray<FWidgetTransition> Transitions;
	/** Lifecycle callbacks for the small subset of transitions that bind Start or Finished. */
	TMap<uint64, FWidgetTransitionEvents> EventCallbacks;
	/** Monotonic key source; a sparse-array index cannot be used because indices are reused. */
	uint64 NextTransitionId = 1;
};
