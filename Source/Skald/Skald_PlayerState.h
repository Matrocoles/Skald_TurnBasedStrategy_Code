#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerState.h"
#include "GridBattleManager.h"
#include "SkaldTypes.h"
#include "TimerManager.h"
#include "Skald_PlayerState.generated.h"

namespace Skald {
namespace ArmyPlacement {
/** Maximum number of deployable units allowed per territory during army
 * placement. */
static constexpr int32 DeployPerTerritoryLimit = 10;
} // namespace ArmyPlacement
} // namespace Skald

/**
 * Player state containing basic information for turn management.
 */
UCLASS(Blueprintable, BlueprintType)
class SKALD_API ASkaldPlayerState : public APlayerState {
  GENERATED_BODY()

public:
  ASkaldPlayerState();

  virtual void BeginPlay() override;

  /** Retrieve the player name, logging if none has been assigned. */
  FString GetResolvedPlayerName(const TCHAR *Context = TEXT("SkaldPlayerState"))
      const;

  /** Ensure player-facing name fields are populated with a deterministic default. */
  void EnsureDefaultPlayerName();

  /** Deployable units available for placement. */
  UPROPERTY(BlueprintReadWrite, ReplicatedUsing = OnRep_DeployableUnits,
            Category = "PlayerState")
  int32 DeployableUnits;

  /** Initiative roll determining turn order. */
  UPROPERTY(BlueprintReadWrite, Replicated, Category = "PlayerState")
  int32 InitiativeRoll;

  /** Resource points available to the player. */
  UPROPERTY(BlueprintReadWrite, Replicated, Category = "PlayerState")
  int32 Resources;

  /** Player chosen display name. */
  UPROPERTY(BlueprintReadWrite, ReplicatedUsing = OnRep_PlayerDisplayName,
            Category = "PlayerState")
  FString PlayerDisplayName;

  /** Selected faction for this player. */
  UPROPERTY(Replicated, BlueprintReadOnly, Category = "Skald|Player")
  ESkaldFaction Faction = ESkaldFaction::None;

  /** Fighters chosen during pre-battle selection. */
  UPROPERTY(Replicated, BlueprintReadOnly, Category = "Skald|Player")
  TArray<FFighterDefinition> PendingArmy;

  /** Maximum budget allowed for the pending army selection. */
  UPROPERTY(ReplicatedUsing = OnRep_PendingArmyBudget, BlueprintReadOnly,
            Category = "Skald|Player")
  int32 PendingArmyBudget = 0;

  /** Whether the player has finalised their army selection. */
  UPROPERTY(Replicated, BlueprintReadOnly, Category = "Skald|Player")
  bool bArmyLockedIn = false;

  /** Whether this player is controlled by AI. */
  UPROPERTY(BlueprintReadWrite, ReplicatedUsing = OnRep_IsAI,
            Category = "PlayerState")
  bool bIsAI;

  /** Whether the player has locked in their actions for the current turn. */
  UPROPERTY(BlueprintReadWrite, ReplicatedUsing = OnRep_HasLockedIn,
            Category = "PlayerState")
  bool bHasLockedIn;

  /** Whether this player has been eliminated from the match. */
  UPROPERTY(BlueprintReadWrite, ReplicatedUsing = OnRep_IsEliminated,
            Category = "PlayerState")
  bool IsEliminated;

  /** Territory currently selected by this player. */
  UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_SelectedTerritory,
            Category = "Skald|Selection")
  TObjectPtr<class ATerritory> SelectedTerritory = nullptr;

private:
  UFUNCTION()
  void OnRep_PendingArmyBudget();

  /** Stable identifier replicated to all clients for consistent selection routing. */
  UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_StablePlayerId,
            Category = "PlayerState")
  int32 StablePlayerId;

  UFUNCTION()
  void OnRep_DeployableUnits();

  UFUNCTION()
  void OnRep_HasLockedIn();

  UFUNCTION()
  void OnRep_IsEliminated();

  UFUNCTION()
  void OnRep_PlayerDisplayName();

  UFUNCTION()
  void OnRep_IsAI();

  virtual void OnRep_PlayerId() override;

  UFUNCTION()
  void OnRep_StablePlayerId();

  UFUNCTION()
  void OnRep_SelectedTerritory();

  virtual void GetLifetimeReplicatedProps(
      TArray<FLifetimeProperty> &OutLifetimeProps) const override;

  /** Clears per-territory deployment tracking for the army placement phase. */
  void ResetArmyPlacementDeployments();

  /** Records an additional deployment to the specified territory. */
  void AddArmyPlacementDeployment(int32 TerritoryId, int32 Amount);

  /** Retrieves the number of units deployed to the territory this phase. */
  int32 GetArmyPlacementDeploymentForTerritory(int32 TerritoryId) const;

  /** Returns true if any deployments were recorded during the current phase. */
  bool HasArmyPlacementDeployments() const;

  /** Unique identifier that remains stable even before PlayerId replication. */
  int32 GetAuthoritativePlayerId() const;

  /** Stable identifier safe to use for routing selection events client-side. */
  int32 GetStablePlayerId() const;

  /** Server-side setter used to validate and update the player's selection. */
  void SetSelectedTerritory(class ATerritory *Territory);

  /** Refreshes and replicates the stable identifier on the server. */
  void RefreshStablePlayerId();

private:
  /** Tracks deployments per territory during the army placement phase. */
  UPROPERTY()
  TMap<int32, int32> ArmyPlacementDeployments;

  /** Timer used to retry selection application until the world map is ready. */
  FTimerHandle SelectionReplayTimerHandle;

  /** Attempts to push the replicated selection into the WorldMap when present. */
  void ApplySelectedTerritoryToWorldMap(bool bAllowRetry);

  /** Clears the pending retry timer if one is active. */
  void ClearSelectionReplayTimer();

  /** Timer callback used to retry selection application after map spawn. */
  void RetryApplySelectedTerritory();
};
