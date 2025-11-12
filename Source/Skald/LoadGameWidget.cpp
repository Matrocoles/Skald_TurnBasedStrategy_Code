#include "LoadGameWidget.h"
#include "Skald.h"
#include "SkaldLogging.h"
#include "Components/Button.h"
#include "Kismet/GameplayStatics.h"
#include "SlotNameConstants.h"
#include "UObject/ScriptDelegates.h"
#include "GameFramework/PlayerController.h"
#include "SkaldSaveGame.h"
#include "Skald_GameInstance.h"
#include "Skald_PlayerController.h"
#include "Blueprint/WidgetBlueprintLibrary.h"
#include "Templates/UnrealTemplate.h"
#include "UI/InGameMenuWidget.h"
#include "LobbyMenuWidget.h"
#include "UI/SkaldMainHUDWidget.h"

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

    InitialiseSlotButton(Slot0Button, 0, GET_FUNCTION_NAME_CHECKED(ULoadGameWidget, OnLoadSlot0));
    InitialiseSlotButton(Slot1Button, 1, GET_FUNCTION_NAME_CHECKED(ULoadGameWidget, OnLoadSlot1));
    InitialiseSlotButton(Slot2Button, 2, GET_FUNCTION_NAME_CHECKED(ULoadGameWidget, OnLoadSlot2));
    BindButton(MainMenuButton, GET_FUNCTION_NAME_CHECKED(ULoadGameWidget, OnMainMenu));
}

void ULoadGameWidget::NativeDestruct()
{
    UnbindButton(Slot0Button, GET_FUNCTION_NAME_CHECKED(ULoadGameWidget, OnLoadSlot0));
    UnbindButton(Slot1Button, GET_FUNCTION_NAME_CHECKED(ULoadGameWidget, OnLoadSlot1));
    UnbindButton(Slot2Button, GET_FUNCTION_NAME_CHECKED(ULoadGameWidget, OnLoadSlot2));
    UnbindButton(MainMenuButton, GET_FUNCTION_NAME_CHECKED(ULoadGameWidget, OnMainMenu));

    Super::NativeDestruct();
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
    CleanupAndReturnToMenu();
}

void ULoadGameWidget::HandleLoadSlot(int32 SlotIndex)
{
    if (!IsValidSlotIndex(SlotIndex))
    {
        return;
    }

    ShowLoadingIndicator();

    USkaldSaveGame* LoadedGame = Cast<USkaldSaveGame>(UGameplayStatics::LoadGameFromSlot(SlotNames[SlotIndex], 0));
    if (LoadedGame)
    {
        if (USkaldGameInstance* GI = GetGameInstance<USkaldGameInstance>())
        {
            GI->LoadedSaveGame = LoadedGame;
            GI->QueuePendingStatusMessage(NSLOCTEXT("SkaldHUD", "LoadingIndicator", "Loading..."));
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
        HideLoadingIndicator();
    }
}

void ULoadGameWidget::BindButton(UButton* Button, FName HandlerName)
{
    if (Button)
    {
        FScriptDelegate Delegate;
        Delegate.BindUFunction(this, HandlerName);
        Button->OnClicked.AddUnique(Delegate);
    }
}

void ULoadGameWidget::UnbindButton(UButton* Button, FName HandlerName)
{
    if (Button)
    {
        FScriptDelegate Delegate;
        Delegate.BindUFunction(this, HandlerName);
        Button->OnClicked.Remove(Delegate);
    }
}

void ULoadGameWidget::InitialiseSlotButton(UButton* Button, int32 SlotIndex, FName HandlerName)
{
    if (!Button)
    {
        return;
    }

    if (!IsValidSlotIndex(SlotIndex))
    {
        Button->SetVisibility(ESlateVisibility::Collapsed);
        return;
    }

    BindButton(Button, HandlerName);
    const bool bHasSave = UGameplayStatics::DoesSaveGameExist(SlotNames[SlotIndex], 0);
    Button->SetVisibility(bHasSave ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
}

void ULoadGameWidget::CleanupAndReturnToMenu()
{
    HideLoadingIndicator();

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

bool ULoadGameWidget::IsValidSlotIndex(int32 SlotIndex) const
{
    return ensure(SlotIndex >= 0 && SlotIndex < UE_ARRAY_COUNT(SlotNames));
}

void ULoadGameWidget::ShowLoadingIndicator()
{
    const FText LoadingText = NSLOCTEXT("SkaldHUD", "LoadingIndicator", "Loading...");

    if (USkaldGameInstance *GI = GetGameInstance<USkaldGameInstance>())
    {
        GI->ShowGlobalStatusMessage(LoadingText, 0.f, true);
        return;
    }

    if (ASkaldPlayerController *PC = GetOwningPlayer<ASkaldPlayerController>())
    {
        if (USkaldMainHUDWidget *HUD = PC->GetHUDWidget())
        {
            HUD->ShowStatusMessage(LoadingText, 0.f);
        }
    }
}

void ULoadGameWidget::HideLoadingIndicator()
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

