#pragma once

#include "CoreMinimal.h"
#include "K2Node.h"
#include "WidgetTransition.h"

#include "K2Node_WidgetTransition.generated.h"

class FBlueprintActionDatabaseRegistrar;
class FKismetCompilerContext;
class UEdGraph;
struct FPropertyChangedEvent;

UENUM(meta = (Bitflags, UseEnumValuesAsMaskValuesInEditor = "true"))
enum class EWidgetTransitionOptionalPin : uint16
{
	From = 1 << 0,
	/** Uses the bit formerly reserved for Time, keeping saved node masks compatible. */
	WidgetAndProperty = 1 << 1,
	Delay = 1 << 2,
	Repeat = 1 << 3,
	YoYo = 1 << 4,
	RemoveFromParent = 1 << 5,
	Spring = 1 << 6,
	OnUpdate = 1 << 7,
	OnStarted = 1 << 8,
	OnFinished = 1 << 9,
};

UCLASS()
class ELASTICUMGEDITOR_API UK2Node_WidgetTransition final : public UK2Node
{
	GENERATED_BODY()

public:
	virtual void AllocateDefaultPins() override;
	virtual FText GetNodeTitle(ENodeTitleType::Type TitleType) const override;
	virtual FText GetTooltipText() const override;
	virtual FText GetMenuCategory() const override;
	virtual void GetMenuActions(FBlueprintActionDatabaseRegistrar& ActionRegistrar) const override;
	virtual void ExpandNode(FKismetCompilerContext& CompilerContext, UEdGraph* SourceGraph) override;
	virtual void PinDefaultValueChanged(UEdGraphPin* Pin) override;
	virtual void NotifyPinConnectionListChanged(UEdGraphPin* Pin) override;
	virtual void PostReconstructNode() override;
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	virtual bool CanSplitPin(const UEdGraphPin* Pin) const override;
	virtual bool IsNodePure() const override { return true; }

	bool IsValueTypeResolved() const { return bValueTypeResolved; }
	EWidgetTransitionValueType GetValueType() const { return ValueType; }
	void SetValueType(EWidgetTransitionValueType InValueType);
	void SetPropertyValueType(EWidgetTransitionValueType InValueType);
	void SetOptionalPins(int32 InOptionalPins);
	int32 GetOptionalPins() const { return OptionalPins; }
	bool IsOptionalPinVisible(EWidgetTransitionOptionalPin Pin) const { return (OptionalPins & static_cast<int32>(Pin)) != 0; }
	bool IsEasingVisible() const { return bShowEasing; }
	bool SupportsTransitionMode(FName Mode) const;
	void SetEasingVisible(bool bInShowEasing);
	void SetTransitionMode(bool bInUseSpring);
	bool IsCustomEasingSelected() const;
	FName GetSelectedEasingPreset() const { return SelectedEasingPreset; }
	void SetSelectedEasingPreset(FName InPreset);
	FWidgetTransitionEasingValue GetDisplayedEasingValue() const;

private:
	// Pin layout is deliberately expressed as ordered rules in the .cpp.
	void ShowExecutionPins(const class UEdGraphSchema_K2* Schema);
	void ShowWidgetSelectionPins(const class UEdGraphSchema_K2* Schema);
	void ShowValueTypePinWhenWidgetSelectionIsHidden(const class UEdGraphSchema_K2* Schema);
	void ShowTransitionValuePins(const class UEdGraphSchema_K2* Schema);
	void ShowTimingAndEasingPins(const class UEdGraphSchema_K2* Schema);
	void ShowRepeatPin(const class UEdGraphSchema_K2* Schema);
	bool ShowSpringPins(const class UEdGraphSchema_K2* Schema);
	bool ShowUpdatePin(const class UEdGraphSchema_K2* Schema);
	bool ShowAdvancedPins(const class UEdGraphSchema_K2* Schema);
	void UpdateAdvancedPinVisibility(bool bHasAdvancedPins);
	bool SupportsSpring() const;

	UPROPERTY()
	EWidgetTransitionValueType ValueType = EWidgetTransitionValueType::Float;

	UPROPERTY()
	bool bValueTypeResolved = false;

	UPROPERTY()
	EWidgetTransitionValueType ManualValueType = EWidgetTransitionValueType::Float;

	UPROPERTY()
	bool bValuePinsDisabled = true;

	UPROPERTY()
	bool bRepeatCountIsInfinite = false;

	UPROPERTY()
	bool bShowEasing = false;

	/** "Custom" enables the editable Easing struct; every other value resolves to a preset. */
	UPROPERTY()
	FName SelectedEasingPreset;

	/** Select the optional inputs displayed on this node. */
	UPROPERTY(EditAnywhere, Category = "Widget Transition", meta = (Bitmask, BitmaskEnum = "/Script/ElasticUMGEditor.EWidgetTransitionOptionalPin"))
	int32 OptionalPins = static_cast<int32>(EWidgetTransitionOptionalPin::WidgetAndProperty);
};
