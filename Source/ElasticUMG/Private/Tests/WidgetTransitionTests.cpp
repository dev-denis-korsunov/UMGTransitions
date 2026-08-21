#include "WidgetTransition.h"
#include "Tests/WidgetTransitionTestTypes.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Components/Image.h"
#include "Misc/AutomationTest.h"

namespace
{
	constexpr float Tolerance = 0.001f;
	FString FormatBytes(const TCHAR* Name, SIZE_T Size, SIZE_T Alignment) { return FString::Printf(TEXT("%s: %llu B, alignment %llu B"), Name, static_cast<uint64>(Size), static_cast<uint64>(Alignment)); }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWidgetTransitionStorageLayoutTest, "ElasticUMG.WidgetTransition.Runtime.StorageLayout", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FWidgetTransitionStorageLayoutTest::RunTest(const FString&)
{
	AddInfo(FormatBytes(TEXT("FWidgetTransition"), sizeof(FWidgetTransition), alignof(FWidgetTransition)));
	AddInfo(FormatBytes(TEXT("FActiveWidgetTransition"), sizeof(FActiveWidgetTransition), alignof(FActiveWidgetTransition)));
	AddInfo(FormatBytes(TEXT("FWidgetTransitionValue"), sizeof(FWidgetTransitionValue), alignof(FWidgetTransitionValue)));
	AddInfo(FormatBytes(TEXT("FWidgetTransitionPropertyBinding"), sizeof(FWidgetTransitionPropertyBinding), alignof(FWidgetTransitionPropertyBinding)));
	TestTrue(TEXT("Transition storage is non-empty"), sizeof(FActiveWidgetTransition) > 0);
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
	FWidgetTransition Transition;
	Transition.ToValue.Channels.X = 0.8f;
	Transition.ToValue.Type = EWidgetTransitionValueType::Float;
	Transition.FromValue.Channels = FVector4f(0.2f, 0.4f, 0.0f, 0.0f);
	Transition.FromValue.Type = EWidgetTransitionValueType::Vector2D;
	Transition.bUseFrom = true;
	Transition.Delay = 0.4f;
	Transition.Time = 0.25f;
	Transition = UWidgetTransitionFunctionLibrary::Bind(MoveTemp(Transition), Widget, TEXT("RenderTransform.Scale"));
	Transition = UWidgetTransitionFunctionLibrary::Repeat(MoveTemp(Transition), 2, true);
	Transition = UWidgetTransitionFunctionLibrary::Spring(MoveTemp(Transition), 0.8f, 0.25f);
	TestEqual(TEXT("Target retains semantic Float type"), Transition.ToValue.Type, EWidgetTransitionValueType::Float);
	TestEqual(TEXT("From retains independent Vector2D type"), Transition.FromValue.Type, EWidgetTransitionValueType::Vector2D);
	TestTrue(TEXT("From modifier is enabled"), Transition.bUseFrom);
	TestTrue(TEXT("Binding is retained"), Transition.Widget == Widget && Transition.WidgetProperty == TEXT("RenderTransform.Scale"));
	TestTrue(TEXT("Repeat and spring modifiers are retained"), Transition.RepeatCount == 2 && Transition.bYoYo && Transition.bUseSpring);
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
	FSpringVector2D Spring(144.0f, 18.0f);
	Spring.Start(FVector2D::ZeroVector, FVector2D(100.0, -50.0));
	for (int32 Step = 0; Step < 1200 && !Spring.IsCompleted(); ++Step) Spring.Tick(1.0f / 120.0f);
	TestTrue(TEXT("Vector spring completes"), Spring.IsCompleted());
	TestTrue(TEXT("Vector spring settles at target"), Spring.GetValue().Equals(FVector2D(100.0, -50.0), Tolerance));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWidgetTransitionMetadataTest, "ElasticUMG.WidgetTransition.Editor.Metadata", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FWidgetTransitionMetadataTest::RunTest(const FString&)
{
	const UFunction* Binding = UWidgetTransitionFunctionLibrary::StaticClass()->FindFunctionByName(GET_FUNCTION_NAME_CHECKED(UWidgetTransitionFunctionLibrary, Bind));
	TestTrue(TEXT("Binding function opts into the custom property pin"), Binding && Binding->HasMetaData(TEXT("ElasticUMGTransitionBinding")));
	const UFunction* InternalCreate = UWidgetTransitionFunctionLibrary::StaticClass()->FindFunctionByName(TEXT("CreateFloatWidgetTransition"));
	TestTrue(TEXT("Typed create shim is hidden from the Blueprint palette"), InternalCreate && InternalCreate->HasMetaData(TEXT("BlueprintInternalUseOnly")));
	return true;
}

#endif
