#include "UI/ConfirmAttackWidget.h"
#include "Components/Button.h"
#include "Components/SpinBox.h"

void UConfirmAttackWidget::NativeConstruct() {
  Super::NativeConstruct();

  if (ArmySelector) {
    ArmySelector->SetMinValue(1.f);
    ArmySelector->SetDelta(1.f);
    ArmySelector->SetValue(1.f);
    ArmySelector->OnValueChanged.AddDynamic(
        this, &UConfirmAttackWidget::HandleValueChanged);
  }
}

void UConfirmAttackWidget::Setup(int32 MaxUnits) {
  if (ArmySelector) {
    ArmySelector->SetMaxValue(static_cast<double>(MaxUnits));
    ArmySelector->SetValue(FMath::Clamp(ArmyCount, 1, MaxUnits));
  }
  ArmyCount = FMath::Clamp(ArmyCount, 1, MaxUnits);
}

void UConfirmAttackWidget::HandleValueChanged(float NewValue) {
  ArmyCount = FMath::RoundToInt(NewValue);
}
