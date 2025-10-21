#pragma once

#include "Blueprint/UserWidget.h"
#include "Components/WidgetComponent.h"
#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "GridBattleManager.h"
#include "TimerManager.h"
#include "FighterPawn.generated.h"

class UGridOverlayComponent;
class UCapsuleComponent;
class UTexture2D;
class UFighterActivationWidget;
class UFighterHealthWidget;
class UCurveFloat;
class UMaterialInstanceDynamic;

UENUM(BlueprintType)
enum class EFighterPawnFootprint : uint8 {
  SingleCell UMETA(DisplayName = "1 Cell"),
  FourCells UMETA(DisplayName = "4 Cells")
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnHealthChanged, int32, NewHealth);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnActionsChanged, int32,
                                            NewActionsRemaining);
DECLARE_MULTICAST_DELEGATE(FOnQueuedAttackFinalized);

/** Pawn representing a fighter in grid battles. */
UCLASS()
class SKALD_API AFighterPawn : public APawn {
  GENERATED_BODY()

public:
  AFighterPawn();

  virtual void Tick(float DeltaSeconds) override;
  virtual void OnConstruction(const FTransform &Transform) override;
  virtual void BeginPlay() override;
  virtual void GetLifetimeReplicatedProps(
      TArray<FLifetimeProperty> &OutLifetimeProps) const override;

  /** Prepare the fighter for its activation. */
  UFUNCTION(BlueprintCallable, Category = "Fighter")
  void BeginActivation();

  /** Clear round-based activation flags. */
  UFUNCTION(BlueprintCallable, Category = "Fighter")
  void ResetActivationState();

  /** Mark this fighter's turn as complete. */
  UFUNCTION(BlueprintCallable, Category = "Fighter")
  void FinishActivation();

  /** Move to the specified grid cell if actions remain. */
  UFUNCTION(BlueprintCallable, Category = "Fighter")
  void MoveToCell(FIntPoint TargetCell);

  /** Units per second used when travelling between grid cells. */
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fighter|Movement",
            meta = (ClampMin = "0.0"))
  float MovementSpeed = 600.f;

  /** Distance threshold for snapping to the target cell during movement. */
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fighter|Movement",
            meta = (ClampMin = "0.0"))
  float MovementStopTolerance = 1.f;

  /** Perform an attack against another fighter. */
  UFUNCTION(BlueprintCallable, Category = "Fighter")
  void PerformAttack(AFighterPawn *Target);

  /** True while queued attack rolls are still being processed. */
  bool IsResolvingQueuedAttack() const;

  /** Event fired after any queued attack finishes resolving. */
  FOnQueuedAttackFinalized OnQueuedAttackFinalized;

  /** Check whether this fighter is still alive. */
  UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Fighter")
  bool IsAlive() const;

  /** Get the grid overlay component, caching the result. */
  UGridOverlayComponent *GetGrid() const;

  /** Retrieve the grid cell currently occupied by this fighter. */
  FIntPoint GetCurrentCell() const;

  /** Cells occupied by this fighter given an anchor grid coordinate. */
  TArray<FIntPoint> GetOccupiedCells(const FIntPoint &Anchor) const;

  /** Cells currently occupied by this fighter. */
  TArray<FIntPoint> GetOccupiedCells() const { return GetOccupiedCells(CurrentCell); }

  /** Returns true if the fighter's footprint overlaps the specified cell. */
  bool OccupiesCell(const FIntPoint &Cell) const;

  /** Chebyshev distance from the fighter's footprint to a specific cell. */
  int32 GetFootprintDistanceToCell(const FIntPoint &Cell,
                                   FIntPoint *OutClosestCell = nullptr) const;

  /** Chebyshev distance between this fighter's footprint and another's. */
  int32 GetFootprintDistanceToFighter(
      const AFighterPawn *Other, FIntPoint *OutSelfCell = nullptr,
      FIntPoint *OutOtherCell = nullptr) const;

  /** Determine if any occupied cells have line of sight within range. */
  bool HasLineOfSightToFighter(const AFighterPawn *Other, int32 Range,
                               UGridOverlayComponent *Grid,
                               FIntPoint *OutSelfCell = nullptr,
                               FIntPoint *OutOtherCell = nullptr) const;

  /** Size of the fighter footprint measured in cells per side. */
  int32 GetFootprintSideLength() const;

  /** Statistics describing this fighter. */
  UPROPERTY(BlueprintReadWrite, EditAnywhere, ReplicatedUsing = OnRep_Stats,
            Category = "Fighter")
  FFighterStats Stats;

  /** Identifier of the fighter definition used to spawn this pawn. */
  UPROPERTY(BlueprintReadOnly, Replicated, Category = "Fighter")
  FName FighterId;

  /** Faction owning this fighter instance. */
  UPROPERTY(BlueprintReadWrite, EditAnywhere, Replicated, Category = "Fighter")
  ESkaldFaction Faction = ESkaldFaction::None;

  /** Portrait associated with this fighter definition. */
  UPROPERTY(BlueprintReadOnly, Replicated, Category = "Fighter|UI")
  TSoftObjectPtr<UTexture2D> FighterPortrait;

  /** Number of grid cells occupied by this fighter (1 or 4). */
  UPROPERTY(EditAnywhere, BlueprintReadWrite,
            ReplicatedUsing = OnRep_GridFootprint, Category = "Fighter|Grid")
  EFighterPawnFootprint GridFootprint = EFighterPawnFootprint::SingleCell;

  /** True if this fighter belongs to the attacking side. */
  UPROPERTY(BlueprintReadWrite, EditAnywhere, Replicated,
            Category = "Fighter")
  bool bIsAttacker = false;

  /** Actions remaining for the current activation. */
  UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_ActionsRemaining,
            Category = "Fighter")
  int32 ActionsRemaining;

  /** True once the fighter has activated during the current round. */
  UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_HasActivatedThisRound,
            Category = "Fighter")
  bool bHasActivatedThisRound;

  /** True while this fighter is currently taking its activation. */
  UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_IsCurrentlyActive,
            Category = "Fighter")
  bool bIsCurrentlyActive;

  /** True to override the incoming spawn rotation with SpawnFacingYaw. */
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fighter|Appearance")
  bool bOverrideSpawnFacingYaw = false;

  /** Desired world yaw to apply at spawn while preserving mesh offsets when
      overriding the spawn rotation. */
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fighter|Appearance",
            meta = (EditCondition = "bOverrideSpawnFacingYaw"))
  float SpawnFacingYaw = 0.f;

  /** Offset applied to locally computed facings to preserve the desired spawn
      orientation when overriding incoming rotations. */
  UPROPERTY(Replicated)
  float SpawnFacingYawDelta = 0.f;

  /** Mesh used to display the fighter. */
  UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
  UStaticMeshComponent *DisplayMesh;

  /** Collision capsule used for movement and placement. */
  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Fighter",
            meta = (AllowPrivateAccess = "true"))
  UCapsuleComponent *CollisionComponent;

  /** Widget displaying the current health. */
  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Fighter|UI")
  UWidgetComponent *HealthWidget;

  /** Rear-facing widget mirroring the current health display. */
  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Fighter|UI")
  UWidgetComponent *HealthWidgetBack;

  /** Widget class used for the health display. */
  UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Fighter|UI")
  TSubclassOf<UUserWidget> HealthWidgetTemplate;

  /** Widget indicating activation state (front facing). */
  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Fighter|UI")
  UWidgetComponent *ActivationWidget;

  /** Widget indicating activation state (rear facing). */
  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Fighter|UI")
  UWidgetComponent *ActivationWidgetBack;

  /** Widget class used for the activation indicator. */
  UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Fighter|UI")
  TSubclassOf<UUserWidget> ActivationWidgetTemplate;

  /** Maximum health used for percentage calculations. */
  UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_MaxHealth,
            Category = "Fighter")
  int32 MaxHealth;

  /** Icon displayed while the fighter is taking its activation. */
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fighter|UI")
  TSoftObjectPtr<UTexture2D> ActivationReadyIcon;

  /** Icon displayed once the fighter has exhausted its actions. */
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fighter|UI")
  TSoftObjectPtr<UTexture2D> ActivationSpentIcon;

  /** Event broadcast when health changes. */
  UPROPERTY(BlueprintAssignable, Category = "Fighter|Events")
  FOnHealthChanged OnHealthChanged;

  /** Event broadcast when actions remaining changes. */
  UPROPERTY(BlueprintAssignable, Category = "Fighter|Events")
  FOnActionsChanged OnActionsChanged;

  /** Retrieve the identifier associated with this fighter. */
  FName GetFighterId() const { return FighterId; }

  /** Maximum health for this fighter. */
  int32 GetMaxHealth() const { return MaxHealth; }

  /** Initialize the fighter's maximum health. */
  void InitializeMaxHealth(int32 InMaxHealth);

  /** Temporarily locks the health widget to the provided value. */
  void HoldHealthDisplay(int32 DisplayHealth);

  /** Releases any active hold and applies pending health changes. */
  void ReleaseHealthDisplayHold();

  /** Resolve the portrait texture for this fighter if available. */
  UTexture2D *GetPortraitTexture() const;

protected:
  /** Clear grid occupancy when the fighter is destroyed. */
  virtual void Destroyed() override;

private:
  /** Whether the health display should remain fixed during presentation. */
  bool bHoldHealthDisplay = false;

  /** True when a new health value is waiting for the hold to end. */
  bool bHasPendingHealthDisplay = false;

  /** Deferred health value that will be applied once unlocked. */
  int32 PendingHealthDisplayValue = 0;

  /** True when damage triggered an automatic hold awaiting presentation. */
  bool bAutoHealthDisplayHoldActive = false;

  /** Tracks whether an external system claimed the current health hold. */
  bool bHealthDisplayHoldClaimed = false;

  /** Timer used to release automatic holds if no presentation consumes them. */
  FTimerHandle AutoHealthHoldTimerHandle;

  bool ShouldOverrideSpawnFacingYaw() const;
  float GetCurrentWorldFacingYaw() const;

  /** Update the health widget with a new value. */
  UFUNCTION()
  void UpdateHealthDisplay(int32 NewHealth);

  /** Handles health change broadcasts so we can defer UI updates. */
  UFUNCTION()
  void HandleHealthChanged(int32 NewHealth);

  /** Release an automatic hold if no presentation claimed it in time. */
  void HandleAutoHealthHoldExpired();

  /** Respond when the fighter stats replicate to clients. */
  UFUNCTION()
  void OnRep_Stats(const FFighterStats &OldStats);
  UFUNCTION()
  void OnRep_ActionsRemaining();
  UFUNCTION()
  void OnRep_GridFootprint();
  UFUNCTION()
  void OnRep_HasActivatedThisRound();
  UFUNCTION()
  void OnRep_IsCurrentlyActive();
  UFUNCTION()
  void OnRep_MaxHealth();

  /** Align the visible mesh with the collision capsule. */
  void UpdateMeshOffset();

  /** Apply scale changes based on the footprint size. */
  void ApplyFootprintScale();

  /** Align the fighter's world position with its occupied cells. */
  void AlignToCurrentCell();

  /** Calculate the aligned world location for a given anchor cell. */
  FVector GetAlignedWorldLocation(const FIntPoint &Anchor) const;

  /** Capture the display mesh's yaw offset for use when orienting the pawn. */
  void RefreshDisplayMeshYawOffset();

  /** Helper to broadcast the current actions remaining value. */
  void BroadcastActionsRemaining();

  /** Ensure the activation widget instance is ready for use. */
  void EnsureActivationWidget();

  /** Update the activation widget to match fighter state. */
  void UpdateActivationIndicator();

  /** Resolve and cache an activation icon texture. */
  UTexture2D *ResolveActivationIcon(TSoftObjectPtr<UTexture2D> &IconSource,
                                    UTexture2D *&CachedTexture);

  /** Apply the mesh's yaw offset when orienting the fighter. */
  void ApplyFacingYaw(float TargetYaw);

  /** Rotate the fighter to face a world-space location. */
  void FaceTowardsLocation(const FVector &TargetLocation);

  /** Rotate the fighter to face from one grid cell towards another. */
  void FaceTowardsCells(const FIntPoint &FromCell, const FIntPoint &ToCell);

  /** Prepare and cache dynamic materials used for hit feedback. */
  void InitializeDisplayMeshMaterials();

  /** Begin a hit flash using the supplied damage ratio for intensity. */
  void TriggerHitFlash(float DamageRatio);

  /** Advance the active hit flash timeline. */
  void UpdateHitFlash(float DeltaSeconds);

  /** Apply a normalised flash value across cached material instances. */
  void ApplyHitFlash(float NormalisedValue);

  /** Begin resolving queued attack rolls with a delay between each. */
  void StartQueuedAttack(AFighterPawn *Target, FDiceRollResult &&DiceResult);

  /** Apply the next queued attack roll, showing the appropriate widget. */
  void ResolveNextAttackRoll();

  /** Finalise any pending attack resolution and clean up timers/state. */
  void FinalizeQueuedAttack();

  /** Cancel any pending queued attack without reporting results. */
  void CancelQueuedAttack();

  /** Reset queued attack bookkeeping and optionally notify listeners. */
  void ClearQueuedAttackState(bool bBroadcastFinalized);

  /** Index of the next pending dice outcome to resolve. */
  int32 PendingAttackOutcomeIndex = 0;

  /** Target currently receiving delayed attack rolls. */
  TWeakObjectPtr<AFighterPawn> PendingAttackTarget;

  /** Whether the pending attack target has been reduced to zero health. */
  bool bPendingAttackTargetDied = false;

  /** Timer driving delayed attack roll resolution. */
  FTimerHandle AttackRollTimerHandle;

  /** Tracks whether any pending attack roll has been processed. */
  bool bHasProcessedPendingRoll = false;

  /** Cached dice roll data to broadcast once resolution completes. */
  FDiceRollResult PendingAttackDiceResult;

  /** Tracks whether cached dice data should be reported on finalisation. */
  bool bHasPendingDiceResult = false;

  /** Current cell occupied by the fighter. */
  UPROPERTY(Replicated)
  FIntPoint CurrentCell;

  /** Cached grid overlay component. */
  mutable UGridOverlayComponent *CachedGrid = nullptr;

  /** True while the fighter is interpolating towards a grid cell. */
  bool bIsMoving = false;

  /** World-space destination for the current interpolated move. */
  FVector MovementTargetLocation = FVector::ZeroVector;

  /** Cached yaw offset derived from the display mesh's relative rotation. */
  float DisplayMeshYawOffset = 0.f;

  /** Cached activation widget reference (front). */
  TWeakObjectPtr<UFighterActivationWidget> CachedActivationWidget;

  /** Cached activation widget reference (rear). */
  TWeakObjectPtr<UFighterActivationWidget> CachedActivationWidgetBack;

  /** Cached ready icon texture. */
  UPROPERTY(Transient)
  UTexture2D *ActivationReadyTexture = nullptr;

  /** Cached spent icon texture. */
  UPROPERTY(Transient)
  UTexture2D *ActivationSpentTexture = nullptr;

  /** Scalar curve controlling the hit flash falloff. */
  UPROPERTY(EditDefaultsOnly, Category = "Fighter|VFX")
  TObjectPtr<UCurveFloat> HitFlashCurve = nullptr;

  /** Duration of the hit flash when no curve data is provided. */
  UPROPERTY(EditDefaultsOnly, Category = "Fighter|VFX")
  float HitFlashDuration = 0.35f;

  /** Parameter driven on the display mesh to provide hit feedback. */
  UPROPERTY(EditDefaultsOnly, Category = "Fighter|VFX")
  FName HitFlashParameterName = TEXT("HitFlash");

  /** Dynamic materials sourced from the display mesh for hit effects. */
  UPROPERTY()
  TArray<TObjectPtr<UMaterialInstanceDynamic>> CachedDisplayMeshMIDs;

  float HitFlashElapsed = 0.f;
  float HitFlashStrength = 1.f;
  bool bHitFlashActive = false;
  bool bHasRecordedHealth = false;
  int32 LastKnownHealth = 0;

public:
  /** Returns true if the fighter has already activated this round. */
  bool HasActivatedThisRound() const { return bHasActivatedThisRound; }
};
