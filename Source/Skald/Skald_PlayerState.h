#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerState.h"
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

    UPROPERTY(BlueprintReadWrite, Category="PlayerState")
    bool bIsAI;

    /** Army units available for placement. */
    UPROPERTY(BlueprintReadWrite, Category="PlayerState")
    int32 ArmyPool;

    /** Initiative roll determining turn order. */
    UPROPERTY(BlueprintReadWrite, Category="PlayerState")
    int32 InitiativeRoll;
};

