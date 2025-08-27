#pragma once

#include "CoreMinimal.h"
#include "BattleResolutionSyncTest.generated.h"

/**
 * Helper object to listen for world state change broadcasts.
 */
UCLASS()
class UWorldStateChangedListener : public UObject
{
    GENERATED_BODY()

public:
    bool bBroadcasted = false;

    UFUNCTION()
    void HandleBroadcast()
    {
        bBroadcasted = true;
    }
};

