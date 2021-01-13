// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "NewRoanokeGameMode.h"
#include "NewRoanokeCharacter.h"
#include "UObject/ConstructorHelpers.h"

ANewRoanokeGameMode::ANewRoanokeGameMode()
{
	// set default pawn class to our Blueprinted character
	static ConstructorHelpers::FClassFinder<APawn> PlayerPawnBPClass(TEXT("/Game/ThirdPersonCPP/Blueprints/ThirdPersonCharacter"));
	if (PlayerPawnBPClass.Class != NULL)
	{
		DefaultPawnClass = PlayerPawnBPClass.Class;
	}
}
