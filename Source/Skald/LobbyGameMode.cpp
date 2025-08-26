#include "LobbyGameMode.h"
#include "LobbyMenuWidget.h"
#include "Blueprint/UserWidget.h"
#include "GameFramework/PlayerController.h"

ALobbyGameMode::ALobbyGameMode()
    : LobbyWidgetClass(ULobbyMenuWidget::StaticClass())
{
}

void ALobbyGameMode::BeginPlay()
{
    Super::BeginPlay();

    if (LobbyWidgetClass)
    {
        UUserWidget* Widget = CreateWidget<UUserWidget>(GetWorld(), LobbyWidgetClass);
        if (Widget)
        {
            Widget->AddToViewport();

            if (APlayerController* PC = GetWorld()->GetFirstPlayerController())
            {
                PC->SetInputMode(FInputModeUIOnly());
                PC->bShowMouseCursor = true;
                PC->bEnableClickEvents = true;
                PC->bEnableMouseOverEvents = true;
            }
        }
    }
}

