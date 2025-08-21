#include "Skald_TurnManager.h"
#include "Skald_PlayerController.h"
#include "Skald_PlayerState.h"

ATurnManager::ATurnManager()
{
    PrimaryActorTick.bCanEverTick = false;
    CurrentIndex = 0;
}

void ATurnManager::BeginPlay()
{
    Super::BeginPlay();
}

void ATurnManager::RegisterController(ASkaldPlayerController* Controller)
{
    if (Controller)
    {
        Controllers.Add(Controller);
        Controller->SetTurnManager(this);

        if (Controllers.Num() == 1)
        {
            StartTurns();
        }
    }
}

void ATurnManager::StartTurns()
{
    CurrentIndex = 0;
    if (Controllers.IsValidIndex(CurrentIndex))
    {
        Controllers[CurrentIndex]->StartTurn();
    }
}

void ATurnManager::AdvanceTurn()
{
    if (Controllers.Num() == 0)
    {
        return;
    }

    CurrentIndex = (CurrentIndex + 1) % Controllers.Num();
    Controllers[CurrentIndex]->StartTurn();
}

void ATurnManager::SortControllersByInitiative()
{
    Controllers.Sort([](const ASkaldPlayerController& A, const ASkaldPlayerController& B)
    {
        const ASkaldPlayerState* PSA = A.GetPlayerState<ASkaldPlayerState>();
        const ASkaldPlayerState* PSB = B.GetPlayerState<ASkaldPlayerState>();
        int32 RollA = PSA ? PSA->InitiativeRoll : 0;
        int32 RollB = PSB ? PSB->InitiativeRoll : 0;
        return RollA > RollB;
    });
}

