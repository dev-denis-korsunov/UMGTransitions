#include "WidgetTransition.h"
#include "Tests/WidgetTransitionTestTypes.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Components/Image.h"
#include "Misc/AutomationTest.h"

namespace
{
	constexpr float TestTolerance = 0.001f;

	FString FormatBytes(const TCHAR* Name, SIZE_T Size, SIZE_T Alignment)
	{
		return FString::Printf(TEXT("%s: %llu B, alignment %llu B"), Name, static_cast<uint64>(Size), static_cast<uint64>(Alignment));
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWidgetTransitionStorageLayoutTest, "ElasticUMG.WidgetTransition.Runtime.StorageLayout", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FWidgetTransitionStorageLayoutTest::RunTest(const FString& Parameters)
{
	AddInfo(FormatBytes(TEXT("FWidgetTransition"), sizeof(FWidgetTransition), alignof(FWidgetTransition)));
	AddInfo(FormatBytes(TEXT("FWidgetTransitionPropertyBinding"), sizeof(FWidgetTransitionPropertyBinding), alignof(FWidgetTransitionPropertyBinding)));
	AddInfo(FormatBytes(TEXT("FTransitionValue"), sizeof(FTransitionValue), alignof(FTransitionValue)));
	AddInfo(FormatBytes(TEXT("FWidgetTransitionEasingValue"), sizeof(FWidgetTransitionEasingValue), alignof(FWidgetTransitionEasingValue)));
	AddInfo(FormatBytes(TEXT("FWidgetTransitionUpdateCallback"), sizeof(FWidgetTransitionUpdateCallback), alignof(FWidgetTransitionUpdateCallback)));
	AddInfo(FormatBytes(TEXT("FWidgetTransitionSpringState"), sizeof(FWidgetTransitionSpringState), alignof(FWidgetTransitionSpringState)));
	AddInfo(FormatBytes(TEXT("FSpringFloat"), sizeof(FSpringFloat), alignof(FSpringFloat)));
	AddInfo(FormatBytes(TEXT("FSpringVector2D"), sizeof(FSpringVector2D), alignof(FSpringVector2D)));
	AddInfo(FormatBytes(TEXT("FOnFloatWidgetTransitionUpdate"), sizeof(FOnFloatWidgetTransitionUpdate), alignof(FOnFloatWidgetTransitionUpdate)));
	AddInfo(FormatBytes(TEXT("FOnWidgetTransitionEvent"), sizeof(FOnWidgetTransitionEvent), alignof(FOnWidgetTransitionEvent)));

	TestTrue(TEXT("Transition storage is non-empty"), sizeof(FWidgetTransition) > 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWidgetTransitionEasingTest, "ElasticUMG.WidgetTransition.Runtime.Easing.Endpoints", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FWidgetTransitionEasingTest::RunTest(const FString& Parameters)
{
	const FWidgetTransitionEasingValue Easing;
	TestEqual(TEXT("Cubic Bezier starts at zero"), FWidgetTransitionEasing::EvaluateCubicBezier(Easing.ControlPoint1, Easing.ControlPoint2, 0.0f), 0.0f);
	TestEqual(TEXT("Cubic Bezier ends at one"), FWidgetTransitionEasing::EvaluateCubicBezier(Easing.ControlPoint1, Easing.ControlPoint2, 1.0f), 1.0f);

	const float Midpoint = FWidgetTransitionEasing::EvaluateCubicBezier(Easing.ControlPoint1, Easing.ControlPoint2, 0.5f);
	TestTrue(TEXT("Cubic Bezier midpoint stays normalized"), Midpoint > 0.0f && Midpoint < 1.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWidgetTransitionPropertyBindingTest, "ElasticUMG.WidgetTransition.Runtime.PropertyBinding.RenderOpacity", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FWidgetTransitionPropertyBindingTest::RunTest(const FString& Parameters)
{
	UImage* Widget = NewObject<UImage>(GetTransientPackage());
	FWidgetTransitionPropertyBinding Binding;
	TestTrue(TEXT("RenderOpacity property resolves"), Binding.Resolve(Widget, TEXT("RenderOpacity")));
	TestEqual(TEXT("RenderOpacity resolves as Float"), Binding.ValueType, EWidgetTransitionValueType::Float);

	FTransitionValue Value;
	TestTrue(TEXT("RenderOpacity value can be read"), Binding.Read(Widget, Value));
	TestTrue(TEXT("Read value is Float"), Value.IsType<float>());
	FTransitionValue NewValue;
	NewValue.Emplace<float>(0.35f);
	TestTrue(TEXT("RenderOpacity value can be written"), Binding.Apply(Widget, NewValue));
	TestTrue(TEXT("RenderOpacity was updated"), FMath::IsNearlyEqual(Widget->GetRenderOpacity(), 0.35f, TestTolerance));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWidgetTransitionSpringTest, "ElasticUMG.WidgetTransition.Runtime.Spring.Converges", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FWidgetTransitionSpringTest::RunTest(const FString& Parameters)
{
	FSpringFloat FloatSpring(144.0f, 18.0f);
	FloatSpring.Start(0.0f, 1.0f);
	for (int32 Step = 0; Step < 1200 && !FloatSpring.IsCompleted(); ++Step)
	{
		FloatSpring.Tick(1.0f / 120.0f);
	}
	TestTrue(TEXT("Float spring completes"), FloatSpring.IsCompleted());
	TestTrue(TEXT("Float spring settles at target"), FMath::IsNearlyEqual(FloatSpring.GetValue(), 1.0f, TestTolerance));

	FSpringVector2D VectorSpring(144.0f, 18.0f);
	VectorSpring.Start(FVector2D(0.0, 0.0), FVector2D(100.0, -50.0));
	for (int32 Step = 0; Step < 1200 && !VectorSpring.IsCompleted(); ++Step)
	{
		VectorSpring.Tick(1.0f / 120.0f);
	}
	TestTrue(TEXT("Vector spring completes"), VectorSpring.IsCompleted());
	TestTrue(TEXT("Vector spring settles at target"), VectorSpring.GetValue().Equals(FVector2D(100.0, -50.0), TestTolerance));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWidgetTransitionEventRegistryTest, "ElasticUMG.WidgetTransition.Runtime.EventRegistry", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FWidgetTransitionEventRegistryTest::RunTest(const FString& Parameters)
{
	UWidgetTransitionSubsystem* Subsystem = NewObject<UWidgetTransitionSubsystem>(GetTransientPackage());
	UImage* Widget = NewObject<UImage>(GetTransientPackage());
	UWidgetTransitionTestEventReceiver* Receiver = NewObject<UWidgetTransitionTestEventReceiver>(GetTransientPackage());
	FWidgetTransitionEvents Events;
	Events.OnStarted.BindDynamic(Receiver, &UWidgetTransitionTestEventReceiver::HandleStarted);

	constexpr uint64 TransitionId = 42;
	Subsystem->EventCallbacks.Add(TransitionId, MoveTemp(Events));
	TestTrue(TEXT("Event registry finds transition callbacks"), Subsystem->EventCallbacks.Contains(TransitionId));
	if (FWidgetTransitionEvents* StoredEvents = Subsystem->EventCallbacks.Find(TransitionId))
	{
		StoredEvents->OnStarted.ExecuteIfBound(Widget);
	}
	TestEqual(TEXT("Started callback executes once"), Receiver->StartedCount, 1);
	TestTrue(TEXT("Started callback receives transition widget"), Receiver->LastWidget == Widget);

	Subsystem->EventCallbacks.Remove(TransitionId);
	TestFalse(TEXT("Event registry removes completed transition callbacks"), Subsystem->EventCallbacks.Contains(TransitionId));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWidgetTransitionMetadataTest, "ElasticUMG.WidgetTransition.Editor.Metadata", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FWidgetTransitionMetadataTest::RunTest(const FString& Parameters)
{
	struct FExpectedFunction { FName Name; const TCHAR* ValueType; };
	const FExpectedFunction Functions[] =
	{
		{ GET_FUNCTION_NAME_CHECKED(UWidgetTransitionFunctionLibrary, CreateFloatWidgetTransition), TEXT("Float") },
		{ GET_FUNCTION_NAME_CHECKED(UWidgetTransitionFunctionLibrary, CreateBoolWidgetTransition), TEXT("Bool") },
		{ GET_FUNCTION_NAME_CHECKED(UWidgetTransitionFunctionLibrary, CreateVectorWidgetTransition), TEXT("Vector2D") },
		{ GET_FUNCTION_NAME_CHECKED(UWidgetTransitionFunctionLibrary, CreateColorWidgetTransition), TEXT("LinearColor") },
	};

	for (const FExpectedFunction& Expected : Functions)
	{
		const UFunction* Function = UWidgetTransitionFunctionLibrary::StaticClass()->FindFunctionByName(Expected.Name);
		TestNotNull(*FString::Printf(TEXT("%s exists"), *Expected.Name.ToString()), Function);
		if (!Function) continue;
		TestTrue(*FString::Printf(TEXT("%s is a transition function"), *Expected.Name.ToString()), Function->HasMetaData(TEXT("ElasticUMGTransition")));
		TestEqual(*FString::Printf(TEXT("%s has its declared value type"), *Expected.Name.ToString()), Function->GetMetaData(TEXT("ElasticUMGValueType")), FString(Expected.ValueType));
	}

	const UFunction* FloatFunction = UWidgetTransitionFunctionLibrary::StaticClass()->FindFunctionByName(GET_FUNCTION_NAME_CHECKED(UWidgetTransitionFunctionLibrary, CreateFloatWidgetTransition));
	const FProperty* Delay = FloatFunction ? FindFProperty<FProperty>(FloatFunction, TEXT("Delay")) : nullptr;
	const FProperty* OnStarted = FloatFunction ? FindFProperty<FProperty>(FloatFunction, TEXT("OnStarted")) : nullptr;
	TestNotNull(TEXT("Delay parameter exists"), Delay);
	TestNotNull(TEXT("OnStarted parameter exists"), OnStarted);
	if (Delay) TestEqual(TEXT("Delay belongs to Basic tab"), Delay->GetMetaData(TEXT("ElasticUMGTab")), FString(TEXT("Basic")));
	if (OnStarted) TestEqual(TEXT("On Started has an editor role"), OnStarted->GetMetaData(TEXT("ElasticUMGRole")), FString(TEXT("OnStarted")));
	if (FloatFunction) TestEqual(TEXT("Float supports both transition modes"), FloatFunction->GetMetaData(TEXT("ElasticUMGModes")), FString(TEXT("Interpolation,Spring")));
	return true;
}

#endif
