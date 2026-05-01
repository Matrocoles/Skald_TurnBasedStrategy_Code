#include "UI/InGameMenuWidget.h"
#include "Engine/LocalPlayer.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "LoadGameWidget.h"
#include "SaveGameWidget.h"
#include "SettingsWidget.h"
#include "UI/QuitConfirmationWidget.h"
#include "Skald_PlayerController.h"
#include "UObject/ConstructorHelpers.h"
#include "Widgets/SWidget.h"

namespace
{
UTextBlock* CreateLabel(UWidgetTree* Tree, const FString& Text)
{
    UTextBlock* Label = Tree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
    if (Label)
    {
        Label->SetText(FText::FromString(Text));
        Label->SetJustification(ETextJustify::Center);
    }
    return Label;
}

UButton* CreateMenuButton(UWidgetTree* Tree, const FString& Text)
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

    if (UTextBlock* Label = CreateLabel(Tree, Text))
    {
        Button->AddChild(Label);
    }

    return Button;
}
}

UInGameMenuWidget::UInGameMenuWidget(const FObjectInitializer& ObjectInitializer)
    : Super(ObjectInitializer)
{
    static ConstructorHelpers::FClassFinder<USaveGameWidget> SaveBP(TEXT("/Game/Blueprints/UI/Skald_SaveGameWidget"));
    if (SaveBP.Succeeded())
    {
        SaveGameWidgetClass = SaveBP.Class;
    }

    static ConstructorHelpers::FClassFinder<ULoadGameWidget> LoadBP(TEXT("/Game/Blueprints/UI/Skald_LoadGameWidget"));
    if (LoadBP.Succeeded())
    {
        LoadGameWidgetClass = LoadBP.Class;
    }

    static ConstructorHelpers::FClassFinder<USettingsWidget> SettingsBP(TEXT("/Game/Blueprints/UI/Skald_SettingsWidget"));
    if (SettingsBP.Succeeded())
    {
        SettingsWidgetClass = SettingsBP.Class;
    }

    QuitConfirmationWidgetClass = UQuitConfirmationWidget::StaticClass();
}

void UInGameMenuWidget::NativeConstruct()
{
    Super::NativeConstruct();

    EnsureLayout();

    if (SaveGameButton)
    {
        SaveGameButton->OnClicked.AddDynamic(this, &UInGameMenuWidget::HandleSaveGameClicked);
    }

    if (LoadGameButton)
    {
        LoadGameButton->OnClicked.AddDynamic(this, &UInGameMenuWidget::HandleLoadGameClicked);
    }

    if (SettingsButton)
    {
        SettingsButton->OnClicked.AddDynamic(this, &UInGameMenuWidget::HandleSettingsClicked);
    }

    if (MainMenuButton)
    {
        MainMenuButton->OnClicked.AddDynamic(this, &UInGameMenuWidget::HandleMainMenuClicked);
    }

    SetIsFocusable(true);

    CachedVisibility = GetVisibility();
    HandleVisibilityChange(CachedVisibility);
}

void UInGameMenuWidget::NativeDestruct()
{
    if (SaveGameButton)
    {
        SaveGameButton->OnClicked.RemoveDynamic(this, &UInGameMenuWidget::HandleSaveGameClicked);
    }

    if (LoadGameButton)
    {
        LoadGameButton->OnClicked.RemoveDynamic(this, &UInGameMenuWidget::HandleLoadGameClicked);
    }

    if (SettingsButton)
    {
        SettingsButton->OnClicked.RemoveDynamic(this, &UInGameMenuWidget::HandleSettingsClicked);
    }

    if (MainMenuButton)
    {
        MainMenuButton->OnClicked.RemoveDynamic(this, &UInGameMenuWidget::HandleMainMenuClicked);
    }

    ActiveChildWidget.Reset();
    Super::NativeDestruct();
}

void UInGameMenuWidget::HandleVisibilityChange(ESlateVisibility NewVisibility)
{
    if (NewVisibility == ESlateVisibility::Visible)
    {
        // became visible -> setup (play animations, refresh data, bind delegates, etc.)

        if (ActiveChildWidget.IsValid())
        {
            ActiveChildWidget->RemoveFromParent();
            ActiveChildWidget.Reset();
        }
    }
    else
    {
        // became hidden/collapsed -> cleanup (stop timers, unbind, pause, etc.)

        if (ActiveChildWidget.IsValid() && !ActiveChildWidget->IsInViewport())
        {
            ActiveChildWidget.Reset();
        }
    }
}

void UInGameMenuWidget::HandleSubMenuClosed(UUserWidget* ClosedWidget)
{
    if (ActiveChildWidget.Get() == ClosedWidget)
    {
        ActiveChildWidget.Reset();
    }
}

void UInGameMenuWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
    Super::NativeTick(MyGeometry, InDeltaTime);

    const ESlateVisibility Current = GetVisibility();
    if (Current != CachedVisibility)
    {
        CachedVisibility = Current;
        HandleVisibilityChange(Current);
    }
}

void UInGameMenuWidget::EnsureLayout()
{
    if (WidgetTree && !WidgetTree->RootWidget)
    {
        UVerticalBox* Root = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("InGameMenuRoot"));
        WidgetTree->RootWidget = Root;

        if (Root)
        {
            SaveGameButton = CreateMenuButton(WidgetTree, TEXT("Save Game"));
            LoadGameButton = CreateMenuButton(WidgetTree, TEXT("Load Game"));
            SettingsButton = CreateMenuButton(WidgetTree, TEXT("Settings"));
            MainMenuButton = CreateMenuButton(WidgetTree, TEXT("Main Menu"));

            const TArray<UButton*> Buttons = {SaveGameButton, LoadGameButton, SettingsButton, MainMenuButton};
            for (UButton* Button : Buttons)
            {
                if (Button)
                {
                    if (UVerticalBoxSlot* ButtonSlot = Root->AddChildToVerticalBox(Button))
                    {
                        ButtonSlot->SetPadding(FMargin(8.0f));
                    }
                }
            }
        }
    }
}

void UInGameMenuWidget::HandleSaveGameClicked()
{
    ShowChildWidget(SaveGameWidgetClass);
}

void UInGameMenuWidget::HandleLoadGameClicked()
{
    ShowChildWidget(LoadGameWidgetClass);
}

void UInGameMenuWidget::HandleSettingsClicked()
{
    ShowChildWidget(SettingsWidgetClass);
}

void UInGameMenuWidget::HandleMainMenuClicked()
{
    if (QuitConfirmationWidget.IsValid())
    {
        if (!QuitConfirmationWidget->IsInViewport())
        {
            QuitConfirmationWidget->AddToViewport(100);
        }
        return;
    }

    TSubclassOf<UQuitConfirmationWidget> WidgetClass = QuitConfirmationWidgetClass;
    if (!WidgetClass)
    {
        WidgetClass = UQuitConfirmationWidget::StaticClass();
    }
    if (!WidgetClass)
    {
        return;
    }

    if (APlayerController* PC = GetOwningPlayer())
    {
        if (ULocalPlayer* LocalPlayer = PC->GetLocalPlayer())
        {
            if (UQuitConfirmationWidget* Widget = CreateWidget<UQuitConfirmationWidget>(LocalPlayer, WidgetClass))
            {
                Widget->AddToViewport(100);
                QuitConfirmationWidget = Widget;
            }
        }
    }
    else
    {
        UE_LOG(LogTemp, Warning,
               TEXT("InGameMenu: skipped quit confirmation widget creation (no owning player/local player)."));
    }
}

void UInGameMenuWidget::ShowChildWidget(TSubclassOf<UUserWidget> WidgetClass)
{
    if (!WidgetClass)
    {
        return;
    }

    if (ActiveChildWidget.IsValid())
    {
        ActiveChildWidget->RemoveFromParent();
        ActiveChildWidget.Reset();
    }

    if (ASkaldPlayerController* PC = Cast<ASkaldPlayerController>(GetOwningPlayer()))
    {
        if (ULocalPlayer* LocalPlayer = PC->GetLocalPlayer())
        {
            if (UUserWidget* Child = CreateWidget<UUserWidget>(LocalPlayer, WidgetClass))
            {
                if (USaveGameWidget* SaveWidget = Cast<USaveGameWidget>(Child))
                {
                    SaveWidget->SetOwningMenu(this);
                }
                else if (ULoadGameWidget* LoadWidget = Cast<ULoadGameWidget>(Child))
                {
                    LoadWidget->SetOwningMenu(this);
                }
                else if (USettingsWidget* Settings = Cast<USettingsWidget>(Child))
                {
                    Settings->SetOwningMenu(this);
                }

                Child->AddToViewport(100);
                ActiveChildWidget = Child;

                SetVisibility(ESlateVisibility::Hidden);
                HandleVisibilityChange(ESlateVisibility::Hidden);

                FInputModeGameAndUI Mode;
                // Focus the Slate widget produced by the UUserWidget
                Mode.SetWidgetToFocus(Child->TakeWidget());
                Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
                Mode.SetHideCursorDuringCapture(false);
                PC->SetInputMode(Mode);
                PC->bShowMouseCursor = true;
            }
        }
    }
}
