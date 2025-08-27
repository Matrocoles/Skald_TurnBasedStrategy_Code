#include "StartGameWidget.h"

#include "Components/Button.h"
#include "Components/EditableTextBox.h"
#include "Components/ComboBoxString.h"
#include "Kismet/GameplayStatics.h"
#include "Skald_GameInstance.h"
#include "Skald_PlayerState.h"
#include "GameFramework/PlayerController.h"
#include "LobbyMenuWidget.h"

void UStartGameWidget::NativeConstruct()
{
    Super::NativeConstruct();

    if (DisplayNameBox)
    {
        DisplayNameBox->SetText(FText::GetEmpty());
        DisplayNameBox->OnTextChanged.AddDynamic(this, &UStartGameWidget::OnDisplayNameChanged);
    }

    if (FactionComboBox)
    {
        FactionComboBox->ClearOptions();
        if (UEnum* Enum = StaticEnum<ESkaldFaction>())
        {
            for (int32 i = 0; i < Enum->NumEnums(); ++i)
            {
                if (!Enum->HasMetaData(TEXT("Hidden"), i))
                {
                    const FString Option = Enum->GetNameStringByIndex(i);
                    if (Option != TEXT("None"))
                    {
                        FactionComboBox->AddOption(Option);
                    }
                }
            }
            if (USkaldGameInstance* GI = GetWorld()->GetGameInstance<USkaldGameInstance>())
            {
                for (ESkaldFaction Taken : GI->TakenFactions)
                {
                    const FString Option = Enum->GetNameStringByValue(static_cast<int64>(Taken));
                    FactionComboBox->RemoveOption(Option);
                }
            }
            FactionComboBox->SetSelectedIndex(INDEX_NONE);
        }
        FactionComboBox->OnSelectionChanged.AddDynamic(this, &UStartGameWidget::OnFactionChanged);
    }

    if (LockInButton)
    {
        LockInButton->OnClicked.AddDynamic(this, &UStartGameWidget::OnLockIn);
        LockInButton->SetIsEnabled(true);
    }

    if (SingleplayerButton)
    {
        SingleplayerButton->OnClicked.AddDynamic(this, &UStartGameWidget::OnSingleplayer);
        SingleplayerButton->SetIsEnabled(false);
        SingleplayerButton->SetVisibility(ESlateVisibility::Collapsed);
    }

    if (MultiplayerButton)
    {
        MultiplayerButton->OnClicked.AddDynamic(this, &UStartGameWidget::OnMultiplayer);
        MultiplayerButton->SetIsEnabled(false);
        MultiplayerButton->SetVisibility(ESlateVisibility::Collapsed);
    }

    if (MainMenuButton)
    {
        MainMenuButton->OnClicked.AddDynamic(this, &UStartGameWidget::OnMainMenu);
    }

    ValidateSelections();
}

void UStartGameWidget::OnSingleplayer()
{
    StartGame(false);
}

void UStartGameWidget::OnMultiplayer()
{
    StartGame(true);
}

void UStartGameWidget::OnMainMenu()
{
    RemoveFromParent();
    if (OwningLobbyMenu.IsValid())
    {
        OwningLobbyMenu->SetVisibility(ESlateVisibility::Visible);
    }
}

void UStartGameWidget::OnDisplayNameChanged(const FText& /*Text*/)
{
    ValidateSelections();
}

void UStartGameWidget::OnFactionChanged(FString /*SelectedItem*/, ESelectInfo::Type /*SelectionType*/)
{
    ValidateSelections();
}

void UStartGameWidget::ValidateSelections()
{
    if (LockInButton)
    {
        LockInButton->SetIsEnabled(true);
    }
}

void UStartGameWidget::OnLockIn()
{
    FString Name = DisplayNameBox ? DisplayNameBox->GetText().ToString() : FString();

    if (Name.IsEmpty())
    {
        if (APlayerController* PC = GetOwningPlayer())
        {
            if (APlayerState* PSBase = PC->PlayerState)
            {
                Name = FString::Printf(TEXT("Player%d"), PSBase->GetPlayerId());
            }
            else
            {
                Name = TEXT("Player");
            }
        }
    }

    ESkaldFaction Faction = ESkaldFaction::Human;
    if (FactionComboBox && FactionComboBox->GetSelectedIndex() != INDEX_NONE)
    {
        FString FactionName = FactionComboBox->GetSelectedOption();
        if (UEnum* Enum = StaticEnum<ESkaldFaction>())
        {
            int32 Value = Enum->GetValueByNameString(FactionName);
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
            GI->TakenFactions.AddUnique(Faction);
            GI->OnFactionsUpdated.Broadcast();
        }
    }

    if (APlayerController* PC = GetOwningPlayer())
    {
        if (ASkaldPlayerState* PS = PC->GetPlayerState<ASkaldPlayerState>())
        {
            PS->DisplayName = Name;
            PS->Faction = Faction;
        }
    }

    if (DisplayNameBox)
    {
        DisplayNameBox->SetVisibility(ESlateVisibility::Collapsed);
    }

    if (FactionComboBox)
    {
        FactionComboBox->SetVisibility(ESlateVisibility::Collapsed);
    }

    if (LockInButton)
    {
        LockInButton->SetVisibility(ESlateVisibility::Collapsed);
    }

    if (SingleplayerButton)
    {
        SingleplayerButton->SetIsEnabled(true);
        SingleplayerButton->SetVisibility(ESlateVisibility::Visible);
    }

    if (MultiplayerButton)
    {
        MultiplayerButton->SetIsEnabled(true);
        MultiplayerButton->SetVisibility(ESlateVisibility::Visible);
    }
}

void UStartGameWidget::StartGame(bool bMultiplayer)
{
    if (UWorld* World = GetWorld())
    {
        if (USkaldGameInstance* GI = World->GetGameInstance<USkaldGameInstance>())
        {
            GI->bIsMultiplayer = bMultiplayer;
        }

        if (APlayerController* PC = GetOwningPlayer())
        {
            PC->SetInputMode(FInputModeGameOnly());
            PC->bShowMouseCursor = false;
            PC->bEnableClickEvents = false;
            PC->bEnableMouseOverEvents = false;

            FName LevelName(TEXT("/Game/Blueprints/Maps/OverviewMap"));
            FString Options;
            if (bMultiplayer)
            {
                Options = TEXT("listen");
            }
            UGameplayStatics::OpenLevel(this, LevelName, true, Options);
        }
    }
}

