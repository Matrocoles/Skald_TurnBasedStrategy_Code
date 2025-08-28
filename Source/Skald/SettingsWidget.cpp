#include "SettingsWidget.h"
#include "Components/Button.h"
#include "GameFramework/GameUserSettings.h"
#include "Engine/Engine.h"
#include "LobbyMenuWidget.h"

void USettingsWidget::SetLobbyMenu(ULobbyMenuWidget* InMenu)
{
    LobbyMenu = InMenu;
}

void USettingsWidget::NativeConstruct()
{
    Super::NativeConstruct();

    if (ApplyButton)
    {
        ApplyButton->OnClicked.AddDynamic(this, &USettingsWidget::OnApply);
    }
    if (MainMenuButton)
    {
        MainMenuButton->OnClicked.AddDynamic(this, &USettingsWidget::OnMainMenu);
    }
}

void USettingsWidget::OnApply()
{
    if (UGameUserSettings* Settings = GEngine->GetGameUserSettings())
    {
        Settings->ApplySettings(false);
    }
}

void USettingsWidget::OnMainMenu()
{
    RemoveFromParent();
    if (LobbyMenu.IsValid())
    {
        LobbyMenu->SetVisibility(ESlateVisibility::Visible);
    }
}

