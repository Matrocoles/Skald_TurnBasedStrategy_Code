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

    UPROPERTY(BlueprintReadOnly, meta=(BindWidgetOptional))
    UVerticalBox* Root;

    UPROPERTY(BlueprintReadOnly, meta=(BindWidgetOptional))
    UButton* StartButton;

    UPROPERTY(BlueprintReadOnly, meta=(BindWidgetOptional))
    UButton* LoadButton;

    UPROPERTY(BlueprintReadOnly, meta=(BindWidgetOptional))
    UButton* SettingsButton;

    UPROPERTY(BlueprintReadOnly, meta=(BindWidgetOptional))
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

