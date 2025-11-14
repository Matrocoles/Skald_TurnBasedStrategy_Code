#pragma once

#include "GridObstacleActor.h"
#include "MoatGridObstacleActor.generated.h"

class UGridOverlayComponent;

/**
 * Toggleable obstacle that represents a capital's moat on the battle map.
 */
UCLASS()
class SKALD_API AMoatGridObstacleActor : public AGridObstacleActor {
  GENERATED_BODY()

public:
  AMoatGridObstacleActor();

  virtual void OnConstruction(const FTransform &Transform) override;
  virtual void BeginPlay() override;
  virtual void GetLifetimeReplicatedProps(
      TArray<FLifetimeProperty> &OutLifetimeProps) const override;

  /** Show or hide the moat obstacle during a battle. */
  UFUNCTION(BlueprintCallable, Category = "Moat")
  void SetMoatEnabled(bool bEnabled);

  /** Return true if this moat should activate for the supplied territory ID. */
  UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Moat")
  bool SupportsTerritory(int32 TerritoryID) const;

  /** Specific territory IDs this moat applies to. Empty = all capital battles. */
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Moat")
  TArray<int32> SupportedTerritoryIDs;

  /** Whether the moat should remain visible while editing the level. */
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Moat")
  bool bPreviewInEditor = true;

protected:
  UFUNCTION()
  void OnRep_MoatActive();

  void UpdateMoatVisuals();
  void RefreshObstacleRegistration();

  /** Tracks whether the moat should currently be visible and collidable. */
  UPROPERTY(ReplicatedUsing = OnRep_MoatActive)
  bool bMoatActive = true;

  /** True when the obstacle has been registered with the active grid overlay. */
  bool bObstacleRegistered = false;

  /** Cached grid overlay that currently owns this obstacle registration. */
  TWeakObjectPtr<UGridOverlayComponent> RegisteredGrid;
};
