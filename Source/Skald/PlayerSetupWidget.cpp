#include "PlayerSetupWidget.h"
#include "Skald_GameInstance.h"
#include "Skald_PlayerState.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/PlayerController.h"

void UPlayerSetupWidget::NativeConstruct()
{
    Super::NativeConstruct();
    // UI is expected to be created in Blueprint or elsewhere.
    // Defaults
    SelectedFaction = ESkaldFaction::None;
    DisplayName = TEXT("Player");
    bMultiplayer = false;
}

void UPlayerSetupWidget::OnFactionSelected(ESkaldFaction NewFaction)
{
    SelectedFaction = NewFaction;
}

void UPlayerSetupWidget::OnConfirm()
{
    if (UWorld* World = GetWorld())
    {
        if (USkaldGameInstance* GI = World->GetGameInstance<USkaldGameInstance>())
        {
            GI->DisplayName = DisplayName;
            GI->Faction = SelectedFaction;
            GI->bIsMultiplayer = bMultiplayer;
        }

        if (APlayerController* PC = GetOwningPlayer())
        {
            if (ASkaldPlayerState* PS = PC->GetPlayerState<ASkaldPlayerState>())
            {
                PS->DisplayName = DisplayName;
                PS->Faction = SelectedFaction;
            }

            PC->SetInputMode(FInputModeGameOnly());
            PC->bShowMouseCursor = false;
            PC->bEnableClickEvents = false;
            PC->bEnableMouseOverEvents = false;

            // Launch the main gameplay map once setup is confirmed
            FName LevelName(TEXT("Skald_OverTop"));
            FString Options;
            if (bMultiplayer)
            {
                Options = TEXT("listen");
            }
            UGameplayStatics::OpenLevel(this, LevelName, true, Options);
        }
    }
}

