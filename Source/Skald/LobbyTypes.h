#pragma once

#include "CoreMinimal.h"
#include "SkaldTypes.h"

#include "LobbyTypes.generated.h"

/** Data replicated for each multiplayer lobby slot. */
USTRUCT(BlueprintType)
struct FLobbyPlayerSlot
{
    GENERATED_BODY()

    /** Whether the slot is currently active and should be displayed. */
    UPROPERTY(BlueprintReadOnly, EditAnywhere)
    bool bIsActive = false;

    /** True when the slot represents an AI participant. */
    UPROPERTY(BlueprintReadOnly, EditAnywhere)
    bool bIsAI = false;

    /** Player state identifier occupying this slot (INDEX_NONE if empty/AI). */
    UPROPERTY(BlueprintReadOnly, EditAnywhere)
    int32 PlayerId = INDEX_NONE;

    /** Network display name for this slot. */
    UPROPERTY(BlueprintReadOnly, EditAnywhere)
    FString DisplayName;

    /** Selected faction for the player occupying this slot. */
    UPROPERTY(BlueprintReadOnly, EditAnywhere)
    ESkaldFaction Faction = ESkaldFaction::None;

    /** Ready toggle replicated to clients. */
    UPROPERTY(BlueprintReadOnly, EditAnywhere)
    bool bIsReady = false;
};

