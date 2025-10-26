#include "SaveGameWidget.h"
#include "Components/Button.h"
#include "Kismet/GameplayStatics.h"
#include "Blueprint/UserWidget.h"
#include "Skald.h"
#include "SkaldLogging.h"
#include "SkaldSaveGame.h"
#include "Skald_GameMode.h"
#include "SlotNameConstants.h"
#include "Templates/UnrealTemplate.h"
#include "UI/InGameMenuWidget.h"
#include "LobbyMenuWidget.h"

void USaveGameWidget::SetLobbyMenu(ULobbyMenuWidget* InMenu)
{
  SetOwningMenu(InMenu);
}

void USaveGameWidget::SetOwningMenu(UUserWidget* InMenu)
{
  OwningMenu = InMenu;
}

void USaveGameWidget::NativeConstruct()
{
    Super::NativeConstruct();

    BindButton(Slot0Button, &USaveGameWidget::OnSaveSlot0);
    BindButton(Slot1Button, &USaveGameWidget::OnSaveSlot1);
    BindButton(Slot2Button, &USaveGameWidget::OnSaveSlot2);
    BindButton(MainMenuButton, &USaveGameWidget::OnMainMenu);
}

void USaveGameWidget::NativeDestruct()
{
    UnbindButton(Slot0Button, &USaveGameWidget::OnSaveSlot0);
    UnbindButton(Slot1Button, &USaveGameWidget::OnSaveSlot1);
    UnbindButton(Slot2Button, &USaveGameWidget::OnSaveSlot2);
    UnbindButton(MainMenuButton, &USaveGameWidget::OnMainMenu);

    Super::NativeDestruct();
}

void USaveGameWidget::OnSaveSlot0() { HandleSaveSlot(0); }

void USaveGameWidget::OnSaveSlot1() { HandleSaveSlot(1); }

void USaveGameWidget::OnSaveSlot2() { HandleSaveSlot(2); }

void USaveGameWidget::OnMainMenu()
{
    ReturnToOwningMenu();
}

void USaveGameWidget::HandleSaveSlot(int32 SlotIndex)
{
    if (!ensure(SlotIndex >= 0 && SlotIndex < UE_ARRAY_COUNT(SlotNames)))
    {
        return;
    }

    USkaldSaveGame* SaveGameObject = Cast<USkaldSaveGame>(UGameplayStatics::CreateSaveGameObject(USkaldSaveGame::StaticClass()));
    if (!SaveGameObject)
    {
        UE_LOG(LogSkald, Error, TEXT("Failed to create save game object for slot %d"), SlotIndex);
        return;
    }

    if (ASkaldGameMode* GM = Cast<ASkaldGameMode>(UGameplayStatics::GetGameMode(this)))
    {
        GM->FillSaveGame(SaveGameObject);
    }

    if (UGameplayStatics::SaveGameToSlot(SaveGameObject, SlotNames[SlotIndex], 0))
    {
        ReturnToOwningMenu();
    }
    else
    {
        UE_LOG(LogSkald, Error, TEXT("Failed to save slot %s"), SlotNames[SlotIndex]);
    }
}

void USaveGameWidget::BindButton(UButton* Button, void (USaveGameWidget::*Handler)())
{
    if (Button)
    {
        Button->OnClicked.AddDynamic(this, Handler);
    }
}

void USaveGameWidget::UnbindButton(UButton* Button, void (USaveGameWidget::*Handler)())
{
    if (Button)
    {
        Button->OnClicked.RemoveDynamic(this, Handler);
    }
}

void USaveGameWidget::ReturnToOwningMenu()
{
    RemoveFromParent();

    if (OwningMenu.IsValid())
    {
        if (UInGameMenuWidget* Menu = Cast<UInGameMenuWidget>(OwningMenu.Get()))
        {
            Menu->HandleSubMenuClosed(this);
        }

        OwningMenu->SetVisibility(ESlateVisibility::Visible);
    }
}
