#include "LoadGameWidget.h"
#include "Skald.h"
#include "Components/Button.h"
#include "Kismet/GameplayStatics.h"
#include "LobbyMenuWidget.h"
#include "SlotNameConstants.h"
#include "GameFramework/PlayerController.h"
#include "SkaldSaveGame.h"
#include "Skald_GameInstance.h"
#include "Blueprint/WidgetBlueprintLibrary.h"

void ULoadGameWidget::SetLobbyMenu(ULobbyMenuWidget* InMenu)
{
    LobbyMenu = InMenu;
}

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

    // Ensure no other widgets linger on the viewport and steal input
    TArray<UUserWidget*> Widgets;
    UWidgetBlueprintLibrary::GetAllWidgetsOfClass(this, Widgets, UUserWidget::StaticClass(), /*TopLevelOnly*/ true);
    for (UUserWidget* Widget : Widgets)
    {
        if (Widget && Widget != LobbyMenu.Get())
        {
            Widget->RemoveFromParent();
        }
    }

    if (LobbyMenu.IsValid())
    {
        // Re-enable the lobby once the load-game widget closes
        LobbyMenu->SetVisibility(ESlateVisibility::Visible);
    }
}

void ULoadGameWidget::HandleLoadSlot(int32 SlotIndex)
{
    USkaldSaveGame* LoadedGame = Cast<USkaldSaveGame>(UGameplayStatics::LoadGameFromSlot(SlotNames[SlotIndex], 0));
    if (LoadedGame)
    {
        if (USkaldGameInstance* GI = GetGameInstance<USkaldGameInstance>())
        {
            GI->LoadedSaveGame = LoadedGame;
        }

        if (APlayerController* PC = GetOwningPlayer())
        {
            UWidgetBlueprintLibrary::SetInputMode_GameOnly(PC);
            PC->bShowMouseCursor = false;
        }

        // After loading, transition to the main gameplay map
        const FName LevelName(TEXT("/Game/Blueprints/Maps/Skald_OverTop"));
        UGameplayStatics::OpenLevel(this, LevelName);
    }
    else
    {
        UE_LOG(LogSkald, Error, TEXT("Failed to load save slot %s"), SlotNames[SlotIndex]);
    }
}

