#include "UI/PrepareForBattleWidget.h"
#include "Components/Button.h"
#include "Components/ButtonSlot.h"
#include "Components/Image.h"
#include "Components/SlateWrapperTypes.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "Internationalization/Text.h"
#include "Math/UnrealMathUtility.h"
#include "Styling/SlateColor.h"
#include "Types/SlateEnums.h"
#include "Types/SlateStructs.h"
#include "Blueprint/WidgetTree.h"
#include "TimerManager.h"

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

  if (RetreatButton &&
      !RetreatButton->OnClicked.IsAlreadyBound(
          this, &UPrepareForBattleWidget::HandleRetreatButtonClicked)) {
    RetreatButton->OnClicked.AddDynamic(
        this, &UPrepareForBattleWidget::HandleRetreatButtonClicked);
  }

  RefreshTextWidgets();
}

void UPrepareForBattleWidget::SynchronizeProperties() {
  Super::SynchronizeProperties();
  RefreshTextWidgets();
}

void UPrepareForBattleWidget::NativeDestruct() {
  ClearRetreatStatus();
  if (RetreatButton) {
    RetreatButton->OnClicked.RemoveAll(this);
  }
  if (PrepareForBattleButton) {
    PrepareForBattleButton->OnClicked.RemoveAll(this);
  }
  Super::NativeDestruct();
}

void UPrepareForBattleWidget::SetupBattleDetails(
    const FText &InAttackingPlayerName, const FText &InAttackingTerritoryName,
    UTexture2D *InAttackingFactionIcon, const FText &InDefendingPlayerName,
    const FText &InDefendingTerritoryName, UTexture2D *InDefendingFactionIcon,
    int32 InAttackingUnits, int32 InDefendingUnits) {
  if (WidgetTree && !WidgetTree->RootWidget) {
    BuildFallbackWidgetTree();
  }

  AttackingPlayerNameText = InAttackingPlayerName;
  AttackingTerritoryNameText = InAttackingTerritoryName;
  DefendingPlayerNameText = InDefendingPlayerName;
  DefendingTerritoryNameText = InDefendingTerritoryName;
  AttackingFactionTexture = InAttackingFactionIcon;
  DefendingFactionTexture = InDefendingFactionIcon;
  AttackingUnits = FMath::Max(0, InAttackingUnits);
  DefendingUnits = FMath::Max(0, InDefendingUnits);
  RefreshTextWidgets();
}

void UPrepareForBattleWidget::HandlePrepareButtonClicked() {
  OnPrepareButtonClicked.Broadcast();
}

void UPrepareForBattleWidget::HandleRetreatButtonClicked() {
  OnRetreatButtonClicked.Broadcast();
}

void UPrepareForBattleWidget::SetFactionColors(FLinearColor InAttackerColor,
                                               FLinearColor InDefenderColor) {
  AttackerFactionColor = InAttackerColor;
  DefenderFactionColor = InDefenderColor;
  ApplyFactionColors();
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

  auto BuildUnitsDisplayText = [](const FText &Label, int32 UnitCount) {
    const FText SanitisedLabel = Label.IsEmptyOrWhitespace()
                                     ? FText::FromString(TEXT("Units"))
                                     : Label;
    FFormatOrderedArguments OrderedArgs;
    OrderedArgs.Add(SanitisedLabel);
    OrderedArgs.Add(FText::AsNumber(UnitCount));
    static const FText FormatText = FText::FromString(TEXT("{0}: {1}"));
    return FText::Format(FormatText, OrderedArgs);
  };

  if (AttackingUnitsText) {
    AttackingUnitsText->SetText(
        BuildUnitsDisplayText(AttackingUnitsLabelText, AttackingUnits));
  }

  if (DefendingUnitsText) {
    DefendingUnitsText->SetText(
        BuildUnitsDisplayText(DefendingUnitsLabelText, DefendingUnits));
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

  ApplyFactionColors();
}

void UPrepareForBattleWidget::ShowRetreatStatus(const FText &StatusMessage,
                                                float DisplayDuration) {
  if (RetreatStatusText) {
    RetreatStatusText->SetText(StatusMessage);
    const bool bHasMessage = !StatusMessage.IsEmptyOrWhitespace();
    RetreatStatusText->SetVisibility(bHasMessage ? ESlateVisibility::Visible
                                                 : ESlateVisibility::Collapsed);
  }

  if (UWorld *World = GetWorld()) {
    World->GetTimerManager().ClearTimer(RetreatStatusTimerHandle);
    if (DisplayDuration > 0.f) {
      World->GetTimerManager().SetTimer(
          RetreatStatusTimerHandle, this,
          &UPrepareForBattleWidget::ClearRetreatStatus, DisplayDuration, false);
    }
  }
}

void UPrepareForBattleWidget::SetRetreatButtonVisibility(
    ESlateVisibility NewVisibility) {
  if (RetreatButton) {
    RetreatButton->SetVisibility(NewVisibility);
    const bool bVisible = NewVisibility == ESlateVisibility::Visible ||
                          NewVisibility == ESlateVisibility::HitTestInvisible ||
                          NewVisibility == ESlateVisibility::SelfHitTestInvisible;
    RetreatButton->SetIsEnabled(bVisible);
  }
}

void UPrepareForBattleWidget::ClearRetreatStatus() {
  if (RetreatStatusText) {
    RetreatStatusText->SetText(FText::GetEmpty());
    RetreatStatusText->SetVisibility(ESlateVisibility::Collapsed);
  }
  if (UWorld *World = GetWorld()) {
    World->GetTimerManager().ClearTimer(RetreatStatusTimerHandle);
  }
}

void UPrepareForBattleWidget::ApplyFactionColors() {
  auto AssignColor = [](UTextBlock *TextWidget, const FLinearColor &Color) {
    if (TextWidget) {
      TextWidget->SetColorAndOpacity(FSlateColor(Color));
    }
  };

  AssignColor(AttackingPlayerName, AttackerFactionColor);
  AssignColor(AttackingTerritoryName, AttackerFactionColor);
  AssignColor(AttackingUnitsText, AttackerFactionColor);
  AssignColor(DefendingPlayerName, DefenderFactionColor);
  AssignColor(DefendingTerritoryName, DefenderFactionColor);
  AssignColor(DefendingUnitsText, DefenderFactionColor);
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

  auto AddUnitsLabel = [&](const TCHAR *Name, UTextBlock *&OutTextBlock,
                           const FText &Label, int32 UnitCount) {
    UTextBlock *TextBlock = WidgetTree->ConstructWidget<UTextBlock>(
        UTextBlock::StaticClass(), FName(Name));
    if (!TextBlock) {
      OutTextBlock = nullptr;
      return;
    }

    TextBlock->SetJustification(ETextJustify::Center);
    TextBlock->SetAutoWrapText(true);
    const FText SanitisedLabel = Label.IsEmptyOrWhitespace()
                                     ? FText::FromString(TEXT("Units"))
                                     : Label;
    TextBlock->SetText(FText::Format(FText::FromString(TEXT("{0}: {1}")),
                                     SanitisedLabel, FText::AsNumber(UnitCount)));

    if (UVerticalBoxSlot *TextSlot =
            Root->AddChildToVerticalBox(TextBlock)) {
      TextSlot->SetHorizontalAlignment(EHorizontalAlignment::HAlign_Fill);
      TextSlot->SetPadding(FMargin(4.f, 2.f));
    }

    OutTextBlock = TextBlock;
  };

  AddUnitsLabel(TEXT("AttackingUnitsText"), AttackingUnitsText,
                AttackingUnitsLabelText, AttackingUnits);
  AddUnitsLabel(TEXT("DefendingUnitsText"), DefendingUnitsText,
                DefendingUnitsLabelText, DefendingUnits);

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

  RetreatButton = WidgetTree->ConstructWidget<UButton>(
      UButton::StaticClass(), TEXT("RetreatButton"));
  if (RetreatButton) {
    RetreatButton->SetVisibility(ESlateVisibility::Collapsed);
    if (UVerticalBoxSlot *RetreatSlot =
            Root->AddChildToVerticalBox(RetreatButton)) {
      RetreatSlot->SetHorizontalAlignment(EHorizontalAlignment::HAlign_Center);
      RetreatSlot->SetPadding(FMargin(8.f, 8.f, 8.f, 0.f));
    }

    UTextBlock *RetreatLabel = WidgetTree->ConstructWidget<UTextBlock>(
        UTextBlock::StaticClass(), TEXT("RetreatLabel"));
    if (RetreatLabel) {
      RetreatLabel->SetText(
          NSLOCTEXT("SkaldHUD", "PrepareForBattleRetreat", "Retreat"));
      RetreatLabel->SetJustification(ETextJustify::Center);
      RetreatLabel->SetAutoWrapText(true);

      if (UButtonSlot *RetreatButtonSlot =
              Cast<UButtonSlot>(RetreatButton->AddChild(RetreatLabel))) {
        RetreatButtonSlot->SetHorizontalAlignment(
            EHorizontalAlignment::HAlign_Center);
        RetreatButtonSlot->SetVerticalAlignment(
            EVerticalAlignment::VAlign_Center);
        RetreatButtonSlot->SetPadding(FMargin(12.f, 6.f));
      }
    }
  }

  RetreatStatusText = WidgetTree->ConstructWidget<UTextBlock>(
      UTextBlock::StaticClass(), TEXT("RetreatStatusText"));
  if (RetreatStatusText) {
    RetreatStatusText->SetJustification(ETextJustify::Center);
    RetreatStatusText->SetAutoWrapText(true);
    RetreatStatusText->SetVisibility(ESlateVisibility::Collapsed);
    RetreatStatusText->SetColorAndOpacity(FSlateColor(FLinearColor::Red));
    if (UVerticalBoxSlot *StatusSlot =
            Root->AddChildToVerticalBox(RetreatStatusText)) {
      StatusSlot->SetHorizontalAlignment(
          EHorizontalAlignment::HAlign_Center);
      StatusSlot->SetPadding(FMargin(8.f, 6.f, 8.f, 0.f));
    }
  }

  ApplyFactionColors();
}

