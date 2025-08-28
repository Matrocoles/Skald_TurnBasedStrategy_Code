#include "ChoosePlayerWidget.h"

#include "Components/Button.h"
#include "Components/ComboBoxString.h"
#include "Components/EditableTextBox.h"
#include "Skald_GameInstance.h"
#include "Skald_PlayerController.h"

void UChoosePlayerWidget::NativeConstruct()
{
    Super::NativeConstruct();

    if (LockInButton)
    {
        LockInButton->OnClicked.AddDynamic(this, &UChoosePlayerWidget::OnLockIn);
    }
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
        }
    }

    if (ASkaldPlayerController* PC = Cast<ASkaldPlayerController>(GetOwningPlayer()))
    {
        PC->ServerInitPlayerState(Name, Faction);
    }

    OnPlayerLockedIn.Broadcast();
}

