#include "UI/PrepareForBattleWidget.h"
#include "Components/Button.h"
#include "Components/ButtonSlot.h"
#include "Components/Image.h"
#include "Components/SlateWrapperTypes.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Engine/Texture2D.h"
#include "Types/SlateEnums.h"
#include "Types/SlateStructs.h"
#include "Blueprint/WidgetTree.h"

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
    const FText &InAttackingPlayerName, const FText &InAttackingTerritoryName,
    UTexture2D *InAttackingFactionIcon, const FText &InDefendingPlayerName,
    const FText &InDefendingTerritoryName, UTexture2D *InDefendingFactionIcon) {
  if (WidgetTree && !WidgetTree->RootWidget) {
    BuildFallbackWidgetTree();
  }

  AttackingPlayerNameText = InAttackingPlayerName;
  AttackingTerritoryNameText = InAttackingTerritoryName;
  DefendingPlayerNameText = InDefendingPlayerName;
  DefendingTerritoryNameText = InDefendingTerritoryName;
  AttackingFactionTexture = InAttackingFactionIcon;
  DefendingFactionTexture = InDefendingFactionIcon;
  RefreshTextWidgets();
}

void UPrepareForBattleWidget::HandlePrepareButtonClicked() {
  OnPrepareButtonClicked.Broadcast();
}

void UPrepareForBattleWidget::RefreshTextWidgets() {
  if (AttackingPlayerName) {
    AttackingPlayerName->SetText(AttackingPlayerNameText);
  }
  if (AttackingTerritoryName) {
    AttackingTerritoryName->SetText(AttackingTerritoryNameText);
  }
  if (DefendingPlayerName) {
    DefendingPlayerName->SetText(DefendingPlayerNameText);
  }
  if (DefendingTerritoryName) {
    DefendingTerritoryName->SetText(DefendingTerritoryNameText);
  }

  auto ApplyFactionEmblem = [](UImage *ImageWidget,
                               const TWeakObjectPtr<UTexture2D> &Texture) {
    if (!ImageWidget) {
      return;
    }

    if (Texture.IsValid()) {
      FSlateBrush Brush;
      Brush.SetResourceObject(Texture.Get());
      if (UTexture2D *Resolved = Texture.Get()) {
        Brush.ImageSize = FVector2D(Resolved->GetSizeX(), Resolved->GetSizeY());
      }
      ImageWidget->SetBrush(Brush);
      ImageWidget->SetVisibility(ESlateVisibility::HitTestInvisible);
    } else {
      ImageWidget->SetBrush(FSlateBrush());
      ImageWidget->SetVisibility(ESlateVisibility::Collapsed);
    }
  };

  ApplyFactionEmblem(AttackingFactionEmblem, AttackingFactionTexture);
  ApplyFactionEmblem(DefendingFactionEmblem, DefendingFactionTexture);
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

  auto AddFactionSection = [&](const TCHAR *Prefix, const FText &PlayerText,
                               const FText &TerritoryText,
                               UTextBlock *&OutPlayerText,
                               UTextBlock *&OutTerritoryText,
                               UImage *&OutEmblemImage) {
    UVerticalBox *Section = WidgetTree->ConstructWidget<UVerticalBox>(
        UVerticalBox::StaticClass(), FName(*FString::Printf(TEXT("%sSection"), Prefix)));
    if (!Section) {
      OutPlayerText = nullptr;
      OutTerritoryText = nullptr;
      OutEmblemImage = nullptr;
      return;
    }

    if (UVerticalBoxSlot *SectionSlot =
            Root->AddChildToVerticalBox(Section)) {
      SectionSlot->SetHorizontalAlignment(EHorizontalAlignment::HAlign_Fill);
      SectionSlot->SetPadding(FMargin(8.f, 6.f));
    }

    auto AddText = [&](const TCHAR *Name, const FText &Value,
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

      if (UVerticalBoxSlot *TextSlot =
              Section->AddChildToVerticalBox(TextBlock)) {
        TextSlot->SetHorizontalAlignment(EHorizontalAlignment::HAlign_Fill);
        TextSlot->SetPadding(FMargin(4.f, 2.f));
      }

      OutText = TextBlock;
    };

    OutEmblemImage = WidgetTree->ConstructWidget<UImage>(
        UImage::StaticClass(), FName(*FString::Printf(TEXT("%sEmblem"), Prefix)));
    if (OutEmblemImage) {
      OutEmblemImage->SetVisibility(ESlateVisibility::Collapsed);
      if (UVerticalBoxSlot *ImageSlot =
              Section->AddChildToVerticalBox(OutEmblemImage)) {
        ImageSlot->SetHorizontalAlignment(EHorizontalAlignment::HAlign_Center);
        ImageSlot->SetPadding(FMargin(4.f, 2.f));
      }
    }

    AddText(*FString::Printf(TEXT("%sPlayer"), Prefix), PlayerText, OutPlayerText);
    AddText(*FString::Printf(TEXT("%sTerritory"), Prefix), TerritoryText,
            OutTerritoryText);
  };

  AddFactionSection(TEXT("Attacking"), AttackingPlayerNameText,
                    AttackingTerritoryNameText, AttackingPlayerName,
                    AttackingTerritoryName, AttackingFactionEmblem);
  AddFactionSection(TEXT("Defending"), DefendingPlayerNameText,
                    DefendingTerritoryNameText, DefendingPlayerName,
                    DefendingTerritoryName, DefendingFactionEmblem);

  PrepareForBattleButton = WidgetTree->ConstructWidget<UButton>(
      UButton::StaticClass(), TEXT("PrepareForBattleButton"));
  if (PrepareForBattleButton) {
    if (UVerticalBoxSlot *ButtonContainerSlot =
            Root->AddChildToVerticalBox(PrepareForBattleButton)) {
      ButtonContainerSlot->SetHorizontalAlignment(
          EHorizontalAlignment::HAlign_Center);
      ButtonContainerSlot->SetPadding(FMargin(8.f, 12.f, 8.f, 0.f));
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

