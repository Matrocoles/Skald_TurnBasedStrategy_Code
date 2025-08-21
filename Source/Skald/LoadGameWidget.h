#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "LoadGameWidget.generated.h"

/**
 * Simple load game menu listing a few save slots.
 */
UCLASS()
class SKALD_API ULoadGameWidget : public UUserWidget
{
    GENERATED_BODY()

protected:
    virtual void NativeConstruct() override;

    UFUNCTION()
    void OnLoadSlot0();

    UFUNCTION()
    void OnLoadSlot1();

    UFUNCTION()
    void OnLoadSlot2();
};

