#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "Engine/DataTable.h"
#include "GameFramework/Actor.h"
#include "SkaldTypes.h"
#include "GridBattleManager.generated.h"

class AFighterPawn; // MUST be before the delegates

DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(
    FOnBattleEnded, ESkaldFaction, WinningFaction, int32, AttackerCasualties, int32, DefenderCasualties);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnActiveFighterChanged, AFighterPawn*, NewFighter);

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

    /** Sides of the damage dice rolled on a successful hit. */
    UPROPERTY(BlueprintReadWrite, EditAnywhere, Category="Stats")
    int32 DamageDie = 6;

    /** Damage dealt on a successful hit. */
    UPROPERTY(BlueprintReadWrite, EditAnywhere, Category="Stats")
    int32 AttackDamage = 1;

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

    /** Roll initiative for all fighters participating in the battle. */
    UFUNCTION(BlueprintCallable, Category="Skald|Battle")
    void RollInitiative();

    /** Randomly place all fighters at the start of a round. */
    UFUNCTION(BlueprintCallable, Category="Skald|Battle")
    void StartRound();

    /** Advance to the next fighter in the initiative order. */
    UFUNCTION(BlueprintCallable, Category="Battle")
    void AdvanceTurn();

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

    /** Get fighter definitions for the specified faction. */
    UFUNCTION(BlueprintCallable, BlueprintPure, Category="Battle")
    TArray<FFighterDefinition> GetFightersForFaction(ESkaldFaction Faction) const;

    /** Event fired when the battle ends reporting winner and casualties. */
    UPROPERTY(BlueprintAssignable, Category="Battle|Events")
    FOnBattleEnded OnBattleEnded;

    /** Fired whenever the active fighter changes (including nullptr). */
    UPROPERTY(BlueprintAssignable, Category="Battle|Events")
    FOnActiveFighterChanged OnActiveFighterChanged;

    UFUNCTION(BlueprintCallable, Category="Battle")
    void RegisterFighter(AFighterPawn* Fighter, bool bAsAttacker);

    UFUNCTION(BlueprintCallable, Category="Battle")
    void UnregisterFighter(AFighterPawn* Fighter);

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

private:
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
};

