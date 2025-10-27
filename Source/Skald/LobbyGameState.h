#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameStateBase.h"
#include "LobbyTypes.h"
#include "LobbyGameState.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FLobbySlotsUpdated);

/** Game state replicating lobby slot data to all clients. */
UCLASS()
class SKALD_API ALobbyGameState : public AGameStateBase
{
    GENERATED_BODY()

public:
    ALobbyGameState();

    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

    /** Current configuration for each potential lobby slot. */
    UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_LobbySlots, Category = "Lobby")
    TArray<FLobbyPlayerSlot> LobbySlots;

    /** Total number of slots enabled by the host (2-4). */
    UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_TotalSlots, Category = "Lobby")
    int32 TotalSlots = 2;

    /** Number of AI slots reserved by the host. */
    UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_AISlots, Category = "Lobby")
    int32 ReservedAISlots = 0;

    /** Fired whenever the replicated lobby data changes. */
    UPROPERTY(BlueprintAssignable, Category = "Lobby|Events")
    FLobbySlotsUpdated OnLobbySlotsUpdated;

    /** Returns true when every active slot is marked as ready. */
    UFUNCTION(BlueprintPure, Category = "Lobby")
    bool AreAllSlotsReady() const;

    /** Locate the slot assigned to a given player id. */
    int32 FindSlotIndexForPlayer(int32 PlayerId) const;

    /** Access a slot by index (safe for inactive indices). */
    const FLobbyPlayerSlot* GetSlot(int32 Index) const;

protected:
    UFUNCTION()
    void OnRep_LobbySlots();

    UFUNCTION()
    void OnRep_TotalSlots();

    UFUNCTION()
    void OnRep_AISlots();

    void BroadcastSlotsUpdated();
};

