#pragma once

#include "CoreMinimal.h"
#include "BattleResolutionSyncTest.generated.h"

/**
 * Helper object to listen for world state change broadcasts.
 */
UCLASS()
class SKALD_API UWorldStateChangedListener : public UObject
{
    GENERATED_BODY()

public:
    /** Whether the delegate broadcast has been observed. */
    bool bBroadcasted = false;

    UFUNCTION()
    void HandleBroadcast()
    {
        bBroadcasted = true;
    }
};

