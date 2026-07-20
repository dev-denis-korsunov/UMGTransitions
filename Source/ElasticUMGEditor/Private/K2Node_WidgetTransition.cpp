#include "K2Node_WidgetTransition.h"

#include "BlueprintActionDatabaseRegistrar.h"
#include "BlueprintNodeSpawner.h"
#include "Components/Widget.h"
#include "EdGraphSchema_K2.h"
#include "K2Node_CallFunction.h"
#include "KismetCompiler.h"

namespace WidgetTransitionNode
{
	static const FName WidgetPinName(TEXT("Widget"));
	static const FName WidgetPropertyPinName(TEXT("WidgetProperty"));
	static const FName TargetValuePinName(TEXT("TargetValue"));
	static const FName TimePinName(TEXT("Time"));
	static const FName DelayPinName(TEXT("Delay"));
	static const FName WorldContextPinName(TEXT("WorldContextObject"));

	static FName GetFunctionName(EWidgetTransitionValueType ValueType)
	{
		switch (ValueType)
		{
		case EWidgetTransitionValueType::Bool: return GET_FUNCTION_NAME_CHECKED(UWidgetTransitionFunctionLibrary, CreateBoolWidgetTransition);
		case EWidgetTransitionValueType::Vector2D: return GET_FUNCTION_NAME_CHECKED(UWidgetTransitionFunctionLibrary, CreateVectorWidgetTransition);
		case EWidgetTransitionValueType::LinearColor: return GET_FUNCTION_NAME_CHECKED(UWidgetTransitionFunctionLibrary, CreateColorWidgetTransition);
		default: return GET_FUNCTION_NAME_CHECKED(UWidgetTransitionFunctionLibrary, CreateFloatWidgetTransition);
		}
	}

	static FText GetTitle(EWidgetTransitionValueType ValueType)
	{
		switch (ValueType)
		{
		case EWidgetTransitionValueType::Bool: return NSLOCTEXT("ElasticUMG", "CreateBoolWidgetTransition", "Create Bool Widget Transition");
		case EWidgetTransitionValueType::Vector2D: return NSLOCTEXT("ElasticUMG", "CreateVectorWidgetTransition", "Create Vector Widget Transition");
		case EWidgetTransitionValueType::LinearColor: return NSLOCTEXT("ElasticUMG", "CreateColorWidgetTransition", "Create Color Widget Transition");
		default: return NSLOCTEXT("ElasticUMG", "CreateFloatWidgetTransition", "Create Float Widget Transition");
		}
	}

	template <typename TNode>
	void RegisterNodeAction(const TNode* Node, FBlueprintActionDatabaseRegistrar& ActionRegistrar)
	{
		UClass* ActionKey = Node->GetClass();
		if (ActionRegistrar.IsOpenForRegistration(ActionKey))
		{
			ActionRegistrar.AddBlueprintAction(ActionKey, UBlueprintNodeSpawner::Create(ActionKey));
		}
	}
}

void UK2Node_WidgetTransition::AllocateDefaultPins()
{
	const UEdGraphSchema_K2* Schema = GetDefault<UEdGraphSchema_K2>();
	CreatePin(EGPD_Input, Schema->PC_Exec, UEdGraphSchema_K2::PN_Execute);
	CreatePin(EGPD_Output, Schema->PC_Exec, UEdGraphSchema_K2::PN_Then);
	UEdGraphPin* WorldContextPin = CreatePin(EGPD_Input, Schema->PC_Object, UObject::StaticClass(), WidgetTransitionNode::WorldContextPinName);
	WorldContextPin->DefaultValue = TEXT("self");
	WorldContextPin->bHidden = true;
	CreatePin(EGPD_Input, Schema->PC_Object, UWidget::StaticClass(), WidgetTransitionNode::WidgetPinName);
	CreatePin(EGPD_Input, Schema->PC_String, WidgetTransitionNode::WidgetPropertyPinName);

	switch (GetValueType())
	{
	case EWidgetTransitionValueType::Bool: CreatePin(EGPD_Input, Schema->PC_Boolean, WidgetTransitionNode::TargetValuePinName); break;
	case EWidgetTransitionValueType::Vector2D: CreatePin(EGPD_Input, Schema->PC_Struct, TBaseStructure<FVector2D>::Get(), WidgetTransitionNode::TargetValuePinName); break;
	case EWidgetTransitionValueType::LinearColor: CreatePin(EGPD_Input, Schema->PC_Struct, TBaseStructure<FLinearColor>::Get(), WidgetTransitionNode::TargetValuePinName); break;
	default: CreatePin(EGPD_Input, Schema->PC_Real, Schema->PC_Float, WidgetTransitionNode::TargetValuePinName); break;
	}
	UEdGraphPin* TimePin = CreatePin(EGPD_Input, Schema->PC_Real, Schema->PC_Float, WidgetTransitionNode::TimePinName);
	TimePin->DefaultValue = TEXT("0.0");
	UEdGraphPin* DelayPin = CreatePin(EGPD_Input, Schema->PC_Real, Schema->PC_Float, WidgetTransitionNode::DelayPinName);
	DelayPin->DefaultValue = TEXT("0.0");
}

FText UK2Node_WidgetTransition::GetNodeTitle(ENodeTitleType::Type) const { return WidgetTransitionNode::GetTitle(GetValueType()); }
FText UK2Node_WidgetTransition::GetTooltipText() const { return NSLOCTEXT("ElasticUMG", "WidgetTransitionNodeTooltip", "Animates the selected property of the input widget."); }
FText UK2Node_WidgetTransition::GetMenuCategory() const { return NSLOCTEXT("ElasticUMG", "WidgetTransitionNodeCategory", "Widget Transition"); }

void UK2Node_WidgetTransition::ExpandNode(FKismetCompilerContext& CompilerContext, UEdGraph* SourceGraph)
{
	Super::ExpandNode(CompilerContext, SourceGraph);
	UFunction* Function = UWidgetTransitionFunctionLibrary::StaticClass()->FindFunctionByName(WidgetTransitionNode::GetFunctionName(GetValueType()));
	if (!Function) { CompilerContext.MessageLog.Error(*NSLOCTEXT("ElasticUMG", "MissingWidgetTransitionFunction", "@@ could not find its transition function.").ToString(), this); BreakAllNodeLinks(); return; }
	UK2Node_CallFunction* CallNode = CompilerContext.SpawnIntermediateNode<UK2Node_CallFunction>(this, SourceGraph);
	CallNode->SetFromFunction(Function);
	CallNode->AllocateDefaultPins();
	const auto MoveLinks = [&CompilerContext, this, CallNode](FName PinName)
	{
		if (UEdGraphPin* SourcePin = FindPin(PinName)) if (UEdGraphPin* DestinationPin = CallNode->FindPin(PinName)) CompilerContext.MovePinLinksToIntermediate(*SourcePin, *DestinationPin);
	};
	MoveLinks(UEdGraphSchema_K2::PN_Execute); MoveLinks(UEdGraphSchema_K2::PN_Then); MoveLinks(WidgetTransitionNode::WorldContextPinName);
	MoveLinks(WidgetTransitionNode::WidgetPinName); MoveLinks(WidgetTransitionNode::WidgetPropertyPinName); MoveLinks(WidgetTransitionNode::TargetValuePinName);
	MoveLinks(WidgetTransitionNode::TimePinName); MoveLinks(WidgetTransitionNode::DelayPinName);
	BreakAllNodeLinks();
}

void UK2Node_CreateFloatWidgetTransition::GetMenuActions(FBlueprintActionDatabaseRegistrar& Registrar) const { WidgetTransitionNode::RegisterNodeAction(this, Registrar); }
void UK2Node_CreateBoolWidgetTransition::GetMenuActions(FBlueprintActionDatabaseRegistrar& Registrar) const { WidgetTransitionNode::RegisterNodeAction(this, Registrar); }
void UK2Node_CreateVectorWidgetTransition::GetMenuActions(FBlueprintActionDatabaseRegistrar& Registrar) const { WidgetTransitionNode::RegisterNodeAction(this, Registrar); }
void UK2Node_CreateColorWidgetTransition::GetMenuActions(FBlueprintActionDatabaseRegistrar& Registrar) const { WidgetTransitionNode::RegisterNodeAction(this, Registrar); }
