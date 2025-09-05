#include "StartGameWidget.h"

#include "Components/Button.h"
#include "Components/ComboBoxString.h"
#include "Components/EditableTextBox.h"
#include "Components/SpinBox.h"
#include "Engine/Engine.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
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

  if (HostButton) {
    HostButton->OnClicked.AddDynamic(this, &UStartGameWidget::OnHost);
    HostButton->SetIsEnabled(true);
    HostButton->SetVisibility(ESlateVisibility::Visible);
  }

  if (JoinButton) {
    JoinButton->OnClicked.AddDynamic(this, &UStartGameWidget::OnJoin);
    JoinButton->SetIsEnabled(true);
    JoinButton->SetVisibility(ESlateVisibility::Visible);
  }

  if (MainMenuButton) {
    MainMenuButton->OnClicked.AddDynamic(this, &UStartGameWidget::OnMainMenu);
  }
}

void UStartGameWidget::OnSingleplayer() { StartGame(false, true); }

void UStartGameWidget::OnHost() { StartGame(true, true); }

void UStartGameWidget::OnJoin() { StartGame(true, false); }

void UStartGameWidget::OnMainMenu() {
  RemoveFromParent();
  if (OwningLobbyMenu.IsValid()) {
    OwningLobbyMenu->SetVisibility(ESlateVisibility::Visible);
  }
}

void UStartGameWidget::StartGame(bool bMultiplayer, bool bHost) {
  if (UWorld *World = GetWorld()) {
    if (USkaldGameInstance *GI = World->GetGameInstance<USkaldGameInstance>()) {
      GI->bIsMultiplayer = bMultiplayer;
      GI->bIsHost = bHost;
      if (!bMultiplayer) {
        if (DisplayNameBox) {
          const FString Name = DisplayNameBox->GetText().ToString();
          if (!Name.IsEmpty()) {
            GI->DisplayName = Name;
          }
        }

        if (FactionComboBox) {
          const FString Option = FactionComboBox->GetSelectedOption();
          if (!Option.IsEmpty() && Option != TEXT("None")) {
            if (UEnum *Enum = StaticEnum<ESkaldFaction>()) {
              const int64 Value = Enum->GetValueByNameString(Option);
              if (Value != INDEX_NONE) {
                GI->Faction = static_cast<ESkaldFaction>(Value);
              }
            }
          }
        }

        if (AICountSpinBox) {
          GI->AIPlayersToSpawn =
              FMath::Clamp(FMath::RoundToInt(AICountSpinBox->GetValue()), 1, 3);
        }

        GI->TakenFactions.Empty();
        if (GI->Faction != ESkaldFaction::None) {
          GI->TakenFactions.AddUnique(GI->Faction);
        }
      } else if (!bHost) {
        if (JoinAddressBox) {
          GI->JoinAddress = JoinAddressBox->GetText().ToString();
        }
      }
    }

    if (APlayerController *PC = GetOwningPlayer()) {
      if (!bMultiplayer || bHost) {
        TravelToGameplayMap(PC, bMultiplayer);
      } else {
        FString Address;
        if (USkaldGameInstance *GI = World->GetGameInstance<USkaldGameInstance>()) {
          Address = GI->JoinAddress;
        }
        if (!Address.IsEmpty()) {
          if (GEngine) {
            GEngine->AddOnScreenDebugMessage(
                -1, 4.f, FColor::Green,
                FString::Printf(TEXT("Joining %s"), *Address));
          }
          PC->ClientTravel(Address, ETravelType::TRAVEL_Absolute);
        }
      }
    }
  }
}

void UStartGameWidget::TravelToGameplayMap(APlayerController *PC,
                                           bool bMultiplayer) {
  if (!PC) {
    return;
  }

  const FName LevelName(TEXT("/Game/Blueprints/Maps/OverviewMap"));
  FString Options;
  if (bMultiplayer) {
    Options = TEXT("listen");
  }
  UGameplayStatics::OpenLevel(PC, LevelName, true, Options);
}
