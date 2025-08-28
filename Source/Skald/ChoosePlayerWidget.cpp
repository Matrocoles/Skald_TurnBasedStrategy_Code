#include "ChoosePlayerWidget.h"

#include "Components/Button.h"
#include "Components/ComboBoxString.h"
#include "Components/EditableTextBox.h"
#include "Skald_GameInstance.h"
#include "Skald_PlayerController.h"

void UChoosePlayerWidget::NativeConstruct()
{
    Super::NativeConstruct();

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
                if (EnumPtr->HasMetaData(TEXT("Hidden"), i))
                {
                    continue;
                }

                ESkaldFaction Fac = static_cast<ESkaldFaction>(EnumPtr->GetValueByIndex(i));
                if (Fac == ESkaldFaction::None || Taken.Contains(Fac))
                {
                    continue;
                }
                const FString Name = EnumPtr->GetNameStringByIndex(i);
                FactionComboBox->AddOption(Name);
            }
        }

        FactionComboBox->OnSelectionChanged.AddDynamic(this, &UChoosePlayerWidget::HandleFactionSelected);
    }

    if (DisplayNameBox)
    {
        DisplayNameBox->OnTextChanged.AddDynamic(this, &UChoosePlayerWidget::HandleDisplayNameChanged);
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

    if (UWorld* World = GetWorld())
    {
        if (USkaldGameInstance* GI = World->GetGameInstance<USkaldGameInstance>())
        {
            GI->DisplayName = Name;
            GI->Faction = Faction;
            if (Faction != ESkaldFaction::None)
            {
                GI->TakenFactions.AddUnique(Faction);
            }
        }
    }

    if (ASkaldPlayerController* PC = Cast<ASkaldPlayerController>(GetOwningPlayer()))
    {
        PC->ServerInitPlayerState(Name, Faction);
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

void UChoosePlayerWidget::UpdateLockInEnabled()
{
    const bool bHasName = DisplayNameBox && !DisplayNameBox->GetText().IsEmpty();
    const bool bHasFaction = FactionComboBox && !FactionComboBox->GetSelectedOption().IsEmpty();
    if (LockInButton)
    {
        LockInButton->SetIsEnabled(bHasName && bHasFaction);
    }
}

