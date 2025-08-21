#include "LobbyMenuWidget.h"
#include "StartGameWidget.h"
#include "LoadGameWidget.h"
#include "SettingsWidget.h"
#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Blueprint/WidgetTree.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetSystemLibrary.h"

void ULobbyMenuWidget::NativeConstruct()
{
    Super::NativeConstruct();

    if (WidgetTree)
    {
        UVerticalBox* Root = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());
        WidgetTree->RootWidget = Root;

        auto AddButton = [this, Root](const FString& Label, const FName& FuncName)
        {
            UButton* Button = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass());
            UTextBlock* Text = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
            Text->SetText(FText::FromString(Label));
            Button->AddChild(Text);
            FScriptDelegate Delegate;
            Delegate.BindUFunction(this, FuncName);
            Button->OnClicked.Add(Delegate);
            Root->AddChild(Button);
        };

        AddButton(TEXT("Start Game"), FName("OnStartGame"));
        AddButton(TEXT("Load Game"), FName("OnLoadGame"));
        AddButton(TEXT("Settings"), FName("OnSettings"));
        AddButton(TEXT("Exit"), FName("OnExit"));
    }
}

void ULobbyMenuWidget::OnStartGame()
{
    if (UWorld* World = GetWorld())
    {
        UStartGameWidget* Widget = CreateWidget<UStartGameWidget>(World, UStartGameWidget::StaticClass());
        if (Widget)
        {
            Widget->AddToViewport();
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
            Widget->AddToViewport();
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
            Widget->AddToViewport();
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

