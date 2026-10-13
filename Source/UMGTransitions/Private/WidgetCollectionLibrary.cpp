#include "WidgetCollectionLibrary.h"

#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/PanelWidget.h"
#include "Components/Widget.h"
#include "Layout/Geometry.h"

namespace
{
	void AppendDirectChildren(UWidget* Widget, TArray<UWidget*>& OutWidgets)
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
			if (IsValid(UserWidget) && UserWidget->WidgetTree && IsValid(UserWidget->WidgetTree->RootWidget))
			{
				OutWidgets.Add(UserWidget->WidgetTree->RootWidget);
			}
		}
	}

	UWidget* FindHierarchyParent(UWidget* Widget)
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
		return WidgetTree && WidgetTree->RootWidget == Widget ? Cast<UWidget>(WidgetTree->GetOuter()) : nullptr;
	}

	void UpdateLocalDepths(TArray<FWidgetCollectionItem>& Collection)
	{
		TSet<UWidget*> Members;
		for (const FWidgetCollectionItem& Item : Collection)
		{
			if (IsValid(Item.Widget))
			{
				Members.Add(Item.Widget.Get());
			}
		}
		for (FWidgetCollectionItem& Item : Collection)
		{
			Item.Depth = 0;
			TSet<UWidget*> Visited;
			Visited.Add(Item.Widget.Get());
			for (UWidget* Parent = FindHierarchyParent(Item.Widget); IsValid(Parent) && !Visited.Contains(Parent); Parent = FindHierarchyParent(Parent))
			{
				Visited.Add(Parent);
				if (Members.Contains(Parent))
				{
					++Item.Depth;
				}
			}
		}
	}

	void AppendDescendantsDepthFirst(UWidget* Root, int32 CurrentDepth, int32 MaxDepth, bool bDepthOnly, TArray<FWidgetCollectionItem>& OutWidgets)
	{
		if (!IsValid(Root) || (MaxDepth >= 0 && CurrentDepth >= MaxDepth))
		{
			return;
		}
		TArray<UWidget*> Children;
		AppendDirectChildren(Root, Children);
		for (UWidget* Child : Children)
		{
			if (!bDepthOnly || CurrentDepth + 1 == MaxDepth)
			{
				OutWidgets.Add({OutWidgets.Num(), CurrentDepth + 1, Child});
			}
			AppendDescendantsDepthFirst(Child, CurrentDepth + 1, MaxDepth, bDepthOnly, OutWidgets);
		}
	}

	struct FWaveItem
	{
		int32 CollectionIndex = INDEX_NONE;
		FVector2D Center = FVector2D::ZeroVector;
		FVector2D Size = FVector2D::ZeroVector;
	};

	bool GetWidgetGeometryCenter(const UWidget* Widget, FVector2D& OutCenter)
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

	float GetAxisTolerance(const TArray<FWaveItem>& Items, bool bHorizontal)
	{
		float SmallestExtent = TNumericLimits<float>::Max();
		for (const FWaveItem& Item : Items)
		{
			const float Extent = bHorizontal ? Item.Size.X : Item.Size.Y;
			if (Extent > KINDA_SMALL_NUMBER)
			{
				SmallestExtent = FMath::Min(SmallestExtent, Extent);
			}
		}
		return SmallestExtent == TNumericLimits<float>::Max() ? 1.0f : FMath::Max(1.0f, SmallestExtent * 0.25f);
	}

	TArray<float> GetAxisLevels(const TArray<FWaveItem>& Items, bool bHorizontal, float Tolerance)
	{
		TArray<float> Coordinates;
		Coordinates.Reserve(Items.Num());
		for (const FWaveItem& Item : Items)
		{
			Coordinates.Add(bHorizontal ? Item.Center.X : Item.Center.Y);
		}
		Coordinates.Sort();

		TArray<float> Levels;
		Levels.Reserve(Coordinates.Num());
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

	int32 GetNearestAxisLevel(const TArray<float>& Levels, float Coordinate)
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

	void ApplyWaveToSelection(TArray<FWidgetCollectionItem>& Collection, UWidget* OriginWidget, EWidgetWavePattern Pattern)
	{
		for (int32 Index = 0; Index < Collection.Num(); ++Index)
		{
			Collection[Index].Index = Index;
			Collection[Index].Direction = FVector2D::ZeroVector;
		}

		FVector2D OriginPosition;
		if (!GetWidgetGeometryCenter(OriginWidget, OriginPosition))
		{
			return;
		}

		TArray<FWaveItem> Items;
		Items.Reserve(Collection.Num());
		for (int32 Index = 0; Index < Collection.Num(); ++Index)
		{
			UWidget* Widget = Collection[Index].Widget.Get();
			FVector2D Center;
			if (GetWidgetGeometryCenter(Widget, Center))
			{
				Items.Add({Index, Center, Widget->GetCachedGeometry().GetLocalSize()});
			}
		}
		if (Items.IsEmpty())
		{
			return;
		}

		const TArray<float> HorizontalLevels = GetAxisLevels(Items, true, GetAxisTolerance(Items, true));
		const TArray<float> VerticalLevels = GetAxisLevels(Items, false, GetAxisTolerance(Items, false));
		const int32 OriginHorizontalLevel = GetNearestAxisLevel(HorizontalLevels, OriginPosition.X);
		const int32 OriginVerticalLevel = GetNearestAxisLevel(VerticalLevels, OriginPosition.Y);
		for (const FWaveItem& Item : Items)
		{
			const float HorizontalDistance = FMath::Abs(static_cast<float>(GetNearestAxisLevel(HorizontalLevels, Item.Center.X) - OriginHorizontalLevel));
			const float VerticalDistance = FMath::Abs(static_cast<float>(GetNearestAxisLevel(VerticalLevels, Item.Center.Y) - OriginVerticalLevel));
			float Distance = 0.0f;
			switch (Pattern)
			{
			case EWidgetWavePattern::Horizontal:
				Distance = HorizontalDistance;
				break;
			case EWidgetWavePattern::Vertical:
				Distance = VerticalDistance;
				break;
			case EWidgetWavePattern::Radial:
				Distance = FMath::Sqrt(FMath::Square(HorizontalDistance) + FMath::Square(VerticalDistance));
				break;
			default:
				Distance = HorizontalDistance + VerticalDistance;
				break;
			}

			FWidgetCollectionItem& Selected = Collection[Item.CollectionIndex];
			Selected.Index = FMath::FloorToInt(Distance + KINDA_SMALL_NUMBER);
			Selected.Direction = (Item.Center - OriginPosition).GetSafeNormal();
		}
	}
}

TArray<FWidgetCollectionItem> UWidgetCollectionLibrary::CollectWidgetsByName(UWidget* Root, const FString& NamePattern, bool bIncludeRoot)
{
	TArray<FWidgetCollectionItem> Result;
	if (!IsValid(Root) || NamePattern.IsEmpty())
	{
		return Result;
	}
	if (bIncludeRoot)
	{
		Result.Add({0, 0, Root});
	}
	AppendDescendantsDepthFirst(Root, 0, -1, false, Result);
	Result.RemoveAll([&NamePattern](const FWidgetCollectionItem& Item)
	{
		return !IsValid(Item.Widget) || !Item.Widget->GetName().MatchesWildcard(NamePattern, ESearchCase::CaseSensitive);
	});
	UpdateLocalDepths(Result);
	return Result;
}

TArray<FWidgetCollectionItem> UWidgetCollectionLibrary::CollectWidgets(UWidget* Root, int32 MaxDepth, bool bIncludeRoot, bool bDepthOnly)
{
	TArray<FWidgetCollectionItem> Widgets;
	if (!IsValid(Root) || (bDepthOnly && MaxDepth < 0))
	{
		return Widgets;
	}
	if ((bDepthOnly && MaxDepth == 0) || (!bDepthOnly && bIncludeRoot))
	{
		Widgets.Add({0, 0, Root});
	}
	AppendDescendantsDepthFirst(Root, 0, MaxDepth, bDepthOnly, Widgets);
	UpdateLocalDepths(Widgets);
	return Widgets;
}

TArray<FWidgetCollectionItem> UWidgetCollectionLibrary::MakeWidgetCollection(const TArray<UWidget*>& Widgets)
{
	TArray<FWidgetCollectionItem> Result;
	Result.Reserve(Widgets.Num());
	TSet<UWidget*> Seen;
	Seen.Reserve(Widgets.Num());
	for (UWidget* Widget : Widgets)
	{
		if (IsValid(Widget) && !Seen.Contains(Widget))
		{
			Seen.Add(Widget);
			Result.Add({Result.Num(), 0, Widget});
		}
	}
	UpdateLocalDepths(Result);
	return Result;
}

TArray<FWidgetCollectionItem> UWidgetCollectionLibrary::UnionWidgetCollections(const TArray<FWidgetCollectionItem>& A, const TArray<FWidgetCollectionItem>& B)
{
	TArray<FWidgetCollectionItem> Result;
	Result.Reserve(A.Num() + B.Num());
	TSet<UWidget*> Seen;
	Seen.Reserve(A.Num() + B.Num());
	for (const TArray<FWidgetCollectionItem>* Collection : {&A, &B})
	{
		for (const FWidgetCollectionItem& Item : *Collection)
		{
			if (IsValid(Item.Widget) && !Seen.Contains(Item.Widget.Get()))
			{
				Seen.Add(Item.Widget.Get());
				Result.Add(Item);
			}
		}
	}
	UpdateLocalDepths(Result);
	return Result;
}

TArray<FWidgetCollectionItem> UWidgetCollectionLibrary::FilterWidgetCollection(const TArray<FWidgetCollectionItem>& Collection, const TArray<TSubclassOf<UWidget>>& ClassesToKeep)
{
	TArray<FWidgetCollectionItem> Result;
	Result.Reserve(Collection.Num());
	for (const FWidgetCollectionItem& Item : Collection)
	{
		if (!IsValid(Item.Widget))
		{
			continue;
		}
		for (const TSubclassOf<UWidget>& Class : ClassesToKeep)
		{
			if (Class && Item.Widget->IsA(Class.Get()))
			{
				Result.Add(Item);
				break;
			}
		}
	}
	UpdateLocalDepths(Result);
	return Result;
}

TArray<FWidgetCollectionItem> UWidgetCollectionLibrary::ApplyWidgetWave(const TArray<FWidgetCollectionItem>& Widgets, UWidget* OriginWidget, EWidgetWavePattern Pattern, bool bReverseWave)
{
	TArray<FWidgetCollectionItem> Wave = Widgets;
	ApplyWaveToSelection(Wave, OriginWidget, Pattern);
	if (bReverseWave)
	{
		int32 MaxIndex = 0;
		for (const FWidgetCollectionItem& Item : Wave)
		{
			MaxIndex = FMath::Max(MaxIndex, Item.Index);
		}
		for (FWidgetCollectionItem& Item : Wave)
		{
			Item.Index = MaxIndex - Item.Index;
		}
	}
	UpdateLocalDepths(Wave);
	return Wave;
}

UWidget* UWidgetCollectionLibrary::GetWidgetParent(UWidget* Widget)
{
	return FindHierarchyParent(Widget);
}
