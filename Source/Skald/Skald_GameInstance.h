#pragma once

#include "CoreMinimal.h"
#include "Engine/GameInstance.h"
#include "SkaldTypes.h"
#include "Skald_GameInstance.generated.h"

class UGridBattleManager;
class USkaldSaveGame;

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FSkaldFactionsUpdated);
/** Game instance storing player selections from the lobby. */
UCLASS()
class SKALD_API USkaldGameInstance : public UGameInstance
{
    GENERATED_BODY()

public:
    /** Initialize the game instance. */
    virtual void Init() override;

    /** Player chosen display name. */
    UPROPERTY(BlueprintReadWrite, Category="Player")
    FString DisplayName;

    /** Selected faction for this player. */
    UPROPERTY(BlueprintReadWrite, Category="Player")
    ESkaldFaction Faction;

    /** Whether the game was started in multiplayer mode. */
    UPROPERTY(BlueprintReadWrite, Category="Player")
    bool bIsMultiplayer;

    /** Factions that have already been selected by players or AI. */
    UPROPERTY(BlueprintReadWrite, Category="Player")
    TArray<ESkaldFaction> TakenFactions;

    /** Event fired when the taken faction list changes. */
    UPROPERTY(BlueprintAssignable, Category="Player|Events")
    FSkaldFactionsUpdated OnFactionsUpdated;

    /** Payload describing the battle to resolve when returning from the battle map. */
    UPROPERTY(BlueprintReadWrite, Category="Battle")
    FS_BattlePayload PendingBattle;

    /** Runtime manager used to execute grid based battles. */
    UPROPERTY(BlueprintReadWrite, Category="Battle")
    class UGridBattleManager* GridBattleManager = nullptr;

    /** Random stream used for deterministic combat rolls. */
    UPROPERTY()
    FRandomStream CombatRandomStream;

    /** Index of the current player turn when travelling between maps. */
    UPROPERTY(BlueprintReadWrite, Category="Turn")
    int32 SavedTurnIndex = 0;

    /** Phase of the turn cycle that was active before travelling. */
    UPROPERTY(BlueprintReadWrite, Category="Turn")
    ETurnPhase SavedTurnPhase = ETurnPhase::Reinforcement;

    /** Flag indicating whether the turn manager should resume after travel. */
    UPROPERTY(BlueprintReadWrite, Category="Turn")
    bool bResumeTurns = false;

    /** Seed the combat random stream so all clients use the same sequence. */
    UFUNCTION(BlueprintCallable, Category="Battle")
    void SeedCombatRandomStream(int32 Seed);

    /** Save game loaded when transitioning from the main menu. */
    UPROPERTY(BlueprintReadWrite, Category="SaveGame")
    USkaldSaveGame* LoadedSaveGame = nullptr;
};

