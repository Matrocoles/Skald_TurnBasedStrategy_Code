#pragma once

#include "CoreMinimal.h"
#include "SkaldTypes.generated.h"

// Keep generated.h last among includes in this header!

// Gameplay-wide constants
namespace SkaldConstants
{
    // Minimum army units required to attack a capital
    static constexpr int32 CapitalAttackArmyRequirement = 10;
}

UENUM(BlueprintType)
enum class ETurnPhase : uint8
{
    Reinforcement UMETA(DisplayName = "Reinforcement"),
    Attack        UMETA(DisplayName = "Attack"),
    Engineering   UMETA(DisplayName = "Engineering"),
    Treasure      UMETA(DisplayName = "Treasure"),
    Movement      UMETA(DisplayName = "Movement"),
    EndTurn       UMETA(DisplayName = "EndTurn"),
    Revolt        UMETA(DisplayName = "Revolt"),
};

/** Factions a player or fighter can belong to. */
UENUM(BlueprintType)
enum class EFaction : uint8
{
    None   UMETA(DisplayName = "None"),
    Empire UMETA(DisplayName = "Empire"),
    Orcs   UMETA(DisplayName = "Orcs"),
    Undead UMETA(DisplayName = "Undead"),
};

UENUM(BlueprintType)
enum class E_SiegeWeapons : uint8   // (Consider E prefix instead of Enum_ for UE style)
{
    BatteringRam UMETA(DisplayName = "BatteringRam"),
    Trebuchet    UMETA(DisplayName = "Trebuchet"),
    SiegeTower   UMETA(DisplayName = "SiegeTower"),
    Catapult     UMETA(DisplayName = "Catapult"),
};

UENUM(BlueprintType)
enum class EBattleStats : uint8
{
    // Example entries; keep your real ones
    Attack       UMETA(DisplayName = "Attack"),
    Defense      UMETA(DisplayName = "Defense"),
    Speed        UMETA(DisplayName = "Speed"),
    // ...
};

// Factions available for players to choose at game start
UENUM(BlueprintType)
enum class EFaction : uint8
{
    Human       UMETA(DisplayName = "Human Faction"),
    Orc         UMETA(DisplayName = "Orc Faction"),
    Dwarf       UMETA(DisplayName = "Dwarf Faction"),
    Elf         UMETA(DisplayName = "Elf Faction"),
    LizardFolk  UMETA(DisplayName = "Lizard Folk Faction"),
    Undead      UMETA(DisplayName = "Undead Faction"),
    Gnoll       UMETA(DisplayName = "Gnoll Faction"),
};

USTRUCT(BlueprintType)
struct SKALD_API FS_ArmyUnit
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadWrite, EditAnywhere)
    int32 UnitID = 0;

    UPROPERTY(BlueprintReadWrite, EditAnywhere)
    int32 OwnerPlayerID = 0;

    UPROPERTY(BlueprintReadWrite, EditAnywhere)
    int32 ArmyCount = 0;

    UPROPERTY(BlueprintReadWrite, EditAnywhere)
    bool HasTreasure = false;

    UPROPERTY(BlueprintReadWrite, EditAnywhere)
    int32 AssignedSiegeID = 0;

    UPROPERTY(BlueprintReadWrite, EditAnywhere)
    bool IsSelected = false;
};

USTRUCT(BlueprintType)
struct SKALD_API FS_BattlePayload
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadWrite, EditAnywhere)
    int32 AttackerPlayerID = 0;

    UPROPERTY(BlueprintReadWrite, EditAnywhere)
    int32 DefenderPlayerID = 0;

    UPROPERTY(BlueprintReadWrite, EditAnywhere)
    int32 FromTerritoryID = 0;

    UPROPERTY(BlueprintReadWrite, EditAnywhere)
    int32 TargetTerritoryID = 0;

    UPROPERTY(BlueprintReadWrite, EditAnywhere)
    int32 ArmyCountSent = 0;

    UPROPERTY(BlueprintReadWrite, EditAnywhere)
    bool IsCapitalAttack = false;

    UPROPERTY(BlueprintReadWrite, EditAnywhere)
    TArray<int32> AssignedSiegeIDs;

    UPROPERTY(BlueprintReadWrite, EditAnywhere)
    bool TreasureFlag = false;

    UPROPERTY(BlueprintReadWrite, EditAnywhere)
    int32 TurnNumber = 0;
};

USTRUCT(BlueprintType)
struct SKALD_API FS_PlayerData
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadWrite, EditAnywhere)
    int32 PlayerID = 0;

    UPROPERTY(BlueprintReadWrite, EditAnywhere)
    FString PlayerName;

    UPROPERTY(BlueprintReadWrite, EditAnywhere)
    bool IsAI = false;

    UPROPERTY(BlueprintReadWrite, EditAnywhere)
    bool IsEliminated = false;

    UPROPERTY(BlueprintReadWrite, EditAnywhere)
    int32 CapitalsOwned = 0;

    UPROPERTY(BlueprintReadWrite, EditAnywhere)
    int32 ColorIndex = 0;

    UPROPERTY(BlueprintReadWrite, EditAnywhere)
    int32 TroopsCount = 0;

    UPROPERTY(BlueprintReadWrite, EditAnywhere)
    int32 Resources = 0;

    UPROPERTY(BlueprintReadWrite, EditAnywhere)
    EFaction Faction = EFaction::Human;

    UPROPERTY(BlueprintReadWrite, EditAnywhere)
    TArray<int32> CapitalTerritoryIDs;

    UPROPERTY(BlueprintReadWrite, EditAnywhere)
    bool IsHuman = false;

    UPROPERTY(BlueprintReadWrite, EditAnywhere)
    bool IsAlive = false;
};

USTRUCT(BlueprintType)
struct SKALD_API FS_Siege
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadWrite, EditAnywhere)
    int32 SiegeID = 0;

    UPROPERTY(BlueprintReadWrite, EditAnywhere)
    E_SiegeWeapons Type = E_SiegeWeapons::BatteringRam;

    UPROPERTY(BlueprintReadWrite, EditAnywhere)
    int32 BuiltAtTerritoryID = 0;

    UPROPERTY(BlueprintReadWrite, EditAnywhere)
    int32 AssignedToUnitID = 0;

    UPROPERTY(BlueprintReadWrite, EditAnywhere)
    TArray<int32> BattleStats;
};

USTRUCT(BlueprintType)
struct SKALD_API FS_Territory
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadWrite, EditAnywhere)
    int32 TerritoryID = 0;

    UPROPERTY(BlueprintReadWrite, EditAnywhere)
    FString TerritoryName;

    UPROPERTY(BlueprintReadWrite, EditAnywhere)
    int32 OwnerPlayerID = 0;

    UPROPERTY(BlueprintReadWrite, EditAnywhere)
    bool IsCapital = false;

    UPROPERTY(BlueprintReadWrite, EditAnywhere)
    int32 CapitalOwner = 0;

    UPROPERTY(BlueprintReadWrite, EditAnywhere)
    int32 ArmyCount = 0;

    UPROPERTY(BlueprintReadWrite, EditAnywhere)
    TArray<int32> AdjacentIDs;

    UPROPERTY(BlueprintReadWrite, EditAnywhere)
    int32 ContinentID = 0;

    UPROPERTY(BlueprintReadWrite, EditAnywhere)
    bool HasTreasure = false;

    UPROPERTY(BlueprintReadWrite, EditAnywhere)
    int32 TreasureAttachedUnitID = 0;

    UPROPERTY(BlueprintReadWrite, EditAnywhere)
    int32 FortificationLevel = 0;

    UPROPERTY(BlueprintReadWrite, EditAnywhere)
    bool Moat = false;

    UPROPERTY(BlueprintReadWrite, EditAnywhere)
    int32 WallHealth = 0;

    UPROPERTY(BlueprintReadWrite, EditAnywhere)
    int32 BuiltSiegeID = 0;

    UPROPERTY(BlueprintReadWrite, EditAnywhere)
    int32 ConqueredTurn = 0;

    UPROPERTY(BlueprintReadWrite, EditAnywhere)
    bool IsNeutralSpawn = false;
};

USTRUCT(BlueprintType)
struct SKALD_API FPlayerSaveStruct
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadWrite, EditAnywhere)
    int32 PlayerID = 0;

    UPROPERTY(BlueprintReadWrite, EditAnywhere)
    FString PlayerName;

    UPROPERTY(BlueprintReadWrite, EditAnywhere)
    bool IsAI = false;

    UPROPERTY(BlueprintReadWrite, EditAnywhere)
    EFaction Faction = EFaction::Human;

    UPROPERTY(BlueprintReadWrite, EditAnywhere)
    TArray<int32> CapitalTerritoryIDs;

    UPROPERTY(BlueprintReadWrite, EditAnywhere)
    bool IsEliminated = false;
};

