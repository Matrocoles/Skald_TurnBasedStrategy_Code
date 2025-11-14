#include "UI/SiegeTransferPromptWidget.h"
#include "UI/SkaldMainHUDWidget.h"

void USiegeTransferPromptWidget::Setup(int32 FromTerritoryID, int32 ToTerritoryID,
                                       USkaldMainHUDWidget *InHUD) {
  PendingFromID = FromTerritoryID;
  PendingToID = ToTerritoryID;
  OwningHUD = InHUD;
}

void USiegeTransferPromptWidget::ConfirmTransfer() {
  if (OwningHUD.IsValid()) {
    OwningHUD->ResolveSiegeTransferChoice(true);
  }
}

void USiegeTransferPromptWidget::DeclineTransfer() {
  if (OwningHUD.IsValid()) {
    OwningHUD->ResolveSiegeTransferChoice(false);
  }
}
