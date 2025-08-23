#include "LoadGameWidget.h"
#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Blueprint/WidgetTree.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/SaveGame.h"

static const TCHAR* SlotNames[3] = { TEXT("Slot0"), TEXT("Slot1"), TEXT("Slot2") };

static void AddSlotButton(UWidgetTree* Tree, UVerticalBox* Root, const FString& Label, ULoadGameWidget* Widget, void (ULoadGameWidget::*Handler)())
{
    UButton* Button = Tree->ConstructWidget<UButton>(UButton::StaticClass());
    UTextBlock* Text = Tree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
    Text->SetText(FText::FromString(Label));
    Button->AddChild(Text);
    Button->OnClicked.AddDynamic(Widget, Handler);
    Root->AddChild(Button);
}

void ULoadGameWidget::NativeConstruct()
{
    Super::NativeConstruct();

    if (WidgetTree)
    {
        UVerticalBox* Root = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());
        WidgetTree->RootWidget = Root;

        // Slot 0
        if (UGameplayStatics::DoesSaveGameExist(SlotNames[0], 0))
        {
            AddSlotButton(WidgetTree, Root, SlotNames[0], this, &ULoadGameWidget::OnLoadSlot0);
        }
        // Slot 1
        if (UGameplayStatics::DoesSaveGameExist(SlotNames[1], 0))
        {
            AddSlotButton(WidgetTree, Root, SlotNames[1], this, &ULoadGameWidget::OnLoadSlot1);
        }
        // Slot 2
        if (UGameplayStatics::DoesSaveGameExist(SlotNames[2], 0))
        {
            AddSlotButton(WidgetTree, Root, SlotNames[2], this, &ULoadGameWidget::OnLoadSlot2);
        }
    }
}

void ULoadGameWidget::OnLoadSlot0()
{
    HandleLoadSlot(0);
}

void ULoadGameWidget::OnLoadSlot1()
{
    HandleLoadSlot(1);
}

void ULoadGameWidget::OnLoadSlot2()
{
    HandleLoadSlot(2);
}

void ULoadGameWidget::HandleLoadSlot(int32 SlotIndex)
{
    USaveGame* LoadedGame = UGameplayStatics::LoadGameFromSlot(SlotNames[SlotIndex], 0);
    if (LoadedGame)
    {
        UGameplayStatics::OpenLevel(this, FName("OverviewMap"));
    }
    else
    {
        UE_LOG(LogTemp, Error, TEXT("Failed to load save slot %s"), SlotNames[SlotIndex]);
    }
}

