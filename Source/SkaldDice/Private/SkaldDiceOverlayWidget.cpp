#include "SkaldDiceOverlayWidget.h"
#include "Blueprint/UserWidget.h"
#include "Components/Image.h"
#include "Components/PanelWidget.h"
#include "Components/TextBlock.h"
#include "Components/Widget.h"
#include "Engine/GameInstance.h"
#include "SkaldDiceManager.h"
#include "DiceRollConfig.h"

USkaldDiceOverlayWidget::USkaldDiceOverlayWidget(const FObjectInitializer& ObjectInitializer)
    : Super(ObjectInitializer)
    , OverlayMode(ESkaldDiceOverlayMode::Attack)
{
    RollingStatusText = NSLOCTEXT("SkaldDice", "OverlayRolling", "Rolling...");
    IdleStatusText = FText::GetEmpty();
    EmptyResultText = NSLOCTEXT("SkaldDice", "OverlayNoResult", "0");
}

void USkaldDiceOverlayWidget::NativeConstruct()
{
    Super::NativeConstruct();

    OverlayMode = DefaultOverlayMode;

    if (bHideOnRollComplete)
    {
        SetVisibility(ESlateVisibility::Collapsed);
    }

    if (bAutoAcquireDiceManager && !Manager)
    {
        if (UGameInstance* GameInstance = GetGameInstance())
        {
            Manager = GameInstance->GetSubsystem<USkaldDiceManager>();
        }
    }

    ResolveConfigReference();

    if (bApplyConfigTintsOnConstruct)
    {
        ApplyConfigTints();
    }

    if (TimerText)
    {
        TimerText->SetText(IdleStatusText);
    }

    BindToManager();
    UpdatePanelVisibility();
}

void USkaldDiceOverlayWidget::NativeDestruct()
{
    UnbindFromManager();

    Super::NativeDestruct();
}

void USkaldDiceOverlayWidget::SetOverlayMode(ESkaldDiceOverlayMode InMode)
{
    OverlayMode = InMode;
    UpdatePanelVisibility();
}

void USkaldDiceOverlayWidget::SetManager(USkaldDiceManager* InManager)
{
    if (Manager == InManager)
    {
        return;
    }

    UnbindFromManager();
    Manager = InManager;
    BindToManager();
}

void USkaldDiceOverlayWidget::SetConfig(UDiceRollConfig* InConfig)
{
    Config = InConfig;
    LoadedConfig = InConfig;
    ApplyConfigTints();
}

void USkaldDiceOverlayWidget::SetPlayerTint(const FLinearColor& InColor)
{
    bHasDynamicPlayerTint = true;
    DynamicPlayerTint = InColor;
    if (PlayerTintImage)
    {
        PlayerTintImage->SetColorAndOpacity(InColor);
    }

    ApplyPlayerTextTint(InColor);
}

void USkaldDiceOverlayWidget::SetEnemyTint(const FLinearColor& InColor)
{
    bHasDynamicEnemyTint = true;
    DynamicEnemyTint = InColor;
    if (EnemyTintImage)
    {
        EnemyTintImage->SetColorAndOpacity(InColor);
    }

    ApplyEnemyTextTint(InColor);
}

void USkaldDiceOverlayWidget::HandleRollStarted(const FGuid& RollId)
{
    ActiveRollId = RollId;
    if (bAutoShowOnRollStart)
    {
        SetVisibility(ESlateVisibility::Visible);
    }
    if (TimerText)
    {
        TimerText->SetText(RollingStatusText);
    }
}

void USkaldDiceOverlayWidget::HandleRollUpdate(const FGuid& RollId, float Elapsed)
{
    if (ActiveRollId != RollId)
    {
        return;
    }

    if (TimerText && bDisplayElapsedTime)
    {
        TimerText->SetText(FText::AsNumber(FMath::RoundToFloat(Elapsed * 100.f) / 100.f));
    }
}

void USkaldDiceOverlayWidget::HandleRollCompleted(const FGuid& RollId, const TArray<int32>& Results)
{
    if (ActiveRollId != RollId)
    {
        return;
    }

    if (TimerText)
    {
        FString ResultText;
        for (int32 Index = 0; Index < Results.Num(); ++Index)
        {
            if (ResultText.Len() > 0)
            {
                ResultText += ResultSeparator;
            }
            ResultText += FString::FromInt(Results[Index]);
        }
        if (ResultText.IsEmpty())
        {
            TimerText->SetText(EmptyResultText);
        }
        else
        {
            TimerText->SetText(FText::FromString(ResultText));
        }
    }

    ActiveRollId.Invalidate();

    if (bHideOnRollComplete)
    {
        SetVisibility(ESlateVisibility::Collapsed);
        if (TimerText && !IdleStatusText.IsEmpty())
        {
            TimerText->SetText(IdleStatusText);
        }
    }
}

void USkaldDiceOverlayWidget::ApplyConfigTints()
{
    const UDiceRollConfig* EffectiveConfig = Config ? Config.Get() : LoadedConfig.Get();
    const FLinearColor PlayerTint = bHasDynamicPlayerTint
                                        ? DynamicPlayerTint
                                        : ((bOverridePlayerTint || !EffectiveConfig)
                                               ? PlayerTintOverride
                                               : EffectiveConfig->PlayerTint);
    if (PlayerTintImage)
    {
        PlayerTintImage->SetColorAndOpacity(PlayerTint);
    }
    ApplyPlayerTextTint(PlayerTint);

    const FLinearColor EnemyTint = bHasDynamicEnemyTint
                                       ? DynamicEnemyTint
                                       : ((bOverrideEnemyTint || !EffectiveConfig)
                                              ? EnemyTintOverride
                                              : EffectiveConfig->EnemyTint);
    if (EnemyTintImage)
    {
        EnemyTintImage->SetColorAndOpacity(EnemyTint);
    }
    ApplyEnemyTextTint(EnemyTint);
}

void USkaldDiceOverlayWidget::UpdatePanelVisibility() const
{
    if (!bAutoTogglePanels)
    {
        return;
    }

    const bool bShowEnemy = OverlayMode == ESkaldDiceOverlayMode::Initiative;
    if (EnemyPanel)
    {
        EnemyPanel->SetVisibility(bShowEnemy ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
    }

    if (PlayerPanel)
    {
        PlayerPanel->SetVisibility(ESlateVisibility::Visible);
    }
}

void USkaldDiceOverlayWidget::ResolveConfigReference()
{
    if (Config)
    {
        LoadedConfig = Config;
        return;
    }

    if (ConfigAsset.IsNull())
    {
        return;
    }

    if (ConfigAsset.IsValid())
    {
        LoadedConfig = ConfigAsset.Get();
        Config = LoadedConfig;
        return;
    }

    if (UDiceRollConfig* Loaded = ConfigAsset.LoadSynchronous())
    {
        LoadedConfig = Loaded;
        Config = Loaded;
    }
}

void USkaldDiceOverlayWidget::BindToManager()
{
    if (!Manager)
    {
        return;
    }

    Manager->OnDiceRollStarted.AddDynamic(this, &USkaldDiceOverlayWidget::HandleRollStarted);
    Manager->OnDiceInterimUpdate.AddDynamic(this, &USkaldDiceOverlayWidget::HandleRollUpdate);
    Manager->OnDiceRollCompleted.AddDynamic(this, &USkaldDiceOverlayWidget::HandleRollCompleted);
}

void USkaldDiceOverlayWidget::UnbindFromManager()
{
    if (!Manager)
    {
        return;
    }

    Manager->OnDiceRollStarted.RemoveDynamic(this, &USkaldDiceOverlayWidget::HandleRollStarted);
    Manager->OnDiceInterimUpdate.RemoveDynamic(this, &USkaldDiceOverlayWidget::HandleRollUpdate);
    Manager->OnDiceRollCompleted.RemoveDynamic(this, &USkaldDiceOverlayWidget::HandleRollCompleted);
}

void USkaldDiceOverlayWidget::ApplyPlayerTextTint(const FLinearColor& Tint)
{
    ApplyTextTintRecursive(PlayerPanel, Tint);
}

void USkaldDiceOverlayWidget::ApplyEnemyTextTint(const FLinearColor& Tint)
{
    ApplyTextTintRecursive(EnemyPanel, Tint);
}

void USkaldDiceOverlayWidget::ApplyTextTintRecursive(UWidget* Widget, const FLinearColor& Tint)
{
    if (!Widget)
    {
        return;
    }

    if (UTextBlock* TextBlock = Cast<UTextBlock>(Widget))
    {
        TextBlock->SetColorAndOpacity(FSlateColor(Tint));
    }

    if (UPanelWidget* PanelWidget = Cast<UPanelWidget>(Widget))
    {
        const int32 ChildCount = PanelWidget->GetChildrenCount();
        for (int32 Index = 0; Index < ChildCount; ++Index)
        {
            ApplyTextTintRecursive(PanelWidget->GetChildAt(Index), Tint);
        }
    }
    else if (UUserWidget* NestedWidget = Cast<UUserWidget>(Widget))
    {
        if (UWidget* RootWidget = NestedWidget->GetRootWidget())
        {
            ApplyTextTintRecursive(RootWidget, Tint);
        }
    }
}
