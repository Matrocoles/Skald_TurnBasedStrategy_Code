#include "SettingsWidget.h"
#include "Components/Button.h"
#include "Components/ComboBoxString.h"
#include "Components/Slider.h"
#include "GameFramework/GameUserSettings.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/Engine.h"
#include "LobbyMenuWidget.h"
#include "Sound/SoundClass.h"
#include "Sound/SoundMix.h"

void USettingsWidget::SetLobbyMenu(ULobbyMenuWidget* InMenu)
{
    LobbyMenu = InMenu;
}

void USettingsWidget::NativeConstruct()
{
    Super::NativeConstruct();

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

    if (UGameUserSettings* Settings = GEngine->GetGameUserSettings())
    {
        PendingResolution = Settings->GetScreenResolution();
        const FString CurrentRes = FString::Printf(TEXT("%dx%d"), PendingResolution.X, PendingResolution.Y);
        if (DisplaySizeCombo)
        {
            DisplaySizeCombo->SetSelectedOption(CurrentRes);
        }

        PendingQuality = Settings->GetOverallScalabilityLevel();
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
    if (UGameUserSettings* Settings = GEngine->GetGameUserSettings())
    {
        Settings->SetScreenResolution(PendingResolution);
        Settings->SetOverallScalabilityLevel(PendingQuality);
        Settings->ApplySettings(false);
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

    if (LobbyMenu.IsValid())
    {
        LobbyMenu->SetVisibility(ESlateVisibility::Visible);
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

