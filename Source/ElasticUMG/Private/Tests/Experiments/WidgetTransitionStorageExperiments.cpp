#include "WidgetTransition.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Components/Image.h"
#include "HAL/PlatformTime.h"
#include "Misc/AutomationTest.h"

namespace
{
	FString FormatStorageExperimentMicroseconds(double Seconds)
	{
		return FString::Printf(TEXT("%.3f us"), Seconds * 1000000.0);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWidgetTransitionSpringStorageExperiment, "ElasticUMG.WidgetTransition.Experiments.SpringStorage", EAutomationTestFlags::EditorContext | EAutomationTestFlags::PerfFilter)
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWidgetTransitionArrayStorageExperiment, "ElasticUMG.WidgetTransition.Experiments.ArrayTransitionStorage", EAutomationTestFlags::EditorContext | EAutomationTestFlags::PerfFilter)
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

#endif
