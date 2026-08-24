#include "WidgetSelectorLibrary.h"

#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/PanelWidget.h"
#include "Components/Widget.h"

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

	static void AppendDescendants(UWidget* Root, TArray<UWidget*>& OutWidgets)
	{
		TArray<UWidget*> Children;
		AppendDirectChildren(Root, Children);
		for (UWidget* Child : Children)
		{
			OutWidgets.Add(Child);
			AppendDescendants(Child, OutWidgets);
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

TArray<UWidget*> UWidgetSelectorLibrary::GetWidgetDescendants(UWidget* Root, bool bIncludeRoot)
{
	TArray<UWidget*> Widgets;
	if (!IsValid(Root))
	{
		return Widgets;
	}

	if (bIncludeRoot)
	{
		Widgets.Add(Root);
	}
	WidgetSelector::AppendDescendants(Root, Widgets);
	return Widgets;
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

	const TArray<UWidget*> Widgets = GetWidgetDescendants(Root, bIncludeRoot);
	for (UWidget* Widget : Widgets)
	{
		if (Names.Contains(Widget->GetFName()))
		{
			Matches.Add(Widget);
		}
	}
	return Matches;
}
