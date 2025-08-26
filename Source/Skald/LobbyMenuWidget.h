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

public:
    UPROPERTY(BlueprintReadOnly, Category="Skald|Widgets", meta=(BindWidgetOptional))
    UVerticalBox* Root;

    UPROPERTY(BlueprintReadOnly, Category="Skald|Widgets", meta=(BindWidgetOptional))
    UButton* StartButton;

    UPROPERTY(BlueprintReadOnly, Category="Skald|Widgets", meta=(BindWidgetOptional))
    UButton* LoadButton;

    UPROPERTY(BlueprintReadOnly, Category="Skald|Widgets", meta=(BindWidgetOptional))
    UButton* SettingsButton;

    UPROPERTY(BlueprintReadOnly, Category="Skald|Widgets", meta=(BindWidgetOptional))
    UButton* ExitButton;

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

