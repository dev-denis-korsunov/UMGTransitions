#include "WidgetComposerLibrary.h"

#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/PanelWidget.h"
#include "Components/Widget.h"
#include "Layout/Geometry.h"

namespace WidgetComposer
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
			if (UWidget* Root = UWidgetComposerLibrary::GetWidgetTreeRoot(UserWidget))
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

	struct FWidgetWaveData
	{
		int32 WaveIndex = 0;
		FVector2D Direction = FVector2D::ZeroVector;
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

	static float GetAxisTolerance(const TArray<FIndexedWidget>& Children, bool bHorizontal)
	{
		float SmallestExtent = TNumericLimits<float>::Max();
		for (const FIndexedWidget& Child : Children)
		{
			if (!IsValid(Child.Widget))
			{
				continue;
			}
			const FVector2D LocalSize = Child.Widget->GetCachedGeometry().GetLocalSize();
			const float Extent = bHorizontal ? LocalSize.X : LocalSize.Y;
			if (Extent > KINDA_SMALL_NUMBER)
			{
				SmallestExtent = FMath::Min(SmallestExtent, Extent);
			}
		}
		return SmallestExtent == TNumericLimits<float>::Max() ? 1.0f : FMath::Max(1.0f, SmallestExtent * 0.25f);
	}

	static TArray<float> GetAxisLevels(const TArray<FVector2D>& Centers, bool bHorizontal, float Tolerance)
	{
		TArray<float> Coordinates;
		Coordinates.Reserve(Centers.Num());
		for (const FVector2D& Center : Centers)
		{
			Coordinates.Add(bHorizontal ? Center.X : Center.Y);
		}
		Coordinates.Sort();

		TArray<float> Levels;
		for (const float Coordinate : Coordinates)
		{
			if (Levels.IsEmpty() || FMath::Abs(Coordinate - Levels.Last()) > Tolerance)
			{
				Levels.Add(Coordinate);
			}
			else
			{
				Levels.Last() = (Levels.Last() + Coordinate) * 0.5f;
			}
		}
		return Levels;
	}

	static int32 GetNearestAxisLevel(const TArray<float>& Levels, float Coordinate)
	{
		if (Levels.IsEmpty())
		{
			return 0;
		}

		int32 NearestIndex = 0;
		float NearestDistance = FMath::Abs(Levels[0] - Coordinate);
		for (int32 LevelIndex = 1; LevelIndex < Levels.Num(); ++LevelIndex)
		{
			const float Distance = FMath::Abs(Levels[LevelIndex] - Coordinate);
			if (Distance < NearestDistance)
			{
				NearestIndex = LevelIndex;
				NearestDistance = Distance;
			}
		}
		return NearestIndex;
	}

	static TMap<UWidget*, FWidgetWaveData> GetTopLevelWaveData(UWidget* Root, EWidgetSiblingOrder SiblingOrder, EWidgetWavePattern WavePattern, EWidgetWaveOrigin WaveOrigin, UWidget* OriginWidget)
	{
		const TArray<FIndexedWidget> Children = GetOrderedChildren(Root, SiblingOrder);
		const FGeometry& RootGeometry = Root->GetCachedGeometry();
		TMap<UWidget*, FWidgetWaveData> WaveData;
		WaveData.Reserve(Children.Num());
		for (const FIndexedWidget& Child : Children)
		{
			WaveData.Add(Child.Widget, {Child.WaveIndex, FVector2D::ZeroVector});
		}

		TArray<FVector2D> Centers;
		Centers.Reserve(Children.Num());
		for (const FIndexedWidget& Child : Children)
		{
			FVector2D AbsoluteCenter;
			if (!GetWidgetGeometryCenter(Child.Widget, AbsoluteCenter))
			{
				return WaveData;
			}
			const FVector2D Center = RootGeometry.AbsoluteToLocal(AbsoluteCenter);
			Centers.Add(Center);
		}

		if (Centers.IsEmpty())
		{
			return WaveData;
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
			FVector2D WidgetAbsoluteCenter;
			if (GetWidgetGeometryCenter(OriginWidget, WidgetAbsoluteCenter))
			{
				WaveOriginPosition = RootGeometry.AbsoluteToLocal(WidgetAbsoluteCenter);
			}
			else
			{
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

		const float HorizontalTolerance = GetAxisTolerance(Children, true);
		const float VerticalTolerance = GetAxisTolerance(Children, false);
		const TArray<float> HorizontalLevels = GetAxisLevels(Centers, true, HorizontalTolerance);
		const TArray<float> VerticalLevels = GetAxisLevels(Centers, false, VerticalTolerance);
		const float CenterHorizontalSpan = WaveOrigin == EWidgetWaveOrigin::Center && HorizontalLevels.Num() % 2 == 0 ? 0.5f : 0.0f;
		const float CenterVerticalSpan = WaveOrigin == EWidgetWaveOrigin::Center && VerticalLevels.Num() % 2 == 0 ? 0.5f : 0.0f;
		const int32 OriginHorizontalLevel = GetNearestAxisLevel(HorizontalLevels, WaveOriginPosition.X);
		const int32 OriginVerticalLevel = GetNearestAxisLevel(VerticalLevels, WaveOriginPosition.Y);
		for (int32 ChildIndex = 0; ChildIndex < Children.Num(); ++ChildIndex)
		{
			const FVector2D Delta = Centers[ChildIndex] - WaveOriginPosition;
			const int32 HorizontalLevel = GetNearestAxisLevel(HorizontalLevels, Centers[ChildIndex].X);
			const int32 VerticalLevel = GetNearestAxisLevel(VerticalLevels, Centers[ChildIndex].Y);
			const float HorizontalDistance = FMath::Max(0.0f, FMath::Abs(static_cast<float>(HorizontalLevel - OriginHorizontalLevel)) - CenterHorizontalSpan);
			const float VerticalDistance = FMath::Max(0.0f, FMath::Abs(static_cast<float>(VerticalLevel - OriginVerticalLevel)) - CenterVerticalSpan);
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
			const int32 WaveIndex = FMath::FloorToInt(Distance + KINDA_SMALL_NUMBER) + 1;
			const FVector2D Direction = Delta.GetSafeNormal();
			WaveData.Add(Children[ChildIndex].Widget, {WaveIndex, Direction});
		}
		return WaveData;
	}

	static FWidgetWaveData GetInheritedWaveData(const TMap<UWidget*, FWidgetWaveData>& TopLevelWaveData, UWidget* Widget, const FWidgetWaveData& InheritedWaveData)
	{
		if (const FWidgetWaveData* WaveData = TopLevelWaveData.Find(Widget))
		{
			return *WaveData;
		}
		return InheritedWaveData;
	}

	static void ApplyWaveToSelection(TArray<FWidgetDescendant>& Descendants, EWidgetWavePattern Pattern, EWidgetWaveOrigin Origin, UWidget* OriginWidget)
	{
		struct FWaveItem
		{
			int32 DescendantIndex = INDEX_NONE;
			FVector2D Center = FVector2D::ZeroVector;
		};

		TArray<FWaveItem> Items;
		TArray<FIndexedWidget> Widgets;
		Items.Reserve(Descendants.Num());
		Widgets.Reserve(Descendants.Num());
		for (int32 Index = 0; Index < Descendants.Num(); ++Index)
		{
			FVector2D Center;
			if (GetWidgetGeometryCenter(Descendants[Index].Value, Center))
			{
				Items.Add({Index, Center});
				Widgets.Add({Descendants[Index].Value, 0});
			}
			else
			{
				// A selection remains usable before its first layout pass.
				Descendants[Index].WaveIndex = Index + 1;
				Descendants[Index].WaveDirection = FVector2D::ZeroVector;
			}
		}

		if (Items.IsEmpty())
		{
			return;
		}

		TArray<FVector2D> Centers;
		Centers.Reserve(Items.Num());
		for (const FWaveItem& Item : Items)
		{
			Centers.Add(Item.Center);
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

		FVector2D OriginPosition = (Minimum + Maximum) * 0.5f;
		switch (Origin)
		{
		case EWidgetWaveOrigin::TopLeft: OriginPosition = Minimum; break;
		case EWidgetWaveOrigin::TopRight: OriginPosition = FVector2D(Maximum.X, Minimum.Y); break;
		case EWidgetWaveOrigin::BottomLeft: OriginPosition = FVector2D(Minimum.X, Maximum.Y); break;
		case EWidgetWaveOrigin::BottomRight: OriginPosition = Maximum; break;
		case EWidgetWaveOrigin::Widget:
			GetWidgetGeometryCenter(OriginWidget, OriginPosition);
			break;
		default: break;
		}

		const TArray<float> HorizontalLevels = GetAxisLevels(Centers, true, GetAxisTolerance(Widgets, true));
		const TArray<float> VerticalLevels = GetAxisLevels(Centers, false, GetAxisTolerance(Widgets, false));
		const float CenterHorizontalSpan = Origin == EWidgetWaveOrigin::Center && HorizontalLevels.Num() % 2 == 0 ? 0.5f : 0.0f;
		const float CenterVerticalSpan = Origin == EWidgetWaveOrigin::Center && VerticalLevels.Num() % 2 == 0 ? 0.5f : 0.0f;
		const int32 OriginHorizontalLevel = GetNearestAxisLevel(HorizontalLevels, OriginPosition.X);
		const int32 OriginVerticalLevel = GetNearestAxisLevel(VerticalLevels, OriginPosition.Y);
		for (const FWaveItem& Item : Items)
		{
			const int32 HorizontalLevel = GetNearestAxisLevel(HorizontalLevels, Item.Center.X);
			const int32 VerticalLevel = GetNearestAxisLevel(VerticalLevels, Item.Center.Y);
			const float HorizontalDistance = FMath::Max(0.0f, FMath::Abs(static_cast<float>(HorizontalLevel - OriginHorizontalLevel)) - CenterHorizontalSpan);
			const float VerticalDistance = FMath::Max(0.0f, FMath::Abs(static_cast<float>(VerticalLevel - OriginVerticalLevel)) - CenterVerticalSpan);
			float Distance = Pattern == EWidgetWavePattern::Horizontal ? HorizontalDistance : Pattern == EWidgetWavePattern::Vertical ? VerticalDistance : Pattern == EWidgetWavePattern::Radial ? FMath::Sqrt(FMath::Square(HorizontalDistance) + FMath::Square(VerticalDistance)) : HorizontalDistance + VerticalDistance;
			FWidgetDescendant& Descendant = Descendants[Item.DescendantIndex];
			Descendant.WaveIndex = FMath::FloorToInt(Distance + KINDA_SMALL_NUMBER) + 1;
			Descendant.WaveDirection = (Item.Center - OriginPosition).GetSafeNormal();
		}
	}

	static void AppendDescendantsDepthFirst(UWidget* Root, int32 CurrentDepth, int32 MaxDepth, EWidgetSiblingOrder SiblingOrder, const FWidgetWaveData& InheritedWaveData, const TMap<UWidget*, FWidgetWaveData>& TopLevelWaveData, TArray<FWidgetDescendant>& OutWidgets)
	{
		if (MaxDepth >= 0 && CurrentDepth >= MaxDepth)
		{
			return;
		}
		const TArray<FIndexedWidget> Children = GetOrderedChildren(Root, SiblingOrder);
		for (const FIndexedWidget& Child : Children)
		{
			const FWidgetWaveData WaveData = GetInheritedWaveData(TopLevelWaveData, Child.Widget, InheritedWaveData);
			OutWidgets.Add({WaveData.WaveIndex, CurrentDepth + 1, Child.Widget, WaveData.Direction});
			AppendDescendantsDepthFirst(Child.Widget, CurrentDepth + 1, MaxDepth, SiblingOrder, WaveData, TopLevelWaveData, OutWidgets);
		}
	}

	static void AppendDescendantsBreadthFirst(UWidget* Root, int32 MaxDepth, EWidgetSiblingOrder SiblingOrder, const TMap<UWidget*, FWidgetWaveData>& TopLevelWaveData, TArray<FWidgetDescendant>& OutWidgets)
	{
		struct FPendingWidget
		{
			UWidget* Widget = nullptr;
			int32 Depth = 0;
			FWidgetWaveData WaveData;
		};
		TArray<FPendingWidget> PendingWidgets;
		PendingWidgets.Add({Root, 0, {}});
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
				const FWidgetWaveData WaveData = GetInheritedWaveData(TopLevelWaveData, Child.Widget, Pending.WaveData);
				OutWidgets.Add({WaveData.WaveIndex, Pending.Depth + 1, Child.Widget, WaveData.Direction});
				PendingWidgets.Add({Child.Widget, Pending.Depth + 1, WaveData});
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

UWidget* UWidgetComposerLibrary::GetWidgetTreeRoot(const UUserWidget* UserWidget)
{
	return IsValid(UserWidget) && UserWidget->WidgetTree ? UserWidget->WidgetTree->RootWidget : nullptr;
}

UWidget* UWidgetComposerLibrary::FindWidgetByName(const UUserWidget* UserWidget, FName Name)
{
	return IsValid(UserWidget) && !Name.IsNone() ? UserWidget->GetWidgetFromName(Name) : nullptr;
}

TArray<UWidget*> UWidgetComposerLibrary::GetWidgetChildren(UWidget* Widget)
{
	TArray<UWidget*> Children;
	WidgetComposer::AppendDirectChildren(Widget, Children);
	return Children;
}

TArray<FWidgetDescendant> UWidgetComposerLibrary::GetWidgetDescendants(UWidget* Root, int32 MaxDepth, EWidgetDescendantTraversal Traversal, EWidgetSiblingOrder SiblingOrder, EWidgetWavePattern WavePattern, EWidgetWaveOrigin WaveOrigin, UWidget* OriginWidget, bool bIncludeRoot)
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
	const TMap<UWidget*, WidgetComposer::FWidgetWaveData> TopLevelWaveData = WidgetComposer::GetTopLevelWaveData(Root, SiblingOrder, WavePattern, WaveOrigin, OriginWidget);
	if (Traversal == EWidgetDescendantTraversal::BreadthFirst)
	{
		WidgetComposer::AppendDescendantsBreadthFirst(Root, MaxDepth, SiblingOrder, TopLevelWaveData, Widgets);
	}
	else
	{
		WidgetComposer::AppendDescendantsDepthFirst(Root, 0, MaxDepth, SiblingOrder, {}, TopLevelWaveData, Widgets);
	}
	return Widgets;
}

TArray<FWidgetDescendant> UWidgetComposerLibrary::CollectWidgets(UWidget* Root, int32 MaxDepth, EWidgetDescendantTraversal Traversal, EWidgetSiblingOrder SiblingOrder, bool bIncludeRoot)
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

	const TMap<UWidget*, WidgetComposer::FWidgetWaveData> NoWaveData;
	if (Traversal == EWidgetDescendantTraversal::BreadthFirst)
	{
		WidgetComposer::AppendDescendantsBreadthFirst(Root, MaxDepth, SiblingOrder, NoWaveData, Widgets);
	}
	else
	{
		WidgetComposer::AppendDescendantsDepthFirst(Root, 0, MaxDepth, SiblingOrder, {}, NoWaveData, Widgets);
	}
	return Widgets;
}

TArray<FWidgetDescendant> UWidgetComposerLibrary::ApplyWidgetWave(const TArray<FWidgetDescendant>& Widgets, EWidgetWavePattern Pattern, EWidgetWaveOrigin Origin, UWidget* OriginWidget)
{
	TArray<FWidgetDescendant> Wave = Widgets;
	WidgetComposer::ApplyWaveToSelection(Wave, Pattern, Origin, OriginWidget);
	return Wave;
}

TArray<FWidgetDescendant> UWidgetComposerLibrary::SortWidgetsByWave(const TArray<FWidgetDescendant>& Widgets, EWidgetWaveSortOrder Order)
{
	TArray<FWidgetDescendant> Sorted = Widgets;
	Sorted.StableSort([Order](const FWidgetDescendant& Left, const FWidgetDescendant& Right)
	{
		return Order == EWidgetWaveSortOrder::NearToFar ? Left.WaveIndex < Right.WaveIndex : Left.WaveIndex > Right.WaveIndex;
	});
	return Sorted;
}

TArray<FWidgetDescendant> UWidgetComposerLibrary::BuildWidgetWave(const TArray<FWidgetDescendant>& Descendants, int32 Columns, EWidgetWavePattern Pattern, EWidgetWaveOrigin Origin, UWidget* OriginWidget)
{
	return ApplyWidgetWave(Descendants, Pattern, Origin, OriginWidget);
}

TArray<UWidget*> UWidgetComposerLibrary::GetWidgetsAtDepth(UWidget* Root, int32 Depth)
{
	TArray<UWidget*> Widgets;
	if (Depth >= 0)
	{
		WidgetComposer::AppendWidgetsAtDepth(Root, 0, Depth, Widgets);
	}
	return Widgets;
}

TArray<UWidget*> UWidgetComposerLibrary::GetWidgetsThroughDepth(UWidget* Root, int32 Depth)
{
	TArray<UWidget*> Widgets;
	if (!IsValid(Root) || Depth < 0)
	{
		return Widgets;
	}

	WidgetComposer::AppendWidgetsThroughDepth(Root, 0, Depth, Widgets);
	return Widgets;
}

UWidget* UWidgetComposerLibrary::GetWidgetParent(UWidget* Widget)
{
	return WidgetComposer::FindHierarchyParent(Widget);
}

TArray<UWidget*> UWidgetComposerLibrary::GetWidgetParents(UWidget* Widget)
{
	TArray<UWidget*> Parents;
	for (UWidget* Parent = GetWidgetParent(Widget); Parent; Parent = GetWidgetParent(Parent))
	{
		Parents.Add(Parent);
	}
	return Parents;
}

TArray<UWidget*> UWidgetComposerLibrary::FindWidgetDescendantsByName(UWidget* Root, FName Name, bool bIncludeRoot)
{
	return FindWidgetDescendantsByNames(Root, {Name}, bIncludeRoot);
}

TArray<UWidget*> UWidgetComposerLibrary::FindWidgetDescendantsByNames(UWidget* Root, const TArray<FName>& Names, bool bIncludeRoot)
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
