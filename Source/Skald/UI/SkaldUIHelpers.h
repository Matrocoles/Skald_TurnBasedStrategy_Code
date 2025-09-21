#pragma once

#include "Blueprint/UserWidget.h"
#include "GameFramework/PlayerController.h"

static inline void FocusWidgetUIOnly(APlayerController *PC, UUserWidget *Widget) {
  if (!PC || !Widget) {
    return;
  }

  Widget->SetIsFocusable(true);

  FInputModeUIOnly Mode;
  if (TSharedPtr<SWidget> Cached = Widget->GetCachedWidget()) {
    Mode.SetWidgetToFocus(Cached);
  } else {
    Mode.SetWidgetToFocus(Widget->TakeWidget());
  }
  Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
  PC->SetInputMode(Mode);
  PC->bShowMouseCursor = true;
}
