#include "LoadGameWidget.h"
#include "Components/Button.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/SaveGame.h"
#include "LobbyMenuWidget.h"
#include "SlotNameConstants.h"
#include "SkaldSaveGame.h"
#include "SkaldSaveGameLibrary.h"
#include "Skald_GameInstance.h"

void ULoadGameWidget::NativeConstruct()
{
    Super::NativeConstruct();

    if (Slot0Button)
    {
        Slot0Button->OnClicked.AddDynamic(this, &ULoadGameWidget::OnLoadSlot0);
        Slot0Button->SetVisibility(UGameplayStatics::DoesSaveGameExist(SlotNames[0], 0) ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
    }

    if (Slot1Button)
    {
        Slot1Button->OnClicked.AddDynamic(this, &ULoadGameWidget::OnLoadSlot1);
        Slot1Button->SetVisibility(UGameplayStatics::DoesSaveGameExist(SlotNames[1], 0) ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
    }

    if (Slot2Button)
    {
        Slot2Button->OnClicked.AddDynamic(this, &ULoadGameWidget::OnLoadSlot2);
        Slot2Button->SetVisibility(UGameplayStatics::DoesSaveGameExist(SlotNames[2], 0) ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
    }

    if (MainMenuButton)
    {
        MainMenuButton->OnClicked.AddDynamic(this, &ULoadGameWidget::OnMainMenu);
    }
}

void ULoadGameWidget::OnLoadSlot0()
{
    HandleLoadSlot(0);
}

void ULoadGameWidget::OnLoadSlot1()
{
    HandleLoadSlot(1);
}

void ULoadGameWidget::OnLoadSlot2()
{
    HandleLoadSlot(2);
}

void ULoadGameWidget::OnMainMenu()
{
    RemoveFromParent();
    if (LobbyMenu.IsValid())
    {
        LobbyMenu->SetVisibility(ESlateVisibility::Visible);
    }
}

void ULoadGameWidget::HandleLoadSlot(int32 SlotIndex)
{
    USkaldSaveGame* LoadedGame = USkaldSaveGameLibrary::LoadSkaldGame(SlotNames[SlotIndex], 0);
    if (LoadedGame)
    {
        if (USkaldGameInstance* GI = GetWorld()->GetGameInstance<USkaldGameInstance>())
        {
            if (LoadedGame->Players.Num() > 0)
            {
                GI->DisplayName = LoadedGame->Players[0].PlayerName;
                GI->Faction = LoadedGame->Players[0].Faction;
            }
        }

        UGameplayStatics::OpenLevel(this, FName("Skald_OverTop"));
    }
    else
    {
        UE_LOG(LogTemp, Error, TEXT("Failed to load save slot %s"), SlotNames[SlotIndex]);
    }
}

