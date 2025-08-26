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
        UStartGameWidget* Widget = CreateWidget<UStartGameWidget>(World, UStartGameWidget::StaticClass());
        if (Widget)
        {
            Widget->SetLobbyMenu(this);
            Widget->AddToViewport();
            SetVisibility(ESlateVisibility::Hidden);
        }
    }
}

void ULobbyMenuWidget::OnLoadGame()
{
    if (UWorld* World = GetWorld())
    {
        ULoadGameWidget* Widget = CreateWidget<ULoadGameWidget>(World, ULoadGameWidget::StaticClass());
        if (Widget)
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
        USettingsWidget* Widget = CreateWidget<USettingsWidget>(World, USettingsWidget::StaticClass());
        if (Widget)
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

