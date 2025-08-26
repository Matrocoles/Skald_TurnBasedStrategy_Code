#include "Skald_PlayerController.h"
#include "Blueprint/UserWidget.h"
#include "Skald_GameState.h"
#include "Skald_PlayerState.h"
#include "Skald_TurnManager.h"
#include "Territory.h"
#include "UI/SkaldMainHUDWidget.h"

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

  if (ASkaldPlayerState *PS = GetPlayerState<ASkaldPlayerState>()) {
    bIsAI = PS->bIsAI;
  }
}

void ASkaldPlayerController::SetTurnManager(ATurnManager *Manager) {
  TurnManager = Manager;
}

void ASkaldPlayerController::StartTurn() {
  if (MainHudWidget) {
    MainHudWidget->HideEndingTurn();
  }
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

void ASkaldPlayerController::MakeAIDecision() {
  UE_LOG(LogTemp, Log, TEXT("AI %s making decision"), *GetName());
}

bool ASkaldPlayerController::IsAIController() const { return bIsAI; }

void ASkaldPlayerController::HandleAttackRequested(int32 FromID, int32 ToID,
                                                   int32 ArmySent) {
  UE_LOG(LogTemp, Log, TEXT("HUD attack from %d to %d with %d"), FromID, ToID,
         ArmySent);
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
