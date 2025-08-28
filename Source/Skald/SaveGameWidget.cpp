#include "SaveGameWidget.h"
#include "Components/Button.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/SaveGame.h"
#include "LobbyMenuWidget.h"
#include "SlotNameConstants.h"
#include "SkaldSaveGame.h"
#include "SkaldSaveGameLibrary.h"
#include "Skald_GameState.h"
#include "Skald_GameInstance.h"
#include "WorldMap.h"
#include "Territory.h"
#include "Skald_PlayerState.h"
#include "EngineUtils.h"

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

void USaveGameWidget::OnSaveSlot0()
{
    HandleSaveSlot(0);
}

void USaveGameWidget::OnSaveSlot1()
{
    HandleSaveSlot(1);
}

void USaveGameWidget::OnSaveSlot2()
{
    HandleSaveSlot(2);
}

void USaveGameWidget::OnMainMenu()
{
    RemoveFromParent();
    if (LobbyMenu.IsValid())
    {
        LobbyMenu->SetVisibility(ESlateVisibility::Visible);
    }
}

void USaveGameWidget::HandleSaveSlot(int32 SlotIndex)
{
    USkaldSaveGame* SaveGameObject =
        Cast<USkaldSaveGame>(UGameplayStatics::CreateSaveGameObject(USkaldSaveGame::StaticClass()));
    if (!SaveGameObject)
    {
        UE_LOG(LogTemp, Error, TEXT("Failed to create save game object"));
        return;
    }

    UWorld* World = GetWorld();
    if (World)
    {
        if (ASkaldGameState* GS = World->GetGameState<ASkaldGameState>())
        {
            SaveGameObject->CurrentPlayerIndex = GS->CurrentTurnIndex;
        }

        if (USkaldGameInstance* GI = World->GetGameInstance<USkaldGameInstance>())
        {
            FPlayerSaveStruct PlayerData;
            PlayerData.PlayerID = 0;
            PlayerData.PlayerName = GI->DisplayName;
            PlayerData.Faction = GI->Faction;
            SaveGameObject->Players.Add(PlayerData);
        }

        for (TActorIterator<AWorldMap> It(World); It; ++It)
        {
            AWorldMap* Map = *It;
            for (ATerritory* Territory : Map->Territories)
            {
                if (!Territory)
                {
                    continue;
                }

                FS_Territory SaveTerr;
                SaveTerr.TerritoryID = Territory->TerritoryID;
                SaveTerr.TerritoryName = Territory->TerritoryName;
                SaveTerr.OwnerPlayerID = Territory->OwningPlayer ? Territory->OwningPlayer->GetPlayerId() : 0;
                SaveTerr.IsCapital = Territory->bIsCapital;
                SaveTerr.ArmyCount = Territory->ArmyStrength;
                SaveTerr.ContinentID = Territory->ContinentID;
                for (ATerritory* Adj : Territory->AdjacentTerritories)
                {
                    if (Adj)
                    {
                        SaveTerr.AdjacentIDs.Add(Adj->TerritoryID);
                    }
                }
                SaveGameObject->Territories.Add(SaveTerr);
            }
            break;
        }
    }

    if (USkaldSaveGameLibrary::SaveSkaldGame(SaveGameObject, SlotNames[SlotIndex], 0))
    {
        RemoveFromParent();
        if (LobbyMenu.IsValid())
        {
            LobbyMenu->SetVisibility(ESlateVisibility::Visible);
        }
    }
    else
    {
        UE_LOG(LogTemp, Error, TEXT("Failed to save slot %s"), SlotNames[SlotIndex]);
    }
}

