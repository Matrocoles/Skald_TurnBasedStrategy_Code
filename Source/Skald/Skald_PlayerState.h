#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerState.h"
#include "SkaldTypes.h"
#include "Skald_PlayerState.generated.h"

/**
 * Player state containing basic information for turn management.
 */
UCLASS(Blueprintable, BlueprintType)
class SKALD_API ASkaldPlayerState : public APlayerState
{
    GENERATED_BODY()

public:
    ASkaldPlayerState();

    UPROPERTY(BlueprintReadWrite, Replicated, Category="PlayerState")
    bool bIsAI;

    /** Army units available for placement. */
    UPROPERTY(BlueprintReadWrite, Replicated, Category="PlayerState")
    int32 ArmyPool;

    /** Initiative roll determining turn order. */
    UPROPERTY(BlueprintReadWrite, Replicated, Category="PlayerState")
    int32 InitiativeRoll;

    /** Resource points available to the player. */
    UPROPERTY(BlueprintReadWrite, Replicated, Category="PlayerState")
    int32 Resources;

    /** Player chosen display name. */
    UPROPERTY(BlueprintReadWrite, Replicated, Category="PlayerState")
    FString PlayerDisplayName;

    /** Selected faction for this player. */
    UPROPERTY(BlueprintReadWrite, Replicated, Category="PlayerState")
    ESkaldFaction Faction;

    /** Whether the player has locked in their actions for the current turn. */
    UPROPERTY(BlueprintReadWrite, ReplicatedUsing = OnRep_HasLockedIn, Category="PlayerState")
    bool bHasLockedIn;

    /** Whether this player has been eliminated from the match. */
    UPROPERTY(BlueprintReadWrite, ReplicatedUsing = OnRep_IsEliminated, Category="PlayerState")
    bool IsEliminated;

    UFUNCTION()
    void OnRep_HasLockedIn();

    UFUNCTION()
    void OnRep_IsEliminated();

    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
};

