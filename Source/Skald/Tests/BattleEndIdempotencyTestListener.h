#pragma once

#include "CoreMinimal.h"
#include "GridBattleManager.h"
#include "BattleEndIdempotencyTestListener.generated.h"

UCLASS()
class SKALD_API UBattleEndIdempotencyTestListener : public UObject
{
    GENERATED_BODY()

public:
    int32 NotificationCount = 0;

    UFUNCTION()
    void HandleBattleEnded(ESkaldFaction /*WinningFaction*/, int32 /*AttackerCasualties*/, int32 /*DefenderCasualties*/)
    {
        ++NotificationCount;
    }
};
