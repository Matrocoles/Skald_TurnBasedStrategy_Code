#include "StartGameWidget.h"

#include "Components/Button.h"
#include "Components/ComboBoxString.h"
#include "Components/EditableTextBox.h"
#include "Engine/Engine.h"
#include "GameFramework/PlayerController.h"
#include "Skald_PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "LobbyMenuWidget.h"
#include "Skald_GameInstance.h"
#include "Skald_PlayerState.h"

void UStartGameWidget::SetLobbyMenu(ULobbyMenuWidget *InMenu) {
  OwningLobbyMenu = InMenu;
}

void UStartGameWidget::NativeConstruct() {
  Super::NativeConstruct();

  if (DisplayNameBox) {
    DisplayNameBox->SetText(FText::GetEmpty());
    DisplayNameBox->OnTextChanged.AddDynamic(
        this, &UStartGameWidget::OnDisplayNameChanged);
  }

  if (FactionComboBox) {
    RefreshFactionOptions();
    FactionComboBox->OnSelectionChanged.AddDynamic(
        this, &UStartGameWidget::OnFactionChanged);
  }

  if (USkaldGameInstance *GI =
          GetWorld()->GetGameInstance<USkaldGameInstance>()) {
    GI->OnFactionsUpdated.AddDynamic(this,
                                     &UStartGameWidget::HandleFactionsUpdated);
  }

  if (LockInButton) {
    LockInButton->OnClicked.AddDynamic(this, &UStartGameWidget::OnLockIn);
    LockInButton->SetIsEnabled(true);
  }

  if (SingleplayerButton) {
    SingleplayerButton->OnClicked.AddDynamic(this,
                                             &UStartGameWidget::OnSingleplayer);
    SingleplayerButton->SetIsEnabled(false);
    SingleplayerButton->SetVisibility(ESlateVisibility::Collapsed);
  }

  if (MultiplayerButton) {
    MultiplayerButton->OnClicked.AddDynamic(this,
                                            &UStartGameWidget::OnMultiplayer);
    MultiplayerButton->SetIsEnabled(false);
    MultiplayerButton->SetVisibility(ESlateVisibility::Collapsed);
  }

  if (MainMenuButton) {
    MainMenuButton->OnClicked.AddDynamic(this, &UStartGameWidget::OnMainMenu);
  }

  ValidateSelections();
}

void UStartGameWidget::OnSingleplayer() { StartGame(false); }

void UStartGameWidget::OnMultiplayer() { StartGame(true); }

void UStartGameWidget::OnMainMenu() {
  RemoveFromParent();
  if (OwningLobbyMenu.IsValid()) {
    OwningLobbyMenu->SetVisibility(ESlateVisibility::Visible);
  }
}

void UStartGameWidget::OnDisplayNameChanged(const FText & /*Text*/) {
  ValidateSelections();
}

void UStartGameWidget::OnFactionChanged(FString /*SelectedItem*/,
                                        ESelectInfo::Type /*SelectionType*/) {
  ValidateSelections();
}

void UStartGameWidget::ValidateSelections() {
  bool bFactionAvailable = true;

  if (FactionComboBox && FactionComboBox->GetSelectedIndex() != INDEX_NONE) {
    const FString Selected = FactionComboBox->GetSelectedOption();
    if (UEnum *Enum = StaticEnum<ESkaldFaction>()) {
      const int32 Value = Enum->GetValueByNameString(Selected);
      if (Value != INDEX_NONE) {
        if (USkaldGameInstance *GI =
                GetWorld()->GetGameInstance<USkaldGameInstance>()) {
          const ESkaldFaction Faction = static_cast<ESkaldFaction>(Value);
          if (GI->TakenFactions.Contains(Faction)) {
            bFactionAvailable = false;
            if (GEngine) {
              GEngine->AddOnScreenDebugMessage(
                  -1, 4.f, FColor::Yellow,
                  TEXT("Selected faction already taken"));
            }
          }
        }
      }
    }
  } else {
    bFactionAvailable = false;
  }

  if (LockInButton) {
    LockInButton->SetIsEnabled(bFactionAvailable);
  }
}

void UStartGameWidget::RefreshFactionOptions() {
  if (!FactionComboBox) {
    return;
  }

  const FString PreviouslySelected = FactionComboBox->GetSelectedOption();
  FactionComboBox->ClearOptions();

  if (UEnum *Enum = StaticEnum<ESkaldFaction>()) {
    for (int32 i = 0; i < Enum->NumEnums(); ++i) {
      if (!Enum->HasMetaData(TEXT("Hidden"), i)) {
        const FString Option = Enum->GetNameStringByIndex(i);
        if (Option != TEXT("None")) {
          FactionComboBox->AddOption(Option);
        }
      }
    }

    if (USkaldGameInstance *GI =
            GetWorld()->GetGameInstance<USkaldGameInstance>()) {
      for (ESkaldFaction Taken : GI->TakenFactions) {
        const FString Option =
            Enum->GetNameStringByValue(static_cast<int64>(Taken));
        FactionComboBox->RemoveOption(Option);
      }
    }
  }

  const int32 Index = FactionComboBox->FindOptionIndex(PreviouslySelected);
  if (Index != INDEX_NONE) {
    FactionComboBox->SetSelectedIndex(Index);
  } else {
    if (!PreviouslySelected.IsEmpty() && GEngine) {
      GEngine->AddOnScreenDebugMessage(
          -1, 4.f, FColor::Red,
          TEXT("Selected faction is no longer available"));
    }
    FactionComboBox->SetSelectedIndex(INDEX_NONE);
  }

  ValidateSelections();
}

void UStartGameWidget::HandleFactionsUpdated() { RefreshFactionOptions(); }

void UStartGameWidget::OnLockIn() {
  FString Name =
      DisplayNameBox ? DisplayNameBox->GetText().ToString() : FString();

  if (Name.IsEmpty()) {
    if (APlayerController *PC = GetOwningPlayer()) {
      if (APlayerState *PSBase = PC->PlayerState) {
        Name = FString::Printf(TEXT("Player%d"), PSBase->GetPlayerId());
      } else {
        Name = TEXT("Player");
      }
    }
  }

  ESkaldFaction Faction = ESkaldFaction::Human;
  if (FactionComboBox && FactionComboBox->GetSelectedIndex() != INDEX_NONE) {
    FString FactionName = FactionComboBox->GetSelectedOption();
    if (UEnum *Enum = StaticEnum<ESkaldFaction>()) {
      int32 Value = Enum->GetValueByNameString(FactionName);
      if (Value != INDEX_NONE) {
        Faction = static_cast<ESkaldFaction>(Value);
      }
    }
  }

  if (UWorld *World = GetWorld()) {
    if (USkaldGameInstance *GI = World->GetGameInstance<USkaldGameInstance>()) {
      if (GI->TakenFactions.Contains(Faction)) {
        if (LockInButton) {
          LockInButton->SetIsEnabled(false);
        }
        if (GEngine) {
          GEngine->AddOnScreenDebugMessage(
              -1, 4.f, FColor::Red,
              TEXT("Selected faction is no longer available"));
        }
        RefreshFactionOptions();
        return;
      }

      GI->DisplayName = Name;
      GI->Faction = Faction;
      GI->TakenFactions.AddUnique(Faction);
      GI->OnFactionsUpdated.Broadcast();
    }
  }

  if (ASkaldPlayerController *PC =
          Cast<ASkaldPlayerController>(GetOwningPlayer())) {
    if (ASkaldPlayerState *PS = PC->GetPlayerState<ASkaldPlayerState>()) {
      PS->DisplayName = Name;
      PS->Faction = Faction;
    }
    PC->ServerInitPlayerState(Name, Faction);
  }

  if (DisplayNameBox) {
    DisplayNameBox->SetVisibility(ESlateVisibility::Collapsed);
  }

  if (FactionComboBox) {
    FactionComboBox->SetVisibility(ESlateVisibility::Collapsed);
  }

  if (LockInButton) {
    LockInButton->SetVisibility(ESlateVisibility::Collapsed);
  }

  if (SingleplayerButton) {
    SingleplayerButton->SetIsEnabled(true);
    SingleplayerButton->SetVisibility(ESlateVisibility::Visible);
  }

  if (MultiplayerButton) {
    MultiplayerButton->SetIsEnabled(true);
    MultiplayerButton->SetVisibility(ESlateVisibility::Visible);
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
    if (WorldToTravel->IsServer()) {
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
