#include "LobbyMenuWidget.h"
#include "StartGameWidget.h"
#include "LoadGameWidget.h"
#include "SettingsWidget.h"
#include "Components/Button.h"
#include "Engine/LocalPlayer.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetSystemLibrary.h"
#include "UObject/ConstructorHelpers.h"
#include "Skald_GameState.h"
#include "UI/FighterSelectionWidget.h"

ULobbyMenuWidget::ULobbyMenuWidget(const FObjectInitializer& ObjectInitializer)
    : Super(ObjectInitializer) {
    static ConstructorHelpers::FClassFinder<UStartGameWidget> StartBP(TEXT("/Game/Blueprints/UI/Skald_StartGameWidget"));
    if (StartBP.Succeeded()) { StartGameWidgetClass = StartBP.Class; }
    static ConstructorHelpers::FClassFinder<ULoadGameWidget> LoadBP(TEXT("/Game/Blueprints/UI/Skald_LoadGameWidget"));
    if (LoadBP.Succeeded()) { LoadGameWidgetClass = LoadBP.Class; }
    static ConstructorHelpers::FClassFinder<USettingsWidget> SettingsBP(TEXT("/Game/Blueprints/UI/Skald_SettingsWidget"));
    if (SettingsBP.Succeeded()) { SettingsWidgetClass = SettingsBP.Class; }
}

void ULobbyMenuWidget::NativeConstruct()
{
    Super::NativeConstruct();

    if (StartButton)
    {
        StartButton->OnClicked.AddDynamic(this, &ULobbyMenuWidget::OnStartGame);
    }

    if (LoadButton)
    {
        LoadButton->OnClicked.AddDynamic(this, &ULobbyMenuWidget::OnLoadGame);
    }

    if (SettingsButton)
    {
        SettingsButton->OnClicked.AddDynamic(this, &ULobbyMenuWidget::OnSettings);
    }

    if (ExitButton)
    {
        ExitButton->OnClicked.AddDynamic(this, &ULobbyMenuWidget::OnExit);
    }

    if (ASkaldGameState* GS = GetWorld() ? GetWorld()->GetGameState<ASkaldGameState>() : nullptr)
    {
        if (FighterSelection)
        {
            // Initial fill
            FighterSelection->SetAvailableFighters(GS->GetFighterRoster());
            // Live updates
            GS->OnFighterRosterUpdated.AddDynamic(this, &ULobbyMenuWidget::HandleFighterRosterUpdated);
        }
    }
}

void ULobbyMenuWidget::OnStartGame()
{
    if (APlayerController* PC = GetOwningPlayer())
    {
        if (StartGameWidgetClass)
        {
            if (ULocalPlayer* LocalPlayer = PC->GetLocalPlayer())
            {
                if (UStartGameWidget* Widget = CreateWidget<UStartGameWidget>(PC, StartGameWidgetClass))
                {
                    Widget->SetLobbyMenu(this);
                    Widget->AddToViewport();
                    SetVisibility(ESlateVisibility::Hidden);
                }
            }
        }
    }
}

void ULobbyMenuWidget::OnLoadGame()
{
    if (APlayerController* PC = GetOwningPlayer())
    {
        if (!LoadGameWidgetClass)
        {
            UE_LOG(LogTemp, Error, TEXT("LoadGameWidgetClass is not set."));
            return;
        }

        if (ULocalPlayer* LocalPlayer = PC->GetLocalPlayer())
        {
            if (ULoadGameWidget* Widget = CreateWidget<ULoadGameWidget>(PC, LoadGameWidgetClass))
            {
                // Pass a reference to the lobby so it can be restored later
                Widget->SetLobbyMenu(this);
                Widget->AddToViewport();

                // Hide the lobby while the load-game menu is active
                SetVisibility(ESlateVisibility::Hidden);
            }
        }
    }
}

void ULobbyMenuWidget::OnSettings()
{
    if (APlayerController* PC = GetOwningPlayer())
    {
        if (!SettingsWidgetClass)
        {
            UE_LOG(LogTemp, Error, TEXT("SettingsWidgetClass is not set."));
            return;
        }

        if (ULocalPlayer* LocalPlayer = PC->GetLocalPlayer())
        {
            if (USettingsWidget* Widget = CreateWidget<USettingsWidget>(PC, SettingsWidgetClass))
            {
                Widget->SetLobbyMenu(this);
                Widget->AddToViewport();
                SetVisibility(ESlateVisibility::Hidden);
            }
        }
    }
}

void ULobbyMenuWidget::OnExit()
{
    if (APlayerController* PC = GetOwningPlayer())
    {
        UKismetSystemLibrary::QuitGame(this, PC, EQuitPreference::Quit, false);
    }
}

void ULobbyMenuWidget::HandleFighterRosterUpdated()
{
    if (ASkaldGameState* GS = GetWorld() ? GetWorld()->GetGameState<ASkaldGameState>() : nullptr)
    {
        if (FighterSelection)
        {
            FighterSelection->SetAvailableFighters(GS->GetFighterRoster());
        }
    }
}
