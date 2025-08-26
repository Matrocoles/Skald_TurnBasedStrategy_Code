#include "SettingsWidget.h"
#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Blueprint/WidgetTree.h"
#include "GameFramework/GameUserSettings.h"
#include "Engine/Engine.h"
#include "LobbyMenuWidget.h"

void USettingsWidget::NativeConstruct()
{
    Super::NativeConstruct();

    if (WidgetTree)
    {
        UVerticalBox* Root = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());
        WidgetTree->RootWidget = Root;

        UButton* ApplyButton = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass());
        UTextBlock* Text = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
        Text->SetText(FText::FromString(TEXT("Apply Settings")));
        ApplyButton->AddChild(Text);
        ApplyButton->OnClicked.AddDynamic(this, &USettingsWidget::OnApply);
        Root->AddChild(ApplyButton);

        UButton* MainMenuButton = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass());
        UTextBlock* MainText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
        MainText->SetText(FText::FromString(TEXT("Main Menu")));
        MainMenuButton->AddChild(MainText);
        MainMenuButton->OnClicked.AddDynamic(this, &USettingsWidget::OnMainMenu);
        Root->AddChild(MainMenuButton);
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

