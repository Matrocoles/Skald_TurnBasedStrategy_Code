#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "Engine/DataTable.h"
#include "GameFramework/Actor.h"
#include "SkaldTypes.h"

class AFighterPawn;

// Event fired when a grid battle concludes.
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(
    FOnBattleEnded, ESkaldFaction, WinningFaction, int32, AttackerCasualties, int32, DefenderCasualties);

// Event fired whenever the active fighter changes (including nullptr)
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnActiveFighterChanged, AFighterPawn*, NewFighter);

#include "GridBattleManager.generated.h"

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

    /** Begin the battle and resolve rounds until a victor is found. */
    UFUNCTION(BlueprintCallable, Category="Battle")
    void StartBattle(UPARAM(ref) FRandomStream& RandomStream);

    /** Roll a D6 to determine initiative. */
    UFUNCTION(BlueprintCallable, Category="Battle")
    static int32 RollInitiativeDie(UPARAM(ref) FRandomStream& RandomStream);

    /** Resolve an attack following strength/defence rules. Returns true if the defender is defeated. */
    UFUNCTION(BlueprintCallable, Category="Battle")
    static bool ResolveAttack(FFighter& Attacker, FFighter& Defender, int32& OutDamage, UPARAM(ref) FRandomStream& RandomStream);

    /** Roll initiative for all fighters participating in the battle. */
    UFUNCTION(BlueprintCallable, Category="Battle")
    void RollInitiative();

    /** Randomly place all fighters at the start of a round. */
    UFUNCTION(BlueprintCallable, Category="Battle")
    void StartRound(UPARAM(ref) FRandomStream& RandomStream);

    /** Advance to the next fighter in the initiative order. */
    UFUNCTION(BlueprintCallable, Category="Battle")
    void AdvanceTurn();

    /** Conclude the battle and broadcast the results. */
    UFUNCTION(BlueprintCallable, Category="Battle")
    void EndBattle();

    /** Total army cost of surviving attackers after the battle concludes. */
    UFUNCTION(BlueprintCallable, BlueprintPure, Category="Battle")
    int32 GetAttackerSurvivors();

    /** Total army cost of surviving defenders after the battle concludes. */
    UFUNCTION(BlueprintCallable, BlueprintPure, Category="Battle")
    int32 GetDefenderSurvivors();

    /** Total army cost of surviving attackers. Equivalent to GetAttackerSurvivors. */
    UFUNCTION(BlueprintCallable, BlueprintPure, Category="Battle")
    int32 GetAttackerSurvivorCost() const;

    /** Total army cost of surviving defenders. Equivalent to GetDefenderSurvivors. */
    UFUNCTION(BlueprintCallable, BlueprintPure, Category="Battle")
    int32 GetDefenderSurvivorCost() const;

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
    UPROPERTY(BlueprintReadOnly, Category="Battle")
    UDataTable* FighterDefinitions;

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

    /** Total starting army cost for attackers. */
    UPROPERTY(BlueprintReadOnly, Category="Battle")
    int32 AttackerInitialArmyCost = 0;

    /** Total starting army cost for defenders. */
    UPROPERTY(BlueprintReadOnly, Category="Battle")
    int32 DefenderInitialArmyCost = 0;

    /** Cached surviving army-cost totals for each side. */
    UPROPERTY(BlueprintReadOnly, Category="Battle")
    int32 AttackerSurvivorCount = 0;

    UPROPERTY(BlueprintReadOnly, Category="Battle")
    int32 DefenderSurvivorCount = 0;

    /** Whether fighters have been assigned to attacker or defender sides. */
    bool bTeamsAssigned = false;
};

