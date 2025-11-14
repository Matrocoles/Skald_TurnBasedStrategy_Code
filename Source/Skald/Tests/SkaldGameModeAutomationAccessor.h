#pragma once

#if defined(WITH_AUTOMATION_TESTS)
#if WITH_AUTOMATION_TESTS

#include "Skald_GameMode.h"
#include "Skald_PlayerController.h"

struct FSkaldGameModeAutomationAccessor
{
    static void BeginArmyPlacementPhase(ASkaldGameMode* GameMode)
    {
        if (GameMode)
        {
            GameMode->BeginArmyPlacementPhase();
        }
    }

    static void HandleArmyPlacementFailsafe(ASkaldGameMode* GameMode)
    {
        if (GameMode)
        {
            GameMode->HandleArmyPlacementFailsafe();
        }
    }

    static bool IsRetryInitTimerActive(ASkaldGameMode* GameMode)
    {
        if (!GameMode)
        {
            return false;
        }

        if (UWorld* World = GameMode->GetWorld())
        {
            return World->GetTimerManager().IsTimerActive(GameMode->RetryInitTimerHandle);
        }

        return false;
    }

    static float GetRetryInitTimerRate(ASkaldGameMode* GameMode)
    {
        if (!GameMode)
        {
            return -1.f;
        }

        if (UWorld* World = GameMode->GetWorld())
        {
            return World->GetTimerManager().GetTimerRate(GameMode->RetryInitTimerHandle);
        }

        return -1.f;
    }

    static void RegisterPlayer(ASkaldGameMode* GameMode, ASkaldPlayerController* PlayerController)
    {
        if (GameMode)
        {
            GameMode->RegisterPlayer(PlayerController);
        }
    }
};

#endif  // WITH_AUTOMATION_TESTS
#endif  // defined(WITH_AUTOMATION_TESTS)

