#include "SaveGameWidget.h"
#include "Components/Button.h"
#include "Kismet/GameplayStatics.h"
#include "Blueprint/UserWidget.h"
#include "Skald.h"
#include "SkaldLogging.h"
#include "SkaldSaveGame.h"
#include "Skald_GameInstance.h"
#include "Skald_GameMode.h"
#include "Skald_PlayerController.h"
#include "SlotNameConstants.h"
#include "Templates/UnrealTemplate.h"
#include "UI/InGameMenuWidget.h"
#include "UI/SkaldMainHUDWidget.h"
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

    if (Slot0Button)
    {
        Slot0Button->OnClicked.AddDynamic(this, &USaveGameWidget::OnSaveSlot0);
    }

    if (Slot1Button)
    {
        Slot1Button->OnClicked.AddDynamic(this, &USaveGameWidget::OnSaveSlot1);
    }

    if (Slot2Button)
    {
        Slot2Button->OnClicked.AddDynamic(this, &USaveGameWidget::OnSaveSlot2);
    }

    if (MainMenuButton)
    {
        MainMenuButton->OnClicked.AddDynamic(this, &USaveGameWidget::OnMainMenu);
    }
}

void USaveGameWidget::NativeDestruct()
{
    if (Slot0Button)
    {
        Slot0Button->OnClicked.RemoveDynamic(this, &USaveGameWidget::OnSaveSlot0);
    }

    if (Slot1Button)
    {
        Slot1Button->OnClicked.RemoveDynamic(this, &USaveGameWidget::OnSaveSlot1);
    }

    if (Slot2Button)
    {
        Slot2Button->OnClicked.RemoveDynamic(this, &USaveGameWidget::OnSaveSlot2);
    }

    if (MainMenuButton)
    {
        MainMenuButton->OnClicked.RemoveDynamic(this, &USaveGameWidget::OnMainMenu);
    }

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

    ShowSavingIndicator();

    USkaldSaveGame* SaveGameObject = Cast<USkaldSaveGame>(UGameplayStatics::CreateSaveGameObject(USkaldSaveGame::StaticClass()));
    if (!SaveGameObject)
    {
        UE_LOG(LogSkald, Error, TEXT("Failed to create save game object for slot %d"), SlotIndex);
        HideSavingIndicator();
        return;
    }

    if (ASkaldGameMode* GM = Cast<ASkaldGameMode>(UGameplayStatics::GetGameMode(this)))
    {
        GM->FillSaveGame(SaveGameObject);
    }

    if (UGameplayStatics::SaveGameToSlot(SaveGameObject, SlotNames[SlotIndex], 0))
    {
        HideSavingIndicator();
        ReturnToOwningMenu();
    }
    else
    {
        UE_LOG(LogSkald, Error, TEXT("Failed to save slot %s"), SlotNames[SlotIndex]);
        HideSavingIndicator();
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

void USaveGameWidget::ShowSavingIndicator()
{
    const FText SavingText = NSLOCTEXT("SkaldHUD", "SavingIndicator", "Saving...");

    if (USkaldGameInstance *GI = GetGameInstance<USkaldGameInstance>())
    {
        GI->ShowGlobalStatusMessage(SavingText, 0.f, true);
        return;
    }

    if (ASkaldPlayerController *PC = GetOwningPlayer<ASkaldPlayerController>())
    {
        if (USkaldMainHUDWidget *HUD = PC->GetHUDWidget())
        {
            HUD->ShowStatusMessage(SavingText, 0.f);
        }
    }
}

void USaveGameWidget::HideSavingIndicator()
{
    if (USkaldGameInstance *GI = GetGameInstance<USkaldGameInstance>())
    {
        GI->HideGlobalStatusMessage();
        return;
    }

    if (ASkaldPlayerController *PC = GetOwningPlayer<ASkaldPlayerController>())
    {
        if (USkaldMainHUDWidget *HUD = PC->GetHUDWidget())
        {
            HUD->HideStatusMessage();
        }
    }
}
