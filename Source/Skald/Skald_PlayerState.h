#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerState.h"
#include "SkaldTypes.h"
#include "GridBattleManager.h"
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

    /** Deployable units available for placement. */
    UPROPERTY(BlueprintReadWrite, ReplicatedUsing = OnRep_DeployableUnits, Category="PlayerState")
    int32 DeployableUnits;

    /** Initiative roll determining turn order. */
    UPROPERTY(BlueprintReadWrite, Replicated, Category="PlayerState")
    int32 InitiativeRoll;

    /** Resource points available to the player. */
    UPROPERTY(BlueprintReadWrite, Replicated, Category="PlayerState")
    int32 Resources;

    /** Player chosen display name. */
    UPROPERTY(BlueprintReadWrite, ReplicatedUsing = OnRep_PlayerDisplayName, Category="PlayerState")
    FString PlayerDisplayName;

    /** Selected faction for this player. */
    UPROPERTY(Replicated, BlueprintReadOnly, Category="Skald|Player")
    ESkaldFaction Faction = ESkaldFaction::None;

    /** Fighters chosen during pre-battle selection. */
    UPROPERTY(Replicated, BlueprintReadOnly, Category="Skald|Player")
    TArray<FFighterDefinition> PendingArmy;

    /** Whether the player has finalised their army selection. */
    UPROPERTY(Replicated, BlueprintReadOnly, Category="Skald|Player")
    bool bArmyLockedIn = false;

    /** Whether this player is controlled by AI. */
    UPROPERTY(BlueprintReadWrite, ReplicatedUsing = OnRep_IsAI, Category="PlayerState")
    bool bIsAI;

    /** Whether the player has locked in their actions for the current turn. */
    UPROPERTY(BlueprintReadWrite, ReplicatedUsing = OnRep_HasLockedIn, Category="PlayerState")
    bool bHasLockedIn;

    /** Whether this player has been eliminated from the match. */
    UPROPERTY(BlueprintReadWrite, ReplicatedUsing = OnRep_IsEliminated, Category="PlayerState")
    bool IsEliminated;

    UFUNCTION()
    void OnRep_DeployableUnits();

    UFUNCTION()
    void OnRep_HasLockedIn();

    UFUNCTION()
    void OnRep_IsEliminated();

    UFUNCTION()
    void OnRep_PlayerDisplayName();

    UFUNCTION()
    void OnRep_IsAI();

    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
};

