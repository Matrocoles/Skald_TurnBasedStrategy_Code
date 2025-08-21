#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "LobbyMenuWidget.generated.h"

/**
 * Main menu widget shown on the lobby map.
 */
UCLASS()
class SKALD_API ULobbyMenuWidget : public UUserWidget
{
    GENERATED_BODY()

protected:
    virtual void NativeConstruct() override;

    UFUNCTION()
    void OnStartGame();

    UFUNCTION()
    void OnLoadGame();

    UFUNCTION()
    void OnSettings();

    UFUNCTION()
    void OnExit();
};

