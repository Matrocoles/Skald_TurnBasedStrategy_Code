#pragma once

#include "CoreMinimal.h"
#include "Engine/EngineTypes.h"
#include "GameFramework/SaveGame.h"
#include "SkaldTypes.h"
#include "SkaldSaveGame.generated.h"

class USoundBase;
class UTexture2D;

/** Snapshot of camera data for a single controller. */
USTRUCT(BlueprintType)
struct FSkaldCameraSaveData
{
    GENERATED_BODY()

    /** World-space location of the pawn driving the camera. */
    UPROPERTY(BlueprintReadWrite, EditAnywhere, SaveGame)
    FVector Location = FVector::ZeroVector;

    /** Control rotation applied to the camera. */
    UPROPERTY(BlueprintReadWrite, EditAnywhere, SaveGame)
    FRotator Rotation = FRotator::ZeroRotator;

    /** Zoom captured from the active spring arm (or FOV fallback). */
    UPROPERTY(BlueprintReadWrite, EditAnywhere, SaveGame)
    float Zoom = 0.f;

    /** True if the overview camera was locked onto a territory. */
    UPROPERTY(BlueprintReadWrite, EditAnywhere, SaveGame)
    bool bHasLockedTerritory = false;

    /** Identifier of the territory the camera was focused on. */
    UPROPERTY(BlueprintReadWrite, EditAnywhere, SaveGame)
    int32 LockedTerritoryId = INDEX_NONE;

    /** Whether the tactical battle camera was active. */
    UPROPERTY(BlueprintReadWrite, EditAnywhere, SaveGame)
    bool bBattleCameraActive = false;
};

/** Player controller level data captured for saving/loading. */
USTRUCT(BlueprintType)
struct FSkaldControllerSaveData
{
    GENERATED_BODY()

    /** True when this controller is an AI participant. */
    UPROPERTY(BlueprintReadWrite, EditAnywhere, SaveGame)
    bool bIsAI = false;

    /** Player identifier associated with the controller. */
    UPROPERTY(BlueprintReadWrite, EditAnywhere, SaveGame)
    int32 PlayerId = INDEX_NONE;

    /** Display name used for UI and announcements. */
    UPROPERTY(BlueprintReadWrite, EditAnywhere, SaveGame)
    FString PlayerName;

    /** Faction selected by the controller. */
    UPROPERTY(BlueprintReadWrite, EditAnywhere, SaveGame)
    ESkaldFaction Faction = ESkaldFaction::None;

    /** Optional emblem texture used for UI. */
    UPROPERTY(BlueprintReadWrite, EditAnywhere, SaveGame)
    TSoftObjectPtr<UTexture2D> FactionEmblem;

    /** Representative colour used for UI indicators. */
    UPROPERTY(BlueprintReadWrite, EditAnywhere, SaveGame)
    FLinearColor PlayerColor = FLinearColor::White;

    /** Territory identifiers owned by this controller. */
    UPROPERTY(BlueprintReadWrite, EditAnywhere, SaveGame)
    TArray<int32> OwnedTerritoryIds;

    /** Snapshot of the controller's camera configuration. */
    UPROPERTY(BlueprintReadWrite, EditAnywhere, SaveGame)
    FSkaldCameraSaveData Camera;

    /** Index of this controller within the saved turn order. */
    UPROPERTY(BlueprintReadWrite, EditAnywhere, SaveGame)
    int32 TurnOrderIndex = INDEX_NONE;
};

/** Turn order entry captured for restoring the sequence. */
USTRUCT(BlueprintType)
struct FSkaldTurnParticipantSaveData
{
    GENERATED_BODY()

    /** Player identifier represented by this entry. */
    UPROPERTY(BlueprintReadWrite, EditAnywhere, SaveGame)
    int32 PlayerId = INDEX_NONE;

    /** True when the participant was controlled by AI. */
    UPROPERTY(BlueprintReadWrite, EditAnywhere, SaveGame)
    bool bIsAI = false;

    /** Initiative value used for ordering. */
    UPROPERTY(BlueprintReadWrite, EditAnywhere, SaveGame)
    int32 Initiative = 0;
};

/** Movement action usage captured from the turn manager. */
USTRUCT(BlueprintType)
struct FSkaldMovementActionSaveData
{
    GENERATED_BODY()

    /** Identifier of the player who has consumed movement actions. */
    UPROPERTY(BlueprintReadWrite, EditAnywhere, SaveGame)
    int32 PlayerId = INDEX_NONE;

    /** Count of movement actions spent during the current phase. */
    UPROPERTY(BlueprintReadWrite, EditAnywhere, SaveGame)
    int32 ActionsTaken = 0;
};

/** Aggregated turn/phase state saved with the game. */
USTRUCT(BlueprintType)
struct FSkaldGameFlowSaveData
{
    GENERATED_BODY()

    /** Global turn number when the save was taken. */
    UPROPERTY(BlueprintReadWrite, EditAnywhere, SaveGame)
    int32 TurnNumber = 1;

    /** Index into TurnOrder representing the active participant. */
    UPROPERTY(BlueprintReadWrite, EditAnywhere, SaveGame)
    int32 ActiveTurnIndex = 0;

    /** Phase the turn manager was currently resolving. */
    UPROPERTY(BlueprintReadWrite, EditAnywhere, SaveGame)
    ETurnPhase CurrentPhase = ETurnPhase::Reinforcement;

    /** Ordered list of turn participants. */
    UPROPERTY(BlueprintReadWrite, EditAnywhere, SaveGame)
    TArray<FSkaldTurnParticipantSaveData> TurnOrder;

    /** Whether the turn loop had begun. */
    UPROPERTY(BlueprintReadWrite, EditAnywhere, SaveGame)
    bool bTurnsStarted = false;

    /** Pending battle waiting to travel or resolve. */
    UPROPERTY(BlueprintReadWrite, EditAnywhere, SaveGame)
    FS_BattlePayload PendingBattle;

    /** Pending battle currently waiting on readiness. */
    UPROPERTY(BlueprintReadWrite, EditAnywhere, SaveGame)
    FS_BattlePayload PendingBattlePreparation;

    /** Cached readiness state for the pending battle. */
    UPROPERTY(BlueprintReadWrite, EditAnywhere, SaveGame)
    FSkaldBattleReadyState PendingReadyState;

    /** Movement action counts tracked per player. */
    UPROPERTY(BlueprintReadWrite, EditAnywhere, SaveGame)
    TArray<FSkaldMovementActionSaveData> MovementActions;
};

/** Audio component state saved to preserve ambience. */
USTRUCT(BlueprintType)
struct FSkaldAudioComponentSaveData
{
    GENERATED_BODY()

    /** Sound asset associated with the component. */
    UPROPERTY(BlueprintReadWrite, EditAnywhere, SaveGame)
    TSoftObjectPtr<USoundBase> Sound;

    /** Whether the component was actively playing. */
    UPROPERTY(BlueprintReadWrite, EditAnywhere, SaveGame)
    bool bWasPlaying = false;

    /** Playback time cached when the save occurred. */
    UPROPERTY(BlueprintReadWrite, EditAnywhere, SaveGame)
    float PlaybackTime = 0.f;
};

/** High-level world state persisted alongside gameplay data. */
USTRUCT(BlueprintType)
struct FSkaldWorldStateSaveData
{
    GENERATED_BODY()

    /** Selected territory at the time of saving (if any). */
    UPROPERTY(BlueprintReadWrite, EditAnywhere, SaveGame)
    int32 SelectedTerritoryId = INDEX_NONE;

    /** Player who initiated the active territory selection. */
    UPROPERTY(BlueprintReadWrite, EditAnywhere, SaveGame)
    int32 SelectedByPlayerId = INDEX_NONE;

    /** Ambient audio components that should resume after loading. */
    UPROPERTY(BlueprintReadWrite, EditAnywhere, SaveGame)
    TArray<FSkaldAudioComponentSaveData> ActiveAudio;
};

/**
 * Native save game object storing persistent game data.
 */
UCLASS(BlueprintType)
class SKALD_API USkaldSaveGame : public USaveGame
{
    GENERATED_BODY()

public:
    USkaldSaveGame();

    /** Name of the save slot. */
    UPROPERTY(BlueprintReadWrite, Category="SaveGame", SaveGame)
    FString SaveName;

    /** Date the save was created. */
    UPROPERTY(BlueprintReadWrite, Category="SaveGame", SaveGame)
    FDateTime SaveDate;

    /** Legacy turn number (retained for backwards compatibility). */
    UPROPERTY(BlueprintReadWrite, Category="SaveGame", SaveGame)
    int32 TurnNumber = 0;

    /** Legacy active player index field. */
    UPROPERTY(BlueprintReadWrite, Category="SaveGame", SaveGame)
    int32 CurrentPlayerIndex = 0;

    /** Legacy saved turn index field. */
    UPROPERTY(BlueprintReadWrite, Category="SaveGame", SaveGame)
    int32 SavedTurnIndex = 0;

    /** Legacy saved active player identifier. */
    UPROPERTY(BlueprintReadWrite, Category="SaveGame", SaveGame)
    int32 SavedTurnPlayerID = 0;

    /** Legacy saved phase field. */
    UPROPERTY(BlueprintReadWrite, Category="SaveGame", SaveGame)
    ETurnPhase SavedTurnPhase = ETurnPhase::Reinforcement;

    /** Legacy flag indicating whether turns had started. */
    UPROPERTY(BlueprintReadWrite, Category="SaveGame", SaveGame)
    bool bTurnsStarted = false;

    /** Detailed turn/phase data captured for the save. */
    UPROPERTY(BlueprintReadWrite, Category="SaveGame", SaveGame)
    FSkaldGameFlowSaveData GameFlow;

    /** Stored territory state data. */
    UPROPERTY(BlueprintReadWrite, Category="SaveGame", SaveGame)
    TArray<FS_Territory> Territories;

    /** Stored player state snapshots. */
    UPROPERTY(BlueprintReadWrite, Category="SaveGame", SaveGame)
    TArray<FPlayerSaveStruct> Players;

    /** Stored siege equipment information. */
    UPROPERTY(BlueprintReadWrite, Category="SaveGame", SaveGame)
    TArray<FS_Siege> Sieges;

    /** Snapshot of controller specific data (human + AI). */
    UPROPERTY(BlueprintReadWrite, Category="SaveGame", SaveGame)
    TArray<FSkaldControllerSaveData> Controllers;

    /** Random seed used for deterministic operations. */
    UPROPERTY(BlueprintReadWrite, Category="SaveGame", SaveGame)
    int32 RandomSeed = 0;

    /** Asset path for the persistent map when the save occurred. */
    UPROPERTY(BlueprintReadWrite, Category="SaveGame", SaveGame)
    FString MapAssetPath;

    /** Legacy overview camera offset. */
    UPROPERTY(BlueprintReadWrite, Category="SaveGame", SaveGame)
    FVector2D SavedViewOffset = FVector2D::ZeroVector;

    /** Legacy overview camera zoom level. */
    UPROPERTY(BlueprintReadWrite, Category="SaveGame", SaveGame)
    float SavedZoomAmount = 0.f;

    /** Snapshot of world level data (selection, ambience, etc.). */
    UPROPERTY(BlueprintReadWrite, Category="SaveGame", SaveGame)
    FSkaldWorldStateSaveData WorldState;
};

