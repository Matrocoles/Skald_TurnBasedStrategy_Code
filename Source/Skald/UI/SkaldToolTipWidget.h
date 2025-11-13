#pragma once

#include "Blueprint/UserWidget.h"
#include "SkaldToolTipWidget.generated.h"

UCLASS()
class SKALD_API USkaldToolTipWidget : public UUserWidget
{
    GENERATED_BODY()

public:
    virtual FVector2D NativeGetDesiredToolTipPosition(FVector2D MousePosition) const override;
};
