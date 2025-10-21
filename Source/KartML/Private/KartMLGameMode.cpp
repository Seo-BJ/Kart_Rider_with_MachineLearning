// Copyright Epic Games, Inc. All Rights Reserved.

#include "KartMLGameMode.h"
#include "KartMLPlayerController.h"

AKartMLGameMode::AKartMLGameMode()
{
	PlayerControllerClass = AKartMLPlayerController::StaticClass();
}
