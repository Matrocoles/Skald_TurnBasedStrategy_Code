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
    // REMOVE UI creation here; UI is now created in ALobbyPlayerController::BeginPlay() on the local client.
}

