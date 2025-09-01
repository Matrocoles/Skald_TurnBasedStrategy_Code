#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "SettingsWidget.generated.h"

class ULobbyMenuWidget;
class UButton;
class UComboBoxString;
class USlider;
class USoundClass;
class USoundMix;
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
    UComboBoxString* DisplaySizeCombo;

    UPROPERTY(BlueprintReadOnly, Category="Skald|Widgets", meta = (BindWidgetOptional))
    UComboBoxString* GraphicsQualityCombo;

    UPROPERTY(BlueprintReadOnly, Category="Skald|Widgets", meta = (BindWidgetOptional))
    USlider* AudioSlider;

    UPROPERTY(EditAnywhere, Category="Skald|Audio")
    USoundMix* MasterSoundMix;

    UPROPERTY(EditAnywhere, Category="Skald|Audio")
    USoundClass* MasterSoundClass;

    UFUNCTION(BlueprintCallable, Category="Skald|Widgets")
    void SetLobbyMenu(ULobbyMenuWidget* InMenu);

protected:
    virtual void NativeConstruct() override;

    UFUNCTION(BlueprintCallable)
    void OnApply();

    UFUNCTION(BlueprintCallable, Category="Skald|Widgets")
    void OnMainMenu();

    UFUNCTION(BlueprintCallable, Category="Skald|Widgets")
    void HandleResolutionChanged(FString SelectedItem, ESelectInfo::Type SelectionType);

    UFUNCTION(BlueprintCallable, Category="Skald|Widgets")
    void HandleQualityChanged(FString SelectedItem, ESelectInfo::Type SelectionType);

    UFUNCTION(BlueprintCallable, Category="Skald|Widgets")
    void HandleAudioChanged(float Value);

private:
    UPROPERTY()
    TWeakObjectPtr<ULobbyMenuWidget> LobbyMenu;

    TMap<FString, FIntPoint> ResolutionMap;
    TMap<FString, int32> QualityMap;

    FIntPoint PendingResolution;
    int32 PendingQuality;
    float PendingAudioVolume;
};

