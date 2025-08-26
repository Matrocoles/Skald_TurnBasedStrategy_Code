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
    const bool bHasName = DisplayNameBox && !DisplayNameBox->GetText().IsEmpty();
    const bool bHasFaction = FactionComboBox && FactionComboBox->GetSelectedIndex() != INDEX_NONE;

    const bool bEnable = bHasName && bHasFaction;

    if (SingleplayerButton)
    {
        SingleplayerButton->SetIsEnabled(bEnable);
        SingleplayerButton->SetVisibility(bEnable ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
    }

    if (MultiplayerButton)
    {
        MultiplayerButton->SetIsEnabled(bEnable);
        MultiplayerButton->SetVisibility(bEnable ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
    }
}

void UStartGameWidget::StartGame(bool bMultiplayer)
{
    FString Name = DisplayNameBox ? DisplayNameBox->GetText().ToString() : TEXT("Player");
    FString FactionName = FactionComboBox ? FactionComboBox->GetSelectedOption() : TEXT("None");

    ESkaldFaction Faction = ESkaldFaction::None;
    if (UEnum* Enum = StaticEnum<ESkaldFaction>())
    {
        int32 Value = Enum->GetValueByNameString(FactionName);
        if (Value != INDEX_NONE)
        {
            Faction = static_cast<ESkaldFaction>(Value);
        }
    }

    if (UWorld* World = GetWorld())
    {
        if (USkaldGameInstance* GI = World->GetGameInstance<USkaldGameInstance>())
        {
            GI->DisplayName = Name;
            GI->Faction = Faction;
            GI->bIsMultiplayer = bMultiplayer;
            GI->TakenFactions.AddUnique(Faction);
        }

        if (APlayerController* PC = GetOwningPlayer())
        {
            if (ASkaldPlayerState* PS = PC->GetPlayerState<ASkaldPlayerState>())
            {
                PS->DisplayName = Name;
                PS->Faction = Faction;
            }

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

