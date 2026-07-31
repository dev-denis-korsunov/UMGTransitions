#pragma once

#include "CoreMinimal.h"
#include "K2Node.h"
#include "WidgetTransition.h"

#include "K2Node_WidgetTransition.generated.h"

class FBlueprintActionDatabaseRegistrar;
class FKismetCompilerContext;
class UEdGraph;

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
	virtual bool IsNodePure() const override { return false; }

	bool IsValueTypeResolved() const { return bValueTypeResolved; }
	EWidgetTransitionValueType GetValueType() const { return ValueType; }
	void SetValueType(EWidgetTransitionValueType InValueType);

private:
	UPROPERTY()
	EWidgetTransitionValueType ValueType = EWidgetTransitionValueType::Float;

	UPROPERTY()
	bool bValueTypeResolved = false;

	UPROPERTY()
	bool bRepeatCountIsInfinite = false;
};
