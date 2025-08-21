#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "LobbyMenuWidget.generated.h"

/**
 * Main menu widget shown on the lobby map.
 */
UCLASS(Blueprintable, BlueprintType)
class SKALD_API ULobbyMenuWidget : public UUserWidget
{
    GENERATED_BODY()

protected:
    virtual void NativeConstruct() override;

    UFUNCTION(BlueprintCallable)
    void OnStartGame();

    UFUNCTION(BlueprintCallable)
    void OnLoadGame();

    UFUNCTION(BlueprintCallable)
    void OnSettings();

    UFUNCTION(BlueprintCallable)
    void OnExit();
};

