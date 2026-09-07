#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"

#include "WidgetSelectorLibrary.generated.h"

class UUserWidget;
class UWidget;

UENUM(BlueprintType)
enum class EWidgetDescendantTraversal : uint8
{
	DepthFirst UMETA(DisplayName = "Depth First"),
	BreadthFirst UMETA(DisplayName = "Breadth First"),
};

UENUM(BlueprintType)
enum class EWidgetSiblingOrder : uint8
{
	LeftToRight UMETA(DisplayName = "Left to Right"),
	RightToLeft UMETA(DisplayName = "Right to Left"),
	CenterOut UMETA(DisplayName = "Center Out"),
};

UENUM(BlueprintType)
enum class EWidgetWavePattern : uint8
{
	Horizontal UMETA(DisplayName = "Horizontal"),
	Vertical UMETA(DisplayName = "Vertical"),
	Manhattan UMETA(DisplayName = "Manhattan"),
	Radial UMETA(DisplayName = "Radial"),
};

UENUM(BlueprintType)
enum class EWidgetWaveOrigin : uint8
{
	TopLeft UMETA(DisplayName = "Top Left"),
	TopRight UMETA(DisplayName = "Top Right"),
	BottomLeft UMETA(DisplayName = "Bottom Left"),
	BottomRight UMETA(DisplayName = "Bottom Right"),
	Center UMETA(DisplayName = "Center"),
	Widget UMETA(DisplayName = "Widget"),
};

USTRUCT(BlueprintType)
struct UMGTRANSITIONS_API FWidgetDescendant
{
	GENERATED_BODY()

	FWidgetDescendant() = default;
	FWidgetDescendant(int32 InWaveIndex, int32 InDepth, UWidget* InValue, FVector2D InWaveDirection = FVector2D::ZeroVector)
		: WaveIndex(InWaveIndex), WaveDirection(InWaveDirection), Depth(InDepth), Value(InValue)
	{
	}

	/** One-based animation wave index. It can repeat when widgets share a wave. */
	UPROPERTY(BlueprintReadOnly, Category = "Widget Selector")
	int32 WaveIndex = 0;

	/** Normalized screen-space direction from the wave origin to the top-level widget. */
	UPROPERTY(BlueprintReadOnly, Category = "Widget Selector")
	FVector2D WaveDirection = FVector2D::ZeroVector;

	/** Hierarchy depth relative to the input root. */
	UPROPERTY(BlueprintReadOnly, Category = "Widget Selector")
	int32 Depth = 0;

	/** Selected widget. */
	UPROPERTY(BlueprintReadOnly, Category = "Widget Selector")
	TObjectPtr<UWidget> Value = nullptr;
};

/**
 * Blueprint helpers for selecting widgets from a UMG hierarchy.
 *
 * Traversal functions return widgets in depth-first tree order. The root has
 * depth zero; its direct children have depth one. When traversal reaches a
 * nested User Widget, its WidgetTree root is treated as a child. Invalid inputs
 * return an empty array (or nullptr for singular queries).
 */
UCLASS()
class UMGTRANSITIONS_API UWidgetSelectorLibrary final : public UBlueprintFunctionLibrary
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

	/**
	 * Returns descendants up to MaxDepth. Negative MaxDepth traverses the complete hierarchy.
	 * WaveIndex is computed from the cached geometry of Root's direct children; deeper descendants inherit their top-level parent's wave.
	 */
	UFUNCTION(BlueprintPure, Category = "Widget Selector", meta = (DisplayName = "Get Widget Descendants", AdvancedDisplay = "bIncludeRoot"))
	static TArray<FWidgetDescendant> GetWidgetDescendants(UWidget* Root, int32 MaxDepth = -1, EWidgetDescendantTraversal Traversal = EWidgetDescendantTraversal::DepthFirst, EWidgetSiblingOrder SiblingOrder = EWidgetSiblingOrder::LeftToRight, EWidgetWavePattern WavePattern = EWidgetWavePattern::Manhattan, EWidgetWaveOrigin WaveOrigin = EWidgetWaveOrigin::Center, UWidget* OriginWidget = nullptr, bool bIncludeRoot = false);

	/** Compatibility passthrough for graphs created before wave settings moved to Get Widget Descendants. */
	UFUNCTION(BlueprintPure, Category = "Widget Selector", meta = (DeprecatedFunction, DeprecationMessage = "Configure the wave directly on Get Widget Descendants.", BlueprintInternalUseOnly = "true"))
	static TArray<FWidgetDescendant> BuildWidgetWave(const TArray<FWidgetDescendant>& Descendants, int32 Columns = 1, EWidgetWavePattern Pattern = EWidgetWavePattern::Manhattan, EWidgetWaveOrigin Origin = EWidgetWaveOrigin::Center, UWidget* OriginWidget = nullptr);

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
