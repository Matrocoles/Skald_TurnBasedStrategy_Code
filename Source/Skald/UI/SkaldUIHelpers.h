#pragma once

#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetBlueprintLibrary.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/PlayerController.h"
#include "Widgets/SViewport.h"

static inline void FocusWidgetUIOnly(APlayerController *PC, UUserWidget *Widget) {
  if (!PC || !Widget) {
    return;
  }

  Widget->SetIsFocusable(true);
  Widget->SetFocus();

  // Use Game+UI input while focusing modal widgets so the OS cursor remains
  // visible/interactive even when the viewport previously captured the mouse.
  UWidgetBlueprintLibrary::SetInputMode_GameAndUIEx(
      PC, Widget, EMouseLockMode::DoNotLock, false);
  PC->bShowMouseCursor = true;
}

static inline void FocusGameViewport(APlayerController *PC) {
  if (!PC) {
    return;
  }

  UWorld *World = PC->GetWorld();
  if (!World) {
    return;
  }

  if (!FSlateApplication::IsInitialized()) {
    return;
  }

  if (UGameViewportClient *ViewportClient = World->GetGameViewport()) {
    TSharedPtr<SViewport> ViewportWidget = ViewportClient->GetGameViewportWidget();
    if (ViewportWidget.IsValid()) {
      FSlateApplication::Get().SetKeyboardFocus(ViewportWidget,
                                                EFocusCause::SetDirectly);
    }
  }
}
