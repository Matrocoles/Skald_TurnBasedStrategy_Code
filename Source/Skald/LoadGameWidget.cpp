#include "LoadGameWidget.h"
#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Blueprint/WidgetTree.h"
#include "Kismet/GameplayStatics.h"

static const TCHAR* SlotNames[3] = { TEXT("Slot0"), TEXT("Slot1"), TEXT("Slot2") };

template<typename TFunc>
static void AddSlotButton(UWidgetTree* Tree, UVerticalBox* Root, const FString& Label, TFunc&& Handler)
{
    UButton* Button = Tree->ConstructWidget<UButton>(UButton::StaticClass());
    UTextBlock* Text = Tree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
    Text->SetText(FText::FromString(Label));
    Button->AddChild(Text);
    Button->OnClicked.AddLambda(Handler);
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
            AddSlotButton(WidgetTree, Root, SlotNames[0], [this]() { OnLoadSlot0(); });
        }
        // Slot 1
        if (UGameplayStatics::DoesSaveGameExist(SlotNames[1], 0))
        {
            AddSlotButton(WidgetTree, Root, SlotNames[1], [this]() { OnLoadSlot1(); });
        }
        // Slot 2
        if (UGameplayStatics::DoesSaveGameExist(SlotNames[2], 0))
        {
            AddSlotButton(WidgetTree, Root, SlotNames[2], [this]() { OnLoadSlot2(); });
        }
    }
}

void ULoadGameWidget::OnLoadSlot0()
{
    UGameplayStatics::LoadGameFromSlot(SlotNames[0], 0);
    UGameplayStatics::OpenLevel(this, FName("OverviewMap"));
}

void ULoadGameWidget::OnLoadSlot1()
{
    UGameplayStatics::LoadGameFromSlot(SlotNames[1], 0);
    UGameplayStatics::OpenLevel(this, FName("OverviewMap"));
}

void ULoadGameWidget::OnLoadSlot2()
{
    UGameplayStatics::LoadGameFromSlot(SlotNames[2], 0);
    UGameplayStatics::OpenLevel(this, FName("OverviewMap"));
}

