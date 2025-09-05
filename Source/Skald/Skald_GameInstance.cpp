#include "Skald_GameInstance.h"

#include "Engine/Engine.h"
#include "Kismet/GameplayStatics.h"

void USkaldGameInstance::Init()
{
    Super::Init();
    SeedCombatRandomStream(FMath::Rand());
    TakenFactions.Empty();
    if (Faction != ESkaldFaction::None)
    {
        TakenFactions.Add(Faction);
    }

    if (GEngine)
    {
        GEngine->OnNetworkFailure().AddUObject(this, &USkaldGameInstance::HandleNetworkFailure);
    }
}

void USkaldGameInstance::SeedCombatRandomStream(int32 Seed)
{
    CombatRandomStream.Initialize(Seed);
}

void USkaldGameInstance::HandleNetworkFailure(UWorld* World, UNetDriver* /*Driver*/,
                                              ENetworkFailure::Type /*FailureType*/,
                                              const FString& ErrorString)
{
    if (GEngine)
    {
        GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Red,
                                         FString::Printf(TEXT("Network failure: %s"), *ErrorString));
    }

    bIsMultiplayer = false;
    bIsHost = false;

    const FName LobbyMap(TEXT("/Game/Blueprints/Maps/Skald_Lobby"));
    UGameplayStatics::OpenLevel(World, LobbyMap);
}

