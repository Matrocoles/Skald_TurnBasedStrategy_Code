#pragma once

#include "CoreMinimal.h"
#include "SkaldTypes.generated.h"

// Keep generated.h last among includes in this header!

UENUM(BlueprintType)
enum class E_TurnPhase : uint8
{
    Reinforcement UMETA(DisplayName = "Reinforcement"),
    Attack        UMETA(DisplayName = "Attack"),
    Engineering   UMETA(DisplayName = "Engineering"),
    Treasure      UMETA(DisplayName = "Treasure"),
    Movement      UMETA(DisplayName = "Movement"),
    EndTurn       UMETA(DisplayName = "EndTurn"),
    Revolt        UMETA(DisplayName = "Revolt"),
};

UENUM(BlueprintType)
enum class E_SiegeWeapons : uint8   // (Consider “E” prefix instead of “Enum_” for UE style)
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
    FString FactionName;

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
    FString FactionName;

    UPROPERTY(BlueprintReadWrite, EditAnywhere)
    TArray<int32> CapitalTerritoryIDs;

    UPROPERTY(BlueprintReadWrite, EditAnywhere)
    bool IsEliminated = false;
};

