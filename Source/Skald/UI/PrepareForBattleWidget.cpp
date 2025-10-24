#include "UI/PrepareForBattleWidget.h"
#include "Components/Button.h"
#include "Components/ButtonSlot.h"
#include "Components/SlateWrapperTypes.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Types/SlateEnums.h"
#include "Types/SlateStructs.h"
#include "WidgetTree.h"

void UPrepareForBattleWidget::NativeOnInitialized() {
  Super::NativeOnInitialized();

  if (WidgetTree && !WidgetTree->RootWidget) {
    BuildFallbackWidgetTree();
  }

  if (PrepareForBattleButton &&
      !PrepareForBattleButton->OnClicked.IsAlreadyBound(
          this, &UPrepareForBattleWidget::HandlePrepareButtonClicked)) {
    PrepareForBattleButton->OnClicked.AddDynamic(
        this, &UPrepareForBattleWidget::HandlePrepareButtonClicked);
  }

  RefreshTextWidgets();
}

void UPrepareForBattleWidget::SynchronizeProperties() {
  Super::SynchronizeProperties();
  RefreshTextWidgets();
}

void UPrepareForBattleWidget::SetupBattleDetails(
    const FText &InAttackingPlayerID, const FText &InAttackingTerritoryID,
    const FText &InDefendingPlayerID, const FText &InDefendingTerritoryID) {
  if (WidgetTree && !WidgetTree->RootWidget) {
    BuildFallbackWidgetTree();
  }

  AttackingPlayerIDText = InAttackingPlayerID;
  AttackingTerritoryIDText = InAttackingTerritoryID;
  DefendingPlayerIDText = InDefendingPlayerID;
  DefendingTerritoryIDText = InDefendingTerritoryID;
  RefreshTextWidgets();
}

void UPrepareForBattleWidget::HandlePrepareButtonClicked() {
  OnPrepareButtonClicked.Broadcast();
}

void UPrepareForBattleWidget::RefreshTextWidgets() {
  if (AttackingPlayerID) {
    AttackingPlayerID->SetText(AttackingPlayerIDText);
  }
  if (AttackingTerritoryID) {
    AttackingTerritoryID->SetText(AttackingTerritoryIDText);
  }
  if (DefendingPlayerID) {
    DefendingPlayerID->SetText(DefendingPlayerIDText);
  }
  if (DefendingTerritoryID) {
    DefendingTerritoryID->SetText(DefendingTerritoryIDText);
  }
}

void UPrepareForBattleWidget::BuildFallbackWidgetTree() {
  if (!WidgetTree) {
    return;
  }

  WidgetTree->RootWidget = nullptr;

  UVerticalBox *Root = WidgetTree->ConstructWidget<UVerticalBox>(
      UVerticalBox::StaticClass(), TEXT("PrepareForBattleRoot"));
  if (!Root) {
    return;
  }

  WidgetTree->RootWidget = Root;

  auto AddLabeledText = [this, Root](const TCHAR *Name, const FText &Value,
                                     UTextBlock *&OutText) {
    UTextBlock *TextBlock = WidgetTree->ConstructWidget<UTextBlock>(
        UTextBlock::StaticClass(), FName(Name));
    if (!TextBlock) {
      OutText = nullptr;
      return;
    }

    TextBlock->SetJustification(ETextJustify::Center);
    TextBlock->SetAutoWrapText(true);
    TextBlock->SetText(Value);

    if (UVerticalBoxSlot *Slot = Root->AddChildToVerticalBox(TextBlock)) {
      Slot->SetHorizontalAlignment(EHorizontalAlignment::HAlign_Fill);
      Slot->SetPadding(FMargin(8.f, 4.f));
    }

    OutText = TextBlock;
  };

  AddLabeledText(TEXT("AttackingPlayer"), AttackingPlayerIDText,
                 AttackingPlayerID);
  AddLabeledText(TEXT("AttackingTerritory"), AttackingTerritoryIDText,
                 AttackingTerritoryID);
  AddLabeledText(TEXT("DefendingPlayer"), DefendingPlayerIDText,
                 DefendingPlayerID);
  AddLabeledText(TEXT("DefendingTerritory"), DefendingTerritoryIDText,
                 DefendingTerritoryID);

  PrepareForBattleButton = WidgetTree->ConstructWidget<UButton>(
      UButton::StaticClass(), TEXT("PrepareForBattleButton"));
  if (PrepareForBattleButton) {
    if (UVerticalBoxSlot *Slot =
            Root->AddChildToVerticalBox(PrepareForBattleButton)) {
      Slot->SetHorizontalAlignment(EHorizontalAlignment::HAlign_Center);
      Slot->SetPadding(FMargin(8.f, 12.f, 8.f, 0.f));
    }

    UTextBlock *ButtonLabel = WidgetTree->ConstructWidget<UTextBlock>(
        UTextBlock::StaticClass(), TEXT("PrepareForBattleLabel"));
    if (ButtonLabel) {
      ButtonLabel->SetText(NSLOCTEXT("SkaldHUD", "PrepareForBattleReady",
                                     "Ready for Battle"));
      ButtonLabel->SetJustification(ETextJustify::Center);
      ButtonLabel->SetAutoWrapText(true);

      if (UButtonSlot *ButtonSlot =
              Cast<UButtonSlot>(PrepareForBattleButton->AddChild(ButtonLabel))) {
        ButtonSlot->SetHorizontalAlignment(
            EHorizontalAlignment::HAlign_Center);
        ButtonSlot->SetVerticalAlignment(EVerticalAlignment::VAlign_Center);
        ButtonSlot->SetPadding(FMargin(12.f, 6.f));
      }
    }
  }
}

