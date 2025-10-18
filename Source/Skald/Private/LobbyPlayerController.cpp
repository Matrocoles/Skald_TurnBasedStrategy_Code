#include "LobbyPlayerController.h"
#include "Blueprint/UserWidget.h"
#include "LobbyMenuWidget.h"
#include "UI/TitleScreenWidget.h"
#include "UI/SkaldUIHelpers.h"

ALobbyPlayerController::ALobbyPlayerController()
{
    LobbyWidgetClass = ULobbyMenuWidget::StaticClass();
    TitleScreenWidgetClass = UTitleScreenWidget::StaticClass();
}

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
    LobbyWidgetInstance->SetVisibility(ESlateVisibility::Hidden);

    bool bShowingTitleScreen = false;
    if (!TitleScreenWidgetInstance && TitleScreenWidgetClass)
    {
        TitleScreenWidgetInstance = CreateWidget<UTitleScreenWidget>(this, TitleScreenWidgetClass);
        if (TitleScreenWidgetInstance)
        {
            TitleScreenWidgetInstance->OnDismissed.AddDynamic(this, &ALobbyPlayerController::HandleTitleScreenDismissed);
            TitleScreenWidgetInstance->AddToViewport(1);
            FocusWidgetUIOnly(this, TitleScreenWidgetInstance);
            bShowingTitleScreen = true;
        }
    }

    if (!bShowingTitleScreen)
    {
        LobbyWidgetInstance->SetVisibility(ESlateVisibility::Visible);
        FocusWidgetUIOnly(this, LobbyWidgetInstance);
    }

    bShowMouseCursor = true;
    bEnableClickEvents = true;
    bEnableMouseOverEvents = true;

    // Wiring fighter selection happens inside the lobby widget init (see Change 4).
}

void ALobbyPlayerController::HandleTitleScreenDismissed()
{
    TitleScreenWidgetInstance = nullptr;

    if (LobbyWidgetInstance)
    {
        LobbyWidgetInstance->SetVisibility(ESlateVisibility::Visible);
        FocusWidgetUIOnly(this, LobbyWidgetInstance);
    }
}
