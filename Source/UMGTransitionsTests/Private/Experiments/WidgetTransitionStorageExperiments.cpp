#include "WidgetTransition.h"
#include "WidgetTransitionSubsystem.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Components/Image.h"
#include "Async/ParallelFor.h"
#include "HAL/PlatformTime.h"
#include "Misc/AutomationTest.h"

namespace
{
	FString FormatStorageExperimentMicroseconds(double Seconds)
	{
		return FString::Printf(TEXT("%.3f us"), Seconds * 1000000.0);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWidgetTransitionSpringStorageExperiment, "UMGTransitions.WidgetTransition.Experiments.SpringStorage", EAutomationTestFlags::EditorContext | EAutomationTestFlags::PerfFilter)
bool FWidgetTransitionSpringStorageExperiment::RunTest(const FString&)
{
	constexpr int32 ActiveSpringCount = 500;
	constexpr int32 FrameCount = 300;
	constexpr float DeltaTime = 1.0f / 60.0f;

	auto StartSpring = [](FWidgetTransitionSpring& Spring)
	{
		Spring.Start(FVector4f::Zero(), FVector4f(100.0f, -50.0f, 25.0f, 1.0f));
	};
	auto Measure = [this, ActiveSpringCount, FrameCount, DeltaTime](const TCHAR* Name, auto& Springs)
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
		AddInfo(FString::Printf(TEXT("%s: %s / frame, %s / spring (%d active springs, %d frames)"), Name, *FormatStorageExperimentMicroseconds(ElapsedSeconds / FrameCount), *FormatStorageExperimentMicroseconds(ElapsedSeconds / (FrameCount * ActiveSpringCount)), ActiveSpringCount, FrameCount));
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWidgetTransitionArrayStorageExperiment, "UMGTransitions.WidgetTransition.Experiments.ArrayTransitionStorage", EAutomationTestFlags::EditorContext | EAutomationTestFlags::PerfFilter)
bool FWidgetTransitionArrayStorageExperiment::RunTest(const FString&)
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
	auto Measure = [this, ActiveTransitionCount, FrameCount, DeltaTime](const TCHAR* Name, UWidgetTransitionSubsystem& Subsystem)
	{
		Subsystem.TickTransitionsForTesting(DeltaTime);
		const double StartTime = FPlatformTime::Seconds();
		for (int32 FrameIndex = 0; FrameIndex < FrameCount; ++FrameIndex)
		{
			Subsystem.TickTransitionsForTesting(DeltaTime);
		}
		const double ElapsedSeconds = FPlatformTime::Seconds() - StartTime;
		AddInfo(FString::Printf(TEXT("%s: %s / frame, %s / transition (%d active transitions, %d frames)"), Name, *FormatStorageExperimentMicroseconds(ElapsedSeconds / FrameCount), *FormatStorageExperimentMicroseconds(ElapsedSeconds / (FrameCount * ActiveTransitionCount)), ActiveTransitionCount, FrameCount));
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
	AddInfo(FString::Printf(TEXT("TArray RemoveAtSwap: %s / removal (%d removals)"), *FormatStorageExperimentMicroseconds(ElapsedSeconds / (RemovalBenchmarkCount / 2)), RemovalBenchmarkCount / 2));
	TestEqual(TEXT("RemoveAtSwap leaves half of the benchmark transitions"), RemovalBenchmarkTransitions.Num(), RemovalBenchmarkCount / 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWidgetTransitionSpringParallelForExperiment, "UMGTransitions.WidgetTransition.Experiments.SpringParallelFor", EAutomationTestFlags::EditorContext | EAutomationTestFlags::PerfFilter)
bool FWidgetTransitionSpringParallelForExperiment::RunTest(const FString&)
{
	constexpr int32 ParallelSpringThreshold = 256;
	constexpr int32 FrameCount = 300;
	constexpr float DeltaTime = 1.0f / 60.0f;
	constexpr int32 ActiveSpringCounts[] = { 64, 128, 256, 500, 1000 };

	auto CreateSprings = [](int32 Count)
	{
		TArray<FWidgetTransitionSpring> Springs;
		Springs.Reserve(Count);
		for (int32 Index = 0; Index < Count; ++Index)
		{
			const int32 SpringIndex = Springs.Emplace(1.0f, 0.1f);
			Springs[SpringIndex].Start(FVector4f::Zero(), FVector4f(100.0f, -50.0f, 25.0f, 1.0f));
		}
		return Springs;
	};
	auto TickSequential = [](TArray<FWidgetTransitionSpring>& Springs)
	{
		for (FWidgetTransitionSpring& Spring : Springs)
		{
			Spring.Tick(DeltaTime);
		}
	};
	auto TickParallel = [](TArray<FWidgetTransitionSpring>& Springs)
	{
		ParallelFor(Springs.Num(), [&Springs](int32 SpringIndex)
		{
			Springs[SpringIndex].Tick(DeltaTime);
		}, EParallelForFlags::Unbalanced);
	};
	auto TickThresholded = [&TickSequential, &TickParallel](TArray<FWidgetTransitionSpring>& Springs)
	{
		if (Springs.Num() < ParallelSpringThreshold)
		{
			TickSequential(Springs);
			return;
		}
		TickParallel(Springs);
	};
	auto Measure = [this](const TCHAR* Name, int32 ActiveSpringCount, TArray<FWidgetTransitionSpring>& Springs, auto&& TickPass)
	{
		const double StartTime = FPlatformTime::Seconds();
		for (int32 FrameIndex = 0; FrameIndex < FrameCount; ++FrameIndex)
		{
			TickPass(Springs);
		}
		const double ElapsedSeconds = FPlatformTime::Seconds() - StartTime;
		AddInfo(FString::Printf(TEXT("%s (%d springs): %s / frame, %s / spring"), Name, ActiveSpringCount, *FormatStorageExperimentMicroseconds(ElapsedSeconds / FrameCount), *FormatStorageExperimentMicroseconds(ElapsedSeconds / (FrameCount * ActiveSpringCount))));
		return ElapsedSeconds;
	};
	auto AreNearlyEqual = [](const FVector4f& Left, const FVector4f& Right)
	{
		return FMath::IsNearlyEqual(Left.X, Right.X) && FMath::IsNearlyEqual(Left.Y, Right.Y) && FMath::IsNearlyEqual(Left.Z, Right.Z) && FMath::IsNearlyEqual(Left.W, Right.W);
	};

	for (const int32 ActiveSpringCount : ActiveSpringCounts)
	{
		const TArray<FWidgetTransitionSpring> BaselineSprings = CreateSprings(ActiveSpringCount);
		TArray<FWidgetTransitionSpring> SequentialSprings = BaselineSprings;
		TArray<FWidgetTransitionSpring> ForcedParallelSprings = BaselineSprings;
		TArray<FWidgetTransitionSpring> ThresholdedSprings = BaselineSprings;

		const double SequentialSeconds = Measure(TEXT("Sequential dense spring pass"), ActiveSpringCount, SequentialSprings, TickSequential);
		const double ForcedParallelSeconds = Measure(TEXT("ParallelFor dense spring pass"), ActiveSpringCount, ForcedParallelSprings, TickParallel);
		const double ThresholdedSeconds = Measure(TEXT("Thresholded spring pass (256)"), ActiveSpringCount, ThresholdedSprings, TickThresholded);
		AddInfo(FString::Printf(TEXT("ParallelFor ratio (%d springs): forced %.3fx, thresholded %.3fx relative to sequential"), ActiveSpringCount, ForcedParallelSeconds / SequentialSeconds, ThresholdedSeconds / SequentialSeconds));

		for (int32 SpringIndex = 0; SpringIndex < ActiveSpringCount; ++SpringIndex)
		{
			TestTrue(FString::Printf(TEXT("Parallel spring %d matches sequential at %d active springs"), SpringIndex, ActiveSpringCount), AreNearlyEqual(SequentialSprings[SpringIndex].GetValue(), ForcedParallelSprings[SpringIndex].GetValue()));
			TestTrue(FString::Printf(TEXT("Thresholded spring %d matches sequential at %d active springs"), SpringIndex, ActiveSpringCount), AreNearlyEqual(SequentialSprings[SpringIndex].GetValue(), ThresholdedSprings[SpringIndex].GetValue()));
		}
	}
	return true;
}

#endif
