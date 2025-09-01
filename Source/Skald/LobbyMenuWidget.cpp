#include "LobbyMenuWidget.h"
#include "StartGameWidget.h"
#include "LoadGameWidget.h"
#include "SettingsWidget.h"
#include "Components/Button.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetSystemLibrary.h"

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
}

void ULobbyMenuWidget::OnStartGame()
{
    if (UWorld* World = GetWorld())
    {
        if (UClass* StartGameWidgetClass = LoadClass<UStartGameWidget>(nullptr, TEXT("/Game/Blueprints/UI/Skald_StartGameWidget.Skald_StartGameWidget_C")))
        {
            if (UStartGameWidget* Widget = CreateWidget<UStartGameWidget>(World, StartGameWidgetClass))
            {
                Widget->SetLobbyMenu(this);
                Widget->AddToViewport();
                SetVisibility(ESlateVisibility::Hidden);
            }
        }
    }
}

void ULobbyMenuWidget::OnLoadGame()
{
    if (UWorld* World = GetWorld())
    {
        if (!LoadGameWidgetClass)
        {
            LoadGameWidgetClass = LoadClass<ULoadGameWidget>(nullptr, TEXT("/Game/Blueprints/UI/Skald_LoadGameWidget.Skald_LoadGameWidget_C"));
            if (!LoadGameWidgetClass)
            {
                UE_LOG(LogTemp, Error, TEXT("Failed to load default LoadGameWidget class."));
                return;
            }
        }

        if (ULoadGameWidget* Widget = CreateWidget<ULoadGameWidget>(World, LoadGameWidgetClass))
        {
            Widget->SetLobbyMenu(this);
            Widget->AddToViewport();
            SetVisibility(ESlateVisibility::Hidden);
        }
    }
}

void ULobbyMenuWidget::OnSettings()
{
    if (UWorld* World = GetWorld())
    {
        if (!SettingsWidgetClass)
        {
            SettingsWidgetClass = LoadClass<USettingsWidget>(nullptr, TEXT("/Game/Blueprints/UI/Skald_SettingsWidget.Skald_SettingsWidget_C"));
            if (!SettingsWidgetClass)
            {
                UE_LOG(LogTemp, Error, TEXT("Failed to load default SettingsWidget class."));
                return;
            }
        }

        if (USettingsWidget* Widget = CreateWidget<USettingsWidget>(World, SettingsWidgetClass))
        {
            Widget->SetLobbyMenu(this);
            Widget->AddToViewport();
            SetVisibility(ESlateVisibility::Hidden);
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

