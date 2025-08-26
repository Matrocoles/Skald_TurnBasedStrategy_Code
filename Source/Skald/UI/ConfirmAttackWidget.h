#pragma once

#include "Blueprint/UserWidget.h"
#include "CoreMinimal.h"
#include "ConfirmAttackWidget.generated.h"

class UButton;

/**
 * Simple confirmation widget with Approve/Cancel buttons for attack
 * selection.
 */
UCLASS()
class SKALD_API UConfirmAttackWidget : public UUserWidget {
  GENERATED_BODY()

public:
  virtual void NativeConstruct() override;

  UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
  UButton *ApproveButton;

  UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
  UButton *CancelButton;

  UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Skald|Attack")
  int32 ArmyCount = 0;
};

