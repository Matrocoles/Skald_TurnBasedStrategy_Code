#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "LoadGameWidget.generated.h"

/**
 * Simple load game menu listing a few save slots.
 */
UCLASS(Blueprintable, BlueprintType)
class SKALD_API ULoadGameWidget : public UUserWidget
{
    GENERATED_BODY()

protected:
    virtual void NativeConstruct() override;

    UFUNCTION(BlueprintCallable)
    void OnLoadSlot0();

    UFUNCTION(BlueprintCallable)
    void OnLoadSlot1();

    UFUNCTION(BlueprintCallable)
    void OnLoadSlot2();
};

