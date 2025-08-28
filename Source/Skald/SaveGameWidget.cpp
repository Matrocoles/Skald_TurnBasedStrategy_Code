#include "SaveGameWidget.h"
#include "Components/Button.h"
#include "Kismet/GameplayStatics.h"
#include "LobbyMenuWidget.h"
#include "Skald.h"
#include "SkaldSaveGame.h"
#include "Skald_GameMode.h"
#include "SlotNameConstants.h"

void USaveGameWidget::SetLobbyMenu(ULobbyMenuWidget* InMenu)
{
  LobbyMenu = InMenu;
}

void USaveGameWidget::NativeConstruct() {
  Super::NativeConstruct();

  if (Slot0Button) {
    Slot0Button->OnClicked.AddDynamic(this, &USaveGameWidget::OnSaveSlot0);
  }

  if (Slot1Button) {
    Slot1Button->OnClicked.AddDynamic(this, &USaveGameWidget::OnSaveSlot1);
  }

  if (Slot2Button) {
    Slot2Button->OnClicked.AddDynamic(this, &USaveGameWidget::OnSaveSlot2);
  }

  if (MainMenuButton) {
    MainMenuButton->OnClicked.AddDynamic(this, &USaveGameWidget::OnMainMenu);
  }
}

void USaveGameWidget::NativeDestruct() {
  if (Slot0Button) {
    Slot0Button->OnClicked.RemoveDynamic(this, &USaveGameWidget::OnSaveSlot0);
  }

  if (Slot1Button) {
    Slot1Button->OnClicked.RemoveDynamic(this, &USaveGameWidget::OnSaveSlot1);
  }

  if (Slot2Button) {
    Slot2Button->OnClicked.RemoveDynamic(this, &USaveGameWidget::OnSaveSlot2);
  }

  if (MainMenuButton) {
    MainMenuButton->OnClicked.RemoveDynamic(this, &USaveGameWidget::OnMainMenu);
  }

  Super::NativeDestruct();
}

void USaveGameWidget::OnSaveSlot0() { HandleSaveSlot(0); }

void USaveGameWidget::OnSaveSlot1() { HandleSaveSlot(1); }

void USaveGameWidget::OnSaveSlot2() { HandleSaveSlot(2); }

void USaveGameWidget::OnMainMenu() {
  RemoveFromParent();
  if (LobbyMenu.IsValid()) {
    LobbyMenu->SetVisibility(ESlateVisibility::Visible);
  }
}

void USaveGameWidget::HandleSaveSlot(int32 SlotIndex) {
  USkaldSaveGame *SaveGameObject = Cast<USkaldSaveGame>(
      UGameplayStatics::CreateSaveGameObject(USkaldSaveGame::StaticClass()));
  if (ASkaldGameMode *GM =
          Cast<ASkaldGameMode>(UGameplayStatics::GetGameMode(this))) {
    GM->FillSaveGame(SaveGameObject);
  }
  if (SaveGameObject && UGameplayStatics::SaveGameToSlot(
                            SaveGameObject, SlotNames[SlotIndex], 0)) {
    // After saving, transition back to main menu
    RemoveFromParent();
    if (LobbyMenu.IsValid()) {
      LobbyMenu->SetVisibility(ESlateVisibility::Visible);
    }
  } else {
    UE_LOG(LogSkald, Error, TEXT("Failed to save slot %s"),
           SlotNames[SlotIndex]);
  }
}
