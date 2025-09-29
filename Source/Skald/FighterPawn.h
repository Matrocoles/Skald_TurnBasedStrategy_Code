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

UENUM(BlueprintType)
enum class EFighterPawnFootprint : uint8 {
  SingleCell UMETA(DisplayName = "1 Cell"),
  FourCells UMETA(DisplayName = "4 Cells")
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnHealthChanged, int32, NewHealth);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnActionsChanged, int32,
                                            NewActionsRemaining);

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

  /** Size of the fighter footprint measured in cells per side. */
  int32 GetFootprintSideLength() const;

  /** Statistics describing this fighter. */
  UPROPERTY(BlueprintReadWrite, EditAnywhere, ReplicatedUsing = OnRep_Stats,
            Category = "Fighter")
  FFighterStats Stats;

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
  UPROPERTY(BlueprintReadOnly, Replicated, Category = "Fighter")
  bool bHasActivatedThisRound;

  /** True while this fighter is currently taking its activation. */
  UPROPERTY(BlueprintReadOnly, Replicated, Category = "Fighter")
  bool bIsCurrentlyActive;

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

  /** Widget class used for the health display. */
  UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Fighter|UI")
  TSubclassOf<UUserWidget> HealthWidgetTemplate;

  /** Widget class used for optional damage float indicators. */
  UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Fighter|UI")
  TSubclassOf<UUserWidget> DamageFloatWidgetTemplate;

  /** Widget class used for optional miss indicators. */
  UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Fighter|UI")
  TSubclassOf<UUserWidget> MissWidgetTemplate;

  /** Event broadcast when health changes. */
  UPROPERTY(BlueprintAssignable, Category = "Fighter|Events")
  FOnHealthChanged OnHealthChanged;

  /** Event broadcast when actions remaining changes. */
  UPROPERTY(BlueprintAssignable, Category = "Fighter|Events")
  FOnActionsChanged OnActionsChanged;

protected:
  /** Clear grid occupancy when the fighter is destroyed. */
  virtual void Destroyed() override;

private:
  /** Update the health widget with a new value. */
  UFUNCTION()
  void UpdateHealthDisplay(int32 NewHealth);

  /** Respond when the fighter stats replicate to clients. */
  UFUNCTION()
  void OnRep_Stats(const FFighterStats &OldStats);
  UFUNCTION()
  void OnRep_ActionsRemaining();
  UFUNCTION()
  void OnRep_GridFootprint();

  /** Retrieve or create a damage widget from the pool. */
  UUserWidget *GetDamageWidgetFromPool();

  /** Retrieve or create a miss widget from the pool. */
  UUserWidget *GetMissWidgetFromPool();

  /** Align the visible mesh with the collision capsule. */
  void UpdateMeshOffset();

  /** Apply scale changes based on the footprint size. */
  void ApplyFootprintScale();

  /** Align the fighter's world position with its occupied cells. */
  void AlignToCurrentCell();

  /** Calculate the aligned world location for a given anchor cell. */
  FVector GetAlignedWorldLocation(const FIntPoint &Anchor) const;

  /** Helper to broadcast the current actions remaining value. */
  void BroadcastActionsRemaining();

  /** Rotate the fighter to face a world-space location. */
  void FaceTowardsLocation(const FVector &TargetLocation);

  /** Rotate the fighter to face from one grid cell towards another. */
  void FaceTowardsCells(const FIntPoint &FromCell, const FIntPoint &ToCell);

  /** Data describing a single queued attack roll. */
  struct FQueuedAttackRoll {
    int32 RollValue = 1;
    int32 Damage = 0;
    bool bHit = false;
  };

  /** Begin resolving queued attack rolls with a delay between each. */
  void StartQueuedAttack(AFighterPawn *Target,
                         TArray<FQueuedAttackRoll> &&Rolls);

  /** Apply the next queued attack roll, showing the appropriate widget. */
  void ResolveNextAttackRoll();

  /** Finalise any pending attack resolution and clean up timers/state. */
  void FinalizeQueuedAttack();

  /** Pool of reusable damage widgets to avoid repeated allocations. */
  UPROPERTY()
  TArray<UUserWidget *> DamageWidgetPool;

  /** Pool of reusable miss widgets to avoid repeated allocations. */
  UPROPERTY()
  TArray<UUserWidget *> MissWidgetPool;

  /** Pending attack rolls awaiting delayed resolution. */
  TArray<FQueuedAttackRoll> PendingAttackRolls;

  /** Index of the next pending attack roll to resolve. */
  int32 PendingAttackRollIndex = 0;

  /** Target currently receiving delayed attack rolls. */
  TWeakObjectPtr<AFighterPawn> PendingAttackTarget;

  /** Whether the pending attack target has been reduced to zero health. */
  bool bPendingAttackTargetDied = false;

  /** Timer driving delayed attack roll resolution. */
  FTimerHandle AttackRollTimerHandle;

  /** Tracks whether any pending attack roll has been processed. */
  bool bHasProcessedPendingRoll = false;

  /** Current cell occupied by the fighter. */
  UPROPERTY(Replicated)
  FIntPoint CurrentCell;

  /** Cached grid overlay component. */
  mutable UGridOverlayComponent *CachedGrid = nullptr;

  /** True while the fighter is interpolating towards a grid cell. */
  bool bIsMoving = false;

  /** World-space destination for the current interpolated move. */
  FVector MovementTargetLocation = FVector::ZeroVector;

public:
  /** Returns true if the fighter has already activated this round. */
  bool HasActivatedThisRound() const { return bHasActivatedThisRound; }
};
