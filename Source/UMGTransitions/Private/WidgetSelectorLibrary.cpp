#include "WidgetSelectorLibrary.h"

#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/PanelWidget.h"
#include "Components/Widget.h"
#include "Layout/Geometry.h"

namespace WidgetSelector
{
	static void AppendDirectChildren(UWidget* Widget, TArray<UWidget*>& OutWidgets)
	{
		if (const UPanelWidget* Panel = Cast<UPanelWidget>(Widget))
		{
			for (int32 ChildIndex = 0; ChildIndex < Panel->GetChildrenCount(); ++ChildIndex)
			{
				if (UWidget* Child = Panel->GetChildAt(ChildIndex))
				{
					OutWidgets.Add(Child);
				}
			}
			return;
		}

		if (const UUserWidget* UserWidget = Cast<UUserWidget>(Widget))
		{
			if (UWidget* Root = UWidgetSelectorLibrary::GetWidgetTreeRoot(UserWidget))
			{
				OutWidgets.Add(Root);
			}
		}
	}

	struct FIndexedWidget
	{
		UWidget* Widget = nullptr;
		int32 WaveIndex = 0;
	};

	static TArray<FIndexedWidget> GetOrderedChildren(UWidget* Widget, EWidgetSiblingOrder SiblingOrder)
	{
		TArray<UWidget*> Children;
		AppendDirectChildren(Widget, Children);
		TArray<FIndexedWidget> OrderedChildren;
		OrderedChildren.Reserve(Children.Num());
		if (SiblingOrder == EWidgetSiblingOrder::RightToLeft)
		{
			for (int32 ChildIndex = Children.Num() - 1, WaveIndex = 1; ChildIndex >= 0; --ChildIndex, ++WaveIndex)
			{
				OrderedChildren.Add({Children[ChildIndex], WaveIndex});
			}
			return OrderedChildren;
		}
		if (SiblingOrder == EWidgetSiblingOrder::LeftToRight)
		{
			for (int32 ChildIndex = 0; ChildIndex < Children.Num(); ++ChildIndex)
			{
				OrderedChildren.Add({Children[ChildIndex], ChildIndex + 1});
			}
			return OrderedChildren;
		}
		int32 LeftIndex = Children.Num() / 2 - 1;
		int32 RightIndex = (Children.Num() + 1) / 2;
		int32 WaveIndex = 1;
		if (Children.Num() % 2 != 0)
		{
			OrderedChildren.Add({Children[Children.Num() / 2], WaveIndex++});
		}
		while (LeftIndex >= 0 || RightIndex < Children.Num())
		{
			if (LeftIndex >= 0)
			{
				OrderedChildren.Add({Children[LeftIndex--], WaveIndex});
			}
			if (RightIndex < Children.Num())
			{
				OrderedChildren.Add({Children[RightIndex++], WaveIndex});
			}
			++WaveIndex;
		}
		return OrderedChildren;
	}

	static bool GetWidgetGeometryCenter(const UWidget* Widget, FVector2D& OutCenter)
	{
		if (!IsValid(Widget))
		{
			return false;
		}

		const FGeometry& Geometry = Widget->GetCachedGeometry();
		const FVector2D LocalSize = Geometry.GetLocalSize();
		if (LocalSize.X <= KINDA_SMALL_NUMBER || LocalSize.Y <= KINDA_SMALL_NUMBER)
		{
			return false;
		}

		OutCenter = Geometry.GetAbsolutePositionAtCoordinates(FVector2D(0.5f, 0.5f));
		return true;
	}

	static float GetSmallestPositiveAxisStep(const TArray<FVector2D>& Centers, bool bHorizontal)
	{
		TArray<float> Coordinates;
		Coordinates.Reserve(Centers.Num());
		for (const FVector2D& Center : Centers)
		{
			Coordinates.Add(bHorizontal ? Center.X : Center.Y);
		}
		Coordinates.Sort();

		float SmallestStep = TNumericLimits<float>::Max();
		for (int32 CoordinateIndex = 1; CoordinateIndex < Coordinates.Num(); ++CoordinateIndex)
		{
			const float Step = Coordinates[CoordinateIndex] - Coordinates[CoordinateIndex - 1];
			if (Step > KINDA_SMALL_NUMBER)
			{
				SmallestStep = FMath::Min(SmallestStep, Step);
			}
		}
		return SmallestStep == TNumericLimits<float>::Max() ? 1.0f : SmallestStep;
	}

	static int32 GetAxisLevelCount(const TArray<FVector2D>& Centers, bool bHorizontal)
	{
		TArray<float> Coordinates;
		Coordinates.Reserve(Centers.Num());
		for (const FVector2D& Center : Centers)
		{
			Coordinates.Add(bHorizontal ? Center.X : Center.Y);
		}
		Coordinates.Sort();

		int32 LevelCount = 0;
		float PreviousCoordinate = 0.0f;
		for (const float Coordinate : Coordinates)
		{
			if (LevelCount == 0 || Coordinate - PreviousCoordinate > KINDA_SMALL_NUMBER)
			{
				++LevelCount;
				PreviousCoordinate = Coordinate;
			}
		}
		return LevelCount;
	}

	static TMap<UWidget*, int32> GetTopLevelWaveIndices(UWidget* Root, EWidgetSiblingOrder SiblingOrder, EWidgetWavePattern WavePattern, EWidgetWaveOrigin WaveOrigin, UWidget* OriginWidget)
	{
		const TArray<FIndexedWidget> Children = GetOrderedChildren(Root, SiblingOrder);
		TMap<UWidget*, int32> WaveIndices;
		WaveIndices.Reserve(Children.Num());
		for (const FIndexedWidget& Child : Children)
		{
			WaveIndices.Add(Child.Widget, Child.WaveIndex);
		}

		TArray<FVector2D> Centers;
		Centers.Reserve(Children.Num());
		for (const FIndexedWidget& Child : Children)
		{
			FVector2D Center;
			if (!GetWidgetGeometryCenter(Child.Widget, Center))
			{
				return WaveIndices;
			}
			Centers.Add(Center);
		}

		if (Centers.IsEmpty())
		{
			return WaveIndices;
		}

		FVector2D Minimum = Centers[0];
		FVector2D Maximum = Centers[0];
		for (const FVector2D& Center : Centers)
		{
			Minimum.X = FMath::Min(Minimum.X, Center.X);
			Minimum.Y = FMath::Min(Minimum.Y, Center.Y);
			Maximum.X = FMath::Max(Maximum.X, Center.X);
			Maximum.Y = FMath::Max(Maximum.Y, Center.Y);
		}

		FVector2D WaveOriginPosition = (Minimum + Maximum) * 0.5f;
		switch (WaveOrigin)
		{
		case EWidgetWaveOrigin::TopLeft:
		{
			WaveOriginPosition = Minimum;
			break;
		}
		case EWidgetWaveOrigin::TopRight:
		{
			WaveOriginPosition = FVector2D(Maximum.X, Minimum.Y);
			break;
		}
		case EWidgetWaveOrigin::BottomLeft:
		{
			WaveOriginPosition = FVector2D(Minimum.X, Maximum.Y);
			break;
		}
		case EWidgetWaveOrigin::BottomRight:
		{
			WaveOriginPosition = Maximum;
			break;
		}
		case EWidgetWaveOrigin::Widget:
		{
			FVector2D WidgetCenter;
			if (GetWidgetGeometryCenter(OriginWidget, WidgetCenter))
			{
				WaveOriginPosition = WidgetCenter;
			}
			break;
		}
		case EWidgetWaveOrigin::Center:
		{
			break;
		}
		default:
		{
			break;
		}
		}

		const float HorizontalStep = GetSmallestPositiveAxisStep(Centers, true);
		const float VerticalStep = GetSmallestPositiveAxisStep(Centers, false);
		const float CenterHorizontalSpan = WaveOrigin == EWidgetWaveOrigin::Center && GetAxisLevelCount(Centers, true) % 2 == 0 ? 0.5f : 0.0f;
		const float CenterVerticalSpan = WaveOrigin == EWidgetWaveOrigin::Center && GetAxisLevelCount(Centers, false) % 2 == 0 ? 0.5f : 0.0f;
		for (int32 ChildIndex = 0; ChildIndex < Children.Num(); ++ChildIndex)
		{
			const FVector2D Delta = Centers[ChildIndex] - WaveOriginPosition;
			const float HorizontalDistance = FMath::Max(0.0f, FMath::Abs(Delta.X) / HorizontalStep - CenterHorizontalSpan);
			const float VerticalDistance = FMath::Max(0.0f, FMath::Abs(Delta.Y) / VerticalStep - CenterVerticalSpan);
			float Distance = 0.0f;
			switch (WavePattern)
			{
			case EWidgetWavePattern::Horizontal:
			{
				Distance = HorizontalDistance;
				break;
			}
			case EWidgetWavePattern::Vertical:
			{
				Distance = VerticalDistance;
				break;
			}
			case EWidgetWavePattern::Manhattan:
			{
				Distance = HorizontalDistance + VerticalDistance;
				break;
			}
			case EWidgetWavePattern::Radial:
			{
				Distance = FMath::Sqrt(FMath::Square(HorizontalDistance) + FMath::Square(VerticalDistance));
				break;
			}
			default:
			{
				break;
			}
			}
			WaveIndices.Add(Children[ChildIndex].Widget, FMath::FloorToInt(Distance + KINDA_SMALL_NUMBER) + 1);
		}
		return WaveIndices;
	}

	static int32 GetInheritedWaveIndex(const TMap<UWidget*, int32>& TopLevelWaveIndices, UWidget* Widget, int32 InheritedWaveIndex)
	{
		if (const int32* WaveIndex = TopLevelWaveIndices.Find(Widget))
		{
			return *WaveIndex;
		}
		return InheritedWaveIndex;
	}

	static void AppendDescendantsDepthFirst(UWidget* Root, int32 CurrentDepth, int32 MaxDepth, EWidgetSiblingOrder SiblingOrder, int32 InheritedWaveIndex, const TMap<UWidget*, int32>& TopLevelWaveIndices, TArray<FWidgetDescendant>& OutWidgets)
	{
		if (MaxDepth >= 0 && CurrentDepth >= MaxDepth)
		{
			return;
		}
		const TArray<FIndexedWidget> Children = GetOrderedChildren(Root, SiblingOrder);
		for (const FIndexedWidget& Child : Children)
		{
			const int32 WaveIndex = GetInheritedWaveIndex(TopLevelWaveIndices, Child.Widget, InheritedWaveIndex);
			OutWidgets.Add({WaveIndex, CurrentDepth + 1, Child.Widget});
			AppendDescendantsDepthFirst(Child.Widget, CurrentDepth + 1, MaxDepth, SiblingOrder, WaveIndex, TopLevelWaveIndices, OutWidgets);
		}
	}

	static void AppendDescendantsBreadthFirst(UWidget* Root, int32 MaxDepth, EWidgetSiblingOrder SiblingOrder, const TMap<UWidget*, int32>& TopLevelWaveIndices, TArray<FWidgetDescendant>& OutWidgets)
	{
		struct FPendingWidget
		{
			UWidget* Widget = nullptr;
			int32 Depth = 0;
			int32 WaveIndex = 0;
		};
		TArray<FPendingWidget> PendingWidgets;
		PendingWidgets.Add({Root, 0, 0});
		for (int32 PendingIndex = 0; PendingIndex < PendingWidgets.Num(); ++PendingIndex)
		{
			const FPendingWidget Pending = PendingWidgets[PendingIndex];
			if (MaxDepth >= 0 && Pending.Depth >= MaxDepth)
			{
				continue;
			}
			const TArray<FIndexedWidget> Children = GetOrderedChildren(Pending.Widget, SiblingOrder);
			for (const FIndexedWidget& Child : Children)
			{
				const int32 WaveIndex = GetInheritedWaveIndex(TopLevelWaveIndices, Child.Widget, Pending.WaveIndex);
				OutWidgets.Add({WaveIndex, Pending.Depth + 1, Child.Widget});
				PendingWidgets.Add({Child.Widget, Pending.Depth + 1, WaveIndex});
			}
		}
	}

	static void AppendWidgetsAtDepth(UWidget* Root, int32 CurrentDepth, int32 TargetDepth, TArray<UWidget*>& OutWidgets)
	{
		if (!IsValid(Root) || CurrentDepth > TargetDepth)
		{
			return;
		}

		if (CurrentDepth == TargetDepth)
		{
			OutWidgets.Add(Root);
			return;
		}

		TArray<UWidget*> Children;
		AppendDirectChildren(Root, Children);
		for (UWidget* Child : Children)
		{
			AppendWidgetsAtDepth(Child, CurrentDepth + 1, TargetDepth, OutWidgets);
		}
	}

	static void AppendWidgetsThroughDepth(UWidget* Root, int32 CurrentDepth, int32 MaximumDepth, TArray<UWidget*>& OutWidgets)
	{
		if (!IsValid(Root) || CurrentDepth > MaximumDepth)
		{
			return;
		}

		OutWidgets.Add(Root);
		TArray<UWidget*> Children;
		AppendDirectChildren(Root, Children);
		for (UWidget* Child : Children)
		{
			AppendWidgetsThroughDepth(Child, CurrentDepth + 1, MaximumDepth, OutWidgets);
		}
	}

	static UWidget* FindHierarchyParent(UWidget* Widget)
	{
		if (!IsValid(Widget))
		{
			return nullptr;
		}

		if (UPanelWidget* Parent = Widget->GetParent())
		{
			return Parent;
		}

		const UWidgetTree* WidgetTree = Cast<UWidgetTree>(Widget->GetOuter());
		return WidgetTree ? Cast<UWidget>(WidgetTree->GetOuter()) : nullptr;
	}
}

UWidget* UWidgetSelectorLibrary::GetWidgetTreeRoot(const UUserWidget* UserWidget)
{
	return IsValid(UserWidget) && UserWidget->WidgetTree ? UserWidget->WidgetTree->RootWidget : nullptr;
}

UWidget* UWidgetSelectorLibrary::FindWidgetByName(const UUserWidget* UserWidget, FName Name)
{
	return IsValid(UserWidget) && !Name.IsNone() ? UserWidget->GetWidgetFromName(Name) : nullptr;
}

TArray<UWidget*> UWidgetSelectorLibrary::GetWidgetChildren(UWidget* Widget)
{
	TArray<UWidget*> Children;
	WidgetSelector::AppendDirectChildren(Widget, Children);
	return Children;
}

TArray<FWidgetDescendant> UWidgetSelectorLibrary::GetWidgetDescendants(UWidget* Root, int32 MaxDepth, EWidgetDescendantTraversal Traversal, EWidgetSiblingOrder SiblingOrder, EWidgetWavePattern WavePattern, EWidgetWaveOrigin WaveOrigin, UWidget* OriginWidget, bool bIncludeRoot)
{
	TArray<FWidgetDescendant> Widgets;
	if (!IsValid(Root))
	{
		return Widgets;
	}

	if (bIncludeRoot)
	{
		Widgets.Add({0, 0, Root});
	}
	const TMap<UWidget*, int32> TopLevelWaveIndices = WidgetSelector::GetTopLevelWaveIndices(Root, SiblingOrder, WavePattern, WaveOrigin, OriginWidget);
	if (Traversal == EWidgetDescendantTraversal::BreadthFirst)
	{
		WidgetSelector::AppendDescendantsBreadthFirst(Root, MaxDepth, SiblingOrder, TopLevelWaveIndices, Widgets);
	}
	else
	{
		WidgetSelector::AppendDescendantsDepthFirst(Root, 0, MaxDepth, SiblingOrder, 0, TopLevelWaveIndices, Widgets);
	}
	return Widgets;
}

TArray<FWidgetDescendant> UWidgetSelectorLibrary::BuildWidgetWave(const TArray<FWidgetDescendant>& Descendants, int32 Columns, EWidgetWavePattern Pattern, EWidgetWaveOrigin Origin, UWidget* OriginWidget)
{
	return Descendants;
}

TArray<UWidget*> UWidgetSelectorLibrary::GetWidgetsAtDepth(UWidget* Root, int32 Depth)
{
	TArray<UWidget*> Widgets;
	if (Depth >= 0)
	{
		WidgetSelector::AppendWidgetsAtDepth(Root, 0, Depth, Widgets);
	}
	return Widgets;
}

TArray<UWidget*> UWidgetSelectorLibrary::GetWidgetsThroughDepth(UWidget* Root, int32 Depth)
{
	TArray<UWidget*> Widgets;
	if (!IsValid(Root) || Depth < 0)
	{
		return Widgets;
	}

	WidgetSelector::AppendWidgetsThroughDepth(Root, 0, Depth, Widgets);
	return Widgets;
}

UWidget* UWidgetSelectorLibrary::GetWidgetParent(UWidget* Widget)
{
	return WidgetSelector::FindHierarchyParent(Widget);
}

TArray<UWidget*> UWidgetSelectorLibrary::GetWidgetParents(UWidget* Widget)
{
	TArray<UWidget*> Parents;
	for (UWidget* Parent = GetWidgetParent(Widget); Parent; Parent = GetWidgetParent(Parent))
	{
		Parents.Add(Parent);
	}
	return Parents;
}

TArray<UWidget*> UWidgetSelectorLibrary::FindWidgetDescendantsByName(UWidget* Root, FName Name, bool bIncludeRoot)
{
	return FindWidgetDescendantsByNames(Root, {Name}, bIncludeRoot);
}

TArray<UWidget*> UWidgetSelectorLibrary::FindWidgetDescendantsByNames(UWidget* Root, const TArray<FName>& Names, bool bIncludeRoot)
{
	TArray<UWidget*> Matches;
	if (!IsValid(Root) || Names.IsEmpty())
	{
		return Matches;
	}

	const TArray<FWidgetDescendant> Widgets = GetWidgetDescendants(Root, -1, EWidgetDescendantTraversal::DepthFirst, EWidgetSiblingOrder::LeftToRight, EWidgetWavePattern::Manhattan, EWidgetWaveOrigin::Center, nullptr, bIncludeRoot);
	for (const FWidgetDescendant& Descendant : Widgets)
	{
		if (IsValid(Descendant.Value) && Names.Contains(Descendant.Value->GetFName()))
		{
			Matches.Add(Descendant.Value);
		}
	}
	return Matches;
}
