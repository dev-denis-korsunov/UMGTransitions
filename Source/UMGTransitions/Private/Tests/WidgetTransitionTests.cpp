#include "WidgetTransition.h"
#include "WidgetSelectorLibrary.h"
#include "Tests/WidgetTransitionTestTypes.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Components/Image.h"
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
		const SIZE_T SpringBytes = bWithSprings ? sizeof(FWidgetTransitionSpring) * TransitionCount : 0;
		const SIZE_T SpringIndexBytes = bWithSprings ? sizeof(int32) * TransitionCount : 0;
		return FString::Printf(TEXT("%s (%d transitions): %llu B transition + %llu B spring + %llu B indices = %llu B"), Name, TransitionCount, static_cast<uint64>(TransitionBytes), static_cast<uint64>(SpringBytes), static_cast<uint64>(SpringIndexBytes), static_cast<uint64>(TransitionBytes + SpringBytes + SpringIndexBytes));
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWidgetTransitionStorageLayoutTest, "UMGTransitions.WidgetTransition.Diagnostics.StorageLayout", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FWidgetTransitionStorageLayoutTest::RunTest(const FString&)
{
	AddInfo(FormatBytes(TEXT("FWidgetTransition"), sizeof(FWidgetTransition), alignof(FWidgetTransition)));
	AddInfo(FormatBytes(TEXT("FWidgetTransitionValue"), sizeof(FWidgetTransitionValue), alignof(FWidgetTransitionValue)));
	AddInfo(FormatBytes(TEXT("FWidgetTransitionPropertyBinding"), sizeof(FWidgetTransitionPropertyBinding), alignof(FWidgetTransitionPropertyBinding)));
	AddInfo(FormatBytes(TEXT("FWidgetTransitionSpring"), sizeof(FWidgetTransitionSpring), alignof(FWidgetTransitionSpring)));
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
	Transition = UWidgetTransitionFunctionLibrary::Options(MoveTemp(Transition), true, true);
	Transition = UWidgetTransitionFunctionLibrary::Repeat(MoveTemp(Transition), 2);
	Transition = UWidgetTransitionFunctionLibrary::YoYo(MoveTemp(Transition));
	Transition = UWidgetTransitionFunctionLibrary::Spring(MoveTemp(Transition), 0.8f, 0.25f, true);
	TestEqual(TEXT("Target retains semantic Float type"), Transition.ToValue.Type, EWidgetTransitionValueType::Float);
	TestEqual(TEXT("From retains independent Vector2D type"), Transition.FromValue.Type, EWidgetTransitionValueType::Vector2D);
	TestTrue(TEXT("From modifier is enabled"), Transition.bUseFrom);
	TestTrue(TEXT("Options modifiers are retained"), Transition.bDeferFromValue && Transition.bIgnoreDelayOnRepeat);
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
		return UWidgetTransitionFunctionLibrary::Spring(MoveTemp(Transition), 0.65f, 0.45f, true);
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
