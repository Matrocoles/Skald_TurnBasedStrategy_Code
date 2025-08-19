#include "Skald_PlayerController.h"
#include "Skald_TurnManager.h"
#include "Skald_PlayerState.h"

ASkaldPlayerController::ASkaldPlayerController()
{
    bIsAI = false;
    TurnManager = nullptr;
}

void ASkaldPlayerController::BeginPlay()
{
    Super::BeginPlay();

    if (ASkaldPlayerState* PS = GetPlayerState<ASkaldPlayerState>())
    {
        bIsAI = PS->bIsAI;
    }
}

void ASkaldPlayerController::SetTurnManager(ATurnManager* Manager)
{
    TurnManager = Manager;
}

void ASkaldPlayerController::StartTurn()
{
    if (bIsAI)
    {
        MakeAIDecision();
        EndTurn();
    }
    else
    {
        FInputModeGameAndUI InputMode;
        SetInputMode(InputMode);
        bShowMouseCursor = true;
    }
}

void ASkaldPlayerController::EndTurn()
{
    bShowMouseCursor = false;
    SetInputMode(FInputModeGameOnly());

    if (TurnManager)
    {
        TurnManager->AdvanceTurn();
    }
}

void ASkaldPlayerController::MakeAIDecision()
{
    UE_LOG(LogTemp, Log, TEXT("AI %s making decision"), *GetName());
}

