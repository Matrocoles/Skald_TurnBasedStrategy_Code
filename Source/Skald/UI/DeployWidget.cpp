#include "UI/DeployWidget.h"
#include "Components/Button.h"
#include "Components/SpinBox.h"
#include "Kismet/GameplayStatics.h"
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

  if (HowManyTroops) {
    UGameplayStatics::PlaySound2D(this, HowManyTroops);
  }
}

void UDeployWidget::SetupDeployment(ATerritory *InTerritory,
                                    ASkaldPlayerState *InPlayerState,
                                    USkaldMainHUDWidget *InHUD,
                                    int32 MaxAmount) {
  SourceTerritory = InTerritory;
  TargetTerritory = InTerritory;
  PlayerState = InPlayerState;
  OwningHUD = InHUD;
  Mode = EDeployWidgetMode::Deployment;
  MaxSelectableAmount = MaxAmount;
  if (AmountSelector) {
    AmountSelector->SetMaxValue(MaxAmount);
    AmountSelector->SetValue(FMath::Clamp(1, 1, MaxAmount));
  }
}

void UDeployWidget::SetupTransfer(ATerritory *InSource, ATerritory *InTarget,
                                  USkaldMainHUDWidget *InHUD,
                                  int32 MaxAmount) {
  SourceTerritory = InSource;
  TargetTerritory = InTarget;
  PlayerState = nullptr;
  OwningHUD = InHUD;
  Mode = EDeployWidgetMode::Transfer;
  MaxSelectableAmount = MaxAmount;
  if (AmountSelector) {
    AmountSelector->SetMaxValue(MaxAmount);
    AmountSelector->SetValue(FMath::Clamp(1, 1, MaxAmount));
  }
}

void UDeployWidget::HandleAccept() {
  if (!SourceTerritory || !OwningHUD.IsValid()) {
    if (OwningHUD.IsValid()) {
      OwningHUD->ClearDeployWidget();
    } else if (IsInViewport()) {
      RemoveFromParent();
    }
    return;
  }

  const int32 MaxAllowed = MaxSelectableAmount > 0 ? MaxSelectableAmount : 0;
  const int32 Selected = AmountSelector
                              ? FMath::Clamp(FMath::RoundToInt(AmountSelector->GetValue()), 0,
                                             MaxAllowed)
                              : 0;

  if (Selected > 0) {
    if (Mode == EDeployWidgetMode::Deployment) {
      if (!PlayerState) {
        if (OwningHUD.IsValid()) {
          OwningHUD->ClearDeployWidget();
        } else if (IsInViewport()) {
          RemoveFromParent();
        }
        return;
      }

      ATurnManager *TurnManager = nullptr;
      if (APlayerController *PC = OwningHUD->GetOwningPlayer()) {
        if (ASkaldPlayerController *SKPC = Cast<ASkaldPlayerController>(PC)) {
          SKPC->ServerDeployUnits(SourceTerritory->TerritoryID, Selected);
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
    } else if (Mode == EDeployWidgetMode::Transfer) {
      if (OwningHUD.IsValid() && TargetTerritory) {
        OwningHUD->HandleMoveAmountChosen(SourceTerritory->TerritoryID,
                                          TargetTerritory->TerritoryID, Selected);
      }
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
    if (Mode == EDeployWidgetMode::Transfer) {
      OwningHUD->CancelMoveSelection();
      OwningHUD->ClearDeployWidget();
    } else {
      OwningHUD->HandleDeploymentCancelled();
    }
  } else if (IsInViewport()) {
    RemoveFromParent();
  }
}

