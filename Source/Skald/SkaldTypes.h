#pragma once

#include "CoreMinimal.h"
#include "SkaldTypes.generated.h"

class UTexture2D;

// Keep generated.h last among includes in this header!

// Gameplay-wide constants
namespace SkaldConstants
{
    // Minimum army units required to attack a capital
    static constexpr int32 CapitalAttackArmyRequirement = 10;
}

// Utility helpers for gameplay checks
namespace SkaldHelpers
{
    // Returns true if the given attack meets the capital requirement.
    inline bool MeetsCapitalAttackRequirement(bool bTargetIsCapital, int32 ArmySent)
    {
        return !bTargetIsCapital || ArmySent >= SkaldConstants::CapitalAttackArmyRequirement;
    }
}

UENUM(BlueprintType)
enum class ETurnPhase : uint8
{
    ArmyPlacement UMETA(DisplayName = "Army Placement"),
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
enum class ESkaldFaction : uint8
{
    None       UMETA(DisplayName = "None"),
    Human      UMETA(DisplayName = "Human Faction"),
    Orc        UMETA(DisplayName = "Orc Faction"),
    Dwarf      UMETA(DisplayName = "Dwarf Faction"),
    Elf        UMETA(DisplayName = "Elf Faction"),
    LizardFolk UMETA(DisplayName = "Lizard Folk Faction"),
    Undead     UMETA(DisplayName = "Undead Faction"),
    Gnoll      UMETA(DisplayName = "Gnoll Faction"),
    Goblin     UMETA(DisplayName = "Goblin Faction"),
    Empire     UMETA(DisplayName = "IronLegion"),
    Inflicted  UMETA(DisplayName = "The Inflicted"),
    FrogFolk   UMETA(DisplayName = "ToadFolk"),
    Ravpack    UMETA(DisplayName = "Ravpack"),
};

UENUM(BlueprintType)
enum class ESiegeWeapon : uint8
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

USTRUCT(BlueprintType)
struct SKALD_API FS_ArmyUnit
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadWrite, EditAnywhere)
    int32 UnitID = 0;

    UPROPERTY(BlueprintReadWrite, EditAnywhere)
    int32 OwnerPlayerID = 0;

    UPROPERTY(BlueprintReadWrite, EditAnywhere)
    int32 ArmyUnits = 0;

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

    /** Display name for the territory the attacker is moving from. */
    UPROPERTY(BlueprintReadWrite, EditAnywhere)
    FString AttackerTerritoryName;

    /** Display name for the territory being defended. */
    UPROPERTY(BlueprintReadWrite, EditAnywhere)
    FString DefenderTerritoryName;

    UPROPERTY(BlueprintReadWrite, EditAnywhere)
    int32 ArmyCountSent = 0;

    /** Defending army’s budget for fighter selection. If zero, fall back to ArmyCountSent. */
    UPROPERTY(BlueprintReadWrite, EditAnywhere)
    int32 DefenderArmyCount = 0;

    /** Cached display name for the attacking player when travelling to the battle map. */
    UPROPERTY(BlueprintReadWrite, EditAnywhere)
    FString AttackerDisplayName;

    /** Cached display name for the defending player when travelling to the battle map. */
    UPROPERTY(BlueprintReadWrite, EditAnywhere)
    FString DefenderDisplayName;

    /** Faction the attacker was using on the world map. */
    UPROPERTY(BlueprintReadWrite, EditAnywhere)
    ESkaldFaction AttackerFaction = ESkaldFaction::None;

    /** Faction the defender was using on the world map. */
    UPROPERTY(BlueprintReadWrite, EditAnywhere)
    ESkaldFaction DefenderFaction = ESkaldFaction::None;

    /** Optional faction emblem representing the attacker. */
    UPROPERTY(BlueprintReadWrite, EditAnywhere)
    TSoftObjectPtr<UTexture2D> AttackerFactionEmblem;

    /** Optional faction emblem representing the defender. */
    UPROPERTY(BlueprintReadWrite, EditAnywhere)
    TSoftObjectPtr<UTexture2D> DefenderFactionEmblem;

    /** True when the attacking player was controlled by AI at the time of travel. */
    UPROPERTY(BlueprintReadWrite, EditAnywhere)
    bool bAttackerIsAI = false;

    /** True when the defending player was controlled by AI at the time of travel. */
    UPROPERTY(BlueprintReadWrite, EditAnywhere)
    bool bDefenderIsAI = false;

    UPROPERTY(BlueprintReadWrite, EditAnywhere)
    bool IsCapitalAttack = false;

    UPROPERTY(BlueprintReadWrite, EditAnywhere)
    TArray<int32> AssignedSiegeIDs;

    UPROPERTY(BlueprintReadWrite, EditAnywhere)
    bool TreasureFlag = false;

    UPROPERTY(BlueprintReadWrite, EditAnywhere)
    int32 TurnNumber = 0;

    /** Seed used for deterministic combat resolution. */
    UPROPERTY(BlueprintReadWrite, EditAnywhere)
    int32 RandomSeed = 0;

    /** Map name to travel back to once the battle concludes. */
    UPROPERTY(BlueprintReadWrite, EditAnywhere)
    FString ReturnMap;
};

/** Data broadcast to clients when prompting them to prepare for battle. */
USTRUCT(BlueprintType)
struct SKALD_API FPrepareForBattlePromptData {
  GENERATED_BODY()

  /** Unique ID of the attacking player. */
  UPROPERTY(BlueprintReadWrite, EditAnywhere)
  int32 AttackerPlayerID = INDEX_NONE;

  /** Unique ID of the defending player. */
  UPROPERTY(BlueprintReadWrite, EditAnywhere)
  int32 DefenderPlayerID = INDEX_NONE;

  /** ID of the territory initiating the attack. */
  UPROPERTY(BlueprintReadWrite, EditAnywhere)
  int32 AttackingTerritoryID = 0;

  /** ID of the territory being attacked. */
  UPROPERTY(BlueprintReadWrite, EditAnywhere)
  int32 DefendingTerritoryID = 0;

  /** Localised display name for the attacking player. */
  UPROPERTY(BlueprintReadWrite, EditAnywhere)
  FText AttackerDisplayName;

  /** Localised display name for the defending player. */
  UPROPERTY(BlueprintReadWrite, EditAnywhere)
  FText DefenderDisplayName;

  /** Localised title for the attacking territory. */
  UPROPERTY(BlueprintReadWrite, EditAnywhere)
  FText AttackingTerritoryName;

  /** Localised title for the defending territory. */
  UPROPERTY(BlueprintReadWrite, EditAnywhere)
  FText DefendingTerritoryName;

  /** Faction used by the attacking player. */
  UPROPERTY(BlueprintReadWrite, EditAnywhere)
  ESkaldFaction AttackerFaction = ESkaldFaction::None;

  /** Faction used by the defending player. */
  UPROPERTY(BlueprintReadWrite, EditAnywhere)
  ESkaldFaction DefenderFaction = ESkaldFaction::None;

  /** Optional emblem for the attacking player's faction. */
  UPROPERTY(BlueprintReadWrite, EditAnywhere)
  TSoftObjectPtr<UTexture2D> AttackerFactionEmblem;

  /** Optional emblem for the defending player's faction. */
  UPROPERTY(BlueprintReadWrite, EditAnywhere)
  TSoftObjectPtr<UTexture2D> DefenderFactionEmblem;
};

/** Snapshot describing which participants have confirmed battle readiness. */
USTRUCT(BlueprintType)
struct SKALD_API FSkaldBattleReadyState {
  GENERATED_BODY()

  /** PlayerID of the attacking participant (INDEX_NONE when not required). */
  UPROPERTY(BlueprintReadOnly, VisibleAnywhere)
  int32 AttackerPlayerID = INDEX_NONE;

  /** PlayerID of the defending participant (INDEX_NONE when not required). */
  UPROPERTY(BlueprintReadOnly, VisibleAnywhere)
  int32 DefenderPlayerID = INDEX_NONE;

  /** True when the attacking participant has confirmed readiness. */
  UPROPERTY(BlueprintReadOnly, VisibleAnywhere)
  bool bAttackerReady = false;

  /** True when the defending participant has confirmed readiness. */
  UPROPERTY(BlueprintReadOnly, VisibleAnywhere)
  bool bDefenderReady = false;

  /** Indicates whether the attacking participant is controlled by AI. */
  UPROPERTY(BlueprintReadOnly, VisibleAnywhere)
  bool bAttackerIsAI = false;

  /** Indicates whether the defending participant is controlled by AI. */
  UPROPERTY(BlueprintReadOnly, VisibleAnywhere)
  bool bDefenderIsAI = false;

  /** Server timestamp (seconds) when this snapshot was last updated. */
  UPROPERTY(BlueprintReadOnly, VisibleAnywhere)
  double LastUpdatedTimeSeconds = 0.0;
};

/** Serialized outcome of a resolved grid battle. */
USTRUCT(BlueprintType)
struct SKALD_API FGridBattleResolution
{
    GENERATED_BODY();

    /** True when this structure holds valid resolution data. */
    UPROPERTY(BlueprintReadWrite, EditAnywhere)
    bool bValid = false;

    /** Faction that won the battle (None for stalemates). */
    UPROPERTY(BlueprintReadWrite, EditAnywhere)
    ESkaldFaction WinningFaction = ESkaldFaction::None;

    /** Player ID of the winning side (-1 for stalemates). */
    UPROPERTY(BlueprintReadWrite, EditAnywhere)
    int32 WinningPlayerID = -1;

    /** Player ID that now owns the contested territory. */
    UPROPERTY(BlueprintReadWrite, EditAnywhere)
    int32 NewOwnerPlayerID = -1;

    /** Total attacker casualties expressed in army-cost units. */
    UPROPERTY(BlueprintReadWrite, EditAnywhere)
    int32 AttackerCasualties = 0;

    /** Total defender casualties expressed in army-cost units. */
    UPROPERTY(BlueprintReadWrite, EditAnywhere)
    int32 DefenderCasualties = 0;

    /** Surviving attacker army cost that should garrison the captured territory. */
    UPROPERTY(BlueprintReadWrite, EditAnywhere)
    int32 AttackerSurvivorArmyCost = 0;

    /** Surviving defender army cost that remains in the territory. */
    UPROPERTY(BlueprintReadWrite, EditAnywhere)
    int32 DefenderSurvivorArmyCost = 0;

    /** Total army cost committed by the attacker when the battle began. */
    UPROPERTY(BlueprintReadWrite, EditAnywhere)
    int32 AttackerCommittedArmyCost = 0;

    /** Total army cost committed by the defender when the battle began. */
    UPROPERTY(BlueprintReadWrite, EditAnywhere)
    int32 DefenderCommittedArmyCost = 0;

    /** Remaining army at the source territory after casualties. */
    UPROPERTY(BlueprintReadWrite, EditAnywhere)
    int32 SourceArmyRemaining = 0;

    /** Remaining army at the target territory after casualties/application. */
    UPROPERTY(BlueprintReadWrite, EditAnywhere)
    int32 TargetArmyRemaining = 0;
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
    FString DisplayName;

    UPROPERTY(BlueprintReadWrite, EditAnywhere)
    int32 DesiredControllerIndex = INDEX_NONE;

    UPROPERTY(BlueprintReadWrite, EditAnywhere)
    int32 DesiredTurnIndex = INDEX_NONE;

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
    int32 TerritoriesOwned = 0;

    UPROPERTY(BlueprintReadWrite, EditAnywhere)
    ESkaldFaction Faction = ESkaldFaction::Human;

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
    ESiegeWeapon Type = ESiegeWeapon::BatteringRam;

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
    int32 ArmyUnits = 0;

    UPROPERTY(BlueprintReadWrite, EditAnywhere)
    TArray<int32> AdjacentIDs;

    UPROPERTY(BlueprintReadWrite, EditAnywhere)
    int32 ContinentID = 0;

    UPROPERTY(BlueprintReadWrite, EditAnywhere, SaveGame)
    FVector Location = FVector::ZeroVector;

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
    ESkaldFaction Faction = ESkaldFaction::Human;

    UPROPERTY(BlueprintReadWrite, EditAnywhere)
    int32 Resources = 0;

    UPROPERTY(BlueprintReadWrite, EditAnywhere)
    TArray<int32> CapitalTerritoryIDs;

    UPROPERTY(BlueprintReadWrite, EditAnywhere)
    bool IsEliminated = false;
};

