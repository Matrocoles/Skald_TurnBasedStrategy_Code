#include "ChoosePlayerWidget.h"

#include "Components/Button.h"
#include "Components/ComboBoxString.h"
#include "Components/EditableTextBox.h"
#include "Components/SpinBox.h"
#include "Skald_EnumUtils.h"
#include "Skald_GameInstance.h"
#include "Skald_GameMode.h"
#include "Skald_PlayerController.h"

void UChoosePlayerWidget::NativeConstruct()
{
    Super::NativeConstruct();

    SetIsFocusable(true);
    SetFocus();

    if (FactionComboBox)
    {
        FactionComboBox->ClearOptions();

        TArray<ESkaldFaction> Taken;
        if (UWorld* World = GetWorld())
        {
            if (USkaldGameInstance* GI = World->GetGameInstance<USkaldGameInstance>())
            {
                Taken = GI->TakenFactions;
            }
        }

        if (UEnum* EnumPtr = StaticEnum<ESkaldFaction>())
        {
            for (int32 i = 0; i < EnumPtr->NumEnums(); ++i)
            {
                if (Skald::EnumUtils::IsHiddenEntry(EnumPtr, i))
                {
                    continue;
                }

                const FString EnumName = EnumPtr->GetNameStringByIndex(i);
                if (EnumName.EndsWith(TEXT("_MAX")))
                {
                    continue;
                }

                const int64 Value = EnumPtr->GetValueByIndex(i);
                if (!EnumPtr->IsValidEnumValue(Value))
                {
                    continue;
                }

                ESkaldFaction Fac = static_cast<ESkaldFaction>(Value);
                if (Fac == ESkaldFaction::None || Taken.Contains(Fac))
                {
                    continue;
                }
                FactionComboBox->AddOption(EnumName);
            }
        }
        FactionComboBox->ClearSelection();
        FactionComboBox->OnSelectionChanged.AddDynamic(this, &UChoosePlayerWidget::HandleFactionSelected);
    }

    if (DisplayNameBox)
    {
        DisplayNameBox->OnTextChanged.AddDynamic(this, &UChoosePlayerWidget::HandleDisplayNameChanged);
    }

    if (AICountSpinBox)
    {
        AICountSpinBox->SetMinValue(0.f);
        AICountSpinBox->SetMaxValue(3.f);
        AICountSpinBox->SetDelta(1.f);
        AICountSpinBox->SetValue(0.f);
        AICountSpinBox->OnValueChanged.AddDynamic(this, &UChoosePlayerWidget::HandleAICountChanged);
    }

    if (LockInButton)
    {
        LockInButton->OnClicked.AddDynamic(this, &UChoosePlayerWidget::OnLockIn);
    }

    UpdateLockInEnabled();
}

void UChoosePlayerWidget::OnLockIn()
{
    FString Name;
    if (DisplayNameBox)
    {
        Name = DisplayNameBox->GetText().ToString();
    }

    ESkaldFaction Faction = ESkaldFaction::None;
    if (FactionComboBox)
    {
        const FString Option = FactionComboBox->GetSelectedOption();
        if (UEnum* EnumPtr = StaticEnum<ESkaldFaction>())
        {
            const int64 Value = EnumPtr->GetValueByNameString(Option);
            if (Value != INDEX_NONE)
            {
                Faction = static_cast<ESkaldFaction>(Value);
            }
        }
    }

    int32 AICount = 1;
    if (AICountSpinBox)
    {
        AICount = FMath::Clamp(FMath::RoundToInt(AICountSpinBox->GetValue()), 1, 3);
    }

    if (UWorld* World = GetWorld())
    {
        if (USkaldGameInstance* GI = World->GetGameInstance<USkaldGameInstance>())
        {
            GI->DisplayName = Name;
            GI->Faction = Faction;
            GI->AIPlayersToSpawn = AICount;
            if (Faction != ESkaldFaction::None)
            {
                GI->TakenFactions.AddUnique(Faction);
            }
        }
    }

    if (ASkaldPlayerController* PC = Cast<ASkaldPlayerController>(GetOwningPlayer()))
    {
        bool bShouldInit = true;
        if (UWorld* World = PC->GetWorld())
        {
            if (ASkaldGameMode* GM = World->GetAuthGameMode<ASkaldGameMode>())
            {
                bShouldInit = !GM->IsWorldInitialized();
            }
        }

        if (bShouldInit)
        {
            PC->ServerInitPlayerState(Name, Faction, AICount);
        }
    }

    OnPlayerLockedIn.Broadcast();
}

void UChoosePlayerWidget::HandleDisplayNameChanged(const FText& /*Text*/)
{
    UpdateLockInEnabled();
}

void UChoosePlayerWidget::HandleFactionSelected(FString /*SelectedItem*/, ESelectInfo::Type /*SelectionType*/)
{
    UpdateLockInEnabled();
}

void UChoosePlayerWidget::HandleAICountChanged(float /*Value*/)
{
    UpdateLockInEnabled();
}

void UChoosePlayerWidget::UpdateLockInEnabled()
{
    const bool bHasName = DisplayNameBox && !DisplayNameBox->GetText().IsEmpty();
    const bool bHasFaction = FactionComboBox && !FactionComboBox->GetSelectedOption().IsEmpty();
    const bool bHasAI = AICountSpinBox && AICountSpinBox->GetValue() >= 1.f;
    if (LockInButton)
    {
        LockInButton->SetIsEnabled(bHasName && bHasFaction && bHasAI);
    }
}

