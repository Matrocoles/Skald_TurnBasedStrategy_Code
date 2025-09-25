#include "UI/DeployWidget.h"
#include "Components/Button.h"
#include "Components/SpinBox.h"
#include "Skald_PlayerController.h"
#include "Skald_PlayerState.h"
#include "SkaldTypes.h"
#include "Skald_TurnManager.h"
#include "Territory.h"
#include "UI/SkaldMainHUDWidget.h"

void UDeployWidget::NativeConstruct() {
  Super::NativeConstruct();

  SetIsFocusable(true);
  SetFocus();

  if (AcceptButton) {
    AcceptButton->OnClicked.AddDynamic(this, &UDeployWidget::HandleAccept);
  }
  if (DeclineButton) {
    DeclineButton->OnClicked.AddDynamic(this, &UDeployWidget::HandleDecline);
  }
  if (AmountSelector) {
    AmountSelector->SetMinValue(1.f);
    AmountSelector->SetDelta(1.f);
    AmountSelector->SetValue(1.f);
  }
}

void UDeployWidget::Setup(ATerritory *InTerritory, ASkaldPlayerState *InPlayerState,
                           USkaldMainHUDWidget *InHUD, int32 MaxAmount) {
  Territory = InTerritory;
  PlayerState = InPlayerState;
  OwningHUD = InHUD;
  if (AmountSelector) {
    AmountSelector->SetMaxValue(MaxAmount);
    AmountSelector->SetValue(FMath::Clamp(1, 1, MaxAmount));
  }
}

void UDeployWidget::HandleAccept() {
  if (!Territory || !PlayerState || !OwningHUD.IsValid()) {
    if (OwningHUD.IsValid()) {
      OwningHUD->ClearDeployWidget();
    } else if (IsInViewport()) {
      RemoveFromParent();
    }
    return;
  }

  const int32 Selected = AmountSelector
                              ? FMath::Clamp(FMath::RoundToInt(AmountSelector->GetValue()), 0,
                                             PlayerState->DeployableUnits)
                              : 0;
  if (Selected > 0) {
    ATurnManager *TurnManager = nullptr;
    if (APlayerController *PC = OwningHUD->GetOwningPlayer()) {
      if (ASkaldPlayerController *SKPC = Cast<ASkaldPlayerController>(PC)) {
        SKPC->ServerDeployUnits(Territory->TerritoryID, Selected);
        TurnManager = SKPC->GetTurnManager();
        if (TurnManager) {
          TurnManager->BroadcastDeployableUnits(PlayerState);
        }
      }
    }

    const int32 Remaining = PlayerState->DeployableUnits - Selected;
    if (Remaining <= 0 && OwningHUD->DeployButton) {
      OwningHUD->DeployButton->SetVisibility(ESlateVisibility::Collapsed);
    }
  }

  if (OwningHUD.IsValid()) {
    OwningHUD->ClearDeployWidget();
  } else if (IsInViewport()) {
    RemoveFromParent();
  }
}

void UDeployWidget::HandleDecline() {
  if (OwningHUD.IsValid()) {
    OwningHUD->ClearDeployWidget();
  } else if (IsInViewport()) {
    RemoveFromParent();
  }
}

