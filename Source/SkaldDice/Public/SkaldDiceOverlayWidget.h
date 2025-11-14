#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "SkaldDiceOverlayWidget.generated.h"

class USkaldDiceManager;
class UDiceRollConfig;
class UImage;
class UTextBlock;
class UWidget;

UENUM(BlueprintType)
enum class ESkaldDiceOverlayMode : uint8
{
    Attack,
    Initiative
};

UCLASS()
class SKALDDICE_API USkaldDiceOverlayWidget : public UUserWidget
{
    GENERATED_BODY()

public:
    USkaldDiceOverlayWidget(const FObjectInitializer& ObjectInitializer);

    virtual void NativeConstruct() override;
    virtual void NativeDestruct() override;

    UFUNCTION(BlueprintCallable, Category = "Dice|Overlay")
    void SetOverlayMode(ESkaldDiceOverlayMode InMode);

    UFUNCTION(BlueprintCallable, Category = "Dice|Overlay")
    void SetManager(USkaldDiceManager* InManager);

    UFUNCTION(BlueprintCallable, Category = "Dice|Overlay")
    void SetConfig(UDiceRollConfig* InConfig);

    void SetPlayerTint(const FLinearColor& InColor);
    void SetEnemyTint(const FLinearColor& InColor);

protected:
    UFUNCTION()
    void HandleRollStarted(const FGuid& RollId);

    UFUNCTION()
    void HandleRollUpdate(const FGuid& RollId, float Elapsed);

    UFUNCTION()
    void HandleRollCompleted(const FGuid& RollId, const TArray<int32>& Results);

    void ApplyConfigTints();
    void UpdatePanelVisibility() const;
    void ResolveConfigReference();
    void BindToManager();
    void UnbindFromManager();
    void ApplyPlayerTextTint(const FLinearColor& Tint);
    void ApplyEnemyTextTint(const FLinearColor& Tint);
    void ApplyTextTintRecursive(UWidget* Widget, const FLinearColor& Tint);

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Dice|Overlay")
    bool bAutoAcquireDiceManager = true;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Dice|Overlay")
    bool bApplyConfigTintsOnConstruct = true;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Dice|Overlay")
    bool bDisplayElapsedTime = true;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Dice|Overlay")
    bool bAutoTogglePanels = true;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Dice|Overlay")
    bool bAutoShowOnRollStart = true;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Dice|Overlay")
    bool bHideOnRollComplete = true;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Dice|Overlay")
    ESkaldDiceOverlayMode DefaultOverlayMode = ESkaldDiceOverlayMode::Attack;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Dice|Overlay")
    FText RollingStatusText;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Dice|Overlay")
    FText IdleStatusText;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Dice|Overlay")
    FText EmptyResultText;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Dice|Overlay")
    FString ResultSeparator = TEXT(", ");

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Dice|Overlay")
    TSoftObjectPtr<UDiceRollConfig> ConfigAsset;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Dice|Overlay")
    bool bOverridePlayerTint = false;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Dice|Overlay", meta = (EditCondition = "bOverridePlayerTint"))
    FLinearColor PlayerTintOverride = FLinearColor::White;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Dice|Overlay")
    bool bOverrideEnemyTint = false;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Dice|Overlay", meta = (EditCondition = "bOverrideEnemyTint"))
    FLinearColor EnemyTintOverride = FLinearColor::White;

    UPROPERTY(meta = (BindWidgetOptional))
    TObjectPtr<UImage> PlayerTintImage;

    UPROPERTY(meta = (BindWidgetOptional))
    TObjectPtr<UImage> EnemyTintImage;

    UPROPERTY(meta = (BindWidgetOptional))
    TObjectPtr<UTextBlock> TimerText;

    UPROPERTY(meta = (BindWidgetOptional))
    TObjectPtr<UWidget> PlayerPanel;

    UPROPERTY(meta = (BindWidgetOptional))
    TObjectPtr<UWidget> EnemyPanel;

    ESkaldDiceOverlayMode OverlayMode;

    UPROPERTY(Transient)
    TObjectPtr<USkaldDiceManager> Manager;

    UPROPERTY(Transient)
    TObjectPtr<UDiceRollConfig> Config;

    UPROPERTY(Transient)
    TObjectPtr<UDiceRollConfig> LoadedConfig;

    FGuid ActiveRollId;

    bool bHasDynamicPlayerTint = false;
    bool bHasDynamicEnemyTint = false;
    FLinearColor DynamicPlayerTint = FLinearColor::White;
    FLinearColor DynamicEnemyTint = FLinearColor::White;
};
