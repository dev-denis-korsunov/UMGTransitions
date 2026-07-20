#pragma once

#include "CoreMinimal.h"
#include "K2Node.h"
#include "WidgetTransition.h"

#include "K2Node_WidgetTransition.generated.h"

class FBlueprintActionDatabaseRegistrar;
class FKismetCompilerContext;
class UEdGraph;

UCLASS(Abstract)
class ELASTICUMGEDITOR_API UK2Node_WidgetTransition : public UK2Node
{
	GENERATED_BODY()

public:
	virtual void AllocateDefaultPins() override;
	virtual FText GetNodeTitle(ENodeTitleType::Type TitleType) const override;
	virtual FText GetTooltipText() const override;
	virtual FText GetMenuCategory() const override;
	virtual void ExpandNode(FKismetCompilerContext& CompilerContext, UEdGraph* SourceGraph) override;
	virtual bool IsNodePure() const override { return false; }

	virtual EWidgetTransitionValueType GetValueType() const PURE_VIRTUAL(UK2Node_WidgetTransition::GetValueType, return EWidgetTransitionValueType::Float;);
};

UCLASS()
class ELASTICUMGEDITOR_API UK2Node_CreateFloatWidgetTransition final : public UK2Node_WidgetTransition
{
	GENERATED_BODY()
public:
	virtual EWidgetTransitionValueType GetValueType() const override { return EWidgetTransitionValueType::Float; }
	virtual void GetMenuActions(FBlueprintActionDatabaseRegistrar& ActionRegistrar) const override;
};

UCLASS()
class ELASTICUMGEDITOR_API UK2Node_CreateBoolWidgetTransition final : public UK2Node_WidgetTransition
{
	GENERATED_BODY()
public:
	virtual EWidgetTransitionValueType GetValueType() const override { return EWidgetTransitionValueType::Bool; }
	virtual void GetMenuActions(FBlueprintActionDatabaseRegistrar& ActionRegistrar) const override;
};

UCLASS()
class ELASTICUMGEDITOR_API UK2Node_CreateVectorWidgetTransition final : public UK2Node_WidgetTransition
{
	GENERATED_BODY()
public:
	virtual EWidgetTransitionValueType GetValueType() const override { return EWidgetTransitionValueType::Vector2D; }
	virtual void GetMenuActions(FBlueprintActionDatabaseRegistrar& ActionRegistrar) const override;
};

UCLASS()
class ELASTICUMGEDITOR_API UK2Node_CreateColorWidgetTransition final : public UK2Node_WidgetTransition
{
	GENERATED_BODY()
public:
	virtual EWidgetTransitionValueType GetValueType() const override { return EWidgetTransitionValueType::LinearColor; }
	virtual void GetMenuActions(FBlueprintActionDatabaseRegistrar& ActionRegistrar) const override;
};
