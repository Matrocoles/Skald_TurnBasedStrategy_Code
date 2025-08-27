#include "Skald_PlayerController.h"
#include "Blueprint/UserWidget.h"
#include "Engine/Engine.h"
#include "Kismet/GameplayStatics.h"
#include "Skald_GameState.h"
#include "Skald_PlayerState.h"
#include "Skald_TurnManager.h"
#include "Territory.h"
#include "UI/SkaldMainHUDWidget.h"
#include "WorldMap.h"

ASkaldPlayerController::ASkaldPlayerController() {
  bIsAI = false;
  TurnManager = nullptr;
  HUDRef = nullptr;
  MainHudWidget = nullptr;

  bShowMouseCursor = true;
  bEnableClickEvents = true;
  bEnableMouseOverEvents = true;
}

void ASkaldPlayerController::BeginPlay() {
  Super::BeginPlay();

  // Create and show the HUD widget if a class has been assigned (expected via
  // blueprint).
  if (MainHudWidgetClass) {
    MainHudWidget = CreateWidget<USkaldMainHUDWidget>(this, MainHudWidgetClass);
    if (MainHudWidget) {
      HUDRef = MainHudWidget;
      MainHudWidget->AddToViewport();

      if (ASkaldGameState *GS = GetWorld()->GetGameState<ASkaldGameState>()) {
        TArray<FS_PlayerData> Players;
        for (APlayerState *PSBase : GS->PlayerArray) {
          if (ASkaldPlayerState *PS = Cast<ASkaldPlayerState>(PSBase)) {
            FS_PlayerData Data;
            Data.PlayerID = PS->GetPlayerId();
            Data.PlayerName = PS->DisplayName;
            Data.IsAI = PS->bIsAI;
            Data.Faction = PS->Faction;
            Players.Add(Data);
          }
        }

        const ASkaldPlayerState *CurrentPS = GS->GetCurrentPlayer();
        const int32 CurrentID = CurrentPS ? CurrentPS->GetPlayerId() : -1;
        MainHudWidget->RefreshFromState(CurrentID, /*TurnNumber*/ 1,
                                        ETurnPhase::Reinforcement, Players);
      }

      MainHudWidget->OnAttackRequested.AddDynamic(
          this, &ASkaldPlayerController::HandleAttackRequested);
      MainHudWidget->OnMoveRequested.AddDynamic(
          this, &ASkaldPlayerController::HandleMoveRequested);
      MainHudWidget->OnEndAttackRequested.AddDynamic(
          this, &ASkaldPlayerController::HandleEndAttackRequested);
      MainHudWidget->OnEndMovementRequested.AddDynamic(
          this, &ASkaldPlayerController::HandleEndMovementRequested);
    }
  } else {
    UE_LOG(LogTemp, Warning,
           TEXT("MainHudWidgetClass is null; HUD will not be displayed."));
  }

  if (AWorldMap *WorldMap = Cast<AWorldMap>(
          UGameplayStatics::GetActorOfClass(GetWorld(),
                                            AWorldMap::StaticClass()))) {
    WorldMap->OnTerritorySelected.AddDynamic(
        this, &ASkaldPlayerController::HandleTerritorySelected);
  }

  if (ASkaldPlayerState *PS = GetPlayerState<ASkaldPlayerState>()) {
    bIsAI = PS->bIsAI;
  }
}

void ASkaldPlayerController::SetTurnManager(ATurnManager *Manager) {
  TurnManager = Manager;
}

void ASkaldPlayerController::ShowTurnAnnouncement(const FString &PlayerName,
                                                  bool bIsMyTurn) {
  if (MainHudWidget) {
    MainHudWidget->ShowTurnAnnouncement(PlayerName);
    MainHudWidget->ShowTurnMessage(bIsMyTurn);
  } else if (GEngine) {
    const FString Message = FString::Printf(TEXT("%s's Turn"), *PlayerName);
    GEngine->AddOnScreenDebugMessage(-1, 4.f, FColor::Yellow, Message);
  }
}

void ASkaldPlayerController::StartTurn() {
  if (bIsAI) {
    MakeAIDecision();
    EndTurn();
  } else {
    FInputModeGameAndUI InputMode;
    SetInputMode(InputMode);
  }
}

void ASkaldPlayerController::EndTurn() {
  SetInputMode(FInputModeGameOnly());

  if (TurnManager) {
    TurnManager->AdvanceTurn();
  }
}

void ASkaldPlayerController::EndPhase() {
  if (TurnManager) {
    TurnManager->AdvancePhase();
  }
}

void ASkaldPlayerController::MakeAIDecision() {
  UE_LOG(LogTemp, Log, TEXT("AI %s making decision"), *GetName());
}

bool ASkaldPlayerController::IsAIController() const { return bIsAI; }

void ASkaldPlayerController::HandleAttackRequested(int32 FromID, int32 ToID,
                                                   int32 ArmySent) {
  UE_LOG(LogTemp, Log, TEXT("HUD attack from %d to %d with %d"), FromID, ToID,
         ArmySent);

  FS_BattlePayload Battle;
  if (ASkaldPlayerState *PS = GetPlayerState<ASkaldPlayerState>()) {
    Battle.AttackerPlayerID = PS->GetPlayerId();
  }
  if (AWorldMap *WorldMap = Cast<AWorldMap>(UGameplayStatics::GetActorOfClass(
          GetWorld(), AWorldMap::StaticClass()))) {
    if (ATerritory *Source = WorldMap->GetTerritoryById(FromID)) {
      if (Source->OwningPlayer) {
        Battle.AttackerPlayerID = Source->OwningPlayer->GetPlayerId();
      }
    }
    if (ATerritory *Target = WorldMap->GetTerritoryById(ToID)) {
      if (Target->OwningPlayer) {
        Battle.DefenderPlayerID = Target->OwningPlayer->GetPlayerId();
      }
      Battle.IsCapitalAttack = Target->bIsCapital;
    }
  }
  Battle.FromTerritoryID = FromID;
  Battle.TargetTerritoryID = ToID;
  Battle.ArmyCountSent = ArmySent;

  if (TurnManager) {
    TurnManager->TriggerGridBattle(Battle);
  }
}

void ASkaldPlayerController::HandleMoveRequested(int32 FromID, int32 ToID,
                                                 int32 Troops) {
  UE_LOG(LogTemp, Log, TEXT("HUD move from %d to %d with %d"), FromID, ToID,
         Troops);
}

void ASkaldPlayerController::HandleEndAttackRequested(bool bConfirmed) {
  UE_LOG(LogTemp, Log, TEXT("HUD end attack %s"),
         bConfirmed ? TEXT("confirmed") : TEXT("cancelled"));
}

void ASkaldPlayerController::HandleEndMovementRequested(bool bConfirmed) {
  UE_LOG(LogTemp, Log, TEXT("HUD end move %s"),
         bConfirmed ? TEXT("confirmed") : TEXT("cancelled"));
}

void ASkaldPlayerController::HandleTerritorySelected(ATerritory *Terr) {
  if (!MainHudWidget || !Terr) {
    return;
  }

  FString OwnerName = TEXT("Neutral");
  if (Terr->OwningPlayer) {
    OwnerName = Terr->OwningPlayer->DisplayName;
  }

  MainHudWidget->UpdateTerritoryInfo(Terr->TerritoryName, OwnerName,
                                     Terr->ArmyStrength);
}
