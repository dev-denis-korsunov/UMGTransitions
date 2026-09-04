#include "WidgetTransition.h"
#include "WidgetTransitionAsyncAction.h"
#include "WidgetTransitionSubsystem.h"
#include "WidgetSelectorLibrary.h"
#include "WidgetTransitionTestTypes.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Curves/RichCurve.h"
#include "HAL/PlatformTime.h"
#include "Misc/AutomationTest.h"

namespace
{
	constexpr float Tolerance = 0.001f;

	FString FormatBytes(const TCHAR* Name, SIZE_T Size, SIZE_T Alignment)
	{
		return FString::Printf(TEXT("%s: %llu B, alignment %llu B"), Name, static_cast<uint64>(Size), static_cast<uint64>(Alignment));
	}

	FString FormatStorageBudget(const TCHAR* Name, int32 TransitionCount, bool bWithSprings)
	{
		const SIZE_T TransitionBytes = sizeof(FWidgetTransition) * TransitionCount;
		const SIZE_T CallbackLinkBytes = sizeof(FWidgetTransitionCallbackLinks) * TransitionCount;
		const SIZE_T SpringBytes = bWithSprings ? sizeof(FWidgetTransitionSpring) * TransitionCount : 0;
		const SIZE_T SpringIndexBytes = bWithSprings ? sizeof(int32) * TransitionCount : 0;
		return FString::Printf(TEXT("%s (%d transitions): %llu B transition + %llu B callback links + %llu B spring + %llu B spring indices = %llu B"), Name, TransitionCount, static_cast<uint64>(TransitionBytes), static_cast<uint64>(CallbackLinkBytes), static_cast<uint64>(SpringBytes), static_cast<uint64>(SpringIndexBytes), static_cast<uint64>(TransitionBytes + CallbackLinkBytes + SpringBytes + SpringIndexBytes));
	}

	FString FormatMicroseconds(double Seconds)
	{
		return FString::Printf(TEXT("%.3f us"), Seconds * 1000000.0);
	}

	FWidgetTransition MakeRuntimeOpacityTransition(UImage* Widget)
	{
		FWidgetTransition Transition;
		Transition.Widget = Widget;
		Transition.WidgetProperty = TEXT("RenderOpacity");
		Transition.FromValue = UWidgetTransitionFunctionLibrary::MakeFloatTransitionValue(0.0f);
		Transition.ToValue = UWidgetTransitionFunctionLibrary::MakeFloatTransitionValue(1.0f);
		Transition.Time = 60.0f;
		Transition.bUseFrom = true;
		Transition.bBound = Transition.PropertyBinding.Resolve(Widget, TEXT("RenderOpacity"));
		return Transition;
	}

	void AddTransitionWithCallbacks(UWidgetTransitionSubsystem* Subsystem, FWidgetTransition Transition, FWidgetTransitionCallbacks Callbacks)
	{
		Subsystem->AddTransitionForTesting(MoveTemp(Transition), MoveTemp(Callbacks));
	}

	enum class EWidgetTransitionBenchmarkMode : uint8
	{
		Linear,
		Easing,
		Spring,
	};

	FWidgetTransition MakeBenchmarkTransition(UImage* Widget, bool bBound, EWidgetTransitionBenchmarkMode Mode, UCurveTable* CurveTable, FName CurveRow)
	{
		FWidgetTransition Transition = MakeRuntimeOpacityTransition(Widget);
		Transition.bBound = bBound;
		if (!bBound)
		{
			Transition.WidgetProperty = NAME_None;
			Transition.PropertyBinding.Invalidate();
			Transition.PropertyBinding.ChannelCount = 1;
		}
		if (Mode == EWidgetTransitionBenchmarkMode::Easing)
		{
			Transition.Easing.CurveTable = CurveTable;
			Transition.Easing.RowName = CurveRow;
		}
		else if (Mode == EWidgetTransitionBenchmarkMode::Spring)
		{
			Transition.bUseSpring = true;
			Transition.RepeatCount = -1;
		}
		return Transition;
	}

	const TCHAR* GetBenchmarkModeName(EWidgetTransitionBenchmarkMode Mode)
	{
		switch (Mode)
		{
		case EWidgetTransitionBenchmarkMode::Linear:
		{
			return TEXT("Linear");
		}
		case EWidgetTransitionBenchmarkMode::Easing:
		{
			return TEXT("CurveTable easing");
		}
		case EWidgetTransitionBenchmarkMode::Spring:
		{
			return TEXT("Spring");
		}
		default:
		{
			return TEXT("Unknown");
		}
		}
	}
}

void UWidgetTransitionTestEventReceiver::HandleStarted(FWidgetTransitionValue InValue)
{
	++StartedCount;
	LastTransitionValue = InValue;
	if (UWidgetTransitionSubsystem* Subsystem = SubsystemToClear.Get())
	{
		Subsystem->ClearTransitionsForTesting(WidgetToClear.Get());
	}
}

void UWidgetTransitionTestEventReceiver::HandleUpdated(FWidgetTransitionValue /*InValue*/)
{
	++UpdatedCount;
	if (UWidgetTransitionSubsystem* Subsystem = SubsystemToClear.Get())
	{
		if (bAppendTransitionsOnUpdate)
		{
			for (int32 Index = 0; Index < 16; ++Index)
			{
				FWidgetTransition Transition;
				Transition.Widget = WidgetToClear;
				Transition.Time = 60.0f;
				Subsystem->Transitions.Add(MoveTemp(Transition));
			}
			return;
		}
		Subsystem->ClearTransitionsForTesting(WidgetToClear.Get());
	}
}

void UWidgetTransitionTestEventReceiver::HandleUpdatedAndCapture(FWidgetTransitionValue InValue)
{
	++UpdatedCount;
	LastTransitionValue = InValue;
}

void UWidgetTransitionTestEventReceiver::HandleFinished(FWidgetTransitionValue InValue)
{
	++FinishedCount;
	LastTransitionValue = InValue;
	if (UWidgetTransitionSubsystem* Subsystem = SubsystemToClear.Get())
	{
		Subsystem->ClearTransitionsForTesting(WidgetToClear.Get());
	}
}

void UWidgetTransitionTestEventReceiver::HandleAsyncUpdated(FWidgetTransitionValue InValue)
{
	++AsyncValueUpdateCount;
	if (UTextBlock* Text = CounterText.Get())
	{
		Text->SetText(FText::AsNumber(FMath::RoundToInt(InValue.Channels.X)));
	}
}

FText UWidgetTransitionTestCounterUserWidget::GetCounterText()
{
	return FText::AsNumber(FMath::RoundToInt(CounterValue));
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWidgetTransitionStorageLayoutTest, "UMGTransitions.WidgetTransition.Diagnostics.StorageLayout", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FWidgetTransitionStorageLayoutTest::RunTest(const FString&)
{
	AddInfo(FormatBytes(TEXT("FWidgetTransition"), sizeof(FWidgetTransition), alignof(FWidgetTransition)));
	AddInfo(FormatBytes(TEXT("FWidgetTransitionValue"), sizeof(FWidgetTransitionValue), alignof(FWidgetTransitionValue)));
	AddInfo(FormatBytes(TEXT("FWidgetTransitionPropertyBinding"), sizeof(FWidgetTransitionPropertyBinding), alignof(FWidgetTransitionPropertyBinding)));
	AddInfo(FormatBytes(TEXT("FWidgetTransitionSpring"), sizeof(FWidgetTransitionSpring), alignof(FWidgetTransitionSpring)));
	AddInfo(FormatBytes(TEXT("FWidgetTransitionCallbacks (creation input)"), sizeof(FWidgetTransitionCallbacks), alignof(FWidgetTransitionCallbacks)));
	AddInfo(FormatBytes(TEXT("FWidgetTransitionLifecycleCallbacks"), sizeof(FWidgetTransitionLifecycleCallbacks), alignof(FWidgetTransitionLifecycleCallbacks)));
	AddInfo(FormatBytes(TEXT("FWidgetTransitionUpdateState"), sizeof(FWidgetTransitionUpdateState), alignof(FWidgetTransitionUpdateState)));
	AddInfo(FormatBytes(TEXT("FWidgetTransitionCallbackLinks"), sizeof(FWidgetTransitionCallbackLinks), alignof(FWidgetTransitionCallbackLinks)));
	AddInfo(FormatBytes(TEXT("FWidgetTransitionLifecycleEvent"), sizeof(FWidgetTransitionLifecycleEvent), alignof(FWidgetTransitionLifecycleEvent)));
	AddInfo(FormatBytes(TEXT("UWidgetTransitionAsyncAction"), sizeof(UWidgetTransitionAsyncAction), alignof(UWidgetTransitionAsyncAction)));
	AddInfo(FormatStorageBudget(TEXT("Typical linear workload"), 100, false));
	AddInfo(FormatStorageBudget(TEXT("Typical spring workload"), 100, true));
	AddInfo(FormatStorageBudget(TEXT("Stress linear workload"), 500, false));
	AddInfo(FormatStorageBudget(TEXT("Stress spring workload"), 500, true));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWidgetSelectorHierarchyTest, "UMGTransitions.WidgetSelector.Runtime.Hierarchy", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FWidgetSelectorHierarchyTest::RunTest(const FString&)
{
	UVerticalBox* Root = NewObject<UVerticalBox>(GetTransientPackage(), TEXT("Root"));
	UImage* First = NewObject<UImage>(GetTransientPackage(), TEXT("First"));
	UVerticalBox* Branch = NewObject<UVerticalBox>(GetTransientPackage(), TEXT("Branch"));
	UImage* Grandchild = NewObject<UImage>(GetTransientPackage(), TEXT("Grandchild"));
	UWidgetSelectorTestUserWidget* NestedUserWidget = NewObject<UWidgetSelectorTestUserWidget>(GetTransientPackage(), TEXT("NestedUserWidget"));
	NestedUserWidget->WidgetTree = NewObject<UWidgetTree>(NestedUserWidget);
	UImage* NestedRoot = NewObject<UImage>(NestedUserWidget->WidgetTree, TEXT("NestedRoot"));
	NestedUserWidget->WidgetTree->RootWidget = NestedRoot;
	Root->AddChild(First);
	Root->AddChild(Branch);
	Root->AddChild(NestedUserWidget);
	Branch->AddChild(Grandchild);

	const TArray<UWidget*> Children = UWidgetSelectorLibrary::GetWidgetChildren(Root);
	TestEqual(TEXT("Root has three direct children"), Children.Num(), 3);
	TestTrue(TEXT("Direct child order follows the panel"), Children == TArray<UWidget*>({First, Branch, NestedUserWidget}));
	TestTrue(TEXT("Nested User Widget exposes its WidgetTree root as a child"), UWidgetSelectorLibrary::GetWidgetChildren(NestedUserWidget) == TArray<UWidget*>({NestedRoot}));

	const TArray<UWidget*> Descendants = UWidgetSelectorLibrary::GetWidgetDescendants(Root);
	TestTrue(TEXT("Descendants use depth-first order across WidgetTree boundaries"), Descendants == TArray<UWidget*>({First, Branch, Grandchild, NestedUserWidget, NestedRoot}));
	TestTrue(TEXT("Depth zero is the root"), UWidgetSelectorLibrary::GetWidgetsAtDepth(Root, 0) == TArray<UWidget*>({Root}));
	TestTrue(TEXT("Depth one contains direct children"), UWidgetSelectorLibrary::GetWidgetsAtDepth(Root, 1) == TArray<UWidget*>({First, Branch, NestedUserWidget}));
	TestTrue(TEXT("Depth two crosses into the nested WidgetTree"), UWidgetSelectorLibrary::GetWidgetsAtDepth(Root, 2) == TArray<UWidget*>({Grandchild, NestedRoot}));
	TestTrue(TEXT("Through depth includes every preceding level"), UWidgetSelectorLibrary::GetWidgetsThroughDepth(Root, 1) == TArray<UWidget*>({Root, First, Branch, NestedUserWidget}));
	TestEqual(TEXT("Direct parent is returned"), UWidgetSelectorLibrary::GetWidgetParent(Grandchild), static_cast<UWidget*>(Branch));
	TestTrue(TEXT("Parents are returned from nearest to root"), UWidgetSelectorLibrary::GetWidgetParents(Grandchild) == TArray<UWidget*>({Branch, Root}));
	TestTrue(TEXT("Nested root climbs through the owning User Widget"), UWidgetSelectorLibrary::GetWidgetParents(NestedRoot) == TArray<UWidget*>({NestedUserWidget, Root}));
	TestTrue(TEXT("Single name lookup finds a descendant"), UWidgetSelectorLibrary::FindWidgetDescendantsByName(Root, TEXT("Grandchild")) == TArray<UWidget*>({Grandchild}));
	TestTrue(TEXT("Multiple name lookup preserves tree order"), UWidgetSelectorLibrary::FindWidgetDescendantsByNames(Root, {TEXT("Grandchild"), TEXT("First")}) == TArray<UWidget*>({First, Grandchild}));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWidgetTransitionBuilderTest, "UMGTransitions.WidgetTransition.Runtime.SpecBuilders", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FWidgetTransitionBuilderTest::RunTest(const FString&)
{
	UImage* Widget = NewObject<UImage>(GetTransientPackage());
	FWidgetTransitionValue ToValue = UWidgetTransitionFunctionLibrary::MakeFloatTransitionValue(0.8f);
	FWidgetTransitionValue FromValue = UWidgetTransitionFunctionLibrary::MakeVectorTransitionValue(FVector2D(0.2f, 0.4f));
	FWidgetTransition Transition = UWidgetTransitionFunctionLibrary::CreateWidgetTransition(Widget, TEXT("RenderTransform.Scale"), ToValue, 0.25f, 0.4f);
	Transition = UWidgetTransitionFunctionLibrary::From(MoveTemp(Transition), true, FromValue);
	Transition = UWidgetTransitionFunctionLibrary::Options(MoveTemp(Transition), true, true, 0.05f);
	Transition = UWidgetTransitionFunctionLibrary::Repeat(MoveTemp(Transition), 2);
	Transition = UWidgetTransitionFunctionLibrary::YoYo(MoveTemp(Transition));
	Transition = UWidgetTransitionFunctionLibrary::Spring(MoveTemp(Transition), 0.8f, 0.25f, 0.0f, true);
	TestEqual(TEXT("Target retains semantic Float type"), Transition.ToValue.Type, EWidgetTransitionValueType::Float);
	TestEqual(TEXT("From retains independent Vector2D type"), Transition.FromValue.Type, EWidgetTransitionValueType::Vector2D);
	TestTrue(TEXT("From modifier is enabled"), Transition.bUseFrom);
	TestTrue(TEXT("Options modifiers are retained"), Transition.bDeferFromValue && Transition.bIgnoreDelayOnRepeat);
	TestTrue(TEXT("Options retains the callback update interval"), FMath::IsNearlyEqual(Transition.UpdateInterval, 0.05f));
	TestTrue(TEXT("Binding is retained"), Transition.Widget == Widget && Transition.WidgetProperty == TEXT("RenderTransform.Scale"));
	TestTrue(TEXT("Repeat and spring modifiers are retained"), Transition.RepeatCount == 2 && Transition.bYoYo && Transition.bUseSpring && Transition.bFitSpringToTime);
	Transition = UWidgetTransitionFunctionLibrary::CreateWidgetTransition(Widget, TEXT("Material.Progress"), ToValue, 0.25f, 0.4f);
	TestEqual(TEXT("Material binding retains its virtual channel"), Transition.WidgetProperty, FName(TEXT("Material.Progress")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWidgetTransitionPropertyBindingTest, "UMGTransitions.WidgetTransition.Runtime.PropertyBinding.Channels", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FWidgetTransitionPropertyBindingTest::RunTest(const FString&)
{
	UImage* Widget = NewObject<UImage>(GetTransientPackage());
	FWidgetTransitionPropertyBinding Binding;
	TestTrue(TEXT("RenderOpacity resolves"), Binding.Resolve(Widget, TEXT("RenderOpacity")));
	TestEqual(TEXT("Opacity has one channel"), Binding.ChannelCount, static_cast<uint8>(1));
	FVector4f Value;
	TestTrue(TEXT("Opacity value can be read"), Binding.Read(Widget, Value));
	TestTrue(TEXT("Opacity value can be written"), Binding.Apply(Widget, FVector4f(0.35f, 0.35f, 0.35f, 0.35f)));
	TestTrue(TEXT("Opacity was updated"), FMath::IsNearlyEqual(Widget->GetRenderOpacity(), 0.35f, Tolerance));
	TestTrue(TEXT("Scale resolves"), Binding.Resolve(Widget, TEXT("RenderTransform.Scale")));
	TestEqual(TEXT("Scale has two channels"), Binding.ChannelCount, static_cast<uint8>(2));
	TestTrue(TEXT("Scale value can be written"), Binding.Apply(Widget, FVector4f(1.25f, 0.75f, 0.0f, 0.0f)));
	TestTrue(TEXT("Scale was updated"), Widget->GetRenderTransform().Scale.Equals(FVector2D(1.25f, 0.75f), Tolerance));
	TestTrue(TEXT("Translation resolves"), Binding.Resolve(Widget, TEXT("RenderTransform.Translation")));
	TestTrue(TEXT("Translation value can be written"), Binding.Apply(Widget, FVector4f(10.0f, -20.0f, 0.0f, 0.0f)));
	TestTrue(TEXT("Translation was updated"), Widget->GetRenderTransform().Translation.Equals(FVector2D(10.0f, -20.0f), Tolerance));
	TestTrue(TEXT("Shear resolves"), Binding.Resolve(Widget, TEXT("RenderTransform.Shear")));
	TestTrue(TEXT("Shear value can be written"), Binding.Apply(Widget, FVector4f(3.0f, -4.0f, 0.0f, 0.0f)));
	TestTrue(TEXT("Shear was updated"), Widget->GetRenderTransform().Shear.Equals(FVector2D(3.0f, -4.0f), Tolerance));
	TestTrue(TEXT("Angle resolves"), Binding.Resolve(Widget, TEXT("RenderTransform.Angle")));
	TestTrue(TEXT("Angle value can be written"), Binding.Apply(Widget, FVector4f(45.0f, 45.0f, 45.0f, 45.0f)));
	TestTrue(TEXT("Angle was updated"), FMath::IsNearlyEqual(Widget->GetRenderTransformAngle(), 45.0f, Tolerance));
	TestTrue(TEXT("Pivot resolves"), Binding.Resolve(Widget, TEXT("RenderTransformPivot")));
	TestTrue(TEXT("Pivot value can be written"), Binding.Apply(Widget, FVector4f(0.25f, 0.75f, 0.0f, 0.0f)));
	TestTrue(TEXT("Pivot was updated"), Widget->GetRenderTransformPivot().Equals(FVector2D(0.25f, 0.75f), Tolerance));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWidgetTransitionSpringTest, "UMGTransitions.WidgetTransition.Runtime.Spring.Converges", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FWidgetTransitionSpringTest::RunTest(const FString&)
{
	FWidgetTransitionSpring Spring(144.0f, 18.0f);
	Spring.Start(FVector4f::Zero(), FVector4f(100.0f, -50.0f, 25.0f, 1.0f));
	for (int32 Step = 0; Step < 1200 && !Spring.IsCompleted(); ++Step)
	{
		Spring.Tick(1.0f / 120.0f);
	}
	TestTrue(TEXT("Four-channel spring completes"), Spring.IsCompleted());
	TestTrue(TEXT("Four-channel spring settles at target"), Spring.GetValue().Equals(FVector4f(100.0f, -50.0f, 25.0f, 1.0f), Tolerance));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWidgetTransitionTickDeltaTest, "UMGTransitions.WidgetTransition.Runtime.TickDelta", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FWidgetTransitionTickDeltaTest::RunTest(const FString&)
{
	constexpr float MaximumTickDelta = 1.0f / 20.0f;
	const FVector4f TargetValue(100.0f, -50.0f, 25.0f, 1.0f);
	FWidgetTransitionSpring ExpectedSpring(144.0f, 18.0f);
	ExpectedSpring.Start(FVector4f::Zero(), TargetValue);
	ExpectedSpring.Tick(MaximumTickDelta);

	UWidgetTransitionSubsystem* Subsystem = NewObject<UWidgetTransitionSubsystem>(GetTransientPackage());
	UImage* Widget = NewObject<UImage>(GetTransientPackage());
	FWidgetTransition Transition;
	Transition.Widget = Widget;
	Transition.FromValue = UWidgetTransitionFunctionLibrary::MakeColorTransitionValue(FLinearColor::Transparent);
	Transition.ToValue = UWidgetTransitionFunctionLibrary::MakeColorTransitionValue(FLinearColor(TargetValue.X, TargetValue.Y, TargetValue.Z, TargetValue.W));
	Transition.Time = 60.0f;
	Transition.bUseSpring = true;
	Transition.SpringIndex = Subsystem->Springs.Emplace(144.0f, 18.0f);
	Subsystem->Springs[Transition.SpringIndex].Start(Transition.FromValue.Channels, Transition.ToValue.Channels);
	Subsystem->SpringTransitionIndices.Add(0);
	Subsystem->Transitions.Add(MoveTemp(Transition));

	Subsystem->TickTransitionsForTesting(1.0f);

	TestTrue(TEXT("Spring receives the same capped delta as the transition"), Subsystem->Springs[0].GetValue().Equals(ExpectedSpring.GetValue(), Tolerance));
	TestTrue(TEXT("Transition time is capped together with the spring"), FMath::IsNearlyEqual(Subsystem->Transitions[0].CurrentTime, MaximumTickDelta, Tolerance));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWidgetTransitionInvalidEasingFallbackTest, "UMGTransitions.WidgetTransition.Runtime.InvalidEasingFallback", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FWidgetTransitionInvalidEasingFallbackTest::RunTest(const FString&)
{
	UWidgetTransitionSubsystem* Subsystem = NewObject<UWidgetTransitionSubsystem>(GetTransientPackage());
	UImage* Widget = NewObject<UImage>(GetTransientPackage());
	FWidgetTransition Transition;
	Transition.Widget = Widget;
	Transition.WidgetProperty = TEXT("RenderOpacity");
	Transition.FromValue = UWidgetTransitionFunctionLibrary::MakeFloatTransitionValue(0.0f);
	Transition.ToValue = UWidgetTransitionFunctionLibrary::MakeFloatTransitionValue(1.0f);
	Transition.Time = 0.0f;
	Transition.bUseFrom = true;
	Transition.bBound = Transition.PropertyBinding.Resolve(Widget, TEXT("RenderOpacity"));
	Transition.Easing.CurveTable = NewObject<UCurveTable>(GetTransientPackage());
	Transition.Easing.RowName = TEXT("MissingRow");
	Subsystem->Transitions.Add(MoveTemp(Transition));

	Subsystem->TickTransitionsForTesting(1.0f / 60.0f);

	TestTrue(TEXT("Missing easing row falls back to the linear target value"), FMath::IsNearlyEqual(Widget->GetRenderOpacity(), 1.0f, Tolerance));
	TestEqual(TEXT("Fallback transition completes"), Subsystem->Transitions.Num(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWidgetTransitionUpdateIntervalTest, "UMGTransitions.WidgetTransition.Runtime.UpdateInterval", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FWidgetTransitionUpdateIntervalTest::RunTest(const FString&)
{
	{
		UWidgetTransitionSubsystem* Subsystem = NewObject<UWidgetTransitionSubsystem>(GetTransientPackage());
		UImage* Widget = NewObject<UImage>(GetTransientPackage());
		UWidgetTransitionTestEventReceiver* Receiver = NewObject<UWidgetTransitionTestEventReceiver>(GetTransientPackage());
		FWidgetTransition Transition;
		Transition.Widget = Widget;
		Transition.Time = 60.0f;
		Transition.UpdateInterval = 0.033f;
		FWidgetTransitionCallbacks Callbacks;
		Callbacks.OnUpdated.BindDynamic(Receiver, &UWidgetTransitionTestEventReceiver::HandleUpdated);
		AddTransitionWithCallbacks(Subsystem, MoveTemp(Transition), MoveTemp(Callbacks));

		for (int32 TickIndex = 0; TickIndex < 5; ++TickIndex)
		{
			Subsystem->TickTransitionsForTesting(0.011f);
		}
		TestEqual(TEXT("UpdateInterval=0.033 dispatches after each elapsed thirty-three milliseconds"), Receiver->UpdatedCount, 1);
	}

	{
		UWidgetTransitionSubsystem* Subsystem = NewObject<UWidgetTransitionSubsystem>(GetTransientPackage());
		UImage* Widget = NewObject<UImage>(GetTransientPackage());
		UWidgetTransitionTestEventReceiver* Receiver = NewObject<UWidgetTransitionTestEventReceiver>(GetTransientPackage());
		FWidgetTransition Transition;
		Transition.Widget = Widget;
		Transition.FromValue = UWidgetTransitionFunctionLibrary::MakeFloatTransitionValue(0.0f);
		Transition.ToValue = UWidgetTransitionFunctionLibrary::MakeFloatTransitionValue(10.0f);
		Transition.bUseFrom = true;
		Transition.Time = 0.01f;
		Transition.RepeatCount = 1;
		Transition.UpdateInterval = 0.0f;
		FWidgetTransitionCallbacks Callbacks;
		Callbacks.OnUpdated.BindDynamic(Receiver, &UWidgetTransitionTestEventReceiver::HandleUpdatedAndCapture);
		AddTransitionWithCallbacks(Subsystem, MoveTemp(Transition), MoveTemp(Callbacks));

		Subsystem->TickTransitionsForTesting(0.01f);
		TestTrue(TEXT("Repeat boundary dispatches the completed cycle value"), FMath::IsNearlyEqual(Receiver->LastTransitionValue.Channels.X, 10.0f));
	}

	{
		UWidgetTransitionSubsystem* Subsystem = NewObject<UWidgetTransitionSubsystem>(GetTransientPackage());
		UImage* Widget = NewObject<UImage>(GetTransientPackage());
		UWidgetTransitionTestEventReceiver* Receiver = NewObject<UWidgetTransitionTestEventReceiver>(GetTransientPackage());
		FWidgetTransition Transition;
		Transition.Widget = Widget;
		Transition.Time = 0.0f;
		Transition.UpdateInterval = 0.3f;
		FWidgetTransitionCallbacks Callbacks;
		Callbacks.OnUpdated.BindDynamic(Receiver, &UWidgetTransitionTestEventReceiver::HandleUpdated);
		AddTransitionWithCallbacks(Subsystem, MoveTemp(Transition), MoveTemp(Callbacks));

		Subsystem->TickTransitionsForTesting(1.0f / 60.0f);
		TestEqual(TEXT("Completed transition dispatches its final update before the update interval"), Receiver->UpdatedCount, 1);
	}

	{
		UWidgetTransitionSubsystem* Subsystem = NewObject<UWidgetTransitionSubsystem>(GetTransientPackage());
		UWidgetTransitionTestCounterUserWidget* Widget = NewObject<UWidgetTransitionTestCounterUserWidget>(GetTransientPackage());
		const UE::FieldNotification::FFieldId FieldId = Widget->GetFieldNotificationDescriptor().GetField(Widget->GetClass(), GET_MEMBER_NAME_CHECKED(UWidgetTransitionTestCounterUserWidget, CounterValue));
		int32 NotificationCount = 0;
		Widget->AddFieldValueChangedDelegate(FieldId, INotifyFieldValueChanged::FFieldValueChangedDelegate::CreateLambda([&NotificationCount](UObject*, UE::FieldNotification::FFieldId)
		{
			++NotificationCount;
		}));

		FWidgetTransition Transition;
		Transition.Widget = Widget;
		Transition.Time = 60.0f;
		Transition.UpdateInterval = 0.033f;
		Transition.bBound = Transition.PropertyBinding.Resolve(Widget, TEXT("CounterValue"));
		TestTrue(TEXT("CounterValue resolves as a FieldNotify transition binding"), Transition.PropertyBinding.IsFieldNotify());
		AddTransitionWithCallbacks(Subsystem, MoveTemp(Transition), {});

		for (int32 TickIndex = 0; TickIndex < 5; ++TickIndex)
		{
			Subsystem->TickTransitionsForTesting(0.011f);
		}
		TestEqual(TEXT("UpdateInterval throttles FieldNotify broadcasts with Updated callbacks disabled"), NotificationCount, 1);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWidgetTransitionCallbackReentrancyTest, "UMGTransitions.WidgetTransition.Runtime.CallbackReentrancy", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FWidgetTransitionCallbackReentrancyTest::RunTest(const FString&)
{
	auto AddCallbackTransition = [](UWidgetTransitionSubsystem* Subsystem, UWidget* Widget, FWidgetTransitionCallbacks Callbacks, bool bStarted, float Time)
	{
		FWidgetTransition Transition;
		Transition.Widget = Widget;
		Transition.FromValue = UWidgetTransitionFunctionLibrary::MakeFloatTransitionValue(0.25f);
		Transition.ToValue = UWidgetTransitionFunctionLibrary::MakeFloatTransitionValue(0.75f);
		Transition.Time = Time;
		Transition.bUseFrom = true;
		Transition.bStarted = bStarted;
		Transition.UpdateInterval = Callbacks.OnUpdated.IsBound() ? 0.0f : Transition.UpdateInterval;
		AddTransitionWithCallbacks(Subsystem, MoveTemp(Transition), MoveTemp(Callbacks));
	};

	{
		UWidgetTransitionSubsystem* Subsystem = NewObject<UWidgetTransitionSubsystem>(GetTransientPackage());
		UImage* Widget = NewObject<UImage>(GetTransientPackage());
		UWidgetTransitionTestEventReceiver* Receiver = NewObject<UWidgetTransitionTestEventReceiver>(GetTransientPackage());
		Receiver->SubsystemToClear = Subsystem;
		Receiver->WidgetToClear = Widget;
		FWidgetTransitionCallbacks Callbacks;
		Callbacks.OnStarted.BindDynamic(Receiver, &UWidgetTransitionTestEventReceiver::HandleStarted);
		AddCallbackTransition(Subsystem, Widget, MoveTemp(Callbacks), false, 1.0f);
		Subsystem->TickTransitionsForTesting(1.0f / 60.0f);
		TestEqual(TEXT("Started callback runs once before clearing its transition"), Receiver->StartedCount, 1);
		TestTrue(TEXT("Started callback receives From Value"), FMath::IsNearlyEqual(Receiver->LastTransitionValue.Channels.X, 0.25f));
		TestEqual(TEXT("Started callback can clear its own transition"), Subsystem->Transitions.Num(), 0);
		TestEqual(TEXT("Started removal clears callback links"), Subsystem->CallbackStore.Links.Num(), 0);
		TestEqual(TEXT("Started removal clears lifecycle storage"), Subsystem->CallbackStore.LifecycleCallbacks.Num(), 0);
	}

	{
		UWidgetTransitionSubsystem* Subsystem = NewObject<UWidgetTransitionSubsystem>(GetTransientPackage());
		UImage* Widget = NewObject<UImage>(GetTransientPackage());
		UWidgetTransitionTestEventReceiver* Receiver = NewObject<UWidgetTransitionTestEventReceiver>(GetTransientPackage());
		Receiver->SubsystemToClear = Subsystem;
		Receiver->WidgetToClear = Widget;
		FWidgetTransitionCallbacks Callbacks;
		Callbacks.OnUpdated.BindDynamic(Receiver, &UWidgetTransitionTestEventReceiver::HandleUpdated);
		AddCallbackTransition(Subsystem, Widget, MoveTemp(Callbacks), true, 1.0f);
		Subsystem->TickTransitionsForTesting(1.0f / 60.0f);
		TestEqual(TEXT("Updated callback runs once before clearing its transition"), Receiver->UpdatedCount, 1);
		TestEqual(TEXT("Updated callback can clear its own transition"), Subsystem->Transitions.Num(), 0);
		TestEqual(TEXT("Updated removal clears callback links"), Subsystem->CallbackStore.Links.Num(), 0);
		TestEqual(TEXT("Updated removal clears update storage"), Subsystem->CallbackStore.UpdateStates.Num(), 0);
	}

	{
		UWidgetTransitionSubsystem* Subsystem = NewObject<UWidgetTransitionSubsystem>(GetTransientPackage());
		UImage* Widget = NewObject<UImage>(GetTransientPackage());
		UWidgetTransitionTestEventReceiver* Receiver = NewObject<UWidgetTransitionTestEventReceiver>(GetTransientPackage());
		Receiver->SubsystemToClear = Subsystem;
		Receiver->WidgetToClear = Widget;
		Receiver->bAppendTransitionsOnUpdate = true;
		FWidgetTransitionCallbacks Callbacks;
		Callbacks.OnUpdated.BindDynamic(Receiver, &UWidgetTransitionTestEventReceiver::HandleUpdated);
		AddCallbackTransition(Subsystem, Widget, MoveTemp(Callbacks), true, 0.0f);
		Subsystem->TickTransitionsForTesting(1.0f / 60.0f);
		TestEqual(TEXT("Updated callback runs once before growing the transition array"), Receiver->UpdatedCount, 1);
		TestEqual(TEXT("Original completed transition is removed after callback reallocation"), Subsystem->Transitions.Num(), 16);
		TestTrue(TEXT("Original completed transition is absent after callback reallocation"), !Subsystem->Transitions.ContainsByPredicate([](const FWidgetTransition& Transition) { return Transition.Time <= 0.0f; }));
		TestEqual(TEXT("Callback links remain parallel after callback reallocation"), Subsystem->CallbackStore.Links.Num(), Subsystem->Transitions.Num());
		TestEqual(TEXT("Completed callback state is removed after callback reallocation"), Subsystem->CallbackStore.UpdateStates.Num(), 0);
	}

	{
		UWidgetTransitionSubsystem* Subsystem = NewObject<UWidgetTransitionSubsystem>(GetTransientPackage());
		UImage* Widget = NewObject<UImage>(GetTransientPackage());
		UWidgetTransitionTestEventReceiver* Receiver = NewObject<UWidgetTransitionTestEventReceiver>(GetTransientPackage());
		Receiver->SubsystemToClear = Subsystem;
		Receiver->WidgetToClear = Widget;
		FWidgetTransitionCallbacks Callbacks;
		Callbacks.OnFinished.BindDynamic(Receiver, &UWidgetTransitionTestEventReceiver::HandleFinished);
		AddCallbackTransition(Subsystem, Widget, MoveTemp(Callbacks), true, 0.0f);
		Subsystem->TickTransitionsForTesting(1.0f / 60.0f);
		TestEqual(TEXT("Finished callback runs once before clearing its transition"), Receiver->FinishedCount, 1);
		TestTrue(TEXT("Finished callback receives To Value"), FMath::IsNearlyEqual(Receiver->LastTransitionValue.Channels.X, 0.75f));
		TestEqual(TEXT("Finished callback can clear its own transition"), Subsystem->Transitions.Num(), 0);
		TestEqual(TEXT("Finished removal clears callback links"), Subsystem->CallbackStore.Links.Num(), 0);
		TestEqual(TEXT("Finished removal clears lifecycle storage"), Subsystem->CallbackStore.LifecycleCallbacks.Num(), 0);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWidgetTransitionRemoveAtSwapTest, "UMGTransitions.WidgetTransition.Runtime.RemoveAtSwap", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FWidgetTransitionRemoveAtSwapTest::RunTest(const FString&)
{
	UWidgetTransitionSubsystem* Subsystem = NewObject<UWidgetTransitionSubsystem>(GetTransientPackage());
	UImage* RemainingWidget = NewObject<UImage>(GetTransientPackage());
	FWidgetTransition RemovedTransition;
	RemovedTransition.bUseSpring = true;
	RemovedTransition.SpringIndex = Subsystem->Springs.Emplace(1.0f, 1.0f);
	Subsystem->SpringTransitionIndices.Add(0);
	FWidgetTransition RemainingTransition;
	RemainingTransition.Widget = RemainingWidget;
	RemainingTransition.Time = 60.0f;
	RemainingTransition.ToValue.Channels = FVector4f(1.0f, 0.0f, 0.0f, 0.0f);
	RemainingTransition.bUseSpring = true;
	RemainingTransition.SpringIndex = Subsystem->Springs.Emplace(1.0f, 1.0f);
	Subsystem->SpringTransitionIndices.Add(1);
	Subsystem->Springs[0].Start(FVector4f::Zero(), FVector4f(1.0f));
	Subsystem->Springs[1].Start(FVector4f::Zero(), FVector4f(1.0f));
	Subsystem->Transitions.Add(MoveTemp(RemovedTransition));
	Subsystem->Transitions.Add(MoveTemp(RemainingTransition));

	Subsystem->TickTransitionsForTesting(1.0f / 60.0f);

	TestEqual(TEXT("Invalid transition is removed"), Subsystem->Transitions.Num(), 1);
	TestEqual(TEXT("Its spring is removed"), Subsystem->Springs.Num(), 1);
	TestEqual(TEXT("Remaining transition is moved into the removed index"), Subsystem->Transitions[0].SpringIndex, 0);
	TestEqual(TEXT("Moved spring points to the moved transition"), Subsystem->SpringTransitionIndices[0], 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWidgetTransitionMetadataTest, "UMGTransitions.WidgetTransition.Editor.Metadata", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FWidgetTransitionMetadataTest::RunTest(const FString&)
{
	const UFunction* Create = UWidgetTransitionFunctionLibrary::StaticClass()->FindFunctionByName(GET_FUNCTION_NAME_CHECKED(UWidgetTransitionFunctionLibrary, CreateWidgetTransition));
	TestTrue(TEXT("Create function exists"), Create != nullptr);
	if (!Create)
	{
		return false;
	}
	TestEqual(TEXT("Create function opts into the combined custom property pin"), Create->GetMetaData(TEXT("UMGTransitionsBinding")), FString(TEXT("Combined")));
	const FProperty* WidgetProperty = Create->FindPropertyByName(TEXT("WidgetProperty"));
	TestTrue(TEXT("Widget Property parameter exists"), WidgetProperty != nullptr);
	if (WidgetProperty)
	{
		TestEqual(TEXT("Widget Property opts into the custom selector role"), WidgetProperty->GetMetaData(TEXT("UMGTransitionsRole")), FString(TEXT("WidgetProperty")));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWidgetTransitionConstructionPerformanceTest, "UMGTransitions.WidgetTransition.Performance.Construction", EAutomationTestFlags::EditorContext | EAutomationTestFlags::PerfFilter)
bool FWidgetTransitionConstructionPerformanceTest::RunTest(const FString&)
{
	constexpr int32 IterationCount = 100000;
	UImage* Widget = NewObject<UImage>(GetTransientPackage());
	const FWidgetTransitionValue FromValue = UWidgetTransitionFunctionLibrary::MakeFloatTransitionValue(0.0f);
	const FWidgetTransitionValue ToValue = UWidgetTransitionFunctionLibrary::MakeFloatTransitionValue(1.0f);
	volatile uint64 Sink = 0;

	auto Measure = [this, &Sink, IterationCount](const TCHAR* Name, auto&& Construct)
	{
		const double StartTime = FPlatformTime::Seconds();
		for (int32 Index = 0; Index < IterationCount; ++Index)
		{
			const FWidgetTransition Transition = Construct();
			Sink += static_cast<uint64>(Transition.Time * 1000.0f) + static_cast<uint64>(Transition.RepeatCount + 1) + Transition.bUseSpring;
		}
		const double ElapsedSeconds = FPlatformTime::Seconds() - StartTime;
		AddInfo(FString::Printf(TEXT("%s: %s total, %s / transition (%d transitions)"), Name, *FormatMicroseconds(ElapsedSeconds), *FormatMicroseconds(ElapsedSeconds / IterationCount), IterationCount));
	};

	Measure(TEXT("Direct structure"), [&Widget, &ToValue]()
	{
		FWidgetTransition Transition;
		Transition.Widget = Widget;
		Transition.WidgetProperty = TEXT("RenderOpacity");
		Transition.ToValue = ToValue;
		Transition.Time = 0.2f;
		return Transition;
	});
	Measure(TEXT("Create Widget Transition"), [&Widget, &ToValue]()
	{
		return UWidgetTransitionFunctionLibrary::CreateWidgetTransition(Widget, TEXT("RenderOpacity"), ToValue, 0.2f, 0.1f);
	});
	Measure(TEXT("Full pure pipeline"), [&Widget, &FromValue, &ToValue]()
	{
		FWidgetTransition Transition = UWidgetTransitionFunctionLibrary::CreateWidgetTransition(Widget, TEXT("RenderOpacity"), ToValue, 0.2f, 0.1f);
		Transition = UWidgetTransitionFunctionLibrary::From(MoveTemp(Transition), true, FromValue);
		Transition = UWidgetTransitionFunctionLibrary::Options(MoveTemp(Transition), true, true);
		Transition = UWidgetTransitionFunctionLibrary::Repeat(MoveTemp(Transition), 3);
		Transition = UWidgetTransitionFunctionLibrary::YoYo(MoveTemp(Transition));
		return UWidgetTransitionFunctionLibrary::Spring(MoveTemp(Transition), 0.65f, 0.45f, 0.0f, true);
	});

	TestTrue(TEXT("Construction benchmark executed"), Sink > 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWidgetTransitionConcurrentTickPerformanceTest, "UMGTransitions.WidgetTransition.Performance.ConcurrentTick", EAutomationTestFlags::EditorContext | EAutomationTestFlags::PerfFilter)
bool FWidgetTransitionConcurrentTickPerformanceTest::RunTest(const FString&)
{
	constexpr int32 FrameCount = 300;
	constexpr int32 TransitionCounts[] = { 1, 10, 100, 500 };
	constexpr float DeltaTime = 1.0f / 60.0f;

	for (const int32 TransitionCount : TransitionCounts)
	{
		UWidgetTransitionSubsystem* Subsystem = NewObject<UWidgetTransitionSubsystem>(GetTransientPackage());
		TArray<UImage*> Widgets;
		Widgets.Reserve(TransitionCount);
		for (int32 Index = 0; Index < TransitionCount; ++Index)
		{
			UImage* Widget = NewObject<UImage>(GetTransientPackage());
			Widgets.Add(Widget);
			FWidgetTransition Transition = MakeRuntimeOpacityTransition(Widget);
			TestTrue(FString::Printf(TEXT("RenderOpacity resolves for transition %d"), Index), Transition.bBound);
			Subsystem->Transitions.Emplace(MoveTemp(Transition));
		}

		Subsystem->TickTransitionsForTesting(DeltaTime);
		const double StartTime = FPlatformTime::Seconds();
		for (int32 FrameIndex = 0; FrameIndex < FrameCount; ++FrameIndex)
		{
			Subsystem->TickTransitionsForTesting(DeltaTime);
		}
		const double ElapsedSeconds = FPlatformTime::Seconds() - StartTime;
		const double SecondsPerFrame = ElapsedSeconds / FrameCount;
		const double SecondsPerTransition = ElapsedSeconds / (FrameCount * TransitionCount);
		AddInfo(FString::Printf(TEXT("%d concurrent transitions: %s / frame, %s / transition (%d frames)"), TransitionCount, *FormatMicroseconds(SecondsPerFrame), *FormatMicroseconds(SecondsPerTransition), FrameCount));
		TestEqual(FString::Printf(TEXT("All %d transitions remain active"), TransitionCount), Subsystem->Transitions.Num(), TransitionCount);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWidgetTransitionCallbacksPerformanceTest, "UMGTransitions.WidgetTransition.Performance.Callbacks", EAutomationTestFlags::EditorContext | EAutomationTestFlags::PerfFilter)
bool FWidgetTransitionCallbacksPerformanceTest::RunTest(const FString&)
{
	constexpr int32 TransitionCounts[] = { 20, 30, 50, 100 };
	constexpr int32 FrameCount = 300;
	constexpr float DeltaTime = 1.0f / 60.0f;
	enum class ECallbackMode : uint8
	{
		None,
		Lifecycle,
		Updated,
	};
	auto Measure = [this](int32 TransitionCount, bool bWithBinding, ECallbackMode CallbackMode)
	{
		UWidgetTransitionSubsystem* Subsystem = NewObject<UWidgetTransitionSubsystem>(GetTransientPackage());
		UWidgetTransitionTestEventReceiver* Receiver = NewObject<UWidgetTransitionTestEventReceiver>(GetTransientPackage());
		TArray<UImage*> Widgets;
		Widgets.Reserve(TransitionCount);
		for (int32 Index = 0; Index < TransitionCount; ++Index)
		{
			UImage* Widget = NewObject<UImage>(GetTransientPackage());
			Widgets.Add(Widget);
			FWidgetTransition Transition = MakeRuntimeOpacityTransition(Widget);
			if (!bWithBinding)
			{
				Transition.bBound = false;
				Transition.WidgetProperty = NAME_None;
				Transition.PropertyBinding.Invalidate();
				Transition.PropertyBinding.ChannelCount = 1;
			}
			FWidgetTransitionCallbacks Callbacks;
			if (CallbackMode != ECallbackMode::None)
			{
				if (CallbackMode == ECallbackMode::Lifecycle)
				{
					Callbacks.OnStarted.BindDynamic(Receiver, &UWidgetTransitionTestEventReceiver::HandleStarted);
					Callbacks.OnFinished.BindDynamic(Receiver, &UWidgetTransitionTestEventReceiver::HandleFinished);
				}
				else
				{
					Transition.UpdateInterval = 0.0f;
					Callbacks.OnUpdated.BindDynamic(Receiver, &UWidgetTransitionTestEventReceiver::HandleUpdated);
				}
			}
			AddTransitionWithCallbacks(Subsystem, MoveTemp(Transition), MoveTemp(Callbacks));
		}

		Subsystem->TickTransitionsForTesting(DeltaTime);
		const int32 StartedCountAfterWarmup = Receiver->StartedCount;
		Receiver->UpdatedCount = 0;
		const double StartTime = FPlatformTime::Seconds();
		for (int32 FrameIndex = 0; FrameIndex < FrameCount; ++FrameIndex)
		{
			Subsystem->TickTransitionsForTesting(DeltaTime);
		}
		const double ElapsedSeconds = FPlatformTime::Seconds() - StartTime;
		const TCHAR* BindingName = bWithBinding ? TEXT("with binding") : TEXT("without binding");
		const TCHAR* CallbackName = CallbackMode == ECallbackMode::None ? TEXT("no callbacks") : CallbackMode == ECallbackMode::Lifecycle ? TEXT("lifecycle callbacks") : TEXT("Updated callbacks");
		AddInfo(FString::Printf(TEXT("%d linear transitions, %s, %s: %s / frame, %s / transition"), TransitionCount, BindingName, CallbackName, *FormatMicroseconds(ElapsedSeconds / FrameCount), *FormatMicroseconds(ElapsedSeconds / (FrameCount * TransitionCount))));
		TestEqual(FString::Printf(TEXT("All %d callback benchmark transitions remain active"), TransitionCount), Subsystem->Transitions.Num(), TransitionCount);
		TestEqual(FString::Printf(TEXT("Started callback count for %s, %s"), BindingName, CallbackName), StartedCountAfterWarmup, CallbackMode == ECallbackMode::Lifecycle ? TransitionCount : 0);
		TestEqual(FString::Printf(TEXT("Updated callback count for %s, %s"), BindingName, CallbackName), Receiver->UpdatedCount, CallbackMode == ECallbackMode::Updated ? TransitionCount * FrameCount : 0);
	};

	for (const int32 TransitionCount : TransitionCounts)
	{
		for (const bool bWithBinding : { false, true })
		{
			Measure(TransitionCount, bWithBinding, ECallbackMode::None);
			Measure(TransitionCount, bWithBinding, ECallbackMode::Lifecycle);
			Measure(TransitionCount, bWithBinding, ECallbackMode::Updated);
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWidgetTransitionSpringTargetUpdatePerformanceTest, "UMGTransitions.WidgetTransition.Performance.SpringTargetUpdate", EAutomationTestFlags::EditorContext | EAutomationTestFlags::PerfFilter)
bool FWidgetTransitionSpringTargetUpdatePerformanceTest::RunTest(const FString&)
{
	constexpr int32 FrameCount = 300;
	constexpr float DeltaTime = 1.0f / 60.0f;
	constexpr int32 TransitionCounts[] = { 100, 500 };

	for (const int32 TransitionCount : TransitionCounts)
	{
		UWidgetTransitionSubsystem* Subsystem = NewObject<UWidgetTransitionSubsystem>(GetTransientPackage());
		for (int32 Index = 0; Index < TransitionCount; ++Index)
		{
			UImage* Widget = NewObject<UImage>(GetTransientPackage());
			FWidgetTransition Transition;
			Transition.Widget = Widget;
			Transition.WidgetProperty = TEXT("RenderTransform.Translation");
			Transition.FromValue = UWidgetTransitionFunctionLibrary::MakeVectorTransitionValue(FVector2D::ZeroVector);
			Transition.ToValue = UWidgetTransitionFunctionLibrary::MakeVectorTransitionValue(FVector2D(100.0f, 0.0f));
			Transition.bUseFrom = true;
			Transition.bBound = Transition.PropertyBinding.Resolve(Widget, Transition.WidgetProperty.ToString());
			Transition.bUseSpring = true;
			Transition.RepeatCount = -1;
			Transition.PropertyBinding.ChannelCount = 2;
			const int32 TransitionIndex = Subsystem->Transitions.Emplace(MoveTemp(Transition));
			const int32 SpringIndex = Subsystem->Springs.Emplace(36.0f, 7.2f);
			Subsystem->SpringTransitionIndices.Add(TransitionIndex);
			Subsystem->Transitions[TransitionIndex].SpringIndex = SpringIndex;
			Subsystem->Springs[SpringIndex].Start(FVector4f::Zero(), FVector4f(100.0f, 0.0f, 0.0f, 0.0f));
		}

		Subsystem->TickTransitionsForTesting(DeltaTime);
		const double StartTime = FPlatformTime::Seconds();
		for (int32 FrameIndex = 0; FrameIndex < FrameCount; ++FrameIndex)
		{
			const float TargetX = 100.0f + 50.0f * FMath::Sin(static_cast<float>(FrameIndex) * 0.1f);
			for (FWidgetTransition& Transition : Subsystem->Transitions)
			{
				Transition.ToValue.Channels.X = TargetX;
				Transition.ToValue.Channels.Y = -TargetX * 0.25f;
			}
			Subsystem->TickTransitionsForTesting(DeltaTime);
		}
		const double ElapsedSeconds = FPlatformTime::Seconds() - StartTime;
		AddInfo(FString::Printf(TEXT("%d RenderTransform.Translation spring transitions with per-tick target updates: %s / frame, %s / transition (%d frames)"), TransitionCount, *FormatMicroseconds(ElapsedSeconds / FrameCount), *FormatMicroseconds(ElapsedSeconds / (FrameCount * TransitionCount)), FrameCount));
		TestEqual(FString::Printf(TEXT("All %d spring target-update transitions remain active"), TransitionCount), Subsystem->Transitions.Num(), TransitionCount);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWidgetTransitionUpdateIntervalPerformanceTest, "UMGTransitions.WidgetTransition.Performance.UpdateInterval", EAutomationTestFlags::EditorContext | EAutomationTestFlags::PerfFilter)
bool FWidgetTransitionUpdateIntervalPerformanceTest::RunTest(const FString&)
{
	constexpr int32 TransitionCount = 100;
	constexpr int32 FrameCount = 300;
	constexpr float DeltaTime = 1.0f / 60.0f;
	struct FUpdateIntervalCase
	{
		float Interval;
		int32 ExpectedUpdatesPerTransition;
	};
	const FUpdateIntervalCase Cases[] =
	{
		{ 0.0f, 300 },
		{ 1.0f / 30.0f, 150 },
		{ 1.0f / 20.0f, 100 },
		{ 0.1f, 50 },
	};
	for (const FUpdateIntervalCase& Case : Cases)
	{
		UWidgetTransitionSubsystem* Subsystem = NewObject<UWidgetTransitionSubsystem>(GetTransientPackage());
		UWidgetTransitionTestEventReceiver* Receiver = NewObject<UWidgetTransitionTestEventReceiver>(GetTransientPackage());
		TArray<UImage*> Widgets;
		Widgets.Reserve(TransitionCount);
		for (int32 Index = 0; Index < TransitionCount; ++Index)
		{
			UImage* Widget = NewObject<UImage>(GetTransientPackage());
			FWidgetTransition Transition;
			Transition.Widget = Widget;
			Transition.Time = 60.0f;
			Transition.UpdateInterval = Case.Interval;
			FWidgetTransitionCallbacks Callbacks;
			Callbacks.OnUpdated.BindDynamic(Receiver, &UWidgetTransitionTestEventReceiver::HandleUpdated);
			AddTransitionWithCallbacks(Subsystem, MoveTemp(Transition), MoveTemp(Callbacks));
			Widgets.Add(Widget);
		}

		Subsystem->TickTransitionsForTesting(DeltaTime);
		for (FWidgetTransitionUpdateState& UpdateState : Subsystem->CallbackStore.UpdateStates)
		{
			UpdateState.UpdateElapsed = 0.0f;
		}
		Receiver->UpdatedCount = 0;
		const double StartTime = FPlatformTime::Seconds();
		for (int32 FrameIndex = 0; FrameIndex < FrameCount; ++FrameIndex)
		{
			Subsystem->TickTransitionsForTesting(DeltaTime);
		}
		const double ElapsedSeconds = FPlatformTime::Seconds() - StartTime;
		AddInfo(FString::Printf(TEXT("%d Updated callbacks with %.3f s interval: %s / frame, %s / transition"), TransitionCount, Case.Interval, *FormatMicroseconds(ElapsedSeconds / FrameCount), *FormatMicroseconds(ElapsedSeconds / (FrameCount * TransitionCount))));
		TestEqual(FString::Printf(TEXT("Updated callbacks at %.3f s interval"), Case.Interval), Receiver->UpdatedCount, TransitionCount * Case.ExpectedUpdatesPerTransition);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWidgetTransitionExternalTextBindingPerformanceTest, "UMGTransitions.WidgetTransition.Performance.ExternalTextBinding", EAutomationTestFlags::EditorContext | EAutomationTestFlags::PerfFilter)
bool FWidgetTransitionExternalTextBindingPerformanceTest::RunTest(const FString&)
{
	constexpr int32 TransitionCount = 100;
	constexpr int32 FrameCount = 300;
	constexpr float DeltaTime = 1.0f / 60.0f;
	UWidgetTransitionSubsystem* Subsystem = NewObject<UWidgetTransitionSubsystem>(GetTransientPackage());
	TArray<UWidgetTransitionTestCounterUserWidget*> Widgets;
	TArray<UTextBlock*> TextBlocks;
	Widgets.Reserve(TransitionCount);
	TextBlocks.Reserve(TransitionCount);
	for (int32 Index = 0; Index < TransitionCount; ++Index)
	{
		UWidgetTransitionTestCounterUserWidget* Widget = NewObject<UWidgetTransitionTestCounterUserWidget>(GetTransientPackage());
		UTextBlock* TextBlock = NewObject<UTextBlock>(GetTransientPackage());
		TextBlock->TextDelegate.BindDynamic(Widget, &UWidgetTransitionTestCounterUserWidget::GetCounterText);
		FWidgetTransition Transition;
		Transition.Widget = Widget;
		Transition.WidgetProperty = TEXT("CounterValue");
		Transition.FromValue = UWidgetTransitionFunctionLibrary::MakeFloatTransitionValue(0.0f);
		Transition.ToValue = UWidgetTransitionFunctionLibrary::MakeFloatTransitionValue(100.0f);
		Transition.Time = 60.0f;
		Transition.bBound = Transition.PropertyBinding.Resolve(Widget, TEXT("CounterValue"));
		TestTrue(FString::Printf(TEXT("CounterValue resolves for external widget %d"), Index), Transition.bBound);
		AddTransitionWithCallbacks(Subsystem, MoveTemp(Transition), {});
		Widgets.Add(Widget);
		TextBlocks.Add(TextBlock);
	}

	Subsystem->TickTransitionsForTesting(DeltaTime);
	volatile int32 TextLengthSink = 0;
	const double StartTime = FPlatformTime::Seconds();
	for (int32 FrameIndex = 0; FrameIndex < FrameCount; ++FrameIndex)
	{
		Subsystem->TickTransitionsForTesting(DeltaTime);
		for (UTextBlock* TextBlock : TextBlocks)
		{
			TextLengthSink += TextBlock->TextDelegate.Execute().ToString().Len();
		}
	}
	const double ElapsedSeconds = FPlatformTime::Seconds() - StartTime;
	AddInfo(FString::Printf(TEXT("%d external CounterValue bindings plus UTextBlock text pulls: %s / frame, %s / transition"), TransitionCount, *FormatMicroseconds(ElapsedSeconds / FrameCount), *FormatMicroseconds(ElapsedSeconds / (FrameCount * TransitionCount))));
	TestTrue(TEXT("Text binding result is consumed"), TextLengthSink > 0);
	TestEqual(TEXT("All external text-binding transitions remain active"), Subsystem->Transitions.Num(), TransitionCount);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWidgetTransitionFieldNotifyTextBindingPerformanceTest, "UMGTransitions.WidgetTransition.Performance.FieldNotifyTextBinding", EAutomationTestFlags::EditorContext | EAutomationTestFlags::PerfFilter)
bool FWidgetTransitionFieldNotifyTextBindingPerformanceTest::RunTest(const FString&)
{
	constexpr int32 TransitionCount = 100;
	constexpr int32 FrameCount = 300;
	constexpr float DeltaTime = 1.0f / 60.0f;
	UWidgetTransitionSubsystem* Subsystem = NewObject<UWidgetTransitionSubsystem>(GetTransientPackage());
	TArray<UTextBlock*> TextBlocks;
	TSharedRef<int32> NotificationCount = MakeShared<int32>(0);
	TextBlocks.Reserve(TransitionCount);
	for (int32 Index = 0; Index < TransitionCount; ++Index)
	{
		UWidgetTransitionTestCounterUserWidget* Widget = NewObject<UWidgetTransitionTestCounterUserWidget>(GetTransientPackage());
		UTextBlock* TextBlock = NewObject<UTextBlock>(GetTransientPackage());
		const UE::FieldNotification::FFieldId FieldId = Widget->GetFieldNotificationDescriptor().GetField(Widget->GetClass(), GET_MEMBER_NAME_CHECKED(UWidgetTransitionTestCounterUserWidget, CounterValue));
		TestTrue(FString::Printf(TEXT("CounterValue FieldNotify resolves for external widget %d"), Index), FieldId.IsValid());
		Widget->AddFieldValueChangedDelegate(FieldId, INotifyFieldValueChanged::FFieldValueChangedDelegate::CreateLambda([TextBlock, NotificationCount](UObject* Object, UE::FieldNotification::FFieldId)
		{
			++*NotificationCount;
			if (const UWidgetTransitionTestCounterUserWidget* CounterWidget = Cast<UWidgetTransitionTestCounterUserWidget>(Object))
			{
				TextBlock->SetText(FText::AsNumber(FMath::RoundToInt(CounterWidget->CounterValue)));
			}
		}));
		FWidgetTransition Transition;
		Transition.Widget = Widget;
		Transition.WidgetProperty = TEXT("CounterValue");
		Transition.FromValue = UWidgetTransitionFunctionLibrary::MakeFloatTransitionValue(0.0f);
		Transition.ToValue = UWidgetTransitionFunctionLibrary::MakeFloatTransitionValue(100.0f);
		Transition.Time = 60.0f;
		Transition.UpdateInterval = 1.0f / 30.0f;
		Transition.bBound = Transition.PropertyBinding.Resolve(Widget, TEXT("CounterValue"));
		TestTrue(FString::Printf(TEXT("CounterValue resolves for FieldNotify widget %d"), Index), Transition.bBound);
		AddTransitionWithCallbacks(Subsystem, MoveTemp(Transition), {});
		TextBlocks.Add(TextBlock);
	}

	Subsystem->TickTransitionsForTesting(DeltaTime);
	for (FWidgetTransitionUpdateState& UpdateState : Subsystem->CallbackStore.UpdateStates)
	{
		UpdateState.UpdateElapsed = 0.0f;
	}
	*NotificationCount = 0;
	const double StartTime = FPlatformTime::Seconds();
	for (int32 FrameIndex = 0; FrameIndex < FrameCount; ++FrameIndex)
	{
		Subsystem->TickTransitionsForTesting(DeltaTime);
	}
	const double ElapsedSeconds = FPlatformTime::Seconds() - StartTime;
	int32 TextLengthSink = 0;
	for (UTextBlock* TextBlock : TextBlocks)
	{
		TextLengthSink += TextBlock->GetText().ToString().Len();
	}
	AddInfo(FString::Printf(TEXT("%d CounterValue FieldNotify pushes to UTextBlock: %s / frame, %s / transition"), TransitionCount, *FormatMicroseconds(ElapsedSeconds / FrameCount), *FormatMicroseconds(ElapsedSeconds / (FrameCount * TransitionCount))));
	TestEqual(TEXT("FieldNotify broadcasts at the configured thirty-hertz interval"), *NotificationCount, TransitionCount * (FrameCount / 2));
	TestTrue(TEXT("FieldNotify text binding result is consumed"), TextLengthSink > 0);
	TestEqual(TEXT("All FieldNotify text-binding transitions remain active"), Subsystem->Transitions.Num(), TransitionCount);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWidgetTransitionAsyncTextCounterPerformanceTest, "UMGTransitions.WidgetTransition.Performance.AsyncTextCounter", EAutomationTestFlags::EditorContext | EAutomationTestFlags::PerfFilter)
bool FWidgetTransitionAsyncTextCounterPerformanceTest::RunTest(const FString&)
{
	constexpr int32 ActionCount = 100;
	constexpr int32 FrameCount = 300;
	TArray<UTextBlock*> PlainTextWidgets;
	TArray<UTextBlock*> TextWidgets;
	TArray<UWidgetTransitionAsyncAction*> Actions;
	TArray<UWidgetTransitionTestEventReceiver*> Receivers;
	PlainTextWidgets.Reserve(ActionCount);
	TextWidgets.Reserve(ActionCount);
	Actions.Reserve(ActionCount);
	Receivers.Reserve(ActionCount);
	for (int32 Index = 0; Index < ActionCount; ++Index)
	{
		PlainTextWidgets.Add(NewObject<UTextBlock>(GetTransientPackage()));
		UTextBlock* TextWidget = NewObject<UTextBlock>(GetTransientPackage());
		UWidgetTransitionAsyncAction* Action = NewObject<UWidgetTransitionAsyncAction>(GetTransientPackage());
		UWidgetTransitionTestEventReceiver* Receiver = NewObject<UWidgetTransitionTestEventReceiver>(GetTransientPackage());
		FWidgetTransition Transition;
		Transition.Widget = TextWidget;
		Transition.FromValue = UWidgetTransitionFunctionLibrary::MakeFloatTransitionValue(0.0f);
		Transition.ToValue = UWidgetTransitionFunctionLibrary::MakeFloatTransitionValue(100.0f);
		Transition.bUseFrom = true;
		Action->Updated.AddDynamic(Receiver, &UWidgetTransitionTestEventReceiver::HandleAsyncUpdated);
		Receiver->CounterText = TextWidget;
		TestTrue(FString::Printf(TEXT("Async text counter %d initializes"), Index), Action->InitializeUpdateForTesting(MoveTemp(Transition)));
		TextWidgets.Add(TextWidget);
		Actions.Add(Action);
		Receivers.Add(Receiver);
	}

	for (UTextBlock* TextWidget : PlainTextWidgets)
	{
		TextWidget->SetText(FText::AsNumber(50));
	}
	const double PlainStartTime = FPlatformTime::Seconds();
	for (int32 FrameIndex = 0; FrameIndex < FrameCount; ++FrameIndex)
	{
		const float Progress = FrameIndex == FrameCount - 1 ? 1.0f : static_cast<float>(FrameIndex % 101) / 100.0f;
		const int32 Value = FMath::RoundToInt(Progress * 100.0f);
		for (UTextBlock* TextWidget : PlainTextWidgets)
		{
			TextWidget->SetText(FText::AsNumber(Value));
		}
	}
	const double PlainElapsedSeconds = FPlatformTime::Seconds() - PlainStartTime;

	for (int32 Index = 0; Index < ActionCount; ++Index)
	{
		Actions[Index]->DispatchUpdatedForTesting(UWidgetTransitionFunctionLibrary::MakeFloatTransitionValue(50.0f));
		Receivers[Index]->AsyncValueUpdateCount = 0;
	}
	const double StartTime = FPlatformTime::Seconds();
	for (int32 FrameIndex = 0; FrameIndex < FrameCount; ++FrameIndex)
	{
		const float Progress = FrameIndex == FrameCount - 1 ? 1.0f : static_cast<float>(FrameIndex % 101) / 100.0f;
		for (int32 Index = 0; Index < ActionCount; ++Index)
		{
			Actions[Index]->DispatchUpdatedForTesting(UWidgetTransitionFunctionLibrary::MakeFloatTransitionValue(Progress * 100.0f));
		}
	}
	const double ElapsedSeconds = FPlatformTime::Seconds() - StartTime;
	AddInfo(FString::Printf(TEXT("%d plain text counters: %s / frame, %s / counter"), ActionCount, *FormatMicroseconds(PlainElapsedSeconds / FrameCount), *FormatMicroseconds(PlainElapsedSeconds / (FrameCount * ActionCount))));
	AddInfo(FString::Printf(TEXT("%d async text counters without Widget Property binding: %s / frame, %s / counter"), ActionCount, *FormatMicroseconds(ElapsedSeconds / FrameCount), *FormatMicroseconds(ElapsedSeconds / (FrameCount * ActionCount))));
	AddInfo(FString::Printf(TEXT("Async path overhead over plain text counter: %s / frame, %s / counter"), *FormatMicroseconds((ElapsedSeconds - PlainElapsedSeconds) / FrameCount), *FormatMicroseconds((ElapsedSeconds - PlainElapsedSeconds) / (FrameCount * ActionCount))));
	for (int32 Index = 0; Index < ActionCount; ++Index)
	{
		TestEqual(FString::Printf(TEXT("Plain text counter value %d"), Index), PlainTextWidgets[Index]->GetText().ToString(), FString::FromInt(100));
	}
	for (int32 Index = 0; Index < ActionCount; ++Index)
	{
		TestEqual(FString::Printf(TEXT("Async text counter update count %d"), Index), Receivers[Index]->AsyncValueUpdateCount, FrameCount);
		TestEqual(FString::Printf(TEXT("Async text counter value %d"), Index), TextWidgets[Index]->GetText().ToString(), FString::FromInt(100));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWidgetTransitionModeMatrixPerformanceTest, "UMGTransitions.WidgetTransition.Performance.ModeMatrix", EAutomationTestFlags::EditorContext | EAutomationTestFlags::PerfFilter)
bool FWidgetTransitionModeMatrixPerformanceTest::RunTest(const FString&)
{
	constexpr int32 FrameCount = 300;
	constexpr int32 TransitionCounts[] = { 100, 500 };
	constexpr EWidgetTransitionBenchmarkMode Modes[] = { EWidgetTransitionBenchmarkMode::Linear, EWidgetTransitionBenchmarkMode::Easing, EWidgetTransitionBenchmarkMode::Spring };
	constexpr float DeltaTime = 1.0f / 60.0f;
	const FName CurveRow(TEXT("PerformanceEase"));
	UCurveTable* CurveTable = NewObject<UCurveTable>(GetTransientPackage());
	FRichCurve& EasingCurve = CurveTable->AddRichCurve(CurveRow);
	EasingCurve.AddKey(0.0f, 0.0f);
	EasingCurve.AddKey(0.5f, 0.2f);
	EasingCurve.AddKey(1.0f, 1.0f);

	for (const int32 TransitionCount : TransitionCounts)
	{
		for (const EWidgetTransitionBenchmarkMode Mode : Modes)
		{
			for (const bool bBound : { false, true })
			{
				UWidgetTransitionSubsystem* Subsystem = NewObject<UWidgetTransitionSubsystem>(GetTransientPackage());
				TArray<UImage*> Widgets;
				Widgets.Reserve(TransitionCount);
				for (int32 Index = 0; Index < TransitionCount; ++Index)
				{
					UImage* Widget = NewObject<UImage>(GetTransientPackage());
					Widgets.Add(Widget);
					FWidgetTransition Transition = MakeBenchmarkTransition(Widget, bBound, Mode, CurveTable, CurveRow);
					TestTrue(FString::Printf(TEXT("Transition %d is initialized"), Index), !bBound || Transition.bBound);
					const int32 TransitionIndex = Subsystem->Transitions.Emplace(MoveTemp(Transition));
					FWidgetTransition& StoredTransition = Subsystem->Transitions[TransitionIndex];
					if (Mode == EWidgetTransitionBenchmarkMode::Spring)
					{
						StoredTransition.SpringIndex = Subsystem->Springs.Emplace(1.0f, 1.0f);
						Subsystem->SpringTransitionIndices.Add(TransitionIndex);
						Subsystem->Springs[StoredTransition.SpringIndex].Start(StoredTransition.FromValue.Channels, StoredTransition.ToValue.Channels);
					}
				}

				Subsystem->TickTransitionsForTesting(DeltaTime);
				const double StartTime = FPlatformTime::Seconds();
				for (int32 FrameIndex = 0; FrameIndex < FrameCount; ++FrameIndex)
				{
					Subsystem->TickTransitionsForTesting(DeltaTime);
				}
				const double ElapsedSeconds = FPlatformTime::Seconds() - StartTime;
				const double SecondsPerFrame = ElapsedSeconds / FrameCount;
				const double SecondsPerTransition = ElapsedSeconds / (FrameCount * TransitionCount);
				AddInfo(FString::Printf(TEXT("%d %s, %s binding: %s / frame, %s / transition (%d frames)"), TransitionCount, GetBenchmarkModeName(Mode), bBound ? TEXT("with") : TEXT("without"), *FormatMicroseconds(SecondsPerFrame), *FormatMicroseconds(SecondsPerTransition), FrameCount));
				TestEqual(FString::Printf(TEXT("All %d %s transitions remain active"), TransitionCount, GetBenchmarkModeName(Mode)), Subsystem->Transitions.Num(), TransitionCount);
			}
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWidgetTransitionFastBindingPerformanceTest, "UMGTransitions.WidgetTransition.Performance.FastBindings", EAutomationTestFlags::EditorContext | EAutomationTestFlags::PerfFilter)
bool FWidgetTransitionFastBindingPerformanceTest::RunTest(const FString&)
{
	constexpr int32 TransitionCount = 500;
	constexpr int32 FrameCount = 300;
	constexpr float DeltaTime = 1.0f / 60.0f;

	auto Measure = [this, TransitionCount, FrameCount](FName PropertyName, bool bVector)
	{
		UWidgetTransitionSubsystem* Subsystem = NewObject<UWidgetTransitionSubsystem>(GetTransientPackage());
		TArray<UImage*> Widgets;
		Widgets.Reserve(TransitionCount);
		const FWidgetTransitionValue FromValue = bVector ? UWidgetTransitionFunctionLibrary::MakeVectorTransitionValue(FVector2D::ZeroVector) : UWidgetTransitionFunctionLibrary::MakeFloatTransitionValue(0.0f);
		const FWidgetTransitionValue ToValue = bVector ? UWidgetTransitionFunctionLibrary::MakeVectorTransitionValue(FVector2D(100.0f, -50.0f)) : UWidgetTransitionFunctionLibrary::MakeFloatTransitionValue(1.0f);
		for (int32 Index = 0; Index < TransitionCount; ++Index)
		{
			UImage* Widget = NewObject<UImage>(GetTransientPackage());
			Widgets.Add(Widget);
			FWidgetTransition Transition;
			Transition.Widget = Widget;
			Transition.WidgetProperty = PropertyName;
			Transition.FromValue = FromValue;
			Transition.ToValue = ToValue;
			Transition.Time = 60.0f;
			Transition.bUseFrom = true;
			Transition.bBound = Transition.PropertyBinding.Resolve(Widget, PropertyName.ToString());
			TestTrue(FString::Printf(TEXT("%s resolves for transition %d"), *PropertyName.ToString(), Index), Transition.bBound);
			Subsystem->Transitions.Emplace(MoveTemp(Transition));
		}

		Subsystem->TickTransitionsForTesting(DeltaTime);
		const double StartTime = FPlatformTime::Seconds();
		for (int32 FrameIndex = 0; FrameIndex < FrameCount; ++FrameIndex)
		{
			Subsystem->TickTransitionsForTesting(DeltaTime);
		}
		const double ElapsedSeconds = FPlatformTime::Seconds() - StartTime;
		AddInfo(FString::Printf(TEXT("500 %s transitions: %s / frame, %s / transition (%d frames)"), *PropertyName.ToString(), *FormatMicroseconds(ElapsedSeconds / FrameCount), *FormatMicroseconds(ElapsedSeconds / (FrameCount * TransitionCount)), FrameCount));
		TestEqual(FString::Printf(TEXT("All %s transitions remain active"), *PropertyName.ToString()), Subsystem->Transitions.Num(), TransitionCount);
	};

	Measure(TEXT("RenderOpacity"), false);
	Measure(TEXT("RenderTransform.Translation"), true);
	Measure(TEXT("RenderTransform.Scale"), true);
	Measure(TEXT("RenderTransform.Shear"), true);
	Measure(TEXT("RenderTransform.Angle"), false);
	Measure(TEXT("RenderTransformPivot"), true);
	return true;
}

#endif
