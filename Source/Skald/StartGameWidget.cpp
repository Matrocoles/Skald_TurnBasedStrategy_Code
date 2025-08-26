#include "StartGameWidget.h"
#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/EditableTextBox.h"
#include "Components/ComboBoxString.h"
#include "Blueprint/WidgetTree.h"
#include "Components/EditableTextBox.h"
#include "Components/ComboBoxString.h"
#include "Kismet/GameplayStatics.h"
#include "Skald_GameInstance.h"
#include "Skald_PlayerState.h"
#include "GameFramework/PlayerController.h"
#include "UObject/UnrealType.h"

void UStartGameWidget::NativeConstruct()
{
    Super::NativeConstruct();

    if (DisplayNameBox)
    {
        DisplayNameBox->SetText(FText::FromString(TEXT("Player")));
    }

        DisplayNameBox = WidgetTree->ConstructWidget<UEditableTextBox>(UEditableTextBox::StaticClass());
        DisplayNameBox->SetText(FText::FromString(TEXT("Player")));
        Root->AddChild(DisplayNameBox);

        FactionComboBox = WidgetTree->ConstructWidget<UComboBoxString>(UComboBoxString::StaticClass());
        if (UEnum* Enum = StaticEnum<ESkaldFaction>())
        {
            for (int32 i = 0; i < Enum->NumEnums(); ++i)
            {
                if (!Enum->HasMetaData(TEXT("Hidden"), i))
                {
                    FactionComboBox->AddOption(Enum->GetNameStringByIndex(i));
                }
            }
            FactionComboBox->SetSelectedIndex(0);
        }
        Root->AddChild(FactionComboBox);

        auto AddButton = [this, Root](const FString& Label, const FName& FuncName)
    if (FactionComboBox)
    {
        FactionComboBox->ClearOptions();
        if (UEnum* Enum = StaticEnum<ESkaldFaction>())
        {
            for (int32 i = 0; i < Enum->NumEnums(); ++i)
            {
                if (!Enum->HasMetaData(TEXT("Hidden"), i))
                {
                    FactionComboBox->AddOption(Enum->GetNameStringByIndex(i));
                }
            }
            FactionComboBox->SetSelectedIndex(0);
        }
    }
}

void UStartGameWidget::OnSingleplayer()
{
    StartGame(false);
}

void UStartGameWidget::OnMultiplayer()
{
    StartGame(true);
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
        }

        if (APlayerController* PC = GetOwningPlayer())
        {
            if (ASkaldPlayerState* PS = PC->GetPlayerState<ASkaldPlayerState>())
            {
                PS->DisplayName = Name;
                PS->Faction = Faction;
            }

            // Load the correct gameplay map
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

