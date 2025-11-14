#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "SkaldTypes.h"
#include "SkaldPlayerListEntryWidget.generated.h"

class UTextBlock;
class UWidget;

/**
 * Base widget used for entries in the strategic HUD player list.
 *
 * Blueprint subclasses can override OnPlayerDataUpdated to push the exposed
 * data into whatever text/image widgets they like, allowing easy styling in
 * the UMG editor.
 */
UCLASS(Abstract, BlueprintType, Blueprintable)
class SKALD_API USkaldPlayerListEntryWidget : public UUserWidget
{
  GENERATED_BODY()

public:
  /** Called by the HUD to populate the entry with the latest player data. */
  UFUNCTION(BlueprintCallable, Category = "Skald|PlayerList")
  void SetupPlayerEntry(const FS_PlayerData& InPlayerData, int32 InTerritoryCount);

  /** Most recent snapshot of the player associated with this entry. */
  UPROPERTY(BlueprintReadOnly, Category = "Skald|PlayerList")
  FS_PlayerData PlayerData;

  /** Human readable faction name resolved from PlayerData.Faction. */
  UPROPERTY(BlueprintReadOnly, Category = "Skald|PlayerList")
  FText FactionDisplayName;

  /** Number of territories currently owned by the player. */
  UPROPERTY(BlueprintReadOnly, Category = "Skald|PlayerList")
  int32 TerritoryCount = 0;

protected:
  /** Native hook that fires whenever the player data is refreshed. */
  virtual void NativeOnPlayerDataUpdated();

  /** Blueprint hook for updating the visual representation of the entry. */
  UFUNCTION(BlueprintImplementableEvent, Category = "Skald|PlayerList")
  void OnPlayerDataUpdated();

  /** Optional text block automatically populated with the player's display name. */
  UPROPERTY(BlueprintReadOnly, Category = "Skald|PlayerList|Bindings",
            meta = (BindWidgetOptional))
  UTextBlock *PlayerNameText = nullptr;

  /** Optional text block automatically populated with the faction display name. */
  UPROPERTY(BlueprintReadOnly, Category = "Skald|PlayerList|Bindings",
            meta = (BindWidgetOptional))
  UTextBlock *FactionNameText = nullptr;

  /** Optional text block automatically populated with the owned territory count. */
  UPROPERTY(BlueprintReadOnly, Category = "Skald|PlayerList|Bindings",
            meta = (BindWidgetOptional))
  UTextBlock *TerritoryCountText = nullptr;

  /** Optional text block automatically populated with the capital count. */
  UPROPERTY(BlueprintReadOnly, Category = "Skald|PlayerList|Bindings",
            meta = (BindWidgetOptional))
  UTextBlock *CapitalCountText = nullptr;

  /** Optional text block automatically populated with the player's troop count. */
  UPROPERTY(BlueprintReadOnly, Category = "Skald|PlayerList|Bindings",
            meta = (BindWidgetOptional))
  UTextBlock *TroopCountText = nullptr;

  /** Optional text block automatically populated with the player's gold total. */
  UPROPERTY(BlueprintReadOnly, Category = "Skald|PlayerList|Bindings",
            meta = (BindWidgetOptional))
  UTextBlock *GoldCountText = nullptr;

  /** Legacy binding retained for widgets that still expose a ResourceCountText field. */
  UPROPERTY(BlueprintReadOnly, Category = "Skald|PlayerList|Bindings",
            meta = (BindWidgetOptional))
  UTextBlock *ResourceCountText = nullptr;

  /** Optional widget toggled visible when the player is controlled by AI. */
  UPROPERTY(BlueprintReadOnly, Category = "Skald|PlayerList|Bindings",
            meta = (BindWidgetOptional))
  UWidget *AIIndicatorWidget = nullptr;

  /** Optional widget toggled visible when the player has been eliminated. */
  UPROPERTY(BlueprintReadOnly, Category = "Skald|PlayerList|Bindings",
            meta = (BindWidgetOptional))
  UWidget *EliminatedIndicatorWidget = nullptr;
};

