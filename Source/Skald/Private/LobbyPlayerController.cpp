#include "LobbyPlayerController.h"
#include "Blueprint/UserWidget.h"
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
    if (IsLocalController())
    {
        ServerSetPlayerCount(PlayerCount);
    }
}

void ALobbyPlayerController::RequestAICount(int32 AICount)
{
    if (IsLocalController())
    {
        ServerSetAICount(AICount);
    }
}

void ALobbyPlayerController::ToggleReadyState(bool bReady)
{
    if (IsLocalController())
    {
        ServerSetReady(bReady);
    }
}

void ALobbyPlayerController::RequestFactionSelection(ESkaldFaction Faction)
{
    if (IsLocalController())
    {
        ServerSetFaction(Faction);
    }
}

void ALobbyPlayerController::RequestDisplayNameUpdate(const FString& DisplayName)
{
    if (IsLocalController())
    {
        ServerSetDisplayName(DisplayName);
    }
}

void ALobbyPlayerController::RequestLaunch()
{
    if (IsLocalController())
    {
        ServerLaunchMatch();
    }
}

void ALobbyPlayerController::ServerSetPlayerCount_Implementation(int32 PlayerCount)
{
    if (!IsLocalController())
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
    if (!IsLocalController())
    {
        return;
    }

    if (ALobbyGameMode* GM = GetWorld() ? GetWorld()->GetAuthGameMode<ALobbyGameMode>() : nullptr)
    {
        GM->SetAISlots(AICount);
    }
}

void ALobbyPlayerController::ServerSetReady_Implementation(bool bReady)
{
    if (ALobbyGameMode* GM = GetWorld() ? GetWorld()->GetAuthGameMode<ALobbyGameMode>() : nullptr)
    {
        if (ASkaldPlayerState* PS = GetPlayerState<ASkaldPlayerState>())
        {
            GM->SetPlayerReady(PS->GetPlayerId(), bReady);
        }
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
    if (!IsLocalController())
    {
        return;
    }

    if (ALobbyGameMode* GM = GetWorld() ? GetWorld()->GetAuthGameMode<ALobbyGameMode>() : nullptr)
    {
        GM->TryLaunchMatch(this);
    }
}
