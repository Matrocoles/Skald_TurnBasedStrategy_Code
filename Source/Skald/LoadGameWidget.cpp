#include "LoadGameWidget.h"
#include "Skald.h"
#include "SkaldLogging.h"
#include "Components/Button.h"
#include "Kismet/GameplayStatics.h"
#include "SlotNameConstants.h"
#include "GameFramework/PlayerController.h"
#include "SkaldSaveGame.h"
#include "Skald_GameInstance.h"
#include "Blueprint/WidgetBlueprintLibrary.h"
#include "UI/InGameMenuWidget.h"
#include "LobbyMenuWidget.h"

void ULoadGameWidget::SetLobbyMenu(ULobbyMenuWidget* InMenu)
{
    SetOwningMenu(InMenu);
}

void ULoadGameWidget::SetOwningMenu(UUserWidget* InMenu)
{
    OwningMenu = InMenu;
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
        if (Widget && Widget != OwningMenu.Get())
        {
            Widget->RemoveFromParent();
        }
    }

    if (OwningMenu.IsValid())
    {
        if (UInGameMenuWidget* Menu = Cast<UInGameMenuWidget>(OwningMenu.Get()))
        {
            Menu->HandleSubMenuClosed(this);
        }
        // Re-enable the menu once the load-game widget closes
        OwningMenu->SetVisibility(ESlateVisibility::Visible);
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

        const FString& SavedMapPath = LoadedGame->MapAssetPath;
        const FName LevelName = SavedMapPath.IsEmpty()
                                   ? FName(TEXT("/Game/Blueprints/Maps/OverviewMap"))
                                   : FName(*SavedMapPath);
        UGameplayStatics::OpenLevel(this, LevelName);
    }
    else
    {
        UE_LOG(LogSkald, Error, TEXT("Failed to load save slot %s"), SlotNames[SlotIndex]);
    }
}

