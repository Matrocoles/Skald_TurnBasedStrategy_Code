#include "UI/CombatFloaterPoolSubsystem.h"

#include "Blueprint/UserWidget.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "UI/W_FloatingText.h"

namespace
{
constexpr int32 MaxInvalidWidgetChecksPerSpawn = 4;
}

UCombatFloaterPoolSubsystem::UCombatFloaterPoolSubsystem() {
  FloaterWidgetClass = UW_FloatingText::StaticClass();
}

void UCombatFloaterPoolSubsystem::Deinitialize() {
  InactivePool.Empty();
  SpawnedWidgets.Empty();
  Super::Deinitialize();
}

UClass *UCombatFloaterPoolSubsystem::ResolveFloaterClass() const {
  if (FloaterWidgetClass) {
    return FloaterWidgetClass.Get();
  }
  return UW_FloatingText::StaticClass();
}

UW_FloatingText *
UCombatFloaterPoolSubsystem::SpawnFloater(APlayerController *OwningPlayer) {
  if (!OwningPlayer) {
    return nullptr;
  }

  UW_FloatingText *ResolvedWidget = nullptr;
  for (int32 Index = InactivePool.Num() - 1; Index >= 0; --Index) {
    TWeakObjectPtr<UUserWidget> &Entry = InactivePool[Index];
    if (!Entry.IsValid()) {
      InactivePool.RemoveAtSwap(Index);
      continue;
    }

    if (UW_FloatingText *Floater = Cast<UW_FloatingText>(Entry.Get())) {
      InactivePool.RemoveAtSwap(Index);
      ResolvedWidget = Floater;
      break;
    }

    // Remove any invalid types so the pool stays clean.
    InactivePool.RemoveAtSwap(Index);
  }

  if (!ResolvedWidget) {
    if (UClass *WidgetClass = ResolveFloaterClass()) {
      if (UWorld *World = GetWorld()) {
        if (UUserWidget *NewWidget = CreateWidget(World, WidgetClass, NAME_None)) {
          ResolvedWidget = Cast<UW_FloatingText>(NewWidget);
          if (ResolvedWidget) {
            SpawnedWidgets.Add(NewWidget);
          } else {
            // Widget class mismatch; avoid leaking the instance.
            NewWidget->RemoveFromParent();
          }
        }
      }
    }
  }

  if (ResolvedWidget) {
    ResolvedWidget->SetOwningPlayer(OwningPlayer);
    ResolvedWidget->AddToViewport();
    ResolvedWidget->InitializeFloater(OwningPlayer);
  }

  return ResolvedWidget;
}

void UCombatFloaterPoolSubsystem::ReleaseFloater(
    UW_FloatingText *FloaterWidget) {
  if (!FloaterWidget) {
    return;
  }

  FloaterWidget->ResetForPool();
  FloaterWidget->RemoveFromParent();

  int32 InvalidRemoved = 0;
  for (int32 Index = InactivePool.Num() - 1; Index >= 0 &&
                                   InvalidRemoved < MaxInvalidWidgetChecksPerSpawn;
       --Index, ++InvalidRemoved) {
    if (!InactivePool[Index].IsValid()) {
      InactivePool.RemoveAtSwap(Index);
    }
  }

  InactivePool.Add(FloaterWidget);
}

