#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "LobbyMenuWidget.generated.h"

class UButton;
class UVerticalBox;

/**
 * Main menu widget shown on the lobby map.
 */
UCLASS(Blueprintable, BlueprintType)
class SKALD_API ULobbyMenuWidget : public UUserWidget
{
    GENERATED_BODY()

protected:
    virtual void NativeConstruct() override;

    UPROPERTY(meta=(BindWidget))
    UVerticalBox* Root;

    UPROPERTY(meta=(BindWidget))
    UButton* StartButton;

    UPROPERTY(meta=(BindWidget))
    UButton* LoadButton;

    UPROPERTY(meta=(BindWidget))
    UButton* SettingsButton;

    UPROPERTY(meta=(BindWidget))
    UButton* ExitButton;

    UFUNCTION(BlueprintCallable)
    void OnStartGame();

    UFUNCTION(BlueprintCallable)
    void OnLoadGame();

    UFUNCTION(BlueprintCallable)
    void OnSettings();

    UFUNCTION(BlueprintCallable)
    void OnExit();
};

