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
		case EWidgetTransitionValueType::Vector2D: return GET_FUNCTION_NAME_CHECKED(UWidgetTransitionFunctionLibrary, StartVector2DWidgetTransition);
		case EWidgetTransitionValueType::LinearColor: return GET_FUNCTION_NAME_CHECKED(UWidgetTransitionFunctionLibrary, StartLinearColorWidgetTransition);
		case EWidgetTransitionValueType::Bool: return GET_FUNCTION_NAME_CHECKED(UWidgetTransitionFunctionLibrary, StartBoolWidgetTransition);
		case EWidgetTransitionValueType::Float:
		default: return GET_FUNCTION_NAME_CHECKED(UWidgetTransitionFunctionLibrary, StartFloatWidgetTransition);
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

	switch (ValueType)
	{
	case EWidgetTransitionValueType::Vector2D:
		CreatePin(EGPD_Input, Schema->PC_Struct, TBaseStructure<FVector2D>::Get(), WidgetTransitionNode::TargetValuePinName);
		break;
	case EWidgetTransitionValueType::LinearColor:
		CreatePin(EGPD_Input, Schema->PC_Struct, TBaseStructure<FLinearColor>::Get(), WidgetTransitionNode::TargetValuePinName);
		break;
	case EWidgetTransitionValueType::Bool:
		CreatePin(EGPD_Input, Schema->PC_Boolean, WidgetTransitionNode::TargetValuePinName);
		break;
	case EWidgetTransitionValueType::Float:
	default:
		CreatePin(EGPD_Input, Schema->PC_Real, Schema->PC_Float, WidgetTransitionNode::TargetValuePinName);
		break;
	}

	UEdGraphPin* TimePin = CreatePin(EGPD_Input, Schema->PC_Real, Schema->PC_Float, WidgetTransitionNode::TimePinName);
	TimePin->DefaultValue = TEXT("0.0");
	UEdGraphPin* DelayPin = CreatePin(EGPD_Input, Schema->PC_Real, Schema->PC_Float, WidgetTransitionNode::DelayPinName);
	DelayPin->DefaultValue = TEXT("0.0");
}

FText UK2Node_WidgetTransition::GetNodeTitle(ENodeTitleType::Type TitleType) const
{
	return NSLOCTEXT("ElasticUMG", "WidgetTransitionNodeTitle", "Widget Transition");
}

FText UK2Node_WidgetTransition::GetTooltipText() const
{
	return NSLOCTEXT("ElasticUMG", "WidgetTransitionNodeTooltip", "Animates a selected widget property to the target value.");
}

FText UK2Node_WidgetTransition::GetMenuCategory() const
{
	return NSLOCTEXT("ElasticUMG", "WidgetTransitionNodeCategory", "Widget Transition");
}

void UK2Node_WidgetTransition::GetMenuActions(FBlueprintActionDatabaseRegistrar& ActionRegistrar) const
{
	UClass* ActionKey = GetClass();
	if (ActionRegistrar.IsOpenForRegistration(ActionKey))
	{
		UBlueprintNodeSpawner* NodeSpawner = UBlueprintNodeSpawner::Create(ActionKey);
		ActionRegistrar.AddBlueprintAction(ActionKey, NodeSpawner);
	}
}

void UK2Node_WidgetTransition::ExpandNode(FKismetCompilerContext& CompilerContext, UEdGraph* SourceGraph)
{
	Super::ExpandNode(CompilerContext, SourceGraph);

	UFunction* Function = UWidgetTransitionFunctionLibrary::StaticClass()->FindFunctionByName(WidgetTransitionNode::GetFunctionName(ValueType));
	if (!Function)
	{
		CompilerContext.MessageLog.Error(*NSLOCTEXT("ElasticUMG", "MissingWidgetTransitionFunction", "@@ could not find its transition function.").ToString(), this);
		BreakAllNodeLinks();
		return;
	}

	UK2Node_CallFunction* CallNode = CompilerContext.SpawnIntermediateNode<UK2Node_CallFunction>(this, SourceGraph);
	CallNode->SetFromFunction(Function);
	CallNode->AllocateDefaultPins();

	const auto MoveLinks = [&CompilerContext, this, CallNode](FName PinName)
	{
		if (UEdGraphPin* SourcePin = FindPin(PinName))
		{
			if (UEdGraphPin* DestinationPin = CallNode->FindPin(PinName))
			{
				CompilerContext.MovePinLinksToIntermediate(*SourcePin, *DestinationPin);
			}
		}
	};

	MoveLinks(UEdGraphSchema_K2::PN_Execute);
	MoveLinks(UEdGraphSchema_K2::PN_Then);
	MoveLinks(WidgetTransitionNode::WorldContextPinName);
	MoveLinks(WidgetTransitionNode::WidgetPinName);
	MoveLinks(WidgetTransitionNode::WidgetPropertyPinName);
	MoveLinks(WidgetTransitionNode::TargetValuePinName);
	MoveLinks(WidgetTransitionNode::TimePinName);
	MoveLinks(WidgetTransitionNode::DelayPinName);
	BreakAllNodeLinks();
}

void UK2Node_WidgetTransition::SetValueType(EWidgetTransitionValueType InValueType)
{
	if (ValueType != InValueType)
	{
		Modify();
		ValueType = InValueType;
		ReconstructNode();
	}
}
