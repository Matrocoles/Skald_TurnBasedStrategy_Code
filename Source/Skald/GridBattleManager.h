#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "SkaldTypes.h"
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

/** Event fired when a grid battle concludes. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOnBattleEnded, ESkaldFaction, WinningFaction, int32, AttackerCasualties, int32, DefenderCasualties);

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
    /** Initialise a battle with attackers and defenders. */
    UFUNCTION(BlueprintCallable, Category="Battle")
    void InitBattle(const TArray<FFighter>& Attackers, const TArray<FFighter>& Defenders);

    /** Begin the battle and resolve rounds until a victor is found. */
    UFUNCTION(BlueprintCallable, Category="Battle")
    void StartBattle(UPARAM(ref) FRandomStream& RandomStream);

    /** Roll a D6 to determine initiative. */
    UFUNCTION(BlueprintCallable, Category="Battle")
    static int32 RollInitiative(UPARAM(ref) FRandomStream& RandomStream);

    /** Resolve an attack following strength/defence rules. Returns true if the defender is defeated. */
    UFUNCTION(BlueprintCallable, Category="Battle")
    static bool ResolveAttack(FFighter& Attacker, FFighter& Defender, int32& OutDamage, UPARAM(ref) FRandomStream& RandomStream);

    /** Number of surviving attackers after the battle concludes. */
    UFUNCTION(BlueprintCallable, BlueprintPure, Category="Battle")
    int32 GetAttackerSurvivors() const;

    /** Number of surviving defenders after the battle concludes. */
    UFUNCTION(BlueprintCallable, BlueprintPure, Category="Battle")
    int32 GetDefenderSurvivors() const;

    /** Event fired when the battle ends reporting winner and casualties. */
    UPROPERTY(BlueprintAssignable, Category="Battle|Events")
    FOnBattleEnded OnBattleEnded;

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
};

