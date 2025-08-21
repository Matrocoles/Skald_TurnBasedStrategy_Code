#include "SettingsWidget.h"
#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Blueprint/WidgetTree.h"
#include "GameFramework/GameUserSettings.h"
#include "Engine/Engine.h"

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
    }
}

void USettingsWidget::OnApply()
{
    if (UGameUserSettings* Settings = GEngine->GetGameUserSettings())
    {
        Settings->ApplySettings(false);
    }
}

