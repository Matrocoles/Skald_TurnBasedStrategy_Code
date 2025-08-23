#include "LobbyGameMode.h"
#include "LobbyMenuWidget.h"
#include "Blueprint/UserWidget.h"

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
        }
    }
}

