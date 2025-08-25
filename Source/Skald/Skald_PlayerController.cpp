#include "Skald_PlayerController.h"
#include "Skald_TurnManager.h"
#include "Skald_PlayerState.h"
#include "UI/SkaldMainHUDWidget.h"
#include "UObject/SoftObjectPtr.h"
#include "UObject/SoftObjectPath.h"
#include "Blueprint/UserWidget.h"
#include "UI/SkaldMainHUDWidget.h"

ASkaldPlayerController::ASkaldPlayerController()
{
    bIsAI = false;
    TurnManager = nullptr;
    HUDRef = nullptr;
    MainHudWidget = nullptr;

    bShowMouseCursor = true;
    bEnableClickEvents = true;
    bEnableMouseOverEvents = true;
}

void ASkaldPlayerController::BeginPlay()
{
    Super::BeginPlay();

    // Load a default HUD class if one hasn't been specified in the blueprint.
    if (!MainHudWidgetClass)
    {
        TSoftClassPtr<USkaldMainHUDWidget> HudClassRef(FSoftObjectPath(TEXT("/Game/C++_BPs/Skald_MainHudBP.Skald_MainHudBP_C")));
        MainHudWidgetClass = HudClassRef.LoadSynchronous();
    }

    // Create and show the HUD widget if a class has been assigned.
    if (MainHudWidgetClass)
    {
        MainHudWidget = CreateWidget<USkaldMainHUDWidget>(this, MainHudWidgetClass);
        if (MainHudWidget)
        {
    if (HUDWidgetClass)
    if (MainHudWidgetClass)
    {
        HUDRef = CreateWidget<UUserWidget>(this, HUDWidgetClass);
        if (HUDRef)
        MainHudWidget = CreateWidget<USkaldMainHUDWidget>(this, MainHudWidgetClass);
        if (MainHudWidget)
        {
            HUDRef->AddToViewport();
            MainHudWidget->AddToViewport();

            MainHudWidget->OnAttackRequested.AddDynamic(this, &ASkaldPlayerController::HandleAttackRequested);
            MainHudWidget->OnMoveRequested.AddDynamic(this, &ASkaldPlayerController::HandleMoveRequested);
            MainHudWidget->OnEndAttackRequested.AddDynamic(this, &ASkaldPlayerController::HandleEndAttackRequested);
            MainHudWidget->OnEndMovementRequested.AddDynamic(this, &ASkaldPlayerController::HandleEndMovementRequested);
        }
    }
    else
    {
        UE_LOG(LogTemp, Warning, TEXT("MainHudWidgetClass is null; HUD will not be displayed."));
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
