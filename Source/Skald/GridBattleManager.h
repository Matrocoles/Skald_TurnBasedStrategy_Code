#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "Engine/DataTable.h"
#include "GameFramework/Actor.h"
#include "SkaldTypes.h"
#include "Engine/Texture2D.h"
#include "TimerManager.h"
#include "Delegates/Delegate.h"
#include "GridBattleManager.generated.h"

/** Lightweight optional int32 replacement to avoid relying on engine optional templates. */
struct FOptionalInt32
{
    FOptionalInt32() = default;

    void Reset()
    {
        bIsSet = false;
        Value = 0;
    }

    bool IsSet() const
    {
        return bIsSet;
    }

    int32 GetValue() const
    {
        return Value;
    }

    FOptionalInt32& operator=(int32 InValue)
    {
        Value = InValue;
        bIsSet = true;
        return *this;
    }

private:
    bool bIsSet = false;
    int32 Value = 0;
};

class AFighterPawn; // MUST be before the delegates

DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(
    FOnBattleEnded, ESkaldFaction, WinningFaction, int32, AttackerCasualties, int32, DefenderCasualties);
USTRUCT(BlueprintType)
struct FDiceRollOutcome
{
    GENERATED_BODY();

    UPROPERTY(BlueprintReadOnly, Category="Skald|Battle|Dice")
    int32 RollValue = 1;

    UPROPERTY(BlueprintReadOnly, Category="Skald|Battle|Dice")
    int32 Damage = 0;

    UPROPERTY(BlueprintReadOnly, Category="Skald|Battle|Dice")
    bool bHit = false;

    UPROPERTY(BlueprintReadOnly, Category="Skald|Battle|Dice")
    bool bCritical = false;
};

USTRUCT(BlueprintType)
struct FDiceRollResult
{
    GENERATED_BODY();

    UPROPERTY(BlueprintReadOnly, Category="Skald|Battle|Dice")
    TArray<FDiceRollOutcome> DiceOutcomes;

    UPROPERTY(BlueprintReadOnly, Category="Skald|Battle|Dice")
    int32 TotalDamage = 0;

    UPROPERTY(BlueprintReadOnly, Category="Skald|Battle|Dice")
    int32 HitCount = 0;

    UPROPERTY(BlueprintReadOnly, Category="Skald|Battle|Dice")
    int32 CriticalHitCount = 0;

    UPROPERTY(BlueprintReadOnly, Category="Skald|Battle|Dice")
    int32 HighestCriticalDamage = 0;

    UPROPERTY(BlueprintReadOnly, Category="Skald|Battle|Dice")
    int32 MissCount = 0;

    UPROPERTY(BlueprintReadOnly, Category="Skald|Battle|Dice")
    int32 StartingHealth = 0;

    UPROPERTY(BlueprintReadOnly, Category="Skald|Battle|Dice")
    int32 EndingHealth = 0;

    UPROPERTY(BlueprintReadOnly, Category="Skald|Battle|Dice")
    bool bHighStakesCritical = false;

    UPROPERTY(BlueprintReadOnly, Category="Skald|Battle|Dice")
    ESkaldFaction HighStakesFaction = ESkaldFaction::None;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(
    FOnAttackResolved, AFighterPawn*, Attacker, AFighterPawn*, Defender, const FDiceRollResult&, Result);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(
    FOnAttackRejected, AFighterPawn*, Attacker, AFighterPawn*, Defender, const FText&, Reason);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnActiveFighterChanged, AFighterPawn*, NewFighter);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnRoundStarted, int32, RoundNumber, ESkaldFaction, InitiativeWinner);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnInitiativePhaseStarted, int32, RoundNumber);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_FourParams(FOnInitiativeRollCompleted, int32, RoundNumber,
    int32, AttackerRoll, int32, DefenderRoll, ESkaldFaction, WinningFaction);

/** Statistics for a fighter in grid battle mode. */
USTRUCT(BlueprintType)
struct FFighterStats
{
    GENERATED_BODY();

    UPROPERTY(BlueprintReadWrite, EditAnywhere, Category="Stats")
    int32 Health = 1;

    UPROPERTY(BlueprintReadWrite, EditAnywhere, Category="Stats")
    int32 Defence = 1;

    UPROPERTY(BlueprintReadWrite, EditAnywhere, Category="Stats")
    int32 Strength = 1;

    /** Number of dice rolled when attacking. */
    UPROPERTY(BlueprintReadWrite, EditAnywhere, Category="Stats")
    int32 AttackDice = 1;

    /** Number of squares the attack can reach. */
    UPROPERTY(BlueprintReadWrite, EditAnywhere, Category="Stats")
    int32 AttackRange = 1;

    /** Squares the fighter can move per activation. */
    UPROPERTY(BlueprintReadWrite, EditAnywhere, Category="Stats")
    int32 Movement = 1;

    /** Damage dealt on a successful hit. */
    UPROPERTY(BlueprintReadWrite, EditAnywhere, Category="Stats")
    int32 AttackDamage = 1;

    /** Additional damage dealt when scoring a critical hit. */
    UPROPERTY(BlueprintReadWrite, EditAnywhere, Category="Stats")
    int32 CriticalBonusDamage = 3;

    /** Cost to include this fighter in an army. */
    UPROPERTY(BlueprintReadWrite, EditAnywhere, Category="Stats")
    int32 ArmyCost = 1;
};

/** Definition of a fighter type used by data tables. */
USTRUCT(BlueprintType)
struct FFighterDefinition : public FTableRowBase
{
    GENERATED_BODY();

    FFighterDefinition()
        : Id(NAME_None)
        , MeshClass(nullptr)
        , Faction(ESkaldFaction::None)
        , Stats()
        , Portrait(nullptr)
    {
    }

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Fighter")
    FName Id;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Fighter")
    TSubclassOf<AActor> MeshClass;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Fighter")
    ESkaldFaction Faction = ESkaldFaction::None;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Fighter")
    FFighterStats Stats;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Fighter")
    TSoftObjectPtr<UTexture2D> Portrait;
};

USTRUCT(BlueprintType)
struct FFighter
{
    GENERATED_BODY();

    UPROPERTY(BlueprintReadWrite, EditAnywhere, Category="Fighter")
    FFighterStats Stats;

    UPROPERTY(BlueprintReadWrite, EditAnywhere, Category="Fighter")
    ESkaldFaction Faction = ESkaldFaction::None;

    /** Current grid position of the fighter. */
    UPROPERTY(BlueprintReadWrite, EditAnywhere, Category="Fighter")
    FIntPoint Position = FIntPoint::ZeroValue;
};

/**
 * Manages a simple grid based battle between two teams of fighters.
 * This is a lightweight representation of the Warhammer style mode
 * described in the design. It focuses on dice rolling and turn order
 * logic, leaving visuals and detailed rules to Blueprints or future
 * work.
*/
UENUM()
enum class EGridActivationFinishReason : uint8
{
    Manual,
    Auto
};

UCLASS(Blueprintable)
class SKALD_API UGridBattleManager : public UObject
{
    GENERATED_BODY()

public:
    /** Number of sides on the initiative die. */
    static constexpr int32 InitiativeDiceSides = 6;

    /** Load fighter definitions and set default state. */
    UGridBattleManager();

    /** Initialise a battle with attackers and defenders. */
    UFUNCTION(BlueprintCallable, Category="Battle")
    void InitBattle(const TArray<FFighter>& Attackers, const TArray<FFighter>& Defenders);

    /** Set the seed for the internal random stream. */
    UFUNCTION(BlueprintCallable, Category="Skald|Battle")
    void SetRandomSeed(int32 Seed);

    /** Resolve an attack following strength/defence rules. Returns true if the defender survives. */
    UFUNCTION(BlueprintCallable, Category="Battle")
    static FDiceRollResult ResolveAttackDice(const FFighterStats& AttackerStats, const FFighterStats& DefenderStats, UPARAM(ref) FRandomStream& RandomStream);

    UFUNCTION(BlueprintCallable, Category="Battle|Dice")
    static bool ResolveAttack(UPARAM(ref) FFighter& Attacker, UPARAM(ref) FFighter& Defender, UPARAM(ref) int32& OutDamage, UPARAM(ref) FRandomStream& RandomStream, UPARAM(ref) FDiceRollResult& OutResult);

    /** Roll initiative for the next round, determining which side acts first. */
    UFUNCTION(BlueprintCallable, Category="Skald|Battle")
    void RollInitiative();

    /** Continue the round after the player confirms the initiative roll. */
    UFUNCTION(BlueprintCallable, Category="Skald|Battle", meta = (CPP_Default_AttackerRoll = "-1", CPP_Default_DefenderRoll = "-1"))
    void ConfirmInitiativeRoll(int32 AttackerRoll, int32 DefenderRoll);

    /** Randomly place all fighters at the start of a round. */
    UFUNCTION(BlueprintCallable, Category="Skald|Battle")
    void StartRound();

    /** Finish the currently active fighter's turn and swap sides. */
    UFUNCTION(BlueprintCallable, Category="Battle")
    void AdvanceTurn();

    /** True when at least one attack resolution is still being presented to the player. */
    UFUNCTION(BlueprintCallable, BlueprintPure, Category="Battle")
    bool IsAwaitingAttackPresentation() const { return PendingAttackPresentationCount > 0; }

    /** Notify the battle manager that the latest attack presentation finished playing. */
    UFUNCTION(BlueprintCallable, Category="Battle")
    void NotifyAttackPresentationComplete();

    /** Check whether the specified fighter can activate on the current side. */
    UFUNCTION(BlueprintCallable, Category="Battle")
    bool CanActivateFighter(AFighterPawn* Fighter) const;

    /** Attempt to activate the specified fighter for the current side. */
    UFUNCTION(BlueprintCallable, Category="Battle")
    bool ActivateFighter(AFighterPawn* Fighter);

    /** Complete the active fighter's activation and rotate the turn. */
    UFUNCTION(BlueprintCallable, Category="Battle")
    void FinishActivation(AFighterPawn* Fighter, EGridActivationFinishReason Reason = EGridActivationFinishReason::Auto);

    /** Conclude the battle and broadcast the results. */
    UFUNCTION(BlueprintCallable, Category="Battle")
    void EndBattle();

    /** Total army cost of surviving attackers after the battle concludes. */
    UFUNCTION(BlueprintCallable, BlueprintPure, Category="Skald|Battle")
    int32 GetAttackerSurvivors();

    /** Total army cost of surviving defenders after the battle concludes. */
    UFUNCTION(BlueprintCallable, BlueprintPure, Category="Skald|Battle")
    int32 GetDefenderSurvivors();

    /** Total army cost of surviving attackers. Equivalent to GetAttackerSurvivors. */
    UFUNCTION(BlueprintCallable, BlueprintPure, Category="Skald|Battle")
    int32 GetAttackerSurvivorCost() const;

    /** Total army cost of surviving defenders. Equivalent to GetDefenderSurvivors. */
    UFUNCTION(BlueprintCallable, BlueprintPure, Category="Skald|Battle")
    int32 GetDefenderSurvivorCost() const;

    /** Total initial cost of the attacking army. */
    UFUNCTION(BlueprintCallable, BlueprintPure, Category="Skald|Battle")
    int32 GetAttackerInitialArmyCost() const { return AttackerInitialArmyCost; }

    /** Total initial cost of the defending army. */
    UFUNCTION(BlueprintCallable, BlueprintPure, Category="Skald|Battle")
    int32 GetDefenderInitialArmyCost() const { return DefenderInitialArmyCost; }

      /** Fighter currently taking its turn. */
      UFUNCTION(BlueprintCallable, BlueprintPure, Category="Battle")
      AFighterPawn* GetActiveFighter() const;

    /** Return the faction that won the current round's initiative. */
    UFUNCTION(BlueprintCallable, BlueprintPure, Category="Battle")
    ESkaldFaction GetInitiativeWinner() const { return InitiativeWinnerFaction; }

    /** True when the attacking side is allowed to activate a fighter. */
    UFUNCTION(BlueprintCallable, BlueprintPure, Category="Battle")
    bool IsAttackerTurn() const { return bIsAttackerTurn; }

    /** True when initiative rolling is waiting on player input. */
    UFUNCTION(BlueprintCallable, BlueprintPure, Category="Battle")
    bool IsAwaitingInitiativeRoll() const { return bAwaitingInitiativeRoll; }

    /** Current round number, starting at 1 when combat begins. */
    UFUNCTION(BlueprintCallable, BlueprintPure, Category="Battle")
    int32 GetCurrentRound() const { return CurrentRound; }

    /** Get fighter definitions for the specified faction. */
    UFUNCTION(BlueprintCallable, BlueprintPure, Category="Battle")
    TArray<FFighterDefinition> GetFightersForFaction(ESkaldFaction Faction) const;

    /** Event fired when the battle ends reporting winner and casualties. */
    UPROPERTY(BlueprintAssignable, Category="Battle|Events")
    FOnBattleEnded OnBattleEnded;

    /** Fired whenever an individual attack roll resolves. */
    UPROPERTY(BlueprintAssignable, Category="Battle|Events")
    FOnAttackResolved OnAttackResolved;

    /** Fired when an attack command is rejected. */
    UPROPERTY(BlueprintAssignable, Category="Battle|Events")
    FOnAttackRejected OnAttackRejected;

    /** Fired whenever the active fighter changes (including nullptr). */
    UPROPERTY(BlueprintAssignable, Category="Battle|Events")
    FOnActiveFighterChanged OnActiveFighterChanged;

    /** Fired whenever a new round begins. */
    UPROPERTY(BlueprintAssignable, Category="Battle|Events")
    FOnRoundStarted OnRoundStarted;

    /** Fired when a round enters the initiative rolling phase. */
    UPROPERTY(BlueprintAssignable, Category="Battle|Events")
    FOnInitiativePhaseStarted OnInitiativePhaseStarted;

    /** Fired after initiative dice are rolled. */
    UPROPERTY(BlueprintAssignable, Category="Battle|Events")
    FOnInitiativeRollCompleted OnInitiativeRollCompleted;

    UFUNCTION(BlueprintCallable, Category="Battle")
    void RegisterFighter(AFighterPawn* Fighter, bool bAsAttacker);

    UFUNCTION(BlueprintCallable, Category="Battle")
    void UnregisterFighter(AFighterPawn* Fighter);

    UFUNCTION(BlueprintCallable, Category="Battle|Events")
    void ReportAttackResolution(AFighterPawn* Attacker, AFighterPawn* Defender, const FDiceRollResult& Result);

    UFUNCTION(BlueprintCallable, Category="Battle|Events")
    void ReportSimulatedAttackResolution(const FDiceRollResult& Result);
    void ReportAttackRejected(AFighterPawn* Attacker, AFighterPawn* Defender, const FText& Reason);

    /** Table containing fighter definitions. */
    UPROPERTY(EditDefaultsOnly, Category="Data")
    UDataTable* FighterDefinitions = nullptr;

    /** Random stream used for all rolls. */
    UPROPERTY()
    FRandomStream Rng;

    /** Size of the square grid used in battle. */
    static const int32 GridSize = 48;

protected:
    UPROPERTY(BlueprintReadOnly, Category="Battle")
    TArray<FFighter> AttackerTeam;

    UPROPERTY(BlueprintReadOnly, Category="Battle")
    TArray<FFighter> DefenderTeam;

    UPROPERTY(BlueprintReadOnly, Category="Battle")
    int32 CurrentRound = 0;

    UPROPERTY(BlueprintReadWrite, EditAnywhere, Category="Battle")
    int32 MaxRounds = 100;

    /** Ordered list of fighters based on their initiative rolls. */
    UPROPERTY(BlueprintReadOnly, Category="Battle")
    TArray<AFighterPawn*> InitiativeOrder;

    /** Fighter currently taking its turn. */
    UPROPERTY(BlueprintReadOnly, Category="Battle")
    AFighterPawn* ActiveFighter = nullptr;

    /** Index of the fighter whose turn is active. */
    UPROPERTY(BlueprintReadOnly, Category="Battle")
    int32 CurrentTurn = 0;

    /** Faction that won the latest initiative roll. */
    UPROPERTY(BlueprintReadOnly, Category="Battle")
    ESkaldFaction InitiativeWinnerFaction = ESkaldFaction::None;

    /** True when the attacking side is currently allowed to activate. */
    UPROPERTY(BlueprintReadOnly, Category="Battle")
    bool bIsAttackerTurn = true;

private:
    void ResolveInitiativeRollInternal();
    void ScheduleAIRollIfNeeded();
    void PerformAIRoll();
    void FinalizeRoundStart();
    void ScheduleRoundStart(bool bDelayForPresentation);
    bool ShouldPauseForInitiativePrompt() const;
    void BroadcastBattleConcluded();

    bool HasLivingFighters(bool bForAttackers) const;
    bool HasAvailableFighters(bool bForAttackers) const;
    bool IsSideAIControlled(bool bForAttackers) const;
    void EvaluateRoundProgress(bool bPreviousWasAttacker);
    void ClearInactiveFighters();

    void HandleDeferredActivationFinalized(TWeakObjectPtr<AFighterPawn> FighterPtr);
    void ClearDeferredActivationTracking(TWeakObjectPtr<AFighterPawn> FighterPtr);

    void EnqueueDeferredPresentationFinish(AFighterPawn* Fighter, EGridActivationFinishReason Reason);
    void ProcessDeferredPresentationFinishes();

    struct FDeferredActivationFinish
    {
        EGridActivationFinishReason Reason = EGridActivationFinishReason::Auto;
        bool bWasAttacker = true;
    };

    struct FDeferredPresentationFinish
    {
        TWeakObjectPtr<AFighterPawn> Fighter;
        EGridActivationFinishReason Reason = EGridActivationFinishReason::Auto;
    };

    // Track both counts and costs separately
    int32 AttackerSurvivorUnitCount = 0;
    int32 DefenderSurvivorUnitCount = 0;
    int32 AttackerSurvivorArmyCost = 0;
    int32 DefenderSurvivorArmyCost = 0;

    ESkaldFaction BattleConclusionWinner = ESkaldFaction::None;
    int32 BattleConclusionAttackerCasualties = 0;
    int32 BattleConclusionDefenderCasualties = 0;

    int32 AttackerInitialArmyCost = 0;
    int32 DefenderInitialArmyCost = 0;

    /** Whether fighters have been assigned to attacker or defender sides. */
    bool bTeamsAssigned = false;

    /** Ensures EndBattle only broadcasts once per encounter. */
    bool bBattleConcluded = false;

    /** Whether we are waiting for a player-driven initiative roll. */
    bool bAwaitingInitiativeRoll = false;

    /** Timer used to pause round start while dice are displayed. */
    FTimerHandle InitiativePresentationTimer;

    /** Timer used to delay the AI's initiative roll after the player rolls. */
    FTimerHandle InitiativeAIRollTimer;

    /** Timer used to defer the battle concluded broadcast so VFX/SFX can finish. */
    FTimerHandle BattleConclusionTimerHandle;

    /** Optional pre-supplied initiative roll for attackers. */
    FOptionalInt32 PendingInitiativeRollAttacker;

    /** Optional pre-supplied initiative roll for defenders. */
    FOptionalInt32 PendingInitiativeRollDefender;

    /** Cached initiative roll for attackers to display in the HUD. */
    int32 LastInitiativeRollAttacker = 0;

    /** Cached initiative roll for defenders to display in the HUD. */
    int32 LastInitiativeRollDefender = 0;

    /** Duration that initiative dice should remain visible. */
    static constexpr float InitiativePresentationDelay = 2.f;

    /** Delay before the AI rolls initiative so the player's result can be shown first. */
    static constexpr float InitiativeAIRollDelay = 1.f;

    /** Deferred finish requests awaiting completion of queued attacks. */
    TMap<TWeakObjectPtr<AFighterPawn>, FDeferredActivationFinish> DeferredActivationFinishes;

    /** Handles bound to fighter queued-attack completion delegates. */
    TMap<TWeakObjectPtr<AFighterPawn>, FDelegateHandle> DeferredFinishDelegateHandles;

    /** Finish requests waiting for presentation sequences (dice, floaters, VFX) to complete. */
    TArray<FDeferredPresentationFinish> DeferredPresentationFinishes;

    /** Number of attack presentations that still need to finish before combat can advance. */
    int32 PendingAttackPresentationCount = 0;

    /** Whether a round start attempt was deferred while waiting on presentation. */
    bool bRoundStartDeferred = false;
};

