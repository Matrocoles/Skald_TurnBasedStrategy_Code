#pragma once

#include "FighterPawn.h"
#include "MainGateFighterPawn.generated.h"

/** Fighter pawn that represents a capital's main gate obstacle. */
UCLASS()
class SKALD_API AMainGateFighterPawn : public AFighterPawn {
  GENERATED_BODY()

public:
  AMainGateFighterPawn();

  virtual void OnConstruction(const FTransform &Transform) override;
  virtual void BeginPlay() override;
  virtual void GetLifetimeReplicatedProps(
      TArray<FLifetimeProperty> &OutLifetimeProps) const override;

  /** Return true if this gate should activate for the supplied territory ID. */
  UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Gate")
  bool SupportsTerritory(int32 TerritoryID) const;

  /** Build the stats this gate should use for an upcoming battle. */
  UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Gate")
  FFighterStats BuildBattleStats(int32 FortificationBonus) const;

  /** Apply battle-ready stats to the live pawn and refresh its health display. */
  void ApplyBattleStats(const FFighterStats &BattleStats);

  /** Toggle whether the gate should be visible/collidable on the battle map. */
  UFUNCTION(BlueprintCallable, Category = "Gate")
  void SetGateEnabled(bool bEnabled);

  /** Capital territory IDs that this gate should defend. Empty = all capitals. */
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gate")
  TArray<int32> SupportedTerritoryIDs;

  /** Base health of the gate before fortification bonuses are applied. */
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gate",
            meta = (ClampMin = "1"))
  int32 BaseGateHealth;

protected:
  /** Blueprint-configured stats cached so future battles can restore defaults. */
  UPROPERTY()
  FFighterStats ConfiguredStats;

private:
  void RestoreConfiguredStats();
  void UpdateGridOccupancy(bool bOccupied);

  UFUNCTION()
  void OnRep_GateActive();

  /** Tracks whether the gate is currently visible/collidable. */
  UPROPERTY(ReplicatedUsing = OnRep_GateActive)
  bool bGateActive = false;
};
