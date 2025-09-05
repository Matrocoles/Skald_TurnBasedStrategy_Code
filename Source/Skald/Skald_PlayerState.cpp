#include "Skald_PlayerState.h"
#include "Net/UnrealNetwork.h"
#include "Engine/World.h"
#include "Skald_GameState.h"
#include "Skald_PlayerController.h"
#include "UI/SkaldMainHUDWidget.h"

ASkaldPlayerState::ASkaldPlayerState()
    : DeployableUnits(0)
    , InitiativeRoll(0)
    , Resources(0)
    , PlayerDisplayName(TEXT("Player"))
    , Faction(ESkaldFaction::None)
    , bHasLockedIn(false)
    , IsEliminated(false)
{
}

void ASkaldPlayerState::GetLifetimeReplicatedProps(
    TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);

    DOREPLIFETIME(ASkaldPlayerState, PlayerDisplayName);
    DOREPLIFETIME(ASkaldPlayerState, Faction);
    DOREPLIFETIME(ASkaldPlayerState, DeployableUnits);
    DOREPLIFETIME(ASkaldPlayerState, InitiativeRoll);
    DOREPLIFETIME(ASkaldPlayerState, Resources);
    DOREPLIFETIME(ASkaldPlayerState, bHasLockedIn);
    DOREPLIFETIME(ASkaldPlayerState, IsEliminated);
}

void ASkaldPlayerState::OnRep_DeployableUnits()
{
    if (APlayerController* PC = GetOwner<APlayerController>())
    {
        if (ASkaldPlayerController* SkaldPC = Cast<ASkaldPlayerController>(PC))
        {
            if (USkaldMainHUDWidget* HUD = SkaldPC->GetHUDWidget())
            {
                HUD->UpdateDeployableUnits(DeployableUnits);
            }
        }
    }
}

void ASkaldPlayerState::OnRep_HasLockedIn()
{
    if (UWorld* World = GetWorld())
    {
        if (ASkaldGameState* GS = World->GetGameState<ASkaldGameState>())
        {
            GS->OnPlayersUpdated.Broadcast();
        }
    }
}

void ASkaldPlayerState::OnRep_IsEliminated()
{
    if (UWorld* World = GetWorld())
    {
        if (ASkaldGameState* GS = World->GetGameState<ASkaldGameState>())
        {
            GS->OnPlayersUpdated.Broadcast();
        }
    }
}

void ASkaldPlayerState::OnRep_PlayerDisplayName()
{
    if (UWorld* World = GetWorld())
    {
        if (ASkaldGameState* GS = World->GetGameState<ASkaldGameState>())
        {
            GS->OnPlayersUpdated.Broadcast();
        }
    }
}

