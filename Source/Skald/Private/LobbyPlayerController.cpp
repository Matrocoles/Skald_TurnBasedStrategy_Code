#include "LobbyPlayerController.h"
#include "Blueprint/UserWidget.h"
#include "Engine/LocalPlayer.h"
#include "LobbyGameMode.h"
#include "LobbyGameState.h"
#include "LobbyMenuWidget.h"
#include "LobbySessionWidget.h"
#include "Skald_GameInstance.h"
#include "Skald_PlayerState.h"
#include "UI/TitleScreenWidget.h"
#include "UI/SkaldUIHelpers.h"

ALobbyPlayerController::ALobbyPlayerController()
{
    LobbyWidgetClass = ULobbyMenuWidget::StaticClass();
    TitleScreenWidgetClass = UTitleScreenWidget::StaticClass();
    LobbySessionWidgetClass = ULobbySessionWidget::StaticClass();
}

void ALobbyPlayerController::BeginPlay()
{
    Super::BeginPlay();
    if (IsLocalController())
    {
        InitLobbyUI();
    }
}

void ALobbyPlayerController::InitLobbyUI()
{
    ULocalPlayer* LocalPlayer = GetLocalPlayer();
    if (!IsLocalController() || !IsLocalPlayerController() || !LocalPlayer || Player != LocalPlayer)
    {
        return;
    }

    USkaldGameInstance* GI = GetGameInstance<USkaldGameInstance>();
    const bool bInMultiplayerLobby = GI && GI->bIsMultiplayer;

    if (bInMultiplayerLobby)
    {
        if (!LobbySessionWidgetInstance && LobbySessionWidgetClass)
        {
            LobbySessionWidgetInstance = CreateWidget<ULobbySessionWidget>(this, LobbySessionWidgetClass);
            if (LobbySessionWidgetInstance)
            {
                LobbySessionWidgetInstance->AddToViewport();
                FocusWidgetUIOnly(this, LobbySessionWidgetInstance);
            }
        }

        if (LobbyWidgetInstance)
        {
            LobbyWidgetInstance->RemoveFromParent();
            LobbyWidgetInstance = nullptr;
        }
    }
    else
    {
        if (!LobbyWidgetInstance && LobbyWidgetClass)
        {
            LobbyWidgetInstance = CreateWidget<ULobbyMenuWidget>(this, LobbyWidgetClass);
            if (LobbyWidgetInstance)
            {
                LobbyWidgetInstance->AddToViewport();
                LobbyWidgetInstance->SetVisibility(ESlateVisibility::Hidden);
            }
        }

        bool bShowingTitleScreen = false;
        if (!TitleScreenWidgetInstance && TitleScreenWidgetClass)
        {
            TitleScreenWidgetInstance = CreateWidget<UTitleScreenWidget>(this, TitleScreenWidgetClass);
            if (TitleScreenWidgetInstance)
            {
                TitleScreenWidgetInstance->OnDismissed.AddDynamic(this, &ALobbyPlayerController::HandleTitleScreenDismissed);
                TitleScreenWidgetInstance->AddToViewport(1);
                FocusWidgetUIOnly(this, TitleScreenWidgetInstance);
                bShowingTitleScreen = true;
            }
        }

        if (!bShowingTitleScreen && LobbyWidgetInstance)
        {
            LobbyWidgetInstance->SetVisibility(ESlateVisibility::Visible);
            FocusWidgetUIOnly(this, LobbyWidgetInstance);
        }

        if (LobbySessionWidgetInstance)
        {
            LobbySessionWidgetInstance->RemoveFromParent();
            LobbySessionWidgetInstance = nullptr;
        }
    }

    bShowMouseCursor = true;
    bEnableClickEvents = true;
    bEnableMouseOverEvents = true;
}

void ALobbyPlayerController::HandleTitleScreenDismissed()
{
    TitleScreenWidgetInstance = nullptr;

    if (LobbyWidgetInstance)
    {
        LobbyWidgetInstance->SetVisibility(ESlateVisibility::Visible);
        FocusWidgetUIOnly(this, LobbyWidgetInstance);
    }
}

void ALobbyPlayerController::RequestPlayerCount(int32 PlayerCount)
{
    if (IsLocalController() && IsLocalPlayerLobbyHost())
    {
        ServerSetPlayerCount(PlayerCount);
    }
}

void ALobbyPlayerController::RequestAICount(int32 AICount)
{
    if (IsLocalController() && IsLocalPlayerLobbyHost())
    {
        ServerSetAICount(AICount);
    }
}

void ALobbyPlayerController::RequestLockIn()
{
    if (!IsLocalController())
    {
        return;
    }

    if (const UWorld* World = GetWorld())
    {
        if (const ALobbyGameState* LobbyState = World->GetGameState<ALobbyGameState>())
        {
            if (const ASkaldPlayerState* LocalPlayerState = GetPlayerState<ASkaldPlayerState>())
            {
                const int32 SlotIndex = LobbyState->FindSlotIndexForPlayer(LocalPlayerState->GetPlayerId());
                if (const FLobbyPlayerSlot* Slot = LobbyState->GetSlot(SlotIndex))
                {
                    if (Slot->bIsReady)
                    {
                        return;
                    }
                }
            }
        }
    }

    ServerLockInSelection();
}

void ALobbyPlayerController::RequestFactionSelection(ESkaldFaction Faction)
{
    if (IsLocalController())
    {
        if (USkaldGameInstance* GI = GetGameInstance<USkaldGameInstance>())
        {
            GI->Faction = Faction;
        }
        ServerSetFaction(Faction);
    }
}

void ALobbyPlayerController::RequestDisplayNameUpdate(const FString& DisplayName)
{
    if (IsLocalController())
    {
        if (USkaldGameInstance* GI = GetGameInstance<USkaldGameInstance>())
        {
            GI->DisplayName = DisplayName;
        }
        ServerSetDisplayName(DisplayName);
    }
}

void ALobbyPlayerController::RequestLaunch()
{
    if (IsLocalController() && IsLocalPlayerLobbyHost())
    {
        ServerLaunchMatch();
    }
}

void ALobbyPlayerController::ServerSetPlayerCount_Implementation(int32 PlayerCount)
{
    if (!IsLobbyHostOnServer())
    {
        return;
    }

    if (ALobbyGameMode* GM = GetWorld() ? GetWorld()->GetAuthGameMode<ALobbyGameMode>() : nullptr)
    {
        GM->SetTotalSlots(PlayerCount);
    }
}

void ALobbyPlayerController::ServerSetAICount_Implementation(int32 AICount)
{
    if (!IsLobbyHostOnServer())
    {
        return;
    }

    if (ALobbyGameMode* GM = GetWorld() ? GetWorld()->GetAuthGameMode<ALobbyGameMode>() : nullptr)
    {
        GM->SetAISlots(AICount);
    }
}

void ALobbyPlayerController::ServerSetFaction_Implementation(ESkaldFaction Faction)
{
    if (ALobbyGameMode* GM = GetWorld() ? GetWorld()->GetAuthGameMode<ALobbyGameMode>() : nullptr)
    {
        if (ASkaldPlayerState* PS = GetPlayerState<ASkaldPlayerState>())
        {
            GM->SetPlayerFaction(PS->GetPlayerId(), Faction);
        }
    }
}

void ALobbyPlayerController::ServerSetDisplayName_Implementation(const FString& DisplayName)
{
    if (ALobbyGameMode* GM = GetWorld() ? GetWorld()->GetAuthGameMode<ALobbyGameMode>() : nullptr)
    {
        if (ASkaldPlayerState* PS = GetPlayerState<ASkaldPlayerState>())
        {
            GM->SetPlayerDisplayName(PS->GetPlayerId(), DisplayName);
        }
    }
}

void ALobbyPlayerController::ServerLaunchMatch_Implementation()
{
    if (!IsLobbyHostOnServer())
    {
        return;
    }

    if (ALobbyGameMode* GM = GetWorld() ? GetWorld()->GetAuthGameMode<ALobbyGameMode>() : nullptr)
    {
        GM->TryLaunchMatch(this);
    }
}

void ALobbyPlayerController::ServerLockInSelection_Implementation()
{
    if (ALobbyGameMode* GM = GetWorld() ? GetWorld()->GetAuthGameMode<ALobbyGameMode>() : nullptr)
    {
        if (ASkaldPlayerState* PS = GetPlayerState<ASkaldPlayerState>())
        {
            GM->LockInPlayer(PS->GetPlayerId());
        }
    }
}

bool ALobbyPlayerController::IsLocalPlayerLobbyHost() const
{
    if (const UWorld* World = GetWorld())
    {
        if (const ALobbyGameState* LobbyState = World->GetGameState<ALobbyGameState>())
        {
            if (const ASkaldPlayerState* LocalPlayerState = GetPlayerState<ASkaldPlayerState>())
            {
                const int32 SlotIndex = LobbyState->FindSlotIndexForPlayer(LocalPlayerState->GetPlayerId());
                if (SlotIndex != INDEX_NONE)
                {
                    if (const FLobbyPlayerSlot* Slot = LobbyState->GetSlot(SlotIndex))
                    {
                        return Slot->bIsActive && !Slot->bIsAI && SlotIndex == 0;
                    }
                }
            }
        }
        else if (HasAuthority() || World->GetNetMode() == NM_Standalone)
        {
            return true;
        }
    }

    return false;
}

bool ALobbyPlayerController::IsLobbyHostOnServer() const
{
    if (const UWorld* World = GetWorld())
    {
        if (World->GetNetMode() == NM_Standalone)
        {
            return true;
        }

        if (const ALobbyGameState* LobbyState = World->GetGameState<ALobbyGameState>())
        {
            if (const ASkaldPlayerState* LocalPlayerState = GetPlayerState<ASkaldPlayerState>())
            {
                const int32 SlotIndex = LobbyState->FindSlotIndexForPlayer(LocalPlayerState->GetPlayerId());
                if (SlotIndex == 0)
                {
                    if (const FLobbyPlayerSlot* Slot = LobbyState->GetSlot(SlotIndex))
                    {
                        return Slot->bIsActive && !Slot->bIsAI;
                    }
                }
            }
        }
    }

    return false;
}
