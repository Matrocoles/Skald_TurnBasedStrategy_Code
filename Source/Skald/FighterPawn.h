#pragma once

#include "Blueprint/UserWidget.h"
#include "Components/WidgetComponent.h"
#include "CoreMinimal.h"
#include "Engine/EngineTypes.h"
#include "GameFramework/Pawn.h"
#include "GridBattleManager.h"
#include "TimerManager.h"
#include "Abilities/SkaldAbilityComponent.h"
#include "Abilities/SkaldAbilityTypes.h"
#include "FighterPawn.generated.h"

class UGridOverlayComponent;
class UCapsuleComponent;
class UTexture2D;
class USoundBase;
class UAudioComponent;
class UNiagaraComponent;
class UFighterActivationWidget;
class UFighterHealthWidget;
class UCurveFloat;
class UMaterialInstanceDynamic;
class UDecalComponent;
class UMaterialInterface;
class USkaldAbilityComponent;
class USkaldDiceManager;
class ASkaldPlayerController;
class UStatusEffectFloatingWidget;
class UBuffFloatingTextWidget;
class UDebuffFloatingTextWidget;

UENUM(BlueprintType)
enum class EFighterPawnFootprint : uint8 {
  SingleCell UMETA(DisplayName = "1 Cell"),
  FourCells UMETA(DisplayName = "4 Cells")
};

USTRUCT(BlueprintType)
struct FActiveBuff {
  GENERATED_BODY();

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Fighter|Buffs")
  FName SourceAbilityId = NAME_None;

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Fighter|Buffs")
  FText DisplayName;

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Fighter|Buffs")
  FSkaldAbilityStatDelta Delta;
};

USTRUCT(BlueprintType)
struct FActiveDebuff {
  GENERATED_BODY();

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Fighter|Buffs")
  FName SourceAbilityId = NAME_None;

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Fighter|Buffs")
  FText DisplayName;

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Fighter|Buffs")
  FSkaldAbilityStatDelta Delta;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnHealthChanged, int32, NewHealth);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnActionsChanged, int32,
                                            NewActionsRemaining);
DECLARE_MULTICAST_DELEGATE(FOnQueuedAttackFinalized);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnEngagementChanged, bool,
                                            bIsEngaged);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnHealthDisplayUpdated, int32,
                                            DisplayedHealth);

/** Pawn representing a fighter in grid battles. */
UCLASS()
class SKALD_API AFighterPawn : public APawn {
  GENERATED_BODY()

  friend class UGridOverlayComponent;

public:
  AFighterPawn();

  virtual void Tick(float DeltaSeconds) override;
  virtual void OnConstruction(const FTransform &Transform) override;
  virtual void BeginPlay() override;
  virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
  virtual void GetLifetimeReplicatedProps(
      TArray<FLifetimeProperty> &OutLifetimeProps) const override;

  /** Prepare the fighter for its activation. */
  UFUNCTION(BlueprintCallable, Category = "Fighter")
  void BeginActivation();

  // --- Manual Dice Roll Support ---
  UFUNCTION(BlueprintCallable, Category = "Combat|Dice")
  void TriggerManualAttackRoll();
  void RequestAIAutoManualAttackRoll();

  UFUNCTION(BlueprintCallable, Category = "Combat|Dice")
  void NotifyAIAttackPresentationReady();

  /** Record whether any human player is expected to drive the manual dice UI. */
  void SetAIManualRollHasPresenter(bool bHasPresenter);

  /** Access the dice manager responsible for physical roll orchestration. */
  UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Combat|Dice")
  USkaldDiceManager *GetDiceManager() const;

  /** Cancel any pending delay before an AI controlled fighter rolls dice. */
  void CancelAIDiceRollDelay();

  UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Combat|Dice")
  AFighterPawn *GetPendingPhysicalAttackTarget() const;

  // Check UI Conditions
  UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Combat|Dice")
  bool IsAwaitingPhysicalAttackRoll() const { return bAwaitingPhysicalAttackRoll; }

  /** Determine whether this fighter should be treated as AI-controlled for manual dice flows. */
  UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Combat|Dice")
  bool IsAIControlledParticipant() const;

  /**
   * Rebuild the supplied dice result using concrete physical roll values so the
   * queued attack resolves using the exact faces produced by the dice system.
   */
  static void ApplyPhysicalRollResults(
      FDiceRollResult &Result, const TArray<int32> &RollValues,
      const FFighterStats &AttackerStats,
      const FFighterStats &DefenderStats);

  /** Clear round-based activation flags. */
  UFUNCTION(BlueprintCallable, Category = "Fighter")
  void ResetActivationState();

  /** Mark this fighter's turn as complete. */
  UFUNCTION(BlueprintCallable, Category = "Fighter")
  void FinishActivation();

  /** Spend a single action if available. Returns true on success. */
  UFUNCTION(BlueprintCallable, Category = "Fighter|Actions")
  bool ConsumeAction();

  /** Attempt to restore a spent action if this fighter is currently active. */
  bool TryRestoreAction();

  /** Attempt to refresh a spent reaction if this fighter has an ability component. */
  bool TryRestoreReaction();

  /** Move to the specified grid cell if actions remain. */
  UFUNCTION(BlueprintCallable, Category = "Fighter")
  void MoveToCell(FIntPoint TargetCell);

  /** Attempt to disengage from nearby enemies before moving. */
  UFUNCTION(BlueprintCallable, Category = "Fighter")
  bool TryDisengageToCell(FIntPoint TargetCell);

  /** Teleport to a target grid cell without consuming additional actions. */
  bool TryTeleportToCell(FIntPoint TargetCell, int32 MaxDistance,
                         bool bRequireLineOfSight);

  /** Maximum Chebyshev distance allowed when disengaging. */
  UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Fighter|Movement")
  int32 GetDisengageRange() const;

  /** Units per second used when travelling between grid cells. */
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fighter|Movement",
            meta = (ClampMin = "0.0"))
  float MovementSpeed = 600.f;

  /** Distance threshold for snapping to the target cell during movement. */
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fighter|Movement",
            meta = (ClampMin = "0.0"))
  float MovementStopTolerance = 1.f;

  /** Sound to play while this fighter travels between grid cells. */
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fighter|Audio")
  TObjectPtr<USoundBase> MovementSound = nullptr;

  /** Enable simple visual obstacle avoidance while travelling between cells. */
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fighter|Movement|Visual")
  bool bUseVisualObstacleAvoidance = true;

  /** Radius of the probe used to detect blocking geometry along the travel path. */
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fighter|Movement|Visual",
            meta = (ClampMin = "0.0"))
  float VisualAvoidanceProbeRadius = 40.f;

  /** Side step distance applied when steering around detected obstacles. */
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fighter|Movement|Visual",
            meta = (ClampMin = "0.0"))
  float VisualAvoidanceSideStep = 120.f;

  /** Forward distance used when easing back towards the original travel line. */
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fighter|Movement|Visual",
            meta = (ClampMin = "0.0"))
  float VisualAvoidanceRejoinDistance = 180.f;

  /** Strength of the outward push applied directly from the blocking surface. */
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fighter|Movement|Visual",
            meta = (ClampMin = "0.0"))
  float VisualAvoidanceSurfacePush = 25.f;

  /** Fraction of the sidestep retained when blending back towards the target. */
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fighter|Movement|Visual",
            meta = (ClampMin = "0.0", ClampMax = "1.0"))
  float VisualAvoidanceReturnRatio = 0.35f;

  /** Collision channel used for obstacle avoidance sweeps. */
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fighter|Movement|Visual")
  TEnumAsByte<ECollisionChannel> VisualAvoidanceTraceChannel = ECC_WorldStatic;

  /** Height of the trace used to project movement points onto the terrain. */
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fighter|Movement|Visual",
            meta = (ClampMin = "0.0"))
  float VisualGroundConformTraceHeight = 300.f;

  /** Collision channel used when projecting movement points onto the terrain. */
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fighter|Movement|Visual")
  TEnumAsByte<ECollisionChannel> VisualGroundConformTraceChannel = ECC_WorldStatic;

  /** Perform an attack against another fighter. */
  UFUNCTION(BlueprintCallable, Category = "Fighter")
  void PerformAttack(AFighterPawn *Target);

  /** True while queued attack rolls are still being processed. */
  bool IsResolvingQueuedAttack() const;

  /** Trigger a hit flash scaled by the supplied damage amount. */
  void PlayImpactFlashForDamage(int32 DamageAmount);

  /** Update the ability component with the latest stats/faction if possible. */
  void UpdateAbilityLoadout();

  /** Show or hide the fighter's selection indicator. */
  UFUNCTION(BlueprintCallable, Category = "Fighter|Selection")
  void SetSelectionIndicatorVisible(bool bVisible);

  /** Show or hide the targeted indicator for incoming attacks. */
  UFUNCTION(BlueprintCallable, Category = "Fighter|Targeting")
  void SetTargetedIndicatorVisible(bool bVisible);

  /** Event fired after any queued attack finishes resolving. */
  FOnQueuedAttackFinalized OnQueuedAttackFinalized;

  /** Check whether this fighter is still alive. */
  UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Fighter")
  bool IsAlive() const;

  /** Determine whether the fighter is currently interpolating between cells. */
  bool IsMoving() const;

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
  int32 GetMovementStepCost(const FIntPoint &From, const FIntPoint &To,
                            const UGridOverlayComponent *Grid) const;

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
  UPROPERTY(BlueprintReadWrite, EditAnywhere, ReplicatedUsing = OnRep_Faction,
            Category = "Fighter")
  ESkaldFaction Faction = ESkaldFaction::None;

  /** Portrait associated with this fighter definition. */
  UPROPERTY(BlueprintReadOnly, Replicated, Category = "Fighter|UI")
  TSoftObjectPtr<UTexture2D> FighterPortrait;

  /** Number of grid cells occupied by this fighter (1 or 4). */
  UPROPERTY(EditAnywhere, BlueprintReadWrite,
            ReplicatedUsing = OnRep_GridFootprint, Category = "Fighter|Grid")
  EFighterPawnFootprint GridFootprint = EFighterPawnFootprint::SingleCell;

  /** Primary attack classification used for combat interactions. */
  UPROPERTY(EditAnywhere, BlueprintReadWrite,
            ReplicatedUsing = OnRep_AttackType, Category = "Fighter|Combat")
  EFighterAttackType AttackType = EFighterAttackType::Melee;

  /** Customisable audiovisual payloads for pre-attack presentation. */
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Replicated,
            Category = "Fighter|Combat")
  FFighterAttackFXDefinition AttackFX;

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

  /** Immediately end the activation as soon as it begins when true. */
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fighter|Turn")
  bool bAutoSkipActivation = false;

  /** Expose whether this fighter is actively taking a turn. */
  UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Fighter")
  bool IsCurrentlyActive() const { return bIsCurrentlyActive; }

  /** Return true if this fighter is currently engaged with an enemy. */
  UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Fighter|Combat")
  bool IsEngaged() const { return bIsEngaged; }

  /** Notify the fighter that a passive buff became active. */
  void NotifyPassiveBuffApplied(const FSkaldAbilityDefinition &Definition);

  /** Notify the fighter that a passive buff ended. */
  void NotifyPassiveBuffRemoved(FName AbilityId);

  /** Clear any lingering passive buff visuals. */
  void ClearAllPassiveBuffIndicators();

  /** Register a newly applied modifier for floating status presentation. */
  void NotifyStatusEffectApplied(FName AbilityId, const FText &AbilityName,
                                 const FSkaldAbilityStatDelta &Delta);

  /** Remove a modifier contribution from floating status presentation. */
  void NotifyStatusEffectRemoved(FName AbilityId, const FText &AbilityName,
                                 const FSkaldAbilityStatDelta &Delta);

  /** Reset all floating status indicators. */
  void ClearFloatingStatusEffects();

  /** Refresh the floating buff/debuff widgets immediately. */
  void UpdateFloatingStatusText();

  /** Remaining actions for the active turn. */
  UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Fighter")
  int32 GetActionsRemaining() const { return ActionsRemaining; }

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

  /** Whether this fighter is currently restricted by enemy engagement. */
  UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_IsEngaged,
            Category = "Fighter|Combat")
  bool bIsEngaged = false;

  /** Mesh used to display the fighter. */
  UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
  UStaticMeshComponent *DisplayMesh;

  /** Collision capsule used for movement and placement. */
  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Fighter",
            meta = (AllowPrivateAccess = "true"))
  UCapsuleComponent *CollisionComponent;

  /** Ability manager responsible for faction passives and unit actives. */
  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Fighter|Abilities",
            meta = (AllowPrivateAccess = "true"))
  USkaldAbilityComponent *AbilityComponent;

  UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Fighter|Abilities")
  USkaldAbilityComponent *GetAbilityComponent() const { return AbilityComponent; }

  /** Widget displaying the current health. */
  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Fighter|UI")
  UWidgetComponent *HealthWidget;

  /** Rear-facing widget mirroring the current health display. */
  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Fighter|UI")
  UWidgetComponent *HealthWidgetBack;

  /** Widget used to display currently active buffs. */
  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Fighter|UI")
  UWidgetComponent *BuffStatusWidget;

  /** Widget used to display currently active debuffs. */
  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Fighter|UI")
  UWidgetComponent *DebuffStatusWidget;

  /** Widget class used for the health display. */
  UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Fighter|UI")
  TSubclassOf<UUserWidget> HealthWidgetTemplate;

  /** Widget class used when spawning the floating buff indicator. */
  UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Fighter|UI")
  TSubclassOf<UStatusEffectFloatingWidget> BuffStatusWidgetTemplate;

  /** Widget class used when spawning the floating debuff indicator. */
  UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Fighter|UI")
  TSubclassOf<UStatusEffectFloatingWidget> DebuffStatusWidgetTemplate;

  /** Height offset applied to the buff status widget. */
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fighter|UI")
  float BuffStatusWidgetHeight = 380.f;

  /** Height offset applied to the debuff status widget. */
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fighter|UI")
  float DebuffStatusWidgetHeight = 440.f;

  /** Decal shown when the fighter is selected. */
  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Fighter|Selection")
  UDecalComponent *SelectionDecal;

  /** Decal shown while passive buffs affect the fighter. */
  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Fighter|Buffs")
  UDecalComponent *PassiveBuffDecal = nullptr;

  /** Decal shown while negative status effects affect the fighter. */
  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Fighter|Buffs")
  UDecalComponent *DebuffDecal = nullptr;

  /** Active positive modifiers affecting this fighter. */
  UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_ActiveBuffs,
            Category = "Fighter|Buffs")
  TArray<FActiveBuff> ActiveBuffs;

  /** Active negative modifiers affecting this fighter. */
  UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_ActiveDebuffs,
            Category = "Fighter|Buffs")
  TArray<FActiveDebuff> ActiveDebuffs;

  /** Material used for the selection decal. */
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fighter|Selection")
  TObjectPtr<UMaterialInterface> SelectionDecalMaterial;

  /** Material used for the passive buff decal. */
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fighter|Buffs")
  TObjectPtr<UMaterialInterface> PassiveBuffDecalMaterial;

  /** Material used for the debuff decal. */
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fighter|Buffs")
  TObjectPtr<UMaterialInterface> DebuffDecalMaterial;

  /** Size used for the selection decal on single-cell fighters. */
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fighter|Selection")
  FVector SelectionDecalSizeSingleCell = FVector(32.f, 160.f, 160.f);

  /** Size used for the passive buff decal on single-cell fighters. */
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fighter|Buffs")
  FVector PassiveBuffDecalSizeSingleCell = FVector(32.f, 160.f, 160.f);

  /** Size used for the debuff decal on single-cell fighters. */
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fighter|Buffs")
  FVector DebuffDecalSizeSingleCell = FVector(32.f, 160.f, 160.f);

  /** Size used for the selection decal on four-cell fighters. */
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fighter|Selection")
  FVector SelectionDecalSizeFourCells = FVector(64.f, 320.f, 320.f);

  /** Size used for the passive buff decal on four-cell fighters. */
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fighter|Buffs")
  FVector PassiveBuffDecalSizeFourCells = FVector(64.f, 320.f, 320.f);

  /** Size used for the debuff decal on four-cell fighters. */
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fighter|Buffs")
  FVector DebuffDecalSizeFourCells = FVector(64.f, 320.f, 320.f);

  /** Additional vertical offset applied to the selection decal. */
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fighter|Selection")
  float SelectionDecalFloorOffset = 0.f;

  /** Additional vertical offset applied to the passive buff decal. */
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fighter|Buffs")
  float PassiveBuffDecalFloorOffset = 0.f;

  /** Additional vertical offset applied to the debuff decal. */
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fighter|Buffs")
  float DebuffDecalFloorOffset = 0.f;

  /** Decal shown when the fighter is targeted for an incoming attack. */
  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Fighter|Targeting")
  UDecalComponent *TargetedDecal;

  /** Material used for the targeted decal. */
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fighter|Targeting")
  TObjectPtr<UMaterialInterface> TargetedDecalMaterial;

  /** Size used for the targeted decal on single-cell fighters. */
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fighter|Targeting")
  FVector TargetedDecalSizeSingleCell = FVector(32.f, 160.f, 160.f);

  /** Size used for the targeted decal on four-cell fighters. */
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fighter|Targeting")
  FVector TargetedDecalSizeFourCells = FVector(64.f, 320.f, 320.f);

  /** Additional vertical offset applied to the targeted decal. */
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fighter|Targeting")
  float TargetedDecalFloorOffset = 0.f;

  /** Widget indicating activation state (front facing). */
  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Fighter|UI")
  UWidgetComponent *ActivationWidget;

  /** Widget indicating activation state (rear facing). */
  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Fighter|UI")
  UWidgetComponent *ActivationWidgetBack;

  /** Audio component used to play looping movement audio. */
  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Fighter|Audio",
            meta = (AllowPrivateAccess = "true"))
  UAudioComponent *MovementAudioComponent = nullptr;

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

  /** Event broadcast when the fighter enters or leaves engagement. */
  UPROPERTY(BlueprintAssignable, Category = "Fighter|Events")
  FOnEngagementChanged OnEngagementChanged;

  /** Event broadcast when the health display is updated. */
  UPROPERTY(BlueprintAssignable, Category = "Fighter|Events")
  FOnHealthDisplayUpdated OnHealthDisplayUpdated;

  /** Retrieve the identifier associated with this fighter. */
  FName GetFighterId() const { return FighterId; }

  /** Accessor for the fighter's attack classification. */
  EFighterAttackType GetAttackType() const { return AttackType; }

  /** Override the fighter's attack classification. */
  void SetAttackType(EFighterAttackType InAttackType);

  /** Play the configured pre-attack presentation effects against the target. */
  void TriggerAttackPresentationFX(AFighterPawn *Target);

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

  UFUNCTION(Server, Reliable)
  void ServerShowAttackRollButtonForPlayer();

  void ShowAttackRollButtonForPlayer();

protected:
  /** Clear grid occupancy when the fighter is destroyed. */
  virtual void Destroyed() override;

  /** Re-evaluate engagement relationships across all fighters. */
  void RecalculateBattleEngagement() const;

  /** Refresh this fighter's engagement flag using a cached fighter snapshot. */
  void RefreshEngagementStatusFromSnapshot(
      const TArray<AFighterPawn *> &Fighters);

  /** Determine whether occupying the supplied anchor would touch an enemy. */
  bool HasAdjacentEnemyAtAnchor(
      const FIntPoint &Anchor,
      const TArray<AFighterPawn *> &FighterSnapshot) const;

  /** Determine whether occupying the supplied anchor would touch an enemy. */
  bool HasAdjacentEnemyAtAnchor(const FIntPoint &Anchor) const;

  /** Validate that a movement destination can be reached within a range. */
  bool ValidateMovementDestination(
      const FIntPoint &TargetCell, int32 MaxDistance,
      TArray<FIntPoint> &OutPreviousCells, TArray<FIntPoint> &OutTargetCells,
      UGridOverlayComponent *&OutGrid) const;

  /** Finalize movement updates and optionally consume an action. */
  bool CommitMovementToCell(const FIntPoint &PreviousCell,
                            const FIntPoint &TargetCell,
                            UGridOverlayComponent *Grid,
                            const TArray<FIntPoint> &PreviousCells,
                            const TArray<FIntPoint> &TargetCells,
                            int32 Distance, bool bSpendAction);

  /** Apply a new engagement flag and notify listeners. */
  void SetEngaged(bool bNewEngaged);

  /** Replication notification when the engagement flag changes. */
  UFUNCTION()
  void OnRep_IsEngaged();

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

  bool TreatsDifficultTerrainAsNormal() const;

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
  void OnRep_Faction();
  UFUNCTION()
  void OnRep_MaxHealth();
  UFUNCTION()
  void OnRep_IsMoving();

  UFUNCTION()
  void OnRep_AttackType();

  UFUNCTION()
  void OnRep_ActiveBuffs();

  UFUNCTION()
  void OnRep_ActiveDebuffs();

  UFUNCTION(NetMulticast, Reliable)
  void MulticastPlayPreAttackFX(AFighterPawn *Target);

  void PlayPreAttackFX(AFighterPawn *Target);
  void PlayMeleePreAttackFX(AFighterPawn *Target);
  void PlayRangedPreAttackFX(AFighterPawn *Target);
  FVector ResolveFXOrigin(const FName &SocketName, const FVector &LocalOffset,
                          FRotator *OutSocketRotation = nullptr) const;
  void SpawnPreAttackSoundAtLocation(const FVector &Location) const;

  struct FActiveProjectileFX {
    TWeakObjectPtr<UNiagaraComponent> Component;
    FVector StartLocation = FVector::ZeroVector;
    FVector EndLocation = FVector::ZeroVector;
    float TravelTime = 0.f;
    float ElapsedTime = 0.f;
  };

  void TickActiveProjectileFX(float DeltaSeconds);
  void SpawnProjectileFX(const FVector &SpawnLocation,
                         const FVector &TargetLocation,
                         const FRotator &SpawnRotation);

  TArray<FActiveProjectileFX> ActiveProjectileFX;

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

  /** Update the ability component with the latest stats/faction. */
  void RefreshAbilityLoadout();

  /** Ensure the activation widget instance is ready for use. */
  void EnsureActivationWidget();

  /** Update the activation widget to match fighter state. */
  void UpdateActivationIndicator();

  /** Update the selection decal size to match the current footprint. */
  void UpdateSelectionIndicatorSize();

  /** Align the selection decal with the base of the fighter. */
  void UpdateSelectionIndicatorTransform();

  /** Apply the configured material to the selection decal. */
  void RefreshSelectionIndicatorMaterial();

  /** Update the targeted decal size to match the current footprint. */
  void UpdateTargetedIndicatorSize();

  /** Align the targeted decal with the base of the fighter. */
  void UpdateTargetedIndicatorTransform();

  /** Apply the configured material to the targeted decal. */
  void RefreshTargetedIndicatorMaterial();

  /** Toggle the passive buff decal visibility. */
  void SetPassiveBuffVisible(bool bVisible);

  /** Resize the passive buff decal based on the current footprint. */
  void UpdatePassiveBuffDecalSize();

  /** Align the passive buff decal with the ground plane. */
  void UpdatePassiveBuffDecalTransform();

  /** Apply the configured material to the passive buff decal. */
  void RefreshPassiveBuffDecalMaterial();

  /** Toggle the debuff decal visibility. */
  void SetDebuffDecalVisible(bool bVisible);

  /** Resize the debuff decal based on the current footprint. */
  void UpdateDebuffDecalSize();

  /** Align the debuff decal with the ground plane. */
  void UpdateDebuffDecalTransform();

  /** Apply the configured material to the debuff decal. */
  void RefreshDebuffDecalMaterial();

  /** Resolve and cache an activation icon texture. */
  UTexture2D *ResolveActivationIcon(TSoftObjectPtr<UTexture2D> &IconSource,
                                    UTexture2D *&CachedTexture);

  /** Apply the mesh's yaw offset when orienting the fighter. */
  void ApplyFacingYaw(float TargetYaw);

  /** Rotate the fighter to face a world-space location. */
  void FaceTowardsLocation(const FVector &TargetLocation);

  /** Rotate the fighter to face from one grid cell towards another. */
  void FaceTowardsCells(const FIntPoint &FromCell, const FIntPoint &ToCell);

  /** Trigger any traps occupying the provided destination cells. */
  void ResolveTrapsAtDestination(const TArray<FIntPoint> &DestinationCells);

  /** Update movement audio playback to reflect current settings. */
  void RefreshMovementAudioComponent();

  /** Helper to toggle movement state and trigger audio changes. */
  void SetIsMoving(bool bNewIsMoving);

  /** Generate a lightweight, obstacle-aware path for the current move. */
  void RebuildVisualMovementPath(const FVector &Destination);

  /** Evaluate whether a visual avoidance hit should influence movement. */
  bool ShouldUseAvoidanceHit(const FHitResult &Hit) const;

  /** Sample the cached visual movement path using a normalised distance. */
  FVector SampleVisualMovementPath(float NormalisedDistance) const;

  /** Clear any cached state related to visual movement interpolation. */
  void ResetVisualMovementPath();

  /** Project a movement path point onto the underlying terrain surface. */
  void ConformPathPointToGround(FVector &Location) const;

  /** Update buff/debuff widgets so they continue facing the active camera. */
  void UpdateFloatingStatusWidgetFacing();

  /** Apply a set of status lines to a floating widget component. */
  void ApplyStatusLinesToWidget(UWidgetComponent *Component,
                                const TArray<FText> &Lines);

  /** Build display strings for all active buffs. */
  void BuildBuffStatusLines(TArray<FText> &OutLines) const;

  /** Build display strings for all active debuffs. */
  void BuildDebuffStatusLines(TArray<FText> &OutLines) const;

  /** Apply a modifier delta to the internal status aggregate map. */
  void ApplyStatusEffectDelta(FName AbilityId, const FText &AbilityName,
                              const FSkaldAbilityStatDelta &Delta,
                              bool bIsApplying);

  /** Recalculate replicated status arrays from the aggregate map. */
  void RebuildActiveStatusArrays();

  /** Resolve the rotation used to face floating widgets towards the camera. */
  FRotator ResolveCameraFacingRotation(const FVector &WidgetLocation) const;

  static bool HasAnyDeltaMagnitude(const FSkaldAbilityStatDelta &Delta);
  static FSkaldAbilityStatDelta ExtractPositiveDelta(
      const FSkaldAbilityStatDelta &Delta);
  static FSkaldAbilityStatDelta ExtractNegativeDelta(
      const FSkaldAbilityStatDelta &Delta);
  static void AccumulateDelta(FSkaldAbilityStatDelta &Target,
                              const FSkaldAbilityStatDelta &Source,
                              int32 Direction);
  static FString BuildDeltaFragment(const FSkaldAbilityStatDelta &Delta,
                                    bool bPositive);

  struct FStatusEffectAggregate {
    FText DisplayName;
    FSkaldAbilityStatDelta Delta;
  };

  /** Aggregated status effects keyed by their source ability identifier. */
  TMap<FName, FStatusEffectAggregate> ActiveStatusAggregates;

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

  /** Update cached dice result aggregates using the latest fighter stats. */
  void RefreshPendingAttackResultStats(AFighterPawn *Target);

  /** Ensure any stored physical dice results are applied to the pending attack. */
  void ApplyPendingPhysicalRollValues();

  /** Finalise any pending attack resolution and clean up timers/state. */
  void FinalizeQueuedAttack();

  /** Cancel any pending queued attack without reporting results. */
  void CancelQueuedAttack();

  /** Reset queued attack bookkeeping and optionally notify listeners. */
  void ClearQueuedAttackState(bool bBroadcastFinalized);

  bool AttemptPhysicalAttackRoll(class AFighterPawn* Target);
  void EnsureDiceManagerBinding();
  void CleanupDiceManagerBinding();

  void ProcessPhysicalDiceRollResults(const FGuid &RollId,
                                      const TArray<int32> &Results);

  UFUNCTION(Server, Reliable)
  void ServerSubmitPhysicalDiceRoll(const FGuid &RollId,
                                    const TArray<int32> &Results);

  UFUNCTION()
  void HandleDiceRollCompleted(const FGuid &RollId, const TArray<int32> &Results);

  void HandleIncomingAttackStarted();
  void HandleIncomingAttackFinished();
  void ExecuteManualAttackRoll();
  bool ShouldDelayAIAttackRoll();
  void HandleAIDiceRollDelayComplete();
  bool ShouldWaitForCameraBeforeAIRoll() const;

  /** Index of the next pending dice outcome to resolve. */
  int32 PendingAttackOutcomeIndex = 0;

  /** Target currently receiving delayed attack rolls. */
  TWeakObjectPtr<AFighterPawn> PendingAttackTarget;

  /** Cached physical dice values awaiting application to the pending result. */
  TArray<int32> PendingPhysicalRollValues;

  /** Tracks whether pending physical dice values have already been applied. */
  bool bPendingPhysicalRollValuesApplied = false;

  /** Target awaiting a physical dice roll result. */
  TWeakObjectPtr<AFighterPawn> PendingPhysicalAttackTarget;

  /** Whether the pending attack target has been reduced to zero health. */
  bool bPendingAttackTargetDied = false;

  /** Timer driving delayed attack roll resolution. */
  FTimerHandle AttackRollTimerHandle;
  /** Timer used to delay AI manual dice rolls until the camera finishes panning. */
  FTimerHandle AIDiceRollDelayHandle;

  /** Tracks whether any pending attack roll has been processed. */
  bool bHasProcessedPendingRoll = false;

  /** True while waiting for the dice subsystem to resolve a live roll. */
  bool bAwaitingPhysicalAttackRoll = false;
  /** True while an AI controlled fighter is waiting to start its physical dice roll. */
  bool bPendingAIDiceRollDelay = false;
  bool bAIManualRollAwaitingCamera = false;
  bool bAIManualRollPresentationReady = false;
  bool bAIManualRollPendingTrigger = false;
  bool bAIManualRollLoggedPanning = false;
  bool bAIManualRollLoggedOverview = false;
  bool bAIManualRollLoggedReady = false;
  bool bAIManualRollLoggedCameraTimeout = false;
  bool bAIManualRollHasPresenter = false;

  static constexpr int32 MaxAICameraAckRetries = 2;
  int32 RemainingAICameraAckRetries = 0;

  /** Cached dice roll data to broadcast once resolution completes. */
  FDiceRollResult PendingAttackDiceResult;

  /** Active physical dice roll guid, if any. */
  FGuid PendingAttackRollId;

  /** Snapshot of stats used when requesting a physical dice roll. */
  FFighterStats PendingAttackAttackerSnapshot;
  FFighterStats PendingAttackDefenderSnapshot;

  /** True when the pending attack stat snapshots contain valid data. */
  bool bHasPendingAttackSnapshot = false;

  /** Cached pointer to the dice subsystem for attack rolls. */
  TWeakObjectPtr<USkaldDiceManager> CachedDiceManager;

  /** Tracks whether cached dice data should be reported on finalisation. */
  bool bHasPendingDiceResult = false;

  /** Current cell occupied by the fighter. */
  UPROPERTY(Replicated)
  FIntPoint CurrentCell;

  /** Grid anchor used as the start for the current interpolated move. */
  UPROPERTY(Replicated)
  FIntPoint MovementSourceCell = FIntPoint::ZeroValue;

  /** Cached grid overlay component. */
  mutable UGridOverlayComponent *CachedGrid = nullptr;

  /** True while the fighter is interpolating towards a grid cell. */
  UPROPERTY(ReplicatedUsing = OnRep_IsMoving)
  bool bIsMoving = false;

  /** World-space destination for the current interpolated move. */
  FVector MovementTargetLocation = FVector::ZeroVector;

  /** World-space start location cached when a new move begins. */
  FVector MovementStartLocation = FVector::ZeroVector;

  /** Straight-line distance between the movement start and destination. */
  float MovementStraightLineDistance = 0.f;

  /** Normalised travel progress for the current move. */
  float MovementProgress = 0.f;

  /** Full set of points describing the visual travel path. */
  TArray<FVector> VisualMovementPathPoints;

  /** Accumulated distance along the visual travel path for each point. */
  TArray<float> VisualMovementCumulativeDistances;

  /** Total length of the current visual travel path. */
  float VisualMovementPathLength = 0.f;

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

  /** Tracking for active passive buff sources to manage visuals. */
  TMap<FName, int32> ActivePassiveBuffSources;

  float HitFlashElapsed = 0.f;
  float HitFlashStrength = 1.f;
  bool bHitFlashActive = false;
  bool bHasRecordedHealth = false;
  int32 LastKnownHealth = 0;
  int32 ActiveIncomingAttackCount = 0;

public:
  /** Returns true if the fighter has already activated this round. */
  bool HasActivatedThisRound() const { return bHasActivatedThisRound; }
};
