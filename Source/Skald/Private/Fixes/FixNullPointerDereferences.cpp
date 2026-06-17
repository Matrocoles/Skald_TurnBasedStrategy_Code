/**
 * FIX_001: Null Pointer Dereference in Widget Creation
 * 
 * Issue: Battle HUD initialization doesn't validate LocalPlayer availability before widget creation
 * File: Skald_PlayerController.h (line ~780-830)
 * Severity: CRITICAL
 * 
 * This patch provides guard functions to safely initialize widgets with proper LocalPlayer validation.
 * Applied to: InitializeHUDWidget(), CreateFighterSelectionWidget(), CreateBattleHUDPrompt()
 */

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"

namespace SkaldWidgetSafety
{
	/**
	 * Safely retrieve the local player from a player controller.
	 * @param Controller The player controller to query
	 * @return The local player, or nullptr if not available
	 */
	ULocalPlayer* GetValidLocalPlayer(APlayerController* Controller)
	{
		if (!Controller)
		{
			UE_LOG(LogSkald, Warning, TEXT("GetValidLocalPlayer: PlayerController is nullptr"));
			return nullptr;
		}

		ULocalPlayer* LocalPlayer = Controller->GetLocalPlayer();
		if (!LocalPlayer)
		{
			UE_LOG(LogSkald, Warning, TEXT("GetValidLocalPlayer: LocalPlayer is nullptr for controller %s"), *GetNameSafe(Controller));
			return nullptr;
		}

		return LocalPlayer;
	}

	/**
	 * Safely retrieve the viewport client for widget creation.
	 * @param Controller The player controller to query
	 * @return The viewport client, or nullptr if not available
	 */
	FViewportClient* GetValidViewportClient(APlayerController* Controller)
	{
		ULocalPlayer* LocalPlayer = GetValidLocalPlayer(Controller);
		if (!LocalPlayer)
		{
			return nullptr;
		}

		FViewportClient* ViewportClient = LocalPlayer->ViewportClient;
		if (!ViewportClient)
		{
			UE_LOG(LogSkald, Warning, TEXT("GetValidViewportClient: ViewportClient is nullptr for LocalPlayer"));
			return nullptr;
		}

		return ViewportClient;
	}

	/**
	 * Check if it's safe to create widgets for a given controller.
	 * @param Controller The player controller to validate
	 * @return true if widget creation is safe, false otherwise
	 */
	bool CanCreateWidgets(APlayerController* Controller)
	{
		if (!Controller)
		{
			return false;
		}

		ULocalPlayer* LocalPlayer = GetValidLocalPlayer(Controller);
		if (!LocalPlayer)
		{
			return false;
		}

		if (!LocalPlayer->ViewportClient)
		{
			return false;
		}

		return true;
	}

	/**
	 * Defer widget creation if prerequisites aren't available yet.
	 * Schedules a retry on the next frame.
	 * @param World The world to schedule the timer on
	 * @param Callback Function to call when prerequisites are available
	 * @param MaxRetries Maximum number of retries before giving up
	 */
	void ScheduleDeferredWidgetCreation(UWorld* World, TFunction<void()> Callback, int32 MaxRetries = 5)
	{
		if (!World || !Callback)
		{
			return;
		}

		// Use a timer to retry on the next frame
		FTimerHandle RetryHandle;
		int32 RetryCount = 0;

		auto RetryLambda = [World, Callback, RetryCount, MaxRetries, &RetryHandle]() mutable
		{
			if (RetryCount >= MaxRetries)
			{
				UE_LOG(LogSkald, Error, TEXT("ScheduleDeferredWidgetCreation: Max retries exceeded"));
				if (World)
				{
					World->GetTimerManager().ClearTimer(RetryHandle);
				}
				return;
			}

			Callback();
			if (World)
			{
				World->GetTimerManager().ClearTimer(RetryHandle);
			}
		};

		// Retry in 0.1 seconds
		World->GetTimerManager().SetTimer(
			RetryHandle,
			RetryLambda,
			0.1f,
			false
		);
	}
}

// ============================================================================
// FIX_002: Null Pointer in Grid Pathfinding
// 
// Issue: Grid pointer used in lambda without null check in pathfinding
// File: FighterPawn.cpp (line ~680-710)
// Severity: CRITICAL
//
// Applied to: RebuildVisualMovementPath()
// ============================================================================

namespace SkaldGridSafety
{
	/**
	 * Validate grid component before pathfinding operations.
	 * @param Fighter The fighter pawn to check
	 * @return The grid component, or nullptr if invalid
	 */
	UGridOverlayComponent* GetValidGridForFighter(AFighterPawn* Fighter)
	{
		if (!Fighter)
		{
			UE_LOG(LogSkald, Warning, TEXT("GetValidGridForFighter: Fighter is nullptr"));
			return nullptr;
		}

		UGridOverlayComponent* Grid = Fighter->GetGrid();
		if (!Grid)
		{
			UE_LOG(LogSkald, Warning, TEXT("GetValidGridForFighter: Grid is nullptr for fighter %s"), *GetNameSafe(Fighter));
			return nullptr;
		}

		return Grid;
	}

	/**
	 * Check if a grid is ready for pathfinding operations.
	 * @param Grid The grid to check
	 * @return true if grid is initialized and ready
	 */
	bool IsGridReadyForPathfinding(UGridOverlayComponent* Grid)
	{
		if (!Grid)
		{
			return false;
		}

		if (!Grid->IsInitialized())
		{
			return false;
		}

		return true;
	}

	/**
	 * Safely execute a pathfinding operation with grid validation.
	 * @param Fighter The fighter to pathfind for
	 * @param PathfindingFunc The pathfinding lambda to execute
	 * @return true if pathfinding succeeded, false otherwise
	 */
	bool ExecuteValidatedPathfinding(AFighterPawn* Fighter, TFunction<bool(UGridOverlayComponent*)> PathfindingFunc)
	{
		if (!Fighter || !PathfindingFunc)
		{
			return false;
		}

		UGridOverlayComponent* Grid = GetValidGridForFighter(Fighter);
		if (!Grid)
		{
			return false;
		}

		if (!IsGridReadyForPathfinding(Grid))
		{
			UE_LOG(LogSkald, Warning, TEXT("ExecuteValidatedPathfinding: Grid not ready for pathfinding on fighter %s"), *GetNameSafe(Fighter));
			return false;
		}

		return PathfindingFunc(Grid);
	}
}

// ============================================================================
// FIX_003: Null World Pointer in Level Travel
// 
// Issue: PendingResumeWorld weak pointer dereferenced without IsValid() check
// File: Skald_GameInstance.h (line ~450-480)
// Severity: CRITICAL
//
// Applied to: AttemptResumeAfterTravel()
// ============================================================================

namespace SkaldWorldSafety
{
	/**
	 * Safely dereference a weak world pointer.
	 * @param WeakWorld The weak pointer to the world
	 * @return The valid world, or nullptr
	 */
	UWorld* GetValidWorldFromWeakPtr(TWeakObjectPtr<UWorld>& WeakWorld)
	{
		if (!WeakWorld.IsValid())
		{
			UE_LOG(LogSkald, Warning, TEXT("GetValidWorldFromWeakPtr: World weak pointer is invalid or has been garbage collected"));
			return nullptr;
		}

		UWorld* World = WeakWorld.Get();
		if (!World)
		{
			UE_LOG(LogSkald, Warning, TEXT("GetValidWorldFromWeakPtr: Failed to retrieve world from weak pointer"));
			return nullptr;
		}

		if (World->IsUnreachable())
		{
			UE_LOG(LogSkald, Warning, TEXT("GetValidWorldFromWeakPtr: World is marked as unreachable"));
			return nullptr;
		}

		return World;
	}

	/**
	 * Check if a world is valid and ready for game operations.
	 * @param World The world to validate
	 * @return true if world is valid and ready
	 */
	bool IsWorldReadyForGameplay(UWorld* World)
	{
		if (!World)
		{
			return false;
		}

		if (World->IsUnreachable())
		{
			return false;
		}

		// Check if the game has started
		if (!World->HasBegunPlay())
		{
			return false;
		}

		return true;
	}

	/**
	 * Store a world reference with validation and logging.
	 * @param World The world to store
	 * @param OutWeakWorld Output weak pointer
	 * @return true if world was successfully stored
	 */
	bool StoreValidWorldReference(UWorld* World, TWeakObjectPtr<UWorld>& OutWeakWorld)
	{
		if (!World)
		{
			UE_LOG(LogSkald, Warning, TEXT("StoreValidWorldReference: Cannot store nullptr world reference"));
			return false;
		}

		if (World->IsUnreachable())
		{
			UE_LOG(LogSkald, Warning, TEXT("StoreValidWorldReference: Cannot store unreachable world"));
			return false;
		}

		OutWeakWorld = TWeakObjectPtr<UWorld>(World);
		return true;
	}
}
