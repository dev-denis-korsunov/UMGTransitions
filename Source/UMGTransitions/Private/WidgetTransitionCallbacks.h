#pragma once

#include "WidgetTransition.h"

namespace WidgetTransitionCallbacks
{
	void Register(UWidgetTransitionSubsystem& Subsystem, int32 TransitionIndex, FWidgetTransitionCallbacks Callbacks);
	void EnsureLinks(UWidgetTransitionSubsystem& Subsystem);
	void Remove(UWidgetTransitionSubsystem& Subsystem, int32 TransitionIndex);
	bool IsPendingRemoval(const UWidgetTransitionSubsystem& Subsystem, int32 TransitionIndex);
	void QueueStarted(UWidgetTransitionSubsystem& Subsystem, int32 TransitionIndex);
	void StoreOverrideValue(UWidgetTransitionSubsystem& Subsystem, int32 TransitionIndex, FWidgetTransitionValue Value);
	void QueueCompleted(UWidgetTransitionSubsystem& Subsystem, int32 TransitionIndex, FWidgetTransitionValue Value);
	void TickAndDispatch(UWidgetTransitionSubsystem& Subsystem, float DeltaTime);
}
