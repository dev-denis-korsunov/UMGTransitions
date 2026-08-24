#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"

#include "WidgetSelectorLibrary.generated.h"

class UUserWidget;
class UWidget;

/**
 * Blueprint helpers for selecting widgets from a UMG hierarchy.
 *
 * Traversal functions return widgets in depth-first tree order. The root has
 * depth zero; its direct children have depth one. When traversal reaches a
 * nested User Widget, its WidgetTree root is treated as a child. Invalid inputs
 * return an empty array (or nullptr for singular queries).
 */
UCLASS()
class ELASTICUMG_API UWidgetSelectorLibrary final : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	//~ Begin User widget queries

	/** Returns the root widget of a User Widget's WidgetTree. */
	UFUNCTION(BlueprintPure, Category = "Widget Selector", meta = (DisplayName = "Get Widget Tree Root"))
	static UWidget* GetWidgetTreeRoot(const UUserWidget* UserWidget);

	/** Finds a named widget in a User Widget's WidgetTree. */
	UFUNCTION(BlueprintPure, Category = "Widget Selector", meta = (DisplayName = "Find Widget by Name"))
	static UWidget* FindWidgetByName(const UUserWidget* UserWidget, FName Name);

	//~ End User widget queries

	//~ Begin Hierarchy traversal

	/** Returns direct panel children, or the WidgetTree root when Widget is a User Widget. */
	UFUNCTION(BlueprintPure, Category = "Widget Selector", meta = (DisplayName = "Get Widget Children"))
	static TArray<UWidget*> GetWidgetChildren(UWidget* Widget);

	/** Returns every descendant in depth-first tree order. Optionally includes Root first. */
	UFUNCTION(BlueprintPure, Category = "Widget Selector", meta = (DisplayName = "Get Widget Descendants", AdvancedDisplay = "bIncludeRoot"))
	static TArray<UWidget*> GetWidgetDescendants(UWidget* Root, bool bIncludeRoot = false);

	/** Returns widgets exactly at Depth, where Root is depth zero. */
	UFUNCTION(BlueprintPure, Category = "Widget Selector", meta = (DisplayName = "Get Widgets at Depth"))
	static TArray<UWidget*> GetWidgetsAtDepth(UWidget* Root, int32 Depth);

	/** Returns Root and its descendants through Depth, where Root is depth zero. */
	UFUNCTION(BlueprintPure, Category = "Widget Selector", meta = (DisplayName = "Get Widgets through Depth"))
	static TArray<UWidget*> GetWidgetsThroughDepth(UWidget* Root, int32 Depth);

	/** Returns the direct hierarchy parent, crossing from a WidgetTree root to its owning User Widget. */
	UFUNCTION(BlueprintPure, Category = "Widget Selector", meta = (DisplayName = "Get Widget Parent"))
	static UWidget* GetWidgetParent(UWidget* Widget);

	/** Returns hierarchy parents from the direct parent up to the root, including owning User Widgets. */
	UFUNCTION(BlueprintPure, Category = "Widget Selector", meta = (DisplayName = "Get Widget Parents"))
	static TArray<UWidget*> GetWidgetParents(UWidget* Widget);

	//~ End Hierarchy traversal

	//~ Begin Name selection

	/** Returns descendants whose object name equals Name. Optionally includes Root in the search. */
	UFUNCTION(BlueprintPure, Category = "Widget Selector", meta = (DisplayName = "Find Widget Descendants by Name", AdvancedDisplay = "bIncludeRoot"))
	static TArray<UWidget*> FindWidgetDescendantsByName(UWidget* Root, FName Name, bool bIncludeRoot = false);

	/** Returns descendants whose object name is present in Names, preserving depth-first tree order. */
	UFUNCTION(BlueprintPure, Category = "Widget Selector", meta = (DisplayName = "Find Widget Descendants by Names", AdvancedDisplay = "bIncludeRoot"))
	static TArray<UWidget*> FindWidgetDescendantsByNames(UWidget* Root, const TArray<FName>& Names, bool bIncludeRoot = false);

	//~ End Name selection
};
