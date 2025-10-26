#include "UI/SkaldPlayerListEntryWidget.h"

#include "Internationalization/Text.h"
#include "Math/UnrealMathUtility.h"
#include "Components/TextBlock.h"
#include "Components/Widget.h"

void USkaldPlayerListEntryWidget::SetupPlayerEntry(const FS_PlayerData& InPlayerData,
                                                   int32 InTerritoryCount)
{
  PlayerData = InPlayerData;
  TerritoryCount = FMath::Max(0, InTerritoryCount);

  if (const UEnum* FactionEnum = StaticEnum<ESkaldFaction>())
  {
    FactionDisplayName = FactionEnum->GetDisplayNameTextByValue(
        static_cast<int64>(PlayerData.Faction));
  }
  else
  {
    FactionDisplayName = FText::FromString(TEXT("Unknown"));
  }

  NativeOnPlayerDataUpdated();
  OnPlayerDataUpdated();
}

void USkaldPlayerListEntryWidget::NativeOnPlayerDataUpdated()
{
  if (PlayerNameText)
  {
    PlayerNameText->SetText(FText::FromString(PlayerData.PlayerName));
  }

  if (FactionNameText)
  {
    FactionNameText->SetText(FactionDisplayName);
  }

  if (TerritoryCountText)
  {
    TerritoryCountText->SetText(FText::AsNumber(TerritoryCount));
  }

  if (CapitalCountText)
  {
    CapitalCountText->SetText(FText::AsNumber(FMath::Max(0, PlayerData.CapitalsOwned)));
  }

  if (TroopCountText)
  {
    TroopCountText->SetText(FText::AsNumber(FMath::Max(0, PlayerData.TroopsCount)));
  }

  if (ResourceCountText)
  {
    ResourceCountText->SetText(FText::AsNumber(FMath::Max(0, PlayerData.Resources)));
  }

  if (AIIndicatorWidget)
  {
    AIIndicatorWidget->SetVisibility(PlayerData.IsAI ? ESlateVisibility::Visible
                                                    : ESlateVisibility::Collapsed);
  }

  if (EliminatedIndicatorWidget)
  {
    const bool bIsEliminated = PlayerData.IsEliminated || !PlayerData.IsAlive;
    EliminatedIndicatorWidget->SetVisibility(bIsEliminated ? ESlateVisibility::Visible
                                                           : ESlateVisibility::Collapsed);
  }
}

