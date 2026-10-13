#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"

#include "WidgetCollectionLibrary.generated.h"

class UWidget;

UENUM(BlueprintType)
enum class EWidgetWavePattern : uint8
{
	Horizontal UMETA(DisplayName = "Horizontal"),
	Vertical UMETA(DisplayName = "Vertical"),
	Manhattan UMETA(DisplayName = "Manhattan"),
	Radial UMETA(DisplayName = "Radial"),
};

USTRUCT(BlueprintType)
struct UMGTRANSITIONS_API FWidgetCollectionItem
{
	GENERATED_BODY()

	FWidgetCollectionItem() = default;
	FWidgetCollectionItem(int32 InIndex, int32 InDepth, UWidget* InWidget, FVector2D InDirection = FVector2D::ZeroVector)
		: Index(InIndex), Direction(InDirection), Depth(InDepth), Widget(InWidget)
	{
	}

	/** Zero-based launch index; equal indices launch together. */
	UPROPERTY(BlueprintReadOnly, Category = "Widget Collection")
	int32 Index = 0;

	/** Normalized screen-space direction from the wave origin. */
	UPROPERTY(BlueprintReadOnly, Category = "Widget Collection")
	FVector2D Direction = FVector2D::ZeroVector;

	/** Number of selected ancestors; collection roots have depth zero. */
	UPROPERTY(BlueprintReadOnly, Category = "Widget Collection")
	int32 Depth = 0;

	/** Selected widget. */
	UPROPERTY(BlueprintReadOnly, Category = "Widget Collection")
	TObjectPtr<UWidget> Widget = nullptr;
};

/** UMG collection helpers. All results use local depth. */
UCLASS()
class UMGTRANSITIONS_API UWidgetCollectionLibrary final : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** Collects names matching a case-sensitive Unreal wildcard (* and ?). */
	UFUNCTION(BlueprintPure, Category = "Widget Collection", meta = (DisplayName = "Collect Widgets by Name", DefaultToSelf = "Root"))
	static TArray<FWidgetCollectionItem> CollectWidgetsByName(UWidget* Root, const FString& NamePattern, bool bIncludeRoot = false);

	/** Depth-first collection. MaxDepth = -1 is unlimited; bDepthOnly selects only MaxDepth.
	 * Depth-only level zero selects Root; negative levels return empty. */
	UFUNCTION(BlueprintPure, Category = "Widget Collection", meta = (DisplayName = "Collect Widgets", DefaultToSelf = "Root", AdvancedDisplay = "bIncludeRoot"))
	static TArray<FWidgetCollectionItem> CollectWidgets(UWidget* Root, int32 MaxDepth = -1, bool bIncludeRoot = false, bool bDepthOnly = false);

	/** Creates a collection, skipping invalid widgets and duplicates. */
	UFUNCTION(BlueprintPure, Category = "Widget Collection")
	static TArray<FWidgetCollectionItem> MakeWidgetCollection(const TArray<UWidget*>& Widgets);

	/** Stable union by widget identity. First item wins; local depth is recalculated. */
	UFUNCTION(BlueprintPure, Category = "Widget Collection", meta = (CompactNodeTitle = "+"))
	static TArray<FWidgetCollectionItem> UnionWidgetCollections(const TArray<FWidgetCollectionItem>& A, const TArray<FWidgetCollectionItem>& B);

	/** Keeps listed classes and subclasses; recalculates local depth. Empty list returns empty. */
	UFUNCTION(BlueprintPure, Category = "Widget Collection")
	static TArray<FWidgetCollectionItem> FilterWidgetCollection(const TArray<FWidgetCollectionItem>& Collection, const TArray<TSubclassOf<UWidget>>& ClassesToKeep);

	/** Computes waves from OriginWidget's cached center; recalculates local depth.
	 * bReverseWave launches distant widgets first. Missing origin geometry uses sequential indices. */
	UFUNCTION(BlueprintPure, Category = "Widget Collection", meta = (DisplayName = "Apply Widget Wave"))
	static TArray<FWidgetCollectionItem> ApplyWidgetWave(const TArray<FWidgetCollectionItem>& Widgets, UWidget* OriginWidget, EWidgetWavePattern Pattern = EWidgetWavePattern::Manhattan, bool bReverseWave = false);

	/** Draws a temporary viewport outline with optional item metadata. */
	UFUNCTION(BlueprintCallable, Category = "Widget Collection|Debug", meta = (DevelopmentOnly, DisplayName = "Draw Debug Collection Item"))
	static void DrawDebugCollectionItem(const FWidgetCollectionItem& Item, float Duration = 2.0f, bool bShowDepth = true, bool bShowIndex = true, FLinearColor Color = FLinearColor(0.2f, 1.0f, 0.2f, 1.0f), bool bShowName = false, bool bColorByIndex = false, bool bColorByDepth = false, UPARAM(meta = (ClampMin = "0.0", ClampMax = "1.0")) float IndexOpacity = 0.08f, UPARAM(meta = (ClampMin = "0.0", ClampMax = "1.0")) float DepthOpacity = 0.08f);

	/** Returns the parent, crossing WidgetTree roots to their owning User Widget. */
	UFUNCTION(BlueprintPure, Category = "Widget Collection", meta = (DisplayName = "Get Widget Parent"))
	static UWidget* GetWidgetParent(UWidget* Widget);
};
