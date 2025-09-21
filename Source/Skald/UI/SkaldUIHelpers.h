#pragma once

#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetBlueprintLibrary.h"
#include "GameFramework/PlayerController.h"

static inline void FocusWidgetUIOnly(APlayerController *PC, UUserWidget *Widget) {
  if (!PC || !Widget) {
    return;
  }

  Widget->SetIsFocusable(true);
  Widget->SetFocus();

  UWidgetBlueprintLibrary::SetInputMode_UIOnlyEx(
      PC, Widget, EMouseLockMode::DoNotLock, false);
  PC->bShowMouseCursor = true;
}
