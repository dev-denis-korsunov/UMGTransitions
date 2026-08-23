#pragma once

#include "CoreMinimal.h"
#include "K2Node.h"
#include "WidgetTransition.h"

#include "K2Node_WidgetTransition.generated.h"

class FBlueprintActionDatabaseRegistrar;
class FKismetCompilerContext;
class UEdGraph;

/** Creates a transition, with optional explicit From and repeat settings. */
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
	virtual void ReallocatePinsDuringReconstruction(TArray<UEdGraphPin*>& OldPins) override;
	virtual bool IsNodePure() const override { return true; }

private:
	UPROPERTY()
	bool bUseFrom = false;
};

/** Adds an independently typed explicit From endpoint to a transition. */
UCLASS()
class ELASTICUMGEDITOR_API UK2Node_WidgetTransitionFrom final : public UK2Node
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
	virtual bool IsNodePure() const override { return true; }

private:
	UPROPERTY()
	EWidgetTransitionValueType ValueType = EWidgetTransitionValueType::Float;
};
