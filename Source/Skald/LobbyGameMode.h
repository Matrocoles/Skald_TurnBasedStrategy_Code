#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "LobbyTypes.h"
#include "LobbyGameMode.generated.h"

class ALobbyGameState;
class ASkaldPlayerState;
class USkaldGameInstance;

/**
 * Game mode used for the lobby map.
 */
UCLASS(Blueprintable, BlueprintType)
class SKALD_API ALobbyGameMode : public AGameModeBase
{
    GENERATED_BODY()

public:
    ALobbyGameMode();

    virtual void BeginPlay() override;

    virtual void PostLogin(APlayerController* NewPlayer) override;

    virtual void Logout(AController* Exiting) override;

    /** Called by the host to update the number of total lobby slots. */
    void SetTotalSlots(int32 InTotalSlots);

    /** Called by the host to update the number of AI slots. */
    void SetAISlots(int32 InAISlots);

    /** Assign a newly connected player to the next available human slot. */
    void AssignPlayerToSlot(ASkaldPlayerState* PlayerState);

    /** Update the ready toggle for the supplied player. */
    void SetPlayerReady(int32 PlayerId, bool bReady);

    /** Update the faction choice for the supplied player. */
    bool SetPlayerFaction(int32 PlayerId, ESkaldFaction Faction);

    /** Update the display name for the supplied player. */
    void SetPlayerDisplayName(int32 PlayerId, const FString& DisplayName);

    /** Attempt to start the match after all players ready up. */
    void TryLaunchMatch(APlayerController* RequestingController);

protected:
    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "UI")
    TSubclassOf<class UUserWidget> LobbyWidgetClass;

    /** Generate a unique AI name for lobby preview. */
    FString GenerateUniqueAIName(TSet<FString>& UsedNames) const;

    /** Assign random AI factions using the game instance pool. */
    ESkaldFaction GenerateUniqueAIFaction(TArray<ESkaldFaction>& TakenFactions) const;

    /** Refreshes all replicated lobby slot data. */
    void RefreshReplicatedLobbyState();

    /** Remove the player from any assigned slot. */
    void RemovePlayerFromSlots(int32 PlayerId);

    /** Cached pointer to the lobby game state. */
    UPROPERTY(Transient)
    ALobbyGameState* CachedLobbyState;

    /** Pending lobby slot data on the authority. */
    TArray<FLobbyPlayerSlot> AuthoritySlots;

    /** Desired total slot count set by the host. */
    int32 AuthorityTotalSlots;

    /** Number of AI slots requested by the host. */
    int32 AuthorityAISlots;
};

