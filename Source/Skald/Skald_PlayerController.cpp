#include "Skald_PlayerController.h"
#include "Skald_TurnManager.h"
#include "Skald_PlayerState.h"
#include "Blueprint/UserWidget.h"

ASkaldPlayerController::ASkaldPlayerController()
{
    bIsAI = false;
    TurnManager = nullptr;
    HUDRef = nullptr;

    bShowMouseCursor = true;
    bEnableClickEvents = true;
    bEnableMouseOverEvents = true;
}

void ASkaldPlayerController::BeginPlay()
{
    Super::BeginPlay();

    // Create and show the HUD widget if a class has been assigned.
    if (HUDWidgetClass)
    {
        HUDRef = CreateWidget<UUserWidget>(this, HUDWidgetClass);
        if (HUDRef)
        {
            HUDRef->AddToViewport();
        }
    }

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
    }
}

void ASkaldPlayerController::EndTurn()
{
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

bool ASkaldPlayerController::IsAIController() const
{
    return bIsAI;
}

