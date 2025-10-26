#include "UI/SkaldPlayerListEntryWidget.h"

#include "Internationalization/Text.h"
#include "Math/UnrealMathUtility.h"

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
  // Default implementation intentionally empty. Blueprint subclasses can
  // override OnPlayerDataUpdated to react to changes.
}

