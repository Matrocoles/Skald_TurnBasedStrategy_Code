#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "StartGameWidget.generated.h"

class UPlayerSetupWidget;

/**
 * Menu shown after pressing Start Game, to choose single or multiplayer.
 */
UCLASS(Blueprintable, BlueprintType)
class SKALD_API UStartGameWidget : public UUserWidget
{
    GENERATED_BODY()

protected:
    virtual void NativeConstruct() override;

    UFUNCTION(BlueprintCallable)
    void OnSingleplayer();

    UFUNCTION(BlueprintCallable)
    void OnMultiplayer();

    /** Widget class used for player setup. */
    UPROPERTY(EditDefaultsOnly, Category="UI")
    TSubclassOf<UPlayerSetupWidget> PlayerSetupWidgetClass;
};

