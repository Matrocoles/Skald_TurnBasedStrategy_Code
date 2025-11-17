#include "StartGameWidget.h"

#include "Components/Button.h"
#include "Components/ComboBoxString.h"
#include "Components/EditableTextBox.h"
#include "Components/SpinBox.h"
#include "Engine/Engine.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "LobbyMenuWidget.h"
#include "Skald_EnumUtils.h"
#include "Skald_GameInstance.h"
#include "Skald_PlayerController.h"
#include "Components/TextBlock.h"
#include "Styling/SlateColor.h"

namespace {

constexpr const TCHAR *FactionPlaceholderOption = TEXT("Choose your Faction.");

} // namespace

void UStartGameWidget::SetLobbyMenu(ULobbyMenuWidget *InMenu) {
  OwningLobbyMenu = InMenu;
}

void UStartGameWidget::NativeConstruct() {
  Super::NativeConstruct();

  if (SingleplayerButton) {
    SingleplayerButton->OnClicked.AddDynamic(
        this, &UStartGameWidget::OnSingleplayer);
    SingleplayerButton->SetIsEnabled(true);
    SingleplayerButton->SetVisibility(ESlateVisibility::Visible);
  }

  if (MultiplayerButton) {
    MultiplayerButton->OnClicked.AddDynamic(
        this, &UStartGameWidget::OnMultiplayer);
    MultiplayerButton->SetIsEnabled(true);
    MultiplayerButton->SetVisibility(ESlateVisibility::Visible);
  }

  if (HostButton) {
    HostButton->OnClicked.AddDynamic(this, &UStartGameWidget::OnHost);
    HostButton->SetIsEnabled(true);
    HostButton->SetVisibility(ESlateVisibility::Collapsed);
  }

  if (JoinButton) {
    JoinButton->OnClicked.AddDynamic(this, &UStartGameWidget::OnJoin);
    JoinButton->SetIsEnabled(true);
    JoinButton->SetVisibility(ESlateVisibility::Collapsed);
  }

  if (JoinAddressBox) {
    JoinAddressBox->SetVisibility(ESlateVisibility::Collapsed);
  }

  if (DisplayNameBox) {
    DisplayNameBox->SetVisibility(ESlateVisibility::Collapsed);
  }

  if (FactionComboBox) {
    FactionComboBox->ClearOptions();
    FactionComboBox->AddOption(FactionPlaceholderOption);
    FactionComboBox->OnGenerateWidgetEvent.BindUFunction(
        this, FName("GenerateFactionOptionWidget"));
    if (UEnum *Enum = StaticEnum<ESkaldFaction>()) {
      for (int32 i = 0; i < Enum->NumEnums(); ++i) {
        if (Skald::EnumUtils::IsHiddenEntry(Enum, i)) {
          continue;
        }

        const FString EnumName = Enum->GetNameStringByIndex(i);
        if (EnumName.EndsWith(TEXT("_MAX"))) {
          continue;
        }

        const int64 Value = Enum->GetValueByIndex(i);
        if (!Enum->IsValidEnumValue(Value)) {
          continue;
        }

        if (EnumName != TEXT("None")) {
          FactionComboBox->AddOption(EnumName);
        }
      }
    }
    FactionComboBox->RefreshOptions();
    FactionComboBox->SetSelectedOption(FactionPlaceholderOption);
    if (FactionComboBox->GetSelectedOption() != FactionPlaceholderOption) {
      FactionComboBox->SetSelectedIndex(0);
    }
    FactionComboBox->SetVisibility(ESlateVisibility::Collapsed);
  }

  if (AICountSpinBox) {
    AICountSpinBox->SetMinValue(1.f);
    AICountSpinBox->SetMaxValue(3.f);
    AICountSpinBox->SetDelta(1.f);
    AICountSpinBox->SetValue(1.f);
    AICountSpinBox->SetVisibility(ESlateVisibility::Collapsed);
  }

  if (LockInButton) {
    LockInButton->OnClicked.AddDynamic(this, &UStartGameWidget::OnLockIn);
    LockInButton->SetIsEnabled(true);
    LockInButton->SetVisibility(ESlateVisibility::Collapsed);
  }

  if (MainMenuButton) {
    MainMenuButton->OnClicked.AddDynamic(this, &UStartGameWidget::OnMainMenu);
  }
}

void UStartGameWidget::OnSingleplayer() {
  if (SingleplayerButton) {
    SingleplayerButton->SetVisibility(ESlateVisibility::Collapsed);
  }
  if (MultiplayerButton) {
    MultiplayerButton->SetVisibility(ESlateVisibility::Collapsed);
  }
  if (DisplayNameBox) {
    DisplayNameBox->SetVisibility(ESlateVisibility::Visible);
  }
  if (FactionComboBox) {
    FactionComboBox->SetVisibility(ESlateVisibility::Visible);
  }
  if (AICountSpinBox) {
    AICountSpinBox->SetVisibility(ESlateVisibility::Visible);
  }
  if (LockInButton) {
    LockInButton->SetVisibility(ESlateVisibility::Visible);
  }
}

void UStartGameWidget::OnMultiplayer() {
  if (SingleplayerButton) {
    SingleplayerButton->SetVisibility(ESlateVisibility::Collapsed);
  }
  if (MultiplayerButton) {
    MultiplayerButton->SetVisibility(ESlateVisibility::Collapsed);
  }
  if (HostButton) {
    HostButton->SetVisibility(ESlateVisibility::Visible);
  }
  if (JoinButton) {
    JoinButton->SetVisibility(ESlateVisibility::Visible);
  }
  if (JoinAddressBox) {
    JoinAddressBox->SetVisibility(ESlateVisibility::Visible);
  }
}

void UStartGameWidget::OnHost() { StartGame(true, true); }

void UStartGameWidget::OnJoin() { StartGame(true, false); }

void UStartGameWidget::OnLockIn() { StartGame(false, true); }

void UStartGameWidget::OnMainMenu() {
  RemoveFromParent();
  if (OwningLobbyMenu.IsValid()) {
    OwningLobbyMenu->SetVisibility(ESlateVisibility::Visible);
  }
}

UWidget *UStartGameWidget::GenerateFactionOptionWidget(const FString &Option) {
  UTextBlock *TextBlock = nullptr;

  if (FactionComboBox) {
    TextBlock = NewObject<UTextBlock>(FactionComboBox);
  }

  if (!TextBlock) {
    TextBlock = NewObject<UTextBlock>(this);
  }

  if (TextBlock) {
    TextBlock->SetText(FText::FromString(Option));
    const bool bIsPlaceholder = Option == FactionPlaceholderOption;
    TextBlock->SetIsEnabled(!bIsPlaceholder);
    if (bIsPlaceholder) {
      TextBlock->SetColorAndOpacity(FSlateColor(FLinearColor::Gray));
      if (FactionComboBox && FactionComboBox->IsOpen()) {
        TextBlock->SetVisibility(ESlateVisibility::Collapsed);
      } else {
        TextBlock->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
      }
    } else {
      TextBlock->SetVisibility(ESlateVisibility::Visible);
    }
  }

  return TextBlock;
}

void UStartGameWidget::StartGame(bool bMultiplayer, bool bHost) {
  if (UWorld *World = GetWorld()) {
    if (USkaldGameInstance *GI = World->GetGameInstance<USkaldGameInstance>()) {
      GI->ResetSessionState();

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
          if (!Option.IsEmpty() && Option != TEXT("None") &&
              Option != FactionPlaceholderOption) {
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

  const FName LobbyLevel(TEXT("/Game/Blueprints/Maps/Skald_Lobby"));
  const FName GameplayLevel(TEXT("/Game/Blueprints/Maps/OverviewMap"));
  const FName TargetLevel = bMultiplayer ? LobbyLevel : GameplayLevel;

  FString Options;
  if (bMultiplayer) {
    Options = TEXT("listen");
  }

  UGameplayStatics::OpenLevel(PC, TargetLevel, true, Options);
}
