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
enum class EWidgetTransitionOptionalPin : uint8
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
	virtual bool IsNodePure() const override { return false; }

	bool IsValueTypeResolved() const { return bValueTypeResolved; }
	EWidgetTransitionValueType GetValueType() const { return ValueType; }
	void SetValueType(EWidgetTransitionValueType InValueType);
	void SetPropertyValueType(EWidgetTransitionValueType InValueType);
	void SetOptionalPins(int32 InOptionalPins);
	int32 GetOptionalPins() const { return OptionalPins; }
	bool IsOptionalPinVisible(EWidgetTransitionOptionalPin Pin) const { return (OptionalPins & static_cast<int32>(Pin)) != 0; }
	bool IsEasingVisible() const { return bShowEasing; }
	void SetEasingVisible(bool bInShowEasing);

private:
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

	/** Select the optional inputs displayed on this node. */
	UPROPERTY(EditAnywhere, Category = "Widget Transition", meta = (Bitmask, BitmaskEnum = "/Script/ElasticUMGEditor.EWidgetTransitionOptionalPin"))
	int32 OptionalPins = static_cast<int32>(EWidgetTransitionOptionalPin::WidgetAndProperty);
};
