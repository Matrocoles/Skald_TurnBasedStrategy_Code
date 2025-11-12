#include "UI/QuitConfirmationWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "SettingsWidget.h"
#include "Skald_GameInstance.h"

namespace
{
UTextBlock* CreateLabel(UWidgetTree* Tree, const FText& Text)
{
    if (!Tree)
    {
        return nullptr;
    }

    UTextBlock* Label = Tree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
    if (Label)
    {
        Label->SetJustification(ETextJustify::Center);
        Label->SetText(Text);
    }
    return Label;
}

UButton* CreateButton(UWidgetTree* Tree, const FText& LabelText)
{
    if (!Tree)
    {
        return nullptr;
    }

    UButton* Button = Tree->ConstructWidget<UButton>(UButton::StaticClass());
    if (!Button)
    {
        return nullptr;
    }

    if (UTextBlock* Label = CreateLabel(Tree, LabelText))
    {
        Button->AddChild(Label);
    }

    return Button;
}
} // namespace

UQuitConfirmationWidget::UQuitConfirmationWidget(const FObjectInitializer& ObjectInitializer)
    : Super(ObjectInitializer)
{
}

void UQuitConfirmationWidget::SetOwningSettings(USettingsWidget* InSettings)
{
    OwningSettings = InSettings;
}

void UQuitConfirmationWidget::NativeConstruct()
{
    Super::NativeConstruct();

    EnsureLayout();

    if (ConfirmationText)
    {
        ConfirmationText->SetText(NSLOCTEXT("Skald", "QuitConfirmationText", "Are you sure you want to quit the game?"));
    }

    if (YesButton)
    {
        YesButton->OnClicked.AddDynamic(this, &UQuitConfirmationWidget::HandleYesClicked);
    }

    if (NoButton)
    {
        NoButton->OnClicked.AddDynamic(this, &UQuitConfirmationWidget::HandleNoClicked);
    }
}

void UQuitConfirmationWidget::NativeDestruct()
{
    if (YesButton)
    {
        YesButton->OnClicked.RemoveDynamic(this, &UQuitConfirmationWidget::HandleYesClicked);
    }

    if (NoButton)
    {
        NoButton->OnClicked.RemoveDynamic(this, &UQuitConfirmationWidget::HandleNoClicked);
    }

    Super::NativeDestruct();
}

void UQuitConfirmationWidget::EnsureLayout()
{
    if (!WidgetTree || WidgetTree->RootWidget)
    {
        return;
    }

    UVerticalBox* Root = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("QuitConfirmationRoot"));
    WidgetTree->RootWidget = Root;

    if (!Root)
    {
        return;
    }

    ConfirmationText = CreateLabel(WidgetTree, NSLOCTEXT("Skald", "QuitConfirmationText", "Are you sure you want to quit the game?"));
    if (ConfirmationText)
    {
        if (UVerticalBoxSlot* TextSlot = Root->AddChildToVerticalBox(ConfirmationText))
        {
            TextSlot->SetPadding(FMargin(12.0f));
        }
    }

    UHorizontalBox* ButtonRow = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("QuitConfirmationButtons"));
    if (!ButtonRow)
    {
        return;
    }

    if (UVerticalBoxSlot* ButtonRowSlot = Root->AddChildToVerticalBox(ButtonRow))
    {
        ButtonRowSlot->SetPadding(FMargin(12.0f));
    }

    YesButton = CreateButton(WidgetTree, NSLOCTEXT("Skald", "QuitConfirmationYes", "Yes"));
    if (YesButton)
    {
        if (UHorizontalBoxSlot* YesSlot = ButtonRow->AddChildToHorizontalBox(YesButton))
        {
            YesSlot->SetPadding(FMargin(8.0f));
        }
    }

    NoButton = CreateButton(WidgetTree, NSLOCTEXT("Skald", "QuitConfirmationNo", "No"));
    if (NoButton)
    {
        if (UHorizontalBoxSlot* NoSlot = ButtonRow->AddChildToHorizontalBox(NoButton))
        {
            NoSlot->SetPadding(FMargin(8.0f));
        }
    }
}

void UQuitConfirmationWidget::HandleYesClicked()
{
    if (OwningSettings.IsValid())
    {
        OwningSettings->ClearExitConfirmation();
    }

    if (UWorld* World = GetWorld())
    {
        if (USkaldGameInstance* GameInstance = World->GetGameInstance<USkaldGameInstance>())
        {
            GameInstance->ReturnToMainMenu();
        }
    }
}

void UQuitConfirmationWidget::HandleNoClicked()
{
    if (OwningSettings.IsValid())
    {
        OwningSettings->HandleExitDeclined();
    }
    else
    {
        RemoveFromParent();
    }
}

