#include "SettingsWidget.h"
#include "Components/Button.h"
#include "Components/ComboBoxString.h"
#include "Components/Slider.h"
#include "GameFramework/GameUserSettings.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/Engine.h"
#include "Sound/SoundClass.h"
#include "Sound/SoundMix.h"
#include "Skald_GameUserSettings.h"
#include "UI/InGameMenuWidget.h"
#include "LobbyMenuWidget.h"

void USettingsWidget::SetLobbyMenu(ULobbyMenuWidget* InMenu)
{
    SetOwningMenu(InMenu);
}

void USettingsWidget::SetOwningMenu(UUserWidget* InMenu)
{
    OwningMenu = InMenu;
}

void USettingsWidget::NativeConstruct()
{
    Super::NativeConstruct();

    PendingResolution = FIntPoint::ZeroValue;
    PendingQuality = 0;
    PendingEnemyTurnDelay = 0.75f;
    PendingBattleActionDelay = 0.5f;

    if (ApplyButton)
    {
        ApplyButton->OnClicked.AddDynamic(this, &USettingsWidget::OnApply);
    }
    if (MainMenuButton)
    {
        MainMenuButton->OnClicked.AddDynamic(this, &USettingsWidget::OnMainMenu);
    }

    if (DisplaySizeCombo)
    {
        DisplaySizeCombo->ClearOptions();
        ResolutionMap.Empty();
        ResolutionMap.Add(TEXT("1280x720"), FIntPoint(1280, 720));
        ResolutionMap.Add(TEXT("1920x1080"), FIntPoint(1920, 1080));
        ResolutionMap.Add(TEXT("2560x1440"), FIntPoint(2560, 1440));
        ResolutionMap.Add(TEXT("3840x2160"), FIntPoint(3840, 2160));
        for (const TPair<FString, FIntPoint>& Pair : ResolutionMap)
        {
            DisplaySizeCombo->AddOption(Pair.Key);
        }
        DisplaySizeCombo->OnSelectionChanged.AddDynamic(this, &USettingsWidget::HandleResolutionChanged);
    }

    if (GraphicsQualityCombo)
    {
        GraphicsQualityCombo->ClearOptions();
        QualityMap.Empty();
        QualityMap.Add(TEXT("Poor"), 0);
        QualityMap.Add(TEXT("Medium"), 1);
        QualityMap.Add(TEXT("High"), 2);
        QualityMap.Add(TEXT("Max"), 3);
        for (const TPair<FString, int32>& Pair : QualityMap)
        {
            GraphicsQualityCombo->AddOption(Pair.Key);
        }
        GraphicsQualityCombo->OnSelectionChanged.AddDynamic(this, &USettingsWidget::HandleQualityChanged);
    }

    if (AudioSlider)
    {
        AudioSlider->OnValueChanged.AddDynamic(this, &USettingsWidget::HandleAudioChanged);
        if (MasterSoundClass)
        {
            AudioSlider->SetValue(MasterSoundClass->Properties.Volume);
            PendingAudioVolume = MasterSoundClass->Properties.Volume;
        }
        else
        {
            PendingAudioVolume = AudioSlider->GetValue();
        }
    }

    if (USkaldGameUserSettings* SkaldSettings = USkaldGameUserSettings::GetSkaldGameUserSettings())
    {
        PendingEnemyTurnDelay = SkaldSettings->GetEnemyTurnStepDelay();
        PendingBattleActionDelay = SkaldSettings->GetBattleActionDelay();
    }

    if (EnemyTurnDelaySlider)
    {
        EnemyTurnDelaySlider->OnValueChanged.AddDynamic(this, &USettingsWidget::HandleEnemyTurnDelayChanged);
        EnemyTurnDelaySlider->SetValue(PendingEnemyTurnDelay);
    }

    if (BattleActionDelaySlider)
    {
        BattleActionDelaySlider->OnValueChanged.AddDynamic(this, &USettingsWidget::HandleBattleActionDelayChanged);
        BattleActionDelaySlider->SetValue(PendingBattleActionDelay);
    }

    if (UGameUserSettings* SkaldSettings = USkaldGameUserSettings::GetSkaldGameUserSettings())
    {
        PendingResolution = SkaldSettings->GetScreenResolution();
        const FString CurrentRes = FString::Printf(TEXT("%dx%d"), PendingResolution.X, PendingResolution.Y);
        if (DisplaySizeCombo)
        {
            DisplaySizeCombo->SetSelectedOption(CurrentRes);
        }

        PendingQuality = SkaldSettings->GetOverallScalabilityLevel();
        if (GraphicsQualityCombo)
        {
            if (const FString* Quality = QualityMap.FindKey(PendingQuality))
            {
                GraphicsQualityCombo->SetSelectedOption(*Quality);
            }
        }
    }
    else if (UGameUserSettings* EngineSettings = GEngine->GetGameUserSettings())
    {
        PendingResolution = EngineSettings->GetScreenResolution();
        const FString CurrentRes = FString::Printf(TEXT("%dx%d"), PendingResolution.X, PendingResolution.Y);
        if (DisplaySizeCombo)
        {
            DisplaySizeCombo->SetSelectedOption(CurrentRes);
        }

        PendingQuality = EngineSettings->GetOverallScalabilityLevel();
        if (GraphicsQualityCombo)
        {
            if (const FString* Quality = QualityMap.FindKey(PendingQuality))
            {
                GraphicsQualityCombo->SetSelectedOption(*Quality);
            }
        }
    }
}

void USettingsWidget::OnApply()
{
    if (USkaldGameUserSettings* SkaldSettings = USkaldGameUserSettings::GetSkaldGameUserSettings())
    {
        SkaldSettings->SetScreenResolution(PendingResolution);
        SkaldSettings->SetOverallScalabilityLevel(PendingQuality);
        SkaldSettings->SetEnemyTurnStepDelay(PendingEnemyTurnDelay);
        SkaldSettings->SetBattleActionDelay(PendingBattleActionDelay);
        SkaldSettings->ApplySettings(false);
        SkaldSettings->SaveSettings();
    }
    else if (UGameUserSettings* EngineSettings = GEngine->GetGameUserSettings())
    {
        EngineSettings->SetScreenResolution(PendingResolution);
        EngineSettings->SetOverallScalabilityLevel(PendingQuality);
        EngineSettings->ApplySettings(false);
        EngineSettings->SaveSettings();
    }

    if (MasterSoundMix && MasterSoundClass)
    {
        UGameplayStatics::SetSoundMixClassOverride(this, MasterSoundMix, MasterSoundClass, PendingAudioVolume, 1.0f, 0.0f, true);
        UGameplayStatics::PushSoundMixModifier(this, MasterSoundMix);
    }
}

void USettingsWidget::OnMainMenu()
{
    // Hide the settings widget before showing the main menu again
    SetVisibility(ESlateVisibility::Hidden);
    RemoveFromParent();

    if (OwningMenu.IsValid())
    {
        if (UInGameMenuWidget* Menu = Cast<UInGameMenuWidget>(OwningMenu.Get()))
        {
            Menu->HandleSubMenuClosed(this);
        }
        OwningMenu->SetVisibility(ESlateVisibility::Visible);
    }
}

void USettingsWidget::HandleResolutionChanged(FString SelectedItem, ESelectInfo::Type /*SelectionType*/)
{
    if (const FIntPoint* Res = ResolutionMap.Find(SelectedItem))
    {
        PendingResolution = *Res;
    }
}

void USettingsWidget::HandleQualityChanged(FString SelectedItem, ESelectInfo::Type /*SelectionType*/)
{
    if (const int32* Quality = QualityMap.Find(SelectedItem))
    {
        PendingQuality = *Quality;
    }
}

void USettingsWidget::HandleAudioChanged(float Value)
{
    PendingAudioVolume = Value;
    if (MasterSoundClass)
    {
        MasterSoundClass->Properties.Volume = Value;
    }
}

void USettingsWidget::HandleEnemyTurnDelayChanged(float Value)
{
    PendingEnemyTurnDelay = FMath::Max(0.0f, Value);
}

void USettingsWidget::HandleBattleActionDelayChanged(float Value)
{
    PendingBattleActionDelay = FMath::Max(0.0f, Value);
}

