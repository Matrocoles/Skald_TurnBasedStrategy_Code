#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "SettingsWidget.generated.h"

class ULobbyMenuWidget;
class UUserWidget;
class UButton;
class UComboBoxString;
class USlider;
class USoundClass;
class USoundMix;
class USkaldGameUserSettings;
namespace ESelectInfo
{
    enum Type;
}
/**
 * Basic settings menu allowing to apply current user settings.
 */
UCLASS(Blueprintable, BlueprintType)
class SKALD_API USettingsWidget : public UUserWidget
{
    GENERATED_BODY()

public:
    UPROPERTY(BlueprintReadOnly, Category="Skald|Widgets", meta = (BindWidgetOptional))
    UButton* ApplyButton;

    UPROPERTY(BlueprintReadOnly, Category="Skald|Widgets", meta = (BindWidgetOptional))
    UButton* MainMenuButton;

    UPROPERTY(BlueprintReadOnly, Category="Skald|Widgets", meta = (BindWidgetOptional))
    UButton* ExitButton;

    UPROPERTY(BlueprintReadOnly, Category="Skald|Widgets", meta = (BindWidgetOptional))
    UComboBoxString* DisplaySizeCombo;

    UPROPERTY(BlueprintReadOnly, Category="Skald|Widgets", meta = (BindWidgetOptional))
    UComboBoxString* GraphicsQualityCombo;

    UPROPERTY(BlueprintReadOnly, Category="Skald|Widgets", meta = (BindWidgetOptional))
    USlider* AudioSlider;

    UPROPERTY(BlueprintReadOnly, Category="Skald|Widgets", meta = (BindWidgetOptional))
    USlider* EnemyTurnDelaySlider;

    UPROPERTY(BlueprintReadOnly, Category="Skald|Widgets", meta = (BindWidgetOptional))
    USlider* BattleActionDelaySlider;

    UPROPERTY(EditAnywhere, Category="Skald|Audio")
    USoundMix* MasterSoundMix;

    UPROPERTY(EditAnywhere, Category="Skald|Audio")
    USoundClass* MasterSoundClass;

    UFUNCTION(BlueprintCallable, Category="Skald|Widgets")
    void SetLobbyMenu(ULobbyMenuWidget* InMenu);

    UFUNCTION(BlueprintCallable, Category="Skald|Widgets")
    void SetOwningMenu(UUserWidget* InMenu);

protected:
    virtual void NativeConstruct() override;
    virtual void NativeDestruct() override;

    UFUNCTION(BlueprintCallable)
    void OnApply();

    UFUNCTION(BlueprintCallable, Category="Skald|Widgets")
    void OnMainMenu();

    UFUNCTION(BlueprintCallable, Category="Skald|Widgets")
    void OnExit();

    UFUNCTION(BlueprintCallable, Category="Skald|Widgets")
    void HandleResolutionChanged(FString SelectedItem, ESelectInfo::Type SelectionType);

    UFUNCTION(BlueprintCallable, Category="Skald|Widgets")
    void HandleQualityChanged(FString SelectedItem, ESelectInfo::Type SelectionType);

    UFUNCTION(BlueprintCallable, Category="Skald|Widgets")
    void HandleAudioChanged(float Value);

    UFUNCTION(BlueprintCallable, Category="Skald|Widgets")
    void HandleEnemyTurnDelayChanged(float Value);

    UFUNCTION(BlueprintCallable, Category="Skald|Widgets")
    void HandleBattleActionDelayChanged(float Value);

public:
    /** Called when the exit confirmation dialog is dismissed without quitting. */
    void HandleExitDeclined();

    /** Clear the active exit confirmation widget if present. */
    void ClearExitConfirmation();

private:
    UPROPERTY()
    TWeakObjectPtr<UUserWidget> OwningMenu;

    TMap<FString, FIntPoint> ResolutionMap;
    TMap<FString, int32> QualityMap;

    FIntPoint PendingResolution;
    int32 PendingQuality;
    float PendingAudioVolume;
    float PendingEnemyTurnDelay;
    float PendingBattleActionDelay;

    TWeakObjectPtr<class UQuitConfirmationWidget> ExitConfirmationWidget;
};

