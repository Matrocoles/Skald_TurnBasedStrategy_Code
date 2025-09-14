#include "LobbyPlayerController.h"
#include "Blueprint/UserWidget.h"
#include "LobbyMenuWidget.h"

void ALobbyPlayerController::BeginPlay()
{
    Super::BeginPlay();
    if (IsLocalController())
    {
        InitLobbyUI();
    }
}

void ALobbyPlayerController::InitLobbyUI()
{
    if (LobbyWidgetInstance || !LobbyWidgetClass) { return; }

    LobbyWidgetInstance = CreateWidget<ULobbyMenuWidget>(this, LobbyWidgetClass);
    if (!LobbyWidgetInstance) { return; }

    LobbyWidgetInstance->AddToViewport();

    FInputModeUIOnly Mode;
    Mode.SetWidgetToFocus(LobbyWidgetInstance->TakeWidget());
    Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
    SetInputMode(Mode);

    bShowMouseCursor = true;
    bEnableClickEvents = true;
    bEnableMouseOverEvents = true;

    // Wiring fighter selection happens inside the lobby widget init (see Change 4).
}
