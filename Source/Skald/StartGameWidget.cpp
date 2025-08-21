#include "StartGameWidget.h"
#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Blueprint/WidgetTree.h"
#include "Kismet/GameplayStatics.h"

void UStartGameWidget::NativeConstruct()
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

        AddButton(TEXT("Singleplayer"), FName("OnSingleplayer"));
        AddButton(TEXT("Multiplayer"), FName("OnMultiplayer"));
    }
}

void UStartGameWidget::OnSingleplayer()
{
    UGameplayStatics::OpenLevel(this, FName("OverviewMap"));
}

void UStartGameWidget::OnMultiplayer()
{
    UGameplayStatics::OpenLevel(this, FName("OverviewMap"), true, TEXT("listen"));
}

