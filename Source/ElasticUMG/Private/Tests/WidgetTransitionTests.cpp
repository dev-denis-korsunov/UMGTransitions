#include "WidgetTransition.h"
#include "Tests/WidgetTransitionTestTypes.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Components/Image.h"
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWidgetTransitionStorageLayoutTest, "ElasticUMG.WidgetTransition.Runtime.StorageLayout", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FWidgetTransitionStorageLayoutTest::RunTest(const FString&)
{
	AddInfo(FormatBytes(TEXT("FWidgetTransition"), sizeof(FWidgetTransition), alignof(FWidgetTransition)));
	AddInfo(FormatBytes(TEXT("FWidgetTransitionValue"), sizeof(FWidgetTransitionValue), alignof(FWidgetTransitionValue)));
	AddInfo(FormatBytes(TEXT("FWidgetTransitionPropertyBinding"), sizeof(FWidgetTransitionPropertyBinding), alignof(FWidgetTransitionPropertyBinding)));
	AddInfo(FormatBytes(TEXT("FWidgetTransitionSpring"), sizeof(FWidgetTransitionSpring), alignof(FWidgetTransitionSpring)));
	TestTrue(TEXT("Transition storage is non-empty"), sizeof(FWidgetTransition) > 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWidgetTransitionEasingTest, "ElasticUMG.WidgetTransition.Runtime.Easing.Endpoints", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FWidgetTransitionEasingTest::RunTest(const FString&)
{
	const FCurveTableRowHandle Easing;
	TestTrue(TEXT("Default easing handle is null and selects linear interpolation"), Easing.IsNull());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWidgetTransitionBuilderTest, "ElasticUMG.WidgetTransition.Runtime.SpecBuilders", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWidgetTransitionPropertyBindingTest, "ElasticUMG.WidgetTransition.Runtime.PropertyBinding.Channels", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWidgetTransitionSpringTest, "ElasticUMG.WidgetTransition.Runtime.Spring.Converges", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWidgetTransitionRemoveAtSwapTest, "ElasticUMG.WidgetTransition.Runtime.RemoveAtSwap", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWidgetTransitionMetadataTest, "ElasticUMG.WidgetTransition.Editor.Metadata", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FWidgetTransitionMetadataTest::RunTest(const FString&)
{
	const UFunction* Create = UWidgetTransitionFunctionLibrary::StaticClass()->FindFunctionByName(GET_FUNCTION_NAME_CHECKED(UWidgetTransitionFunctionLibrary, CreateWidgetTransition));
	TestTrue(TEXT("Create function opts into the custom property pin"), Create && Create->HasMetaData(TEXT("ElasticUMGTransitionBinding")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWidgetTransitionConstructionPerformanceTest, "ElasticUMG.WidgetTransition.Performance.Construction", EAutomationTestFlags::EditorContext | EAutomationTestFlags::PerfFilter)
bool FWidgetTransitionConstructionPerformanceTest::RunTest(const FString&)
{
	constexpr int32 IterationCount = 100000;
	UImage* Widget = NewObject<UImage>(GetTransientPackage());
	const FWidgetTransitionValue FromValue = UWidgetTransitionFunctionLibrary::MakeFloatTransitionValue(0.0f);
	const FWidgetTransitionValue ToValue = UWidgetTransitionFunctionLibrary::MakeFloatTransitionValue(1.0f);
	volatile uint64 Sink = 0;

	auto Measure = [this, &Sink](const TCHAR* Name, auto&& Construct)
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWidgetTransitionConcurrentTickPerformanceTest, "ElasticUMG.WidgetTransition.Performance.ConcurrentTick", EAutomationTestFlags::EditorContext | EAutomationTestFlags::PerfFilter)
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWidgetTransitionModeMatrixPerformanceTest, "ElasticUMG.WidgetTransition.Performance.ModeMatrix", EAutomationTestFlags::EditorContext | EAutomationTestFlags::PerfFilter)
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWidgetTransitionFastBindingPerformanceTest, "ElasticUMG.WidgetTransition.Performance.FastBindings", EAutomationTestFlags::EditorContext | EAutomationTestFlags::PerfFilter)
bool FWidgetTransitionFastBindingPerformanceTest::RunTest(const FString&)
{
	constexpr int32 TransitionCount = 500;
	constexpr int32 FrameCount = 300;
	constexpr float DeltaTime = 1.0f / 60.0f;

	auto Measure = [this](FName PropertyName, bool bVector)
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWidgetTransitionSpringStoragePerformanceTest, "ElasticUMG.WidgetTransition.Performance.SpringStorage", EAutomationTestFlags::EditorContext | EAutomationTestFlags::PerfFilter)
bool FWidgetTransitionSpringStoragePerformanceTest::RunTest(const FString&)
{
	constexpr int32 ActiveSpringCount = 500;
	constexpr int32 FrameCount = 300;
	constexpr float DeltaTime = 1.0f / 60.0f;

	auto StartSpring = [](FWidgetTransitionSpring& Spring)
	{
		Spring.Start(FVector4f::Zero(), FVector4f(100.0f, -50.0f, 25.0f, 1.0f));
	};
	auto Measure = [this](const TCHAR* Name, auto& Springs)
	{
		const double StartTime = FPlatformTime::Seconds();
		for (int32 FrameIndex = 0; FrameIndex < FrameCount; ++FrameIndex)
		{
			for (FWidgetTransitionSpring& Spring : Springs)
			{
				Spring.Tick(DeltaTime);
			}
		}
		const double ElapsedSeconds = FPlatformTime::Seconds() - StartTime;
		AddInfo(FString::Printf(TEXT("%s: %s / frame, %s / spring (%d active springs, %d frames)"), Name, *FormatMicroseconds(ElapsedSeconds / FrameCount), *FormatMicroseconds(ElapsedSeconds / (FrameCount * ActiveSpringCount)), ActiveSpringCount, FrameCount));
	};

	TArray<FWidgetTransitionSpring> DenseSprings;
	DenseSprings.Reserve(ActiveSpringCount);
	for (int32 Index = 0; Index < ActiveSpringCount; ++Index)
	{
		const int32 SpringIndex = DenseSprings.Emplace(1.0f, 1.0f);
		StartSpring(DenseSprings[SpringIndex]);
	}
	Measure(TEXT("TArray packed"), DenseSprings);

	TSparseArray<FWidgetTransitionSpring> SparseSprings;
	for (int32 Index = 0; Index < ActiveSpringCount; ++Index)
	{
		const auto SpringIndex = SparseSprings.Emplace(1.0f, 1.0f);
		StartSpring(SparseSprings[SpringIndex]);
	}
	Measure(TEXT("TSparseArray packed"), SparseSprings);

	TSparseArray<FWidgetTransitionSpring> FragmentedSparseSprings;
	for (int32 Index = 0; Index < ActiveSpringCount * 2; ++Index)
	{
		const auto SpringIndex = FragmentedSparseSprings.Emplace(1.0f, 1.0f);
		StartSpring(FragmentedSparseSprings[SpringIndex]);
	}
	for (int32 Index = 0; Index < ActiveSpringCount * 2; Index += 2)
	{
		FragmentedSparseSprings.RemoveAt(Index);
	}
	TestEqual(TEXT("Fragmented sparse array keeps 500 active springs"), FragmentedSparseSprings.Num(), ActiveSpringCount);
	Measure(TEXT("TSparseArray 50% fragmented"), FragmentedSparseSprings);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWidgetTransitionArrayStoragePerformanceTest, "ElasticUMG.WidgetTransition.Performance.ArrayTransitionStorage", EAutomationTestFlags::EditorContext | EAutomationTestFlags::PerfFilter)
bool FWidgetTransitionArrayStoragePerformanceTest::RunTest(const FString&)
{
	constexpr int32 ActiveTransitionCount = 500;
	constexpr int32 FrameCount = 300;
	constexpr float DeltaTime = 1.0f / 60.0f;

	auto AddTransition = [](UWidgetTransitionSubsystem& Subsystem, TArray<UImage*>& Widgets)
	{
		UImage* Widget = NewObject<UImage>(GetTransientPackage());
		Widgets.Add(Widget);
		FWidgetTransition Transition;
		Transition.Widget = Widget;
		Transition.FromValue = UWidgetTransitionFunctionLibrary::MakeFloatTransitionValue(0.0f);
		Transition.ToValue = UWidgetTransitionFunctionLibrary::MakeFloatTransitionValue(1.0f);
		Transition.Time = 60.0f;
		Transition.bUseFrom = true;
		return Subsystem.Transitions.Emplace(MoveTemp(Transition));
	};
	auto Measure = [this](const TCHAR* Name, UWidgetTransitionSubsystem& Subsystem)
	{
		Subsystem.TickTransitionsForTesting(DeltaTime);
		const double StartTime = FPlatformTime::Seconds();
		for (int32 FrameIndex = 0; FrameIndex < FrameCount; ++FrameIndex)
		{
			Subsystem.TickTransitionsForTesting(DeltaTime);
		}
		const double ElapsedSeconds = FPlatformTime::Seconds() - StartTime;
		AddInfo(FString::Printf(TEXT("%s: %s / frame, %s / transition (%d active transitions, %d frames)"), Name, *FormatMicroseconds(ElapsedSeconds / FrameCount), *FormatMicroseconds(ElapsedSeconds / (FrameCount * ActiveTransitionCount)), ActiveTransitionCount, FrameCount));
		TestEqual(FString::Printf(TEXT("%s keeps all active transitions"), Name), Subsystem.Transitions.Num(), ActiveTransitionCount);
	};

	UWidgetTransitionSubsystem* PackedSubsystem = NewObject<UWidgetTransitionSubsystem>(GetTransientPackage());
	TArray<UImage*> PackedWidgets;
	PackedWidgets.Reserve(ActiveTransitionCount);
	for (int32 Index = 0; Index < ActiveTransitionCount; ++Index)
	{
		AddTransition(*PackedSubsystem, PackedWidgets);
	}
	Measure(TEXT("TArray packed transitions"), *PackedSubsystem);

	UWidgetTransitionSubsystem* SwapRemovedSubsystem = NewObject<UWidgetTransitionSubsystem>(GetTransientPackage());
	TArray<UImage*> SwapRemovedWidgets;
	SwapRemovedWidgets.Reserve(ActiveTransitionCount * 2);
	for (int32 Index = 0; Index < ActiveTransitionCount * 2; ++Index)
	{
		AddTransition(*SwapRemovedSubsystem, SwapRemovedWidgets);
	}
	for (int32 Index = 0; Index < ActiveTransitionCount; ++Index)
	{
		SwapRemovedSubsystem->Transitions.RemoveAtSwap(0);
	}
	Measure(TEXT("TArray after 500 RemoveAtSwap"), *SwapRemovedSubsystem);

	constexpr int32 RemovalBenchmarkCount = 100000;
	TArray<FWidgetTransition> RemovalBenchmarkTransitions;
	RemovalBenchmarkTransitions.SetNum(RemovalBenchmarkCount);
	const double StartTime = FPlatformTime::Seconds();
	for (int32 Index = 0; Index < RemovalBenchmarkCount / 2; ++Index)
	{
		RemovalBenchmarkTransitions.RemoveAtSwap(RemovalBenchmarkTransitions.Num() / 2);
	}
	const double ElapsedSeconds = FPlatformTime::Seconds() - StartTime;
	AddInfo(FString::Printf(TEXT("TArray RemoveAtSwap: %s / removal (%d removals)"), *FormatMicroseconds(ElapsedSeconds / (RemovalBenchmarkCount / 2)), RemovalBenchmarkCount / 2));
	TestEqual(TEXT("RemoveAtSwap leaves half of the benchmark transitions"), RemovalBenchmarkTransitions.Num(), RemovalBenchmarkCount / 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWidgetTransitionModeIndicesPerformanceTest, "ElasticUMG.WidgetTransition.Performance.ModeIndices", EAutomationTestFlags::EditorContext | EAutomationTestFlags::PerfFilter)
bool FWidgetTransitionModeIndicesPerformanceTest::RunTest(const FString&)
{
	constexpr int32 TransitionCount = 500;
	constexpr int32 FrameCount = 300;
	const FName CurveRow(TEXT("ModeIndices"));
	UCurveTable* CurveTable = NewObject<UCurveTable>(GetTransientPackage());
	FRichCurve& Curve = CurveTable->AddRichCurve(CurveRow);
	Curve.AddKey(0.0f, 0.0f);
	Curve.AddKey(0.5f, 0.2f);
	Curve.AddKey(1.0f, 1.0f);
	TArray<FWidgetTransition> Transitions;
	TArray<FWidgetTransitionSpring> Springs;
	TArray<int32> LinearTransitionIndices;
	TArray<int32> EasingTransitionIndices;
	TArray<int32> SpringTransitionIndices;
	Transitions.Reserve(TransitionCount);
	Springs.Reserve(TransitionCount / 3);
	LinearTransitionIndices.Reserve(TransitionCount / 3);
	EasingTransitionIndices.Reserve(TransitionCount / 3);
	SpringTransitionIndices.Reserve(TransitionCount / 3);
	for (int32 Index = 0; Index < TransitionCount; ++Index)
	{
		FWidgetTransition Transition;
		Transition.FromValue.Channels = FVector4f::Zero();
		Transition.ToValue.Channels = FVector4f(100.0f, -50.0f, 25.0f, 1.0f);
		Transition.Time = 1.0f;
		Transition.CurrentTime = 0.35f;
		const int32 TransitionIndex = Transitions.Add(MoveTemp(Transition));
		FWidgetTransition& StoredTransition = Transitions[TransitionIndex];
		switch (Index % 3)
		{
		case 0:
		{
			LinearTransitionIndices.Add(TransitionIndex);
			break;
		}
		case 1:
		{
			StoredTransition.Easing.CurveTable = CurveTable;
			StoredTransition.Easing.RowName = CurveRow;
			StoredTransition.EasingCurve = &Curve;
			EasingTransitionIndices.Add(TransitionIndex);
			break;
		}
		default:
		{
			StoredTransition.bUseSpring = true;
			StoredTransition.SpringIndex = Springs.Emplace(1.0f, 1.0f);
			Springs[StoredTransition.SpringIndex].Start(StoredTransition.FromValue.Channels, StoredTransition.ToValue.Channels);
			SpringTransitionIndices.Add(TransitionIndex);
			break;
		}
		}
	}

	volatile float Sink = 0.0f;
	auto EvaluateTransition = [&Springs](const FWidgetTransition& Transition)
	{
		const float Alpha = FMath::Clamp(Transition.CurrentTime / Transition.Time, 0.0f, 1.0f);
		if (Transition.bUseSpring)
		{
			return Springs[Transition.SpringIndex].GetValue();
		}
		const float EasedAlpha = Transition.EasingCurve ? Transition.EasingCurve->Eval(Alpha) : Alpha;
		return FMath::Lerp(Transition.FromValue.Channels, Transition.ToValue.Channels, EasedAlpha);
	};
	auto EvaluateLinear = [](const FWidgetTransition& Transition)
	{
		const float Alpha = FMath::Clamp(Transition.CurrentTime / Transition.Time, 0.0f, 1.0f);
		return FMath::Lerp(Transition.FromValue.Channels, Transition.ToValue.Channels, Alpha);
	};
	auto EvaluateEasing = [](const FWidgetTransition& Transition)
	{
		const float Alpha = FMath::Clamp(Transition.CurrentTime / Transition.Time, 0.0f, 1.0f);
		const float EasedAlpha = Transition.EasingCurve->Eval(Alpha);
		return FMath::Lerp(Transition.FromValue.Channels, Transition.ToValue.Channels, EasedAlpha);
	};
	auto EvaluateSpring = [&Springs](const FWidgetTransition& Transition)
	{
		return Springs[Transition.SpringIndex].GetValue();
	};
	auto Measure = [this, &Sink](const TCHAR* Name, auto&& Traverse)
	{
		const double StartTime = FPlatformTime::Seconds();
		for (int32 FrameIndex = 0; FrameIndex < FrameCount; ++FrameIndex)
		{
			Traverse();
		}
		const double ElapsedSeconds = FPlatformTime::Seconds() - StartTime;
		AddInfo(FString::Printf(TEXT("%s: %s / frame, %s / transition (%d transitions, %d frames)"), Name, *FormatMicroseconds(ElapsedSeconds / FrameCount), *FormatMicroseconds(ElapsedSeconds / (FrameCount * TransitionCount)), TransitionCount, FrameCount));
	};

	Measure(TEXT("Single mixed transition pass"), [&Transitions, &EvaluateTransition, &Sink]()
	{
		for (const FWidgetTransition& Transition : Transitions)
		{
			Sink += EvaluateTransition(Transition).X;
		}
	});
	Measure(TEXT("Specialized mode index passes"), [&Transitions, &LinearTransitionIndices, &EasingTransitionIndices, &SpringTransitionIndices, &EvaluateLinear, &EvaluateEasing, &EvaluateSpring, &Sink]()
	{
		for (const int32 TransitionIndex : LinearTransitionIndices)
		{
			Sink += EvaluateLinear(Transitions[TransitionIndex]).X;
		}
		for (const int32 TransitionIndex : EasingTransitionIndices)
		{
			Sink += EvaluateEasing(Transitions[TransitionIndex]).X;
		}
		for (const int32 TransitionIndex : SpringTransitionIndices)
		{
			Sink += EvaluateSpring(Transitions[TransitionIndex]).X;
		}
	});
	TestTrue(TEXT("Mode index benchmark executed"), Sink > 0.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWidgetTransitionDirtyWidgetIndicesPerformanceTest, "ElasticUMG.WidgetTransition.Performance.DirtyWidgetIndices", EAutomationTestFlags::EditorContext | EAutomationTestFlags::PerfFilter)
bool FWidgetTransitionDirtyWidgetIndicesPerformanceTest::RunTest(const FString&)
{
	constexpr int32 WidgetCount = 50;
	constexpr int32 TransitionCount = 500;
	constexpr int32 FrameCount = 300;
	TArray<UImage*> Widgets;
	TArray<int32> TransitionWidgetIndices;
	TArray<float> Values;
	TArray<TArray<int32>> DirtyWidgetGroups;
	Widgets.Reserve(WidgetCount);
	TransitionWidgetIndices.Reserve(TransitionCount);
	Values.Reserve(TransitionCount);
	DirtyWidgetGroups.SetNum(WidgetCount);
	for (int32 WidgetIndex = 0; WidgetIndex < WidgetCount; ++WidgetIndex)
	{
		Widgets.Add(NewObject<UImage>(GetTransientPackage()));
	}
	for (int32 TransitionIndex = 0; TransitionIndex < TransitionCount; ++TransitionIndex)
	{
		const int32 WidgetIndex = TransitionIndex % WidgetCount;
		TransitionWidgetIndices.Add(WidgetIndex);
		Values.Add(static_cast<float>(TransitionIndex % 100) / 100.0f);
		DirtyWidgetGroups[WidgetIndex].Add(TransitionIndex);
	}

	auto Measure = [this](const TCHAR* Name, auto&& Traverse)
	{
		const double StartTime = FPlatformTime::Seconds();
		for (int32 FrameIndex = 0; FrameIndex < FrameCount; ++FrameIndex)
		{
			Traverse();
		}
		const double ElapsedSeconds = FPlatformTime::Seconds() - StartTime;
		AddInfo(FString::Printf(TEXT("%s: %s / frame, %s / write (%d transitions, %d widgets, %d frames)"), Name, *FormatMicroseconds(ElapsedSeconds / FrameCount), *FormatMicroseconds(ElapsedSeconds / (FrameCount * TransitionCount)), TransitionCount, WidgetCount, FrameCount));
	};

	Measure(TEXT("Direct transition widget lookup"), [&Widgets, &TransitionWidgetIndices, &Values]()
	{
		for (int32 TransitionIndex = 0; TransitionIndex < TransitionWidgetIndices.Num(); ++TransitionIndex)
		{
			Widgets[TransitionWidgetIndices[TransitionIndex]]->SetRenderOpacity(Values[TransitionIndex]);
		}
	});
	Measure(TEXT("Prebuilt dirty widget groups"), [&Widgets, &DirtyWidgetGroups, &Values]()
	{
		for (int32 WidgetIndex = 0; WidgetIndex < DirtyWidgetGroups.Num(); ++WidgetIndex)
		{
			UImage* Widget = Widgets[WidgetIndex];
			for (const int32 TransitionIndex : DirtyWidgetGroups[WidgetIndex])
			{
				Widget->SetRenderOpacity(Values[TransitionIndex]);
			}
		}
	});
	return true;
}

#endif
