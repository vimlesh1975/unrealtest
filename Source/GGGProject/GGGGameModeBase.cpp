#include "GGGGameModeBase.h"

#include "Camera/CameraActor.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"

AGGGGameModeBase::AGGGGameModeBase()
{
	DefaultPawnClass = nullptr;
}

void AGGGGameModeBase::BeginPlay()
{
	Super::BeginPlay();

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	TArray<AActor*> Cameras;
	UGameplayStatics::GetAllActorsOfClass(World, ACameraActor::StaticClass(), Cameras);
	Cameras.Sort([](const AActor& Left, const AActor& Right)
	{
		return Left.GetName() < Right.GetName();
	});

	const int32 ViewCount = FMath::Min(4, Cameras.Num());
	for (int32 Index = 1; Index < ViewCount; ++Index)
	{
		if (!UGameplayStatics::GetPlayerController(World, Index))
		{
			UGameplayStatics::CreatePlayer(World, Index, true);
		}
	}

	for (int32 Index = 0; Index < ViewCount; ++Index)
	{
		if (APlayerController* PlayerController = UGameplayStatics::GetPlayerController(World, Index))
		{
			PlayerController->SetViewTargetWithBlend(Cameras[Index], 0.0f);
		}
	}
}
