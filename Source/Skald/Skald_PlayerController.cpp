#include "Skald_PlayerController.h"
#include "Skald_TurnManager.h"
#include "Skald_PlayerState.h"
#include "UI/SkaldMainHUDWidget.h"
#include "UObject/SoftObjectPath.h"

ASkaldPlayerController::ASkaldPlayerController()
{
    bIsAI = false;
    TurnManager = nullptr;
    HUDRef = nullptr;

    // Use a soft reference so the HUD blueprint is not loaded until BeginPlay,
    // avoiding async loading deadlocks when this controller is subclassed in
    // a blueprint.
    HUDWidgetClass = TSoftClassPtr<USkaldMainHUDWidget>(
        FSoftClassPath(TEXT("/Game/C++_BPs/Skald_MainHudBP.Skald_MainHudBP_C")));

    bShowMouseCursor = true;
    bEnableClickEvents = true;
    bEnableMouseOverEvents = true;
}

void ASkaldPlayerController::BeginPlay()
{
    Super::BeginPlay();

    // Create and show the HUD widget if a class has been assigned.
    if (UClass* LoadedHUDClass = HUDWidgetClass.LoadSynchronous())
    {
        HUDRef = CreateWidget<USkaldMainHUDWidget>(this, LoadedHUDClass);
        if (HUDRef)
        {
            HUDRef->AddToViewport();

            HUDRef->OnAttackRequested.AddDynamic(this, &ASkaldPlayerController::HandleAttackRequested);
            HUDRef->OnMoveRequested.AddDynamic(this, &ASkaldPlayerController::HandleMoveRequested);
            HUDRef->OnEndAttackRequested.AddDynamic(this, &ASkaldPlayerController::HandleEndAttackRequested);
            HUDRef->OnEndMovementRequested.AddDynamic(this, &ASkaldPlayerController::HandleEndMovementRequested);
        }
    }
    else
    {
        UE_LOG(LogTemp, Warning, TEXT("Skald_MainHudBP widget failed to load; HUD will not be displayed."));
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

void ASkaldPlayerController::HandleAttackRequested(int32 FromID, int32 ToID, int32 ArmySent)
{
    UE_LOG(LogTemp, Log, TEXT("HUD attack from %d to %d with %d"), FromID, ToID, ArmySent);
}

void ASkaldPlayerController::HandleMoveRequested(int32 FromID, int32 ToID, int32 Troops)
{
    UE_LOG(LogTemp, Log, TEXT("HUD move from %d to %d with %d"), FromID, ToID, Troops);
}

void ASkaldPlayerController::HandleEndAttackRequested(bool bConfirmed)
{
    UE_LOG(LogTemp, Log, TEXT("HUD end attack %s"), bConfirmed ? TEXT("confirmed") : TEXT("cancelled"));
}

void ASkaldPlayerController::HandleEndMovementRequested(bool bConfirmed)
{
    UE_LOG(LogTemp, Log, TEXT("HUD end move %s"), bConfirmed ? TEXT("confirmed") : TEXT("cancelled"));
}

