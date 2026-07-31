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
	static const FName ValueTypePinName(TEXT("ValueType"));
	static const FName FromValuePinName(TEXT("FromValue"));
	static const FName ToValuePinName(TEXT("ToValue"));
	static const FName TimePinName(TEXT("Time"));
	static const FName DelayPinName(TEXT("Delay"));
	static const FName RepeatCountPinName(TEXT("RepeatCount"));
	static const FName YoYoPinName(TEXT("bYoYo"));
	static const FName RemoveFromParentPinName(TEXT("bRemoveFromParent"));
	static const FName UseSpringPinName(TEXT("bUseSpring"));
	static const FName SpringFactorPinName(TEXT("SpringFactor"));
	static const FName DampingFactorPinName(TEXT("DampingFactor"));
	static const FName MaxVelocityPinName(TEXT("MaxVelocity"));
	static const FName CompleteTolerancePinName(TEXT("CompleteTolerance"));
	static const FName OnUpdatePinName(TEXT("OnUpdate"));
	static const FName WorldContextPinName(TEXT("WorldContextObject"));
	static const FName UseFromPinName(TEXT("bUseFrom"));

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

	static void CreateValuePin(UK2Node_WidgetTransition* Node, EEdGraphPinDirection Direction, FName Name, const UEdGraphSchema_K2* Schema)
	{
		if (!Node->IsValueTypeResolved()) { Node->CreatePin(Direction, Schema->PC_Wildcard, Name); return; }
		switch (Node->GetValueType())
		{
		case EWidgetTransitionValueType::Bool: Node->CreatePin(Direction, Schema->PC_Boolean, Name); break;
		case EWidgetTransitionValueType::Vector2D: Node->CreatePin(Direction, Schema->PC_Struct, TBaseStructure<FVector2D>::Get(), Name); break;
		case EWidgetTransitionValueType::LinearColor: Node->CreatePin(Direction, Schema->PC_Struct, TBaseStructure<FLinearColor>::Get(), Name); break;
		default: Node->CreatePin(Direction, Schema->PC_Real, Schema->PC_Float, Name); break;
		}
	}

	static void CreateFunctionParameterPin(UK2Node_WidgetTransition* Node, FName ParameterName, const UEdGraphSchema_K2* Schema)
	{
		if (!Node->IsValueTypeResolved()) return;
		UFunction* Function = UWidgetTransitionFunctionLibrary::StaticClass()->FindFunctionByName(GetFunctionName(Node->GetValueType()));
		const FProperty* Parameter = Function ? FindFProperty<FProperty>(Function, ParameterName) : nullptr;
		FEdGraphPinType PinType;
		if (Parameter && Schema->ConvertPropertyToPinType(Parameter, PinType)) Node->CreatePin(EGPD_Input, PinType, ParameterName);
	}

	static bool IsInfiniteRepeat(const UEdGraphPin* Pin)
	{
		return Pin && Pin->LinkedTo.IsEmpty() && Pin->DefaultValue == TEXT("-1");
	}

	static bool IsDiscardableTypeSpecificPin(const UEdGraphPin* Pin)
	{
		if (!Pin || !Pin->bOrphanedPin || !Pin->LinkedTo.IsEmpty()) return false;
		const FName Name = Pin->PinName;
		return Name == FromValuePinName || Name == TimePinName || Name == DelayPinName || Name == RepeatCountPinName
			|| Name == YoYoPinName || Name == RemoveFromParentPinName || Name == UseSpringPinName
			|| Name == SpringFactorPinName || Name == DampingFactorPinName || Name == MaxVelocityPinName
			|| Name == CompleteTolerancePinName || Name == OnUpdatePinName || Name == ValueTypePinName;
	}
}

void UK2Node_WidgetTransition::AllocateDefaultPins()
{
	const UEdGraphSchema_K2* Schema = GetDefault<UEdGraphSchema_K2>();
	CreatePin(EGPD_Input, Schema->PC_Exec, UEdGraphSchema_K2::PN_Execute);
	CreatePin(EGPD_Output, Schema->PC_Exec, UEdGraphSchema_K2::PN_Then);
	UEdGraphPin* WorldContextPin = CreatePin(EGPD_Input, Schema->PC_Object, UObject::StaticClass(), WidgetTransitionNode::WorldContextPinName);
	WorldContextPin->DefaultValue = TEXT("self"); WorldContextPin->bHidden = true;
	const bool bShowWidgetAndProperty = IsOptionalPinVisible(EWidgetTransitionOptionalPin::WidgetAndProperty);
	UEdGraphPin* WidgetPin = CreatePin(EGPD_Input, Schema->PC_Object, UWidget::StaticClass(), WidgetTransitionNode::WidgetPinName);
	WidgetPin->bHidden = !bShowWidgetAndProperty;
	UEdGraphPin* WidgetPropertyPin = CreatePin(EGPD_Input, Schema->PC_String, WidgetTransitionNode::WidgetPropertyPinName);
	WidgetPropertyPin->bHidden = !bShowWidgetAndProperty;
	if (!bValueTypeResolved || !bShowWidgetAndProperty || !bHasPropertyValueType)
	{
		UEdGraphPin* ValueTypePin = CreatePin(EGPD_Input, Schema->PC_Byte, StaticEnum<EWidgetTransitionValueType>(), WidgetTransitionNode::ValueTypePinName);
		ValueTypePin->bNotConnectable = true;
		ValueTypePin->DefaultValue = StaticEnum<EWidgetTransitionValueType>()->GetNameStringByValue(static_cast<int64>(ValueType));
	}
	if (IsOptionalPinVisible(EWidgetTransitionOptionalPin::From)) WidgetTransitionNode::CreateValuePin(this, EGPD_Input, WidgetTransitionNode::FromValuePinName, Schema);
	WidgetTransitionNode::CreateValuePin(this, EGPD_Input, WidgetTransitionNode::ToValuePinName, Schema);
	CreatePin(EGPD_Input, Schema->PC_Real, Schema->PC_Float, WidgetTransitionNode::TimePinName)->DefaultValue = TEXT("0.0");
	if (IsOptionalPinVisible(EWidgetTransitionOptionalPin::Delay)) CreatePin(EGPD_Input, Schema->PC_Real, Schema->PC_Float, WidgetTransitionNode::DelayPinName)->DefaultValue = TEXT("0.0");
	if (IsOptionalPinVisible(EWidgetTransitionOptionalPin::Repeat)) CreatePin(EGPD_Input, Schema->PC_Int, WidgetTransitionNode::RepeatCountPinName)->DefaultValue = TEXT("0");
	if (IsOptionalPinVisible(EWidgetTransitionOptionalPin::Spring) && bValueTypeResolved && (ValueType == EWidgetTransitionValueType::Float || ValueType == EWidgetTransitionValueType::Vector2D))
	{
		CreatePin(EGPD_Input, Schema->PC_Real, Schema->PC_Float, WidgetTransitionNode::SpringFactorPinName)->DefaultValue = TEXT("200.0");
		CreatePin(EGPD_Input, Schema->PC_Real, Schema->PC_Float, WidgetTransitionNode::DampingFactorPinName)->DefaultValue = TEXT("16.0");
		CreatePin(EGPD_Input, Schema->PC_Real, Schema->PC_Float, WidgetTransitionNode::MaxVelocityPinName)->DefaultValue = TEXT("1600.0");
		CreatePin(EGPD_Input, Schema->PC_Real, Schema->PC_Float, WidgetTransitionNode::CompleteTolerancePinName)->DefaultValue = TEXT("0.01");
	}
	if (IsOptionalPinVisible(EWidgetTransitionOptionalPin::OnUpdate)) WidgetTransitionNode::CreateFunctionParameterPin(this, WidgetTransitionNode::OnUpdatePinName, Schema);
}

FText UK2Node_WidgetTransition::GetNodeTitle(ENodeTitleType::Type) const { return NSLOCTEXT("ElasticUMG", "CreateWidgetTransition", "Create Widget Transition"); }
FText UK2Node_WidgetTransition::GetTooltipText() const { return NSLOCTEXT("ElasticUMG", "WidgetTransitionNodeTooltip", "Animates a selected property of the input widget. From is optional; when omitted, the current property value is used."); }
FText UK2Node_WidgetTransition::GetMenuCategory() const { return NSLOCTEXT("ElasticUMG", "WidgetTransitionNodeCategory", "Widget Transition"); }

void UK2Node_WidgetTransition::GetMenuActions(FBlueprintActionDatabaseRegistrar& ActionRegistrar) const
{
	UClass* ActionKey = GetClass();
	if (ActionRegistrar.IsOpenForRegistration(ActionKey)) ActionRegistrar.AddBlueprintAction(ActionKey, UBlueprintNodeSpawner::Create(ActionKey));
}

void UK2Node_WidgetTransition::ExpandNode(FKismetCompilerContext& CompilerContext, UEdGraph* SourceGraph)
{
	Super::ExpandNode(CompilerContext, SourceGraph);
	if (!bValueTypeResolved)
	{
		CompilerContext.MessageLog.Error(*NSLOCTEXT("ElasticUMG", "UnresolvedWidgetTransitionType", "@@ requires a Widget Property to resolve its wildcard value type.").ToString(), this);
		BreakAllNodeLinks();
		return;
	}
	UFunction* Function = UWidgetTransitionFunctionLibrary::StaticClass()->FindFunctionByName(WidgetTransitionNode::GetFunctionName(ValueType));
	if (!Function) { CompilerContext.MessageLog.Error(*NSLOCTEXT("ElasticUMG", "MissingWidgetTransitionFunction", "@@ could not find its transition function.").ToString(), this); BreakAllNodeLinks(); return; }
	UK2Node_CallFunction* CallNode = CompilerContext.SpawnIntermediateNode<UK2Node_CallFunction>(this, SourceGraph);
	CallNode->SetFromFunction(Function); CallNode->AllocateDefaultPins();
	const UEdGraphPin* FromPin = FindPin(WidgetTransitionNode::FromValuePinName);
	const bool bUseFrom = FromPin && (!FromPin->LinkedTo.IsEmpty() || !FromPin->DefaultValue.IsEmpty());
	const auto MoveLinks = [&CompilerContext, this, CallNode](FName PinName)
	{
		if (UEdGraphPin* SourcePin = FindPin(PinName)) if (UEdGraphPin* DestinationPin = CallNode->FindPin(PinName)) CompilerContext.MovePinLinksToIntermediate(*SourcePin, *DestinationPin);
	};
	MoveLinks(UEdGraphSchema_K2::PN_Execute); MoveLinks(UEdGraphSchema_K2::PN_Then); MoveLinks(WidgetTransitionNode::WorldContextPinName);
	MoveLinks(WidgetTransitionNode::WidgetPinName); MoveLinks(WidgetTransitionNode::WidgetPropertyPinName); MoveLinks(WidgetTransitionNode::FromValuePinName);
	MoveLinks(WidgetTransitionNode::ToValuePinName); MoveLinks(WidgetTransitionNode::TimePinName); MoveLinks(WidgetTransitionNode::DelayPinName);
	MoveLinks(WidgetTransitionNode::RepeatCountPinName);
	MoveLinks(WidgetTransitionNode::SpringFactorPinName); MoveLinks(WidgetTransitionNode::DampingFactorPinName); MoveLinks(WidgetTransitionNode::MaxVelocityPinName); MoveLinks(WidgetTransitionNode::CompleteTolerancePinName); MoveLinks(WidgetTransitionNode::OnUpdatePinName);
	CallNode->FindPinChecked(WidgetTransitionNode::UseFromPinName)->DefaultValue = bUseFrom ? TEXT("true") : TEXT("false");
	if (UEdGraphPin* YoYoPin = CallNode->FindPin(WidgetTransitionNode::YoYoPinName)) YoYoPin->DefaultValue = IsOptionalPinVisible(EWidgetTransitionOptionalPin::YoYo) ? TEXT("true") : TEXT("false");
	if (UEdGraphPin* RemovePin = CallNode->FindPin(WidgetTransitionNode::RemoveFromParentPinName)) RemovePin->DefaultValue = IsOptionalPinVisible(EWidgetTransitionOptionalPin::RemoveFromParent) && !bRepeatCountIsInfinite ? TEXT("true") : TEXT("false");
	if (UEdGraphPin* SpringPin = CallNode->FindPin(WidgetTransitionNode::UseSpringPinName)) SpringPin->DefaultValue = IsOptionalPinVisible(EWidgetTransitionOptionalPin::Spring) ? TEXT("true") : TEXT("false");
	BreakAllNodeLinks();
}

void UK2Node_WidgetTransition::PinDefaultValueChanged(UEdGraphPin* Pin)
{
	Super::PinDefaultValueChanged(Pin);
	if (Pin && Pin->PinName == WidgetTransitionNode::ValueTypePinName)
	{
		const UEnum* ValueTypeEnum = StaticEnum<EWidgetTransitionValueType>();
		const int64 EnumValue = ValueTypeEnum ? ValueTypeEnum->GetValueByNameString(Pin->DefaultValue) : INDEX_NONE;
		if (EnumValue != INDEX_NONE) SetValueType(static_cast<EWidgetTransitionValueType>(EnumValue));
		return;
	}
	if (Pin && Pin->PinName == WidgetTransitionNode::RepeatCountPinName)
	{
		const bool bNewInfinite = WidgetTransitionNode::IsInfiniteRepeat(Pin);
		if (bRepeatCountIsInfinite != bNewInfinite) { Modify(); bRepeatCountIsInfinite = bNewInfinite; ReconstructNode(); }
	}
}

void UK2Node_WidgetTransition::NotifyPinConnectionListChanged(UEdGraphPin* Pin)
{
	Super::NotifyPinConnectionListChanged(Pin);
	if (Pin && Pin->PinName == WidgetTransitionNode::RepeatCountPinName)
	{
		const bool bNewInfinite = WidgetTransitionNode::IsInfiniteRepeat(Pin);
		if (bRepeatCountIsInfinite != bNewInfinite) { Modify(); bRepeatCountIsInfinite = bNewInfinite; ReconstructNode(); }
	}
}

void UK2Node_WidgetTransition::PostReconstructNode()
{
	Super::PostReconstructNode();
	TArray<UEdGraphPin*> PinsToDiscard;
	for (UEdGraphPin* Pin : Pins)
	{
		if (WidgetTransitionNode::IsDiscardableTypeSpecificPin(Pin)) PinsToDiscard.Add(Pin);
	}
	if (PinsToDiscard.IsEmpty()) return;
	for (UEdGraphPin* Pin : PinsToDiscard) Pins.Remove(Pin);
	DestroyPinList(PinsToDiscard);
}

void UK2Node_WidgetTransition::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	if (PropertyChangedEvent.GetPropertyName() == GET_MEMBER_NAME_CHECKED(UK2Node_WidgetTransition, OptionalPins))
	{
		Modify();
		ReconstructNode();
	}
}

void UK2Node_WidgetTransition::SetValueType(EWidgetTransitionValueType InValueType)
{
	if (!bValueTypeResolved || ValueType != InValueType)
	{
		Modify();
		ValueType = InValueType;
		bValueTypeResolved = true;
		ReconstructNode();
	}
}

void UK2Node_WidgetTransition::SetOptionalPins(int32 InOptionalPins)
{
	if (OptionalPins != InOptionalPins)
	{
		Modify();
		const bool bWasShowingWidgetAndProperty = IsOptionalPinVisible(EWidgetTransitionOptionalPin::WidgetAndProperty);
		OptionalPins = InOptionalPins;
		const bool bIsShowingWidgetAndProperty = IsOptionalPinVisible(EWidgetTransitionOptionalPin::WidgetAndProperty);
		if (!bWasShowingWidgetAndProperty && bIsShowingWidgetAndProperty && bHasPropertyValueType)
		{
			ValueType = PropertyValueType;
			bValueTypeResolved = true;
		}
		ReconstructNode();
	}
}

void UK2Node_WidgetTransition::SetPropertyValueType(EWidgetTransitionValueType InValueType)
{
	const bool bTypeChanged = !bValueTypeResolved || ValueType != InValueType;
	Modify();
	PropertyValueType = InValueType;
	bHasPropertyValueType = true;
	if (bTypeChanged) SetValueType(InValueType);
	else ReconstructNode();
}
