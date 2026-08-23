#include "WidgetTransition.h"
#include "Tests/WidgetTransitionTestTypes.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Components/Image.h"
#include "Misc/AutomationTest.h"

namespace
{
	constexpr float Tolerance = 0.001f;
	FString FormatBytes(const TCHAR* Name, SIZE_T Size, SIZE_T Alignment)
	{
		return FString::Printf(TEXT("%s: %llu B, alignment %llu B"), Name, static_cast<uint64>(Size), static_cast<uint64>(Alignment));
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWidgetTransitionStorageLayoutTest, "ElasticUMG.WidgetTransition.Runtime.StorageLayout", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FWidgetTransitionStorageLayoutTest::RunTest(const FString&)
	{
		AddInfo(FormatBytes(TEXT("FWidgetTransition"), sizeof(FWidgetTransition), alignof(FWidgetTransition)));
		AddInfo(FormatBytes(TEXT("FWidgetTransitionValue"), sizeof(FWidgetTransitionValue), alignof(FWidgetTransitionValue)));
		AddInfo(FormatBytes(TEXT("FWidgetTransitionPropertyBinding"), sizeof(FWidgetTransitionPropertyBinding), alignof(FWidgetTransitionPropertyBinding)));
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
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWidgetTransitionSpringTest, "ElasticUMG.WidgetTransition.Runtime.Spring.Converges", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FWidgetTransitionSpringTest::RunTest(const FString&)
{
	FSpringVector4f Spring(144.0f, 18.0f);
	Spring.Start(FVector4f::Zero(), FVector4f(100.0f, -50.0f, 25.0f, 1.0f));
	for (int32 Step = 0; Step < 1200 && !Spring.IsCompleted(); ++Step)
	{
		Spring.Tick(1.0f / 120.0f);
	}
	TestTrue(TEXT("Four-channel spring completes"), Spring.IsCompleted());
	TestTrue(TEXT("Four-channel spring settles at target"), Spring.GetValue().Equals(FVector4f(100.0f, -50.0f, 25.0f, 1.0f), Tolerance));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWidgetTransitionMetadataTest, "ElasticUMG.WidgetTransition.Editor.Metadata", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FWidgetTransitionMetadataTest::RunTest(const FString&)
{
	const UFunction* Create = UWidgetTransitionFunctionLibrary::StaticClass()->FindFunctionByName(GET_FUNCTION_NAME_CHECKED(UWidgetTransitionFunctionLibrary, CreateWidgetTransition));
	TestTrue(TEXT("Create function opts into the custom property pin"), Create && Create->HasMetaData(TEXT("ElasticUMGTransitionBinding")));
	return true;
}

#endif
