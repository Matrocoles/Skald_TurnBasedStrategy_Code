#include "StartGameWidget.h"

#include "Components/Button.h"
#include "GameFramework/PlayerController.h"
#include "LobbyMenuWidget.h"
#include "Skald_GameInstance.h"
#include "Skald_PlayerController.h"

void UStartGameWidget::SetLobbyMenu(ULobbyMenuWidget *InMenu) {
  OwningLobbyMenu = InMenu;
}

void UStartGameWidget::NativeConstruct() {
  Super::NativeConstruct();

  if (SingleplayerButton) {
    SingleplayerButton->OnClicked.AddDynamic(this,
                                             &UStartGameWidget::OnSingleplayer);
    SingleplayerButton->SetIsEnabled(true);
    SingleplayerButton->SetVisibility(ESlateVisibility::Visible);
  }

  if (MultiplayerButton) {
    MultiplayerButton->OnClicked.AddDynamic(this,
                                            &UStartGameWidget::OnMultiplayer);
    MultiplayerButton->SetIsEnabled(true);
    MultiplayerButton->SetVisibility(ESlateVisibility::Visible);
  }

  if (MainMenuButton) {
    MainMenuButton->OnClicked.AddDynamic(this, &UStartGameWidget::OnMainMenu);
  }
}

void UStartGameWidget::OnSingleplayer() { StartGame(false); }

void UStartGameWidget::OnMultiplayer() { StartGame(true); }

void UStartGameWidget::OnMainMenu() {
  RemoveFromParent();
  if (OwningLobbyMenu.IsValid()) {
    OwningLobbyMenu->SetVisibility(ESlateVisibility::Visible);
  }
}

void UStartGameWidget::StartGame(bool bMultiplayer) {
  if (UWorld *World = GetWorld()) {
    if (USkaldGameInstance *GI = World->GetGameInstance<USkaldGameInstance>()) {
      GI->bIsMultiplayer = bMultiplayer;
    }

    if (APlayerController *PC = GetOwningPlayer()) {
      TravelToGameplayMap(PC, bMultiplayer);
    }
  }
}

void UStartGameWidget::TravelToGameplayMap(APlayerController *PC,
                                           bool bMultiplayer) {
  if (!PC) {
    return;
  }

  const FName LevelName(TEXT("/Game/Blueprints/Maps/OverviewMap"));
  FString URL = LevelName.ToString();

  if (UWorld *WorldToTravel = PC->GetWorld()) {
    if (PC->HasAuthority()) {
      if (bMultiplayer) {
        URL += TEXT("?listen");
      }
      WorldToTravel->ServerTravel(URL);
    } else if (PC->IsLocalController()) {
      PC->ClientTravel(URL, ETravelType::TRAVEL_Absolute);
    } else {
      UE_LOG(LogTemp, Error,
             TEXT("TravelToGameplayMap: invalid context for travel"));
    }
  }
}
