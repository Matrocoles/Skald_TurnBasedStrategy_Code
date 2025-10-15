#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "Engine/DataTable.h"
#include "GameFramework/Actor.h"
#include "SkaldTypes.h"
#include "Engine/Texture2D.h"
#include "TimerManager.h"
#include "GridBattleManager.generated.h"

class AFighterPawn; // MUST be before the delegates

DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(
    FOnBattleEnded, ESkaldFaction, WinningFaction, int32, AttackerCasualties, int32, DefenderCasualties);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_FiveParams(
    FOnAttackResolved, AFighterPawn*, Attacker, AFighterPawn*, Defender, int32, Roll, bool, bHit, int32, Damage);
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
    /** Load fighter definitions and set default state. */
    UGridBattleManager();

    /** Initialise a battle with attackers and defenders. */
    UFUNCTION(BlueprintCallable, Category="Battle")
    void InitBattle(const TArray<FFighter>& Attackers, const TArray<FFighter>& Defenders);

    /** Set the seed for the internal random stream. */
    UFUNCTION(BlueprintCallable, Category="Skald|Battle")
    void SetRandomSeed(int32 Seed);

    /** Resolve an attack following strength/defence rules. Returns true if the defender is defeated. */
    UFUNCTION(BlueprintCallable, Category="Battle")
    static bool ResolveAttack(FFighter& Attacker, FFighter& Defender, int32& OutDamage, UPARAM(ref) FRandomStream& RandomStream);

    /** Roll initiative for the next round, determining which side acts first. */
    UFUNCTION(BlueprintCallable, Category="Skald|Battle")
    void RollInitiative();

    /** Continue the round after the player confirms the initiative roll. */
    UFUNCTION(BlueprintCallable, Category="Skald|Battle")
    void ConfirmInitiativeRoll();

    /** Randomly place all fighters at the start of a round. */
    UFUNCTION(BlueprintCallable, Category="Skald|Battle")
    void StartRound();

    /** Finish the currently active fighter's turn and swap sides. */
    UFUNCTION(BlueprintCallable, Category="Battle")
    void AdvanceTurn();

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

    void ReportAttackRoll(AFighterPawn* Attacker, AFighterPawn* Defender, int32 Roll, bool bHit, int32 Damage);
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
    void FinalizeRoundStart();
    void ScheduleRoundStart(bool bDelayForPresentation);
    bool ShouldPauseForInitiativePrompt() const;

    bool HasLivingFighters(bool bForAttackers) const;
    bool HasAvailableFighters(bool bForAttackers) const;
    bool IsSideAIControlled(bool bForAttackers) const;
    void EvaluateRoundProgress(bool bPreviousWasAttacker);
    void ClearInactiveFighters();

    // Track both counts and costs separately
    int32 AttackerSurvivorUnitCount = 0;
    int32 DefenderSurvivorUnitCount = 0;
    int32 AttackerSurvivorArmyCost = 0;
    int32 DefenderSurvivorArmyCost = 0;

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

    /** Cached initiative roll for attackers to display in the HUD. */
    int32 LastInitiativeRollAttacker = 0;

    /** Cached initiative roll for defenders to display in the HUD. */
    int32 LastInitiativeRollDefender = 0;

    /** Duration that initiative dice should remain visible. */
    static constexpr float InitiativePresentationDelay = 2.f;
};

