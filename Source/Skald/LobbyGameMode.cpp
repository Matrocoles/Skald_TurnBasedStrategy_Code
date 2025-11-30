#include "LobbyGameMode.h"

#include "Algo/RandomShuffle.h"
#include "LobbyGameState.h"
#include "Skald_GameInstance.h"
#include "Skald_PlayerState.h"
#include "SkaldLogging.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Net/UnrealNetwork.h"
#include "Engine/World.h"

namespace
{
constexpr int32 MinLobbySlots = 2;
constexpr int32 MaxLobbySlots = 4;

FString ResolveLobbyDisplayName(ASkaldPlayerState* PlayerState,
                                const FString& Candidate)
{
    FString Resolved = Candidate;
    if (Resolved.IsEmpty() && PlayerState)
    {
        Resolved = PlayerState->PlayerDisplayName;
    }
    if (Resolved.IsEmpty() && PlayerState)
    {
        Resolved = PlayerState->GetPlayerName();
    }
    if (Resolved.IsEmpty() && PlayerState)
    {
        const int32 PlayerId = PlayerState->GetPlayerId();
        if (PlayerId > 0)
        {
            Resolved = FString::Printf(TEXT("Player %d"), PlayerId);
        }
    }
    if (Resolved.IsEmpty())
    {
        Resolved = TEXT("Player");
    }

    Resolved.TrimStartAndEndInline();
    return Resolved;
}

TArray<ESkaldFaction> BuildFactionPool()
{
    TArray<ESkaldFaction> Result;
    if (UEnum* Enum = StaticEnum<ESkaldFaction>())
    {
        for (int32 Index = 0; Index < Enum->NumEnums(); ++Index)
        {
            const int64 Value = Enum->GetValueByIndex(Index);
            if (!Enum->IsValidEnumValue(Value))
            {
                continue;
            }

            const FString Name = Enum->GetNameStringByIndex(Index);
            if (Name.EndsWith(TEXT("_MAX")))
            {
                continue;
            }

            const ESkaldFaction Faction = static_cast<ESkaldFaction>(Value);
            if (Faction != ESkaldFaction::None)
            {
                Result.Add(Faction);
            }
        }
    }

    return Result;
}

TArray<FString> BuildAINames()
{
    return {
        TEXT("Arin the Bold"),        TEXT("Ser Kaelis"),
        TEXT("Mira Stormweaver"),     TEXT("Thalen Duskborn"),
        TEXT("Eira Wolfsong"),        TEXT("Garruk Ironfist"),
        TEXT("Lyra Dawnsong"),        TEXT("Vorik the Stalwart"),
        TEXT("Selene Emberfall"),     TEXT("Hadrin Frostbane"),
        TEXT("Kaelen Nightbloom"),    TEXT("Vessa Dragonsworn"),
        TEXT("Orin Stoneshield"),     TEXT("Nyra Shadowstep"),
        TEXT("Borin Thunderhand"),    TEXT("Talia Ravensdottir"),
        TEXT("Fenric Ashwalker"),     TEXT("Seris Moonwhisper"),
        TEXT("Rothgar Flamebrand"),   TEXT("Elira Wyrmguard"),
        TEXT("Calen Starborn"),       TEXT("Lysa Snowblade"),
        TEXT("Dorian Stormrend"),     TEXT("Velka Ironheart")};
}
} // namespace

ALobbyGameMode::ALobbyGameMode()
    : CachedLobbyState(nullptr)
    , AuthorityTotalSlots(MinLobbySlots)
    , AuthorityAISlots(0)
    , bSlotConfigurationLocked(false)
    , bMatchLaunchInitiated(false)
{
    bUseSeamlessTravel = true;
    GameStateClass = ALobbyGameState::StaticClass();
    PlayerStateClass = ASkaldPlayerState::StaticClass();
    AuthoritySlots.SetNum(MaxLobbySlots);
    for (FLobbyPlayerSlot& Slot : AuthoritySlots)
    {
        Slot.PlayerId = INDEX_NONE;
    }
}

void ALobbyGameMode::BeginPlay()
{
    Super::BeginPlay();

    CachedLobbyState = GetGameState<ALobbyGameState>();
    if (!CachedLobbyState)
    {
        UE_LOG(LogSkald, Error, TEXT("LobbyGameMode missing LobbyGameState."));
        return;
    }

    AuthoritySlots.SetNum(MaxLobbySlots);
    RefreshReplicatedLobbyState();
}

void ALobbyGameMode::PostLogin(APlayerController* NewPlayer)
{
    Super::PostLogin(NewPlayer);

    ASkaldPlayerState* PlayerState = NewPlayer ? NewPlayer->GetPlayerState<ASkaldPlayerState>() : nullptr;
    if (!PlayerState)
    {
        return;
    }

    AssignPlayerToSlot(PlayerState);

    if (!bSlotConfigurationLocked)
    {
        if (AGameStateBase* GS = GetGameState<AGameStateBase>())
        {
            if (GS->PlayerArray.Num() > 1)
            {
                bSlotConfigurationLocked = true;
            }
        }
    }

    RefreshReplicatedLobbyState();
}

void ALobbyGameMode::Logout(AController* Exiting)
{
    const ASkaldPlayerState* PlayerState = Exiting ? Exiting->GetPlayerState<ASkaldPlayerState>() : nullptr;
    const int32 PlayerId = PlayerState ? PlayerState->GetPlayerId() : INDEX_NONE;
    if (PlayerId != INDEX_NONE)
    {
        RemovePlayerFromSlots(PlayerId);
    }

    Super::Logout(Exiting);

    if (AGameStateBase* GS = GetGameState<AGameStateBase>())
    {
        bSlotConfigurationLocked = GS->PlayerArray.Num() > 1;
    }
    else
    {
        bSlotConfigurationLocked = false;
    }

    RefreshReplicatedLobbyState();
}

void ALobbyGameMode::SetTotalSlots(int32 InTotalSlots)
{
    if (bSlotConfigurationLocked)
    {
        UE_LOG(LogSkald, Warning, TEXT("Slot configuration locked; ignoring SetTotalSlots."));
        return;
    }

    AuthorityTotalSlots = FMath::Clamp(InTotalSlots, MinLobbySlots, MaxLobbySlots);
    AuthorityAISlots = FMath::Clamp(AuthorityAISlots, 0, AuthorityTotalSlots - 1);
    RefreshReplicatedLobbyState();

    if (CachedLobbyState && CachedLobbyState->AreAllSlotsReady())
    {
        TryLaunchMatch(nullptr);
    }
}

void ALobbyGameMode::SetAISlots(int32 InAISlots)
{
    if (bSlotConfigurationLocked)
    {
        UE_LOG(LogSkald, Warning, TEXT("Slot configuration locked; ignoring SetAISlots."));
        return;
    }

    AuthorityAISlots = FMath::Clamp(InAISlots, 0, AuthorityTotalSlots - 1);
    RefreshReplicatedLobbyState();

    if (CachedLobbyState && CachedLobbyState->AreAllSlotsReady())
    {
        TryLaunchMatch(nullptr);
    }
}

void ALobbyGameMode::AssignPlayerToSlot(ASkaldPlayerState* PlayerState)
{
    if (!PlayerState)
    {
        return;
    }

    // Ensure existing assignment removed before reassigning.
    RemovePlayerFromSlots(PlayerState->GetPlayerId());

    const int32 HumanSlots = AuthorityTotalSlots - AuthorityAISlots;
    for (int32 Index = 0; Index < HumanSlots; ++Index)
    {
        FLobbyPlayerSlot& Slot = AuthoritySlots[Index];
        if (Slot.PlayerId == INDEX_NONE)
        {
            Slot.bIsActive = true;
            Slot.bIsAI = false;
            Slot.PlayerId = PlayerState->GetPlayerId();
            const FString ResolvedName = ResolveLobbyDisplayName(PlayerState, Slot.DisplayName);
            Slot.DisplayName = ResolvedName;
            if (PlayerState->PlayerDisplayName != ResolvedName)
            {
                PlayerState->PlayerDisplayName = ResolvedName;
            }
            if (PlayerState->GetPlayerName() != ResolvedName)
            {
                PlayerState->SetPlayerName(ResolvedName);
            }
            Slot.Faction = PlayerState->Faction;
            Slot.bIsReady = false;
            return;
        }
    }

    // No free slot; kick the player.
    if (AController* OwningController = PlayerState->GetOwner<AController>())
    {
        if (APlayerController* OwningPlayerController = Cast<APlayerController>(OwningController))
        {
            OwningPlayerController->ClientMessage(TEXT("Lobby is full."));
            OwningPlayerController->ClientReturnToMainMenuWithTextReason(FText::FromString(TEXT("Lobby is full.")));
        }
    }
}

bool ALobbyGameMode::LockInPlayer(int32 PlayerId)
{
    bool bUpdated = false;

    for (FLobbyPlayerSlot& Slot : AuthoritySlots)
    {
        if (Slot.PlayerId != PlayerId)
        {
            continue;
        }

        if (!Slot.bIsActive || Slot.bIsAI)
        {
            return false;
        }

        if (Slot.bIsReady)
        {
            return true;
        }

        if (Slot.Faction == ESkaldFaction::None)
        {
            if (AGameStateBase* GS = GetGameState<AGameStateBase>())
            {
                for (APlayerState* PSBase : GS->PlayerArray)
                {
                    if (ASkaldPlayerState* PlayerState = Cast<ASkaldPlayerState>(PSBase))
                    {
                        if (PlayerState->GetPlayerId() == PlayerId)
                        {
                            if (APlayerController* OwningPC = PlayerState->GetOwner<APlayerController>())
                            {
                                OwningPC->ClientMessage(TEXT("Select a faction before locking in."));
                            }
                            break;
                        }
                    }
                }
            }
            return false;
        }

        ASkaldPlayerState* MatchedPlayerState = nullptr;
        if (AGameStateBase* GS = GetGameState<AGameStateBase>())
        {
            for (APlayerState* PSBase : GS->PlayerArray)
            {
                if (ASkaldPlayerState* PlayerState = Cast<ASkaldPlayerState>(PSBase))
                {
                    if (PlayerState->GetPlayerId() == PlayerId)
                    {
                        MatchedPlayerState = PlayerState;
                        break;
                    }
                }
            }
        }

        const FString ResolvedName = ResolveLobbyDisplayName(MatchedPlayerState, Slot.DisplayName);
        Slot.DisplayName = ResolvedName;
        Slot.bIsReady = true;
        bUpdated = true;

        if (MatchedPlayerState)
        {
            MatchedPlayerState->PlayerDisplayName = ResolvedName;
            if (MatchedPlayerState->GetPlayerName() != ResolvedName)
            {
                MatchedPlayerState->SetPlayerName(ResolvedName);
            }
            MatchedPlayerState->Faction = Slot.Faction;
        }

        break;
    }

    if (!bUpdated)
    {
        return false;
    }

    RefreshReplicatedLobbyState();

    if (CachedLobbyState && CachedLobbyState->AreAllSlotsReady())
    {
        TryLaunchMatch(nullptr);
    }

    return true;
}

bool ALobbyGameMode::SetPlayerFaction(int32 PlayerId, ESkaldFaction Faction)
{
    if (Faction == ESkaldFaction::None)
    {
        return false;
    }

    // Ensure faction not already taken by another active slot.
    for (const FLobbyPlayerSlot& Slot : AuthoritySlots)
    {
        if (Slot.PlayerId != PlayerId && Slot.bIsActive && Slot.Faction == Faction)
        {
            return false;
        }
    }

    bool bUpdated = false;
    for (FLobbyPlayerSlot& Slot : AuthoritySlots)
    {
        if (Slot.PlayerId == PlayerId)
        {
            if (Slot.bIsReady)
            {
                return false;
            }

            Slot.Faction = Faction;
            bUpdated = true;
            break;
        }
    }

    if (bUpdated)
    {
        if (AGameStateBase* GS = GetGameState<AGameStateBase>())
        {
            for (APlayerState* PSBase : GS->PlayerArray)
            {
                if (ASkaldPlayerState* PlayerState = Cast<ASkaldPlayerState>(PSBase))
                {
                    if (PlayerState->GetPlayerId() == PlayerId)
                    {
                        PlayerState->Faction = Faction;
                        break;
                    }
                }
            }
        }

        if (USkaldGameInstance* GI = GetGameInstance<USkaldGameInstance>())
        {
            GI->TakenFactions.Empty();
            for (const FLobbyPlayerSlot& Slot : AuthoritySlots)
            {
                if (Slot.bIsActive && Slot.Faction != ESkaldFaction::None)
                {
                    GI->TakenFactions.AddUnique(Slot.Faction);
                }
            }
            GI->OnFactionsUpdated.Broadcast();
        }

        RefreshReplicatedLobbyState();
    }

    return bUpdated;
}

void ALobbyGameMode::SetPlayerDisplayName(int32 PlayerId, const FString& DisplayName)
{
    bool bUpdated = false;
    bool bLockedSlot = false;
    for (FLobbyPlayerSlot& Slot : AuthoritySlots)
    {
        if (Slot.PlayerId == PlayerId)
        {
            ASkaldPlayerState* MatchedPlayerState = nullptr;
            if (AGameStateBase* GS = GetGameState<AGameStateBase>())
            {
                for (APlayerState* PSBase : GS->PlayerArray)
                {
                    if (ASkaldPlayerState* PlayerState = Cast<ASkaldPlayerState>(PSBase))
                    {
                        if (PlayerState->GetPlayerId() == PlayerId)
                        {
                            MatchedPlayerState = PlayerState;
                            break;
                        }
                    }
                }
            }

            const FString ResolvedName = ResolveLobbyDisplayName(MatchedPlayerState, DisplayName);
            if (Slot.bIsReady)
            {
                bLockedSlot = true;
                break;
            }

            Slot.DisplayName = ResolvedName;
            bUpdated = true;

            if (MatchedPlayerState)
            {
                MatchedPlayerState->PlayerDisplayName = ResolvedName;
                if (MatchedPlayerState->GetPlayerName() != ResolvedName)
                {
                    MatchedPlayerState->SetPlayerName(ResolvedName);
                }
            }
            break;
        }
    }

    if (bUpdated || bLockedSlot)
    {
        RefreshReplicatedLobbyState();
    }
}

void ALobbyGameMode::TryLaunchMatch(APlayerController* RequestingController)
{
    if (!CachedLobbyState)
    {
        return;
    }

    if (!CachedLobbyState->AreAllSlotsReady())
    {
        if (RequestingController)
        {
            RequestingController->ClientMessage(TEXT("All players must lock in."));
        }
        return;
    }

    if (bMatchLaunchInitiated)
    {
        return;
    }

    bMatchLaunchInitiated = true;

    if (USkaldGameInstance* GI = GetGameInstance<USkaldGameInstance>())
    {
        GI->AIPlayersToSpawn = AuthorityAISlots;
        GI->TakenFactions.Empty();
        GI->PendingLobbyAIPlayers.Reset();

        if (AGameStateBase* LocalGameState = GetGameState<AGameStateBase>())
        {
            TArray<FS_PlayerData> LobbyPlayers;
            LobbyPlayers.Reserve(LocalGameState->PlayerArray.Num());

            for (APlayerState* PlayerStateBase : LocalGameState->PlayerArray)
            {
                if (ASkaldPlayerState* PlayerState = Cast<ASkaldPlayerState>(PlayerStateBase))
                {
                    const int32 SlotIndex = CachedLobbyState ? CachedLobbyState->FindSlotIndexForPlayer(PlayerState->GetPlayerId()) : INDEX_NONE;
                    if (SlotIndex != INDEX_NONE && AuthoritySlots.IsValidIndex(SlotIndex))
                    {
                        FLobbyPlayerSlot& Slot = AuthoritySlots[SlotIndex];
                        const FString ResolvedName = ResolveLobbyDisplayName(PlayerState, Slot.DisplayName);
                        Slot.DisplayName = ResolvedName;
                        PlayerState->Faction = Slot.Faction;
                        PlayerState->PlayerDisplayName = ResolvedName;
                        if (PlayerState->GetPlayerName() != ResolvedName)
                        {
                            PlayerState->SetPlayerName(ResolvedName);
                        }
                        GI->TakenFactions.AddUnique(Slot.Faction);

                        if (!PlayerState->bIsAI)
                        {
                            FS_PlayerData PlayerData;
                            PlayerData.PlayerID = PlayerState->GetPlayerId();
                            PlayerData.PlayerName = PlayerState->GetPlayerName();
                            PlayerData.DisplayName = PlayerState->PlayerDisplayName;
                            PlayerData.IsAI = false;
                            PlayerData.Faction = PlayerState->Faction;
                            LobbyPlayers.Add(PlayerData);
                        }
                    }
                }
            }

            const int32 HumanPlayerCount = LobbyPlayers.Num();
            GI->PendingLobbyPlayers = MoveTemp(LobbyPlayers);
            GI->ExpectedLobbyPlayerCount = HumanPlayerCount;
        }

        for (const FLobbyPlayerSlot& Slot : AuthoritySlots)
        {
            if (!Slot.bIsActive || !Slot.bIsAI)
            {
                continue;
            }

            FSkaldAIPlayerConfig Config;
            Config.DisplayName = Slot.DisplayName;
            Config.Faction = Slot.Faction;
            GI->PendingLobbyAIPlayers.Add(Config);
            if (Slot.Faction != ESkaldFaction::None)
            {
                GI->TakenFactions.AddUnique(Slot.Faction);
            }
        }
    }

    if (USkaldGameInstance* GI = GetGameInstance<USkaldGameInstance>())
    {
        GI->bIsMultiplayer = true;
        GI->bIsHost = true;
    }

    if (UWorld* World = GetWorld())
    {
        const bool bIsStandalone = World->IsNetMode(NM_Standalone);
#if WITH_EDITOR
        const bool bIsPIE = (World->WorldType == EWorldType::PIE || World->WorldType == EWorldType::EditorPreview ||
                             World->WorldType == EWorldType::GamePreview);
#else
        const bool bIsPIE = false;
#endif

        if (bIsStandalone)
        {
            UGameplayStatics::OpenLevel(this, FName(TEXT("/Game/Blueprints/Maps/OverviewMap")), true, TEXT("listen"));
            return;
        }

        if (bIsPIE)
        {
            bUseSeamlessTravel = true;
            World->ServerTravel(TEXT("/Game/Blueprints/Maps/OverviewMap?listen"), /*bAbsolute=*/false, /*bShouldSkipGameNotify=*/false);
            return;
        }

        bUseSeamlessTravel = true;
        World->ServerTravel(TEXT("/Game/Blueprints/Maps/OverviewMap?listen"), /*bAbsolute=*/false, /*bShouldSkipGameNotify=*/false);
    }
}

FString ALobbyGameMode::GenerateUniqueAIName(TSet<FString>& UsedNames) const
{
    TArray<FString> Names = BuildAINames();
    Algo::RandomShuffle(Names);

    for (const FString& Candidate : Names)
    {
        if (!UsedNames.Contains(Candidate))
        {
            UsedNames.Add(Candidate);
            return Candidate;
        }
    }

    int32 Suffix = 1;
    FString Generated;
    do
    {
        Generated = FString::Printf(TEXT("AI_%d"), Suffix++);
    } while (UsedNames.Contains(Generated));

    UsedNames.Add(Generated);
    return Generated;
}

ESkaldFaction ALobbyGameMode::GenerateUniqueAIFaction(TArray<ESkaldFaction>& TakenFactions) const
{
    TArray<ESkaldFaction> Pool = BuildFactionPool();
    for (const ESkaldFaction Existing : TakenFactions)
    {
        Pool.Remove(Existing);
    }

    if (Pool.Num() == 0)
    {
        return ESkaldFaction::None;
    }

    const int32 Index = FMath::RandRange(0, Pool.Num() - 1);
    const ESkaldFaction Selected = Pool[Index];
    TakenFactions.Add(Selected);
    return Selected;
}

void ALobbyGameMode::RefreshReplicatedLobbyState()
{
    if (!CachedLobbyState)
    {
        CachedLobbyState = GetGameState<ALobbyGameState>();
        if (!CachedLobbyState)
        {
            return;
        }
    }

    AuthoritySlots.SetNum(MaxLobbySlots);

    // Ensure human-controlled seats remain in the lowest indices so host lookups stay valid.
    CompactHumanSlots();

    // Initialize slot metadata.
    const int32 HumanSlots = FMath::Clamp(AuthorityTotalSlots - AuthorityAISlots, 1, MaxLobbySlots);
    const int32 AISlots = FMath::Clamp(AuthorityAISlots, 0, MaxLobbySlots - 1);

    TSet<FString> UsedNames;
    TArray<ESkaldFaction> TakenFactions;

    // Track any existing player names/factions.
    for (FLobbyPlayerSlot& Slot : AuthoritySlots)
    {
        if (Slot.bIsActive && !Slot.bIsAI && !Slot.DisplayName.IsEmpty())
        {
            UsedNames.Add(Slot.DisplayName);
        }
        if (Slot.bIsActive && Slot.Faction != ESkaldFaction::None)
        {
            TakenFactions.AddUnique(Slot.Faction);
        }
    }

    for (int32 Index = 0; Index < MaxLobbySlots; ++Index)
    {
        FLobbyPlayerSlot& Slot = AuthoritySlots[Index];
        Slot.bIsActive = Index < AuthorityTotalSlots;
        Slot.bIsReady = Slot.bIsActive ? Slot.bIsReady : false;

        if (!Slot.bIsActive)
        {
            Slot.bIsAI = false;
            Slot.PlayerId = INDEX_NONE;
            Slot.DisplayName.Reset();
            Slot.Faction = ESkaldFaction::None;
            continue;
        }

        if (Index >= HumanSlots)
        {
            Slot.bIsAI = true;
            Slot.PlayerId = INDEX_NONE;
            if (Slot.DisplayName.IsEmpty())
            {
                Slot.DisplayName = GenerateUniqueAIName(UsedNames);
            }
            if (Slot.Faction == ESkaldFaction::None)
            {
                Slot.Faction = GenerateUniqueAIFaction(TakenFactions);
            }
            Slot.bIsReady = true;
        }
        else
        {
            Slot.bIsAI = false;
            if (Slot.PlayerId == INDEX_NONE)
            {
                Slot.DisplayName.Reset();
                Slot.Faction = ESkaldFaction::None;
                Slot.bIsReady = false;
            }
        }
    }

    CachedLobbyState->LobbySlots = AuthoritySlots;
    CachedLobbyState->TotalSlots = AuthorityTotalSlots;
    CachedLobbyState->ReservedAISlots = AuthorityAISlots;
    CachedLobbyState->bSlotConfigurationLocked = bSlotConfigurationLocked;
    CachedLobbyState->ForceNetUpdate();
    CachedLobbyState->OnLobbySlotsUpdated.Broadcast();
}

void ALobbyGameMode::RemovePlayerFromSlots(int32 PlayerId)
{
    bool bRemoved = false;
    for (FLobbyPlayerSlot& Slot : AuthoritySlots)
    {
        if (Slot.PlayerId == PlayerId)
        {
            Slot.PlayerId = INDEX_NONE;
            Slot.DisplayName.Reset();
            Slot.Faction = ESkaldFaction::None;
            Slot.bIsReady = false;
            Slot.bIsAI = false;
            bRemoved = true;
        }
    }

    if (bRemoved)
    {
        CompactHumanSlots();
    }
}

void ALobbyGameMode::CompactHumanSlots()
{
    if (AuthoritySlots.Num() == 0)
    {
        return;
    }

    const int32 MaxHumans = FMath::Clamp(AuthorityTotalSlots - AuthorityAISlots, 1, AuthoritySlots.Num());
    TArray<FLobbyPlayerSlot> OccupiedSlots;
    OccupiedSlots.Reserve(MaxHumans);

    for (int32 Index = 0; Index < MaxHumans; ++Index)
    {
        const FLobbyPlayerSlot& Slot = AuthoritySlots[Index];
        if (Slot.PlayerId != INDEX_NONE)
        {
            OccupiedSlots.Add(Slot);
        }
    }

    for (int32 Index = 0; Index < MaxHumans; ++Index)
    {
        FLobbyPlayerSlot& Slot = AuthoritySlots[Index];
        if (OccupiedSlots.IsValidIndex(Index))
        {
            Slot = OccupiedSlots[Index];
        }
        else
        {
            Slot.PlayerId = INDEX_NONE;
            Slot.DisplayName.Reset();
            Slot.Faction = ESkaldFaction::None;
            Slot.bIsReady = false;
        }

        Slot.bIsAI = false;
        Slot.bIsActive = Index < AuthorityTotalSlots;
    }
}

