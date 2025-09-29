#pragma once

#if WITH_AUTOMATION_TESTS
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
    bool bBroadcasted = false;

    UFUNCTION()
    void HandleBroadcast()
    {
        bBroadcasted = true;
    }
};

#endif // WITH_AUTOMATION_TESTS

