#include "GGGGameModeBase.h"

#include "BlackmagicMediaOutput.h"
#include "BlackmagicMediaSource.h"
#include "BlackmagicDeviceProvider.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/Canvas.h"
#include "Engine/SceneCapture2D.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Engine/TextureRenderTarget2D.h"
#include "GameFramework/PlayerController.h"
#include "GenlockedFixedRateCustomTimeStep.h"
#include "HAL/IConsoleManager.h"
#include "InputCoreTypes.h"
#include "Kismet/KismetMathLibrary.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "MediaCapture.h"
#include "MediaIOCoreDefinitions.h"
#include "MediaPlayer.h"
#include "MediaSoundComponent.h"
#include "MediaTexture.h"
#include "TimerManager.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
const TCHAR* MediaIOStandardToString(EMediaIOStandardType Standard)
{
	switch (Standard)
	{
	case EMediaIOStandardType::Progressive:
		return TEXT("Progressive");
	case EMediaIOStandardType::Interlaced:
		return TEXT("Interlaced");
	case EMediaIOStandardType::ProgressiveSegmentedFrame:
		return TEXT("PSF");
	default:
		return TEXT("Unknown");
	}
}

const TCHAR* MediaIOTransportToString(EMediaIOTransportType Transport)
{
	switch (Transport)
	{
	case EMediaIOTransportType::SingleLink:
		return TEXT("SingleLink");
	case EMediaIOTransportType::DualLink:
		return TEXT("DualLink");
	case EMediaIOTransportType::QuadLink:
		return TEXT("QuadLink");
	case EMediaIOTransportType::HDMI:
		return TEXT("HDMI");
	default:
		return TEXT("Unknown");
	}
}

const TCHAR* MediaIOOutputTypeToString(EMediaIOOutputType OutputType)
{
	switch (OutputType)
	{
	case EMediaIOOutputType::Fill:
		return TEXT("Fill");
	case EMediaIOOutputType::FillAndKey:
		return TEXT("FillAndKey");
	default:
		return TEXT("Unknown");
	}
}

const TCHAR* MediaIOReferenceToString(EMediaIOReferenceType Reference)
{
	switch (Reference)
	{
	case EMediaIOReferenceType::FreeRun:
		return TEXT("FreeRun");
	case EMediaIOReferenceType::External:
		return TEXT("External");
	case EMediaIOReferenceType::Input:
		return TEXT("Input");
	default:
		return TEXT("Unknown");
	}
}

bool IsEquivalentFrameRate(const FFrameRate& FrameRate, int32 Numerator, int32 Denominator)
{
	return FrameRate.Denominator != 0
		&& Denominator != 0
		&& static_cast<int64>(FrameRate.Numerator) * Denominator == static_cast<int64>(Numerator) * FrameRate.Denominator;
}

const FVector DeckLinkInputPlateDefaultLocation(0.0f, 0.0f, 120.0f);
const FRotator DeckLinkInputPlateDefaultRotation(0.0f, 45.0f, 65.0f);
const FVector DeckLinkInputPlateDefaultScale(4.2f, 2.3625f, 1.0f);
constexpr float DeckLinkInputPlateMinScale = 0.2f;
constexpr float DeckLinkInputPlateMaxScale = 5.0f;
}

AGGGGameModeBase::AGGGGameModeBase()
{
	DefaultPawnClass = nullptr;
	HUDClass = AGGGPreviewHUD::StaticClass();
	PrimaryActorTick.bCanEverTick = true;

	static ConstructorHelpers::FObjectFinder<UStaticMesh> DisplayMeshFinder(TEXT("/Engine/BasicShapes/Plane.Plane"));
	DeckLinkInputDisplayMesh = DisplayMeshFinder.Object;

	static ConstructorHelpers::FObjectFinder<UMaterialInterface> ScreenMaterialFinder(TEXT("/Engine/EngineMaterials/Widget3DPassThrough_Opaque.Widget3DPassThrough_Opaque"));
	DeckLinkInputScreenMaterial = ScreenMaterialFinder.Object;
}

const TArray<TObjectPtr<UTextureRenderTarget2D>>& AGGGGameModeBase::GetDeckLinkPreviewTargets() const
{
	return DeckLinkRenderTargets;
}

void AGGGGameModeBase::BeginPlay()
{
	Super::BeginPlay();

	ConfigureDeckLinkTiming();

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimerForNextTick(this, &AGGGGameModeBase::SetupSplitScreenCameras);
	}
}

void AGGGGameModeBase::ConfigureDeckLinkTiming()
{
	if (GEngine)
	{
		GEngine->SetMaxFPS(50.0f);

		UGenlockedFixedRateCustomTimeStep* FixedRateStep = NewObject<UGenlockedFixedRateCustomTimeStep>(GEngine);
		FixedRateStep->FrameRate = FFrameRate(50, 1);
		FixedRateStep->bShouldBlock = true;
		FixedRateStep->bForceSingleFrameDeltaTime = true;

		if (GEngine->SetCustomTimeStep(FixedRateStep))
		{
			DeckLinkCustomTimeStep = FixedRateStep;
			UE_LOG(LogTemp, Display, TEXT("DeckLink timing: using GenlockedFixedRateCustomTimeStep at 50 fps."));
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("DeckLink timing: failed to install the fixed 50 fps custom timestep."));
		}
	}

	if (IConsoleVariable* VSyncCVar = IConsoleManager::Get().FindConsoleVariable(TEXT("r.VSync")))
	{
		VSyncCVar->Set(0, ECVF_SetByCode);
	}
}

void AGGGGameModeBase::SetupSplitScreenCameras()
{
	static constexpr int32 DesiredViewCount = 4;

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	TArray<AActor*> Cameras;
	UGameplayStatics::GetAllActorsOfClass(World, ACameraActor::StaticClass(), Cameras);
	if (Cameras.Num() < DesiredViewCount)
	{
		UE_LOG(LogTemp, Warning, TEXT("Expected 4 camera actors, found %d."), Cameras.Num());
		return;
	}

	const FVector FocusPoint(0.0f, 0.0f, 60.0f);
	for (AActor* Camera : Cameras)
	{
		if (Camera)
		{
			Camera->SetActorRotation(UKismetMathLibrary::FindLookAtRotation(Camera->GetActorLocation(), FocusPoint));
		}
	}

	TArray<AActor*> OrderedCameras;
	OrderedCameras.SetNumZeroed(DesiredViewCount);
	for (AActor* Camera : Cameras)
	{
		if (!Camera)
		{
			continue;
		}

		const FVector Location = Camera->GetActorLocation();
		if (Location.Z > 700.0f)
		{
			OrderedCameras[3] = Camera;
		}
		else if (Location.X < -100.0f)
		{
			OrderedCameras[1] = Camera;
		}
		else if (Location.X > 100.0f)
		{
			OrderedCameras[2] = Camera;
		}
		else
		{
			OrderedCameras[0] = Camera;
		}
	}

	for (int32 Index = 0; Index < DesiredViewCount; ++Index)
	{
		if (!OrderedCameras[Index])
		{
			UE_LOG(LogTemp, Warning, TEXT("Missing camera assignment for split-screen view %d."), Index);
			return;
		}
	}

	ControlledCameras.Reset();
	for (AActor* OrderedCamera : OrderedCameras)
	{
		ControlledCameras.Add(Cast<ACameraActor>(OrderedCamera));
	}
	SelectedCameraIndex = 0;
	bControllingDeckLinkInputPlate = false;
	ShowCameraControlStatus();
	UpdateSelectedViewportCamera();
	UE_LOG(LogTemp, Display, TEXT("Viewport monitor grid is showing cameras 1-4 from the DeckLink render targets."));

	SetupDeckLinkInputScreen();
	SetupDeckLinkOutputs(OrderedCameras);
}

void AGGGGameModeBase::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	HandleCameraControl(DeltaSeconds);
	SyncDeckLinkCaptures();
}

void AGGGGameModeBase::HandleCameraControl(float DeltaSeconds)
{
	UWorld* World = GetWorld();
	if (!World || ControlledCameras.Num() == 0)
	{
		return;
	}

	APlayerController* PlayerController = UGameplayStatics::GetPlayerController(World, 0);
	if (!PlayerController)
	{
		return;
	}

	if (PlayerController->WasInputKeyJustPressed(EKeys::Five) && DeckLinkInputScreenActor && DeckLinkInputScreenMeshComponent)
	{
		bControllingDeckLinkInputPlate = true;
		ShowDeckLinkInputPlateControlStatus();
	}

	const TPair<FKey, int32> CameraSelectKeys[] = {
		TPair<FKey, int32>(EKeys::One, 0),
		TPair<FKey, int32>(EKeys::Two, 1),
		TPair<FKey, int32>(EKeys::Three, 2),
		TPair<FKey, int32>(EKeys::Four, 3)
	};

	for (const TPair<FKey, int32>& CameraSelectKey : CameraSelectKeys)
	{
		if (PlayerController->WasInputKeyJustPressed(CameraSelectKey.Key) && ControlledCameras.IsValidIndex(CameraSelectKey.Value))
		{
			bControllingDeckLinkInputPlate = false;
			SelectedCameraIndex = CameraSelectKey.Value;
			UpdateSelectedViewportCamera();
			ShowCameraControlStatus();
		}
	}

	if (bControllingDeckLinkInputPlate)
	{
		HandleDeckLinkInputPlateControl(PlayerController, DeltaSeconds);
		return;
	}

	ACameraActor* SelectedCamera = ControlledCameras.IsValidIndex(SelectedCameraIndex) ? ControlledCameras[SelectedCameraIndex].Get() : nullptr;
	if (!SelectedCamera)
	{
		return;
	}

	const bool bFastMove = PlayerController->IsInputKeyDown(EKeys::LeftShift) || PlayerController->IsInputKeyDown(EKeys::RightShift);
	const float MoveSpeed = bFastMove ? 1200.0f : 300.0f;
	const float RotateSpeed = bFastMove ? 90.0f : 30.0f;

	FVector MoveDirection = FVector::ZeroVector;
	MoveDirection += SelectedCamera->GetActorForwardVector() * (PlayerController->IsInputKeyDown(EKeys::W) ? 1.0f : 0.0f);
	MoveDirection -= SelectedCamera->GetActorForwardVector() * (PlayerController->IsInputKeyDown(EKeys::S) ? 1.0f : 0.0f);
	MoveDirection += SelectedCamera->GetActorRightVector() * (PlayerController->IsInputKeyDown(EKeys::D) ? 1.0f : 0.0f);
	MoveDirection -= SelectedCamera->GetActorRightVector() * (PlayerController->IsInputKeyDown(EKeys::A) ? 1.0f : 0.0f);
	MoveDirection += FVector::UpVector * (PlayerController->IsInputKeyDown(EKeys::E) ? 1.0f : 0.0f);
	MoveDirection -= FVector::UpVector * (PlayerController->IsInputKeyDown(EKeys::Q) ? 1.0f : 0.0f);

	if (!MoveDirection.IsNearlyZero())
	{
		SelectedCamera->AddActorWorldOffset(MoveDirection.GetSafeNormal() * MoveSpeed * DeltaSeconds, false);
	}

	FRotator RotationDelta = FRotator::ZeroRotator;
	RotationDelta.Pitch += PlayerController->IsInputKeyDown(EKeys::Up) ? -1.0f : 0.0f;
	RotationDelta.Pitch += PlayerController->IsInputKeyDown(EKeys::Down) ? 1.0f : 0.0f;
	RotationDelta.Yaw += PlayerController->IsInputKeyDown(EKeys::Right) ? 1.0f : 0.0f;
	RotationDelta.Yaw += PlayerController->IsInputKeyDown(EKeys::Left) ? -1.0f : 0.0f;

	if (!RotationDelta.IsNearlyZero())
	{
		SelectedCamera->AddActorWorldRotation(RotationDelta * RotateSpeed * DeltaSeconds);
	}

	if (PlayerController->WasInputKeyJustPressed(EKeys::R))
	{
		const FVector FocusPoint(0.0f, 0.0f, 60.0f);
		SelectedCamera->SetActorRotation(UKismetMathLibrary::FindLookAtRotation(SelectedCamera->GetActorLocation(), FocusPoint));
	}
}

void AGGGGameModeBase::HandleDeckLinkInputPlateControl(APlayerController* PlayerController, float DeltaSeconds)
{
	if (!PlayerController || !DeckLinkInputScreenActor || !DeckLinkInputScreenMeshComponent)
	{
		return;
	}

	const bool bFastMove = PlayerController->IsInputKeyDown(EKeys::LeftShift) || PlayerController->IsInputKeyDown(EKeys::RightShift);
	const float MoveSpeed = bFastMove ? 900.0f : 250.0f;
	const float RotateSpeed = bFastMove ? 120.0f : 40.0f;
	const float ScaleSpeed = bFastMove ? 1.5f : 0.5f;

	const FRotator PlateYaw(0.0f, DeckLinkInputScreenMeshComponent->GetComponentRotation().Yaw, 0.0f);
	const FVector PlateForward = FRotationMatrix(PlateYaw).GetUnitAxis(EAxis::X);
	const FVector PlateRight = FRotationMatrix(PlateYaw).GetUnitAxis(EAxis::Y);

	FVector MoveDirection = FVector::ZeroVector;
	MoveDirection += PlateForward * (PlayerController->IsInputKeyDown(EKeys::W) ? 1.0f : 0.0f);
	MoveDirection -= PlateForward * (PlayerController->IsInputKeyDown(EKeys::S) ? 1.0f : 0.0f);
	MoveDirection += PlateRight * (PlayerController->IsInputKeyDown(EKeys::D) ? 1.0f : 0.0f);
	MoveDirection -= PlateRight * (PlayerController->IsInputKeyDown(EKeys::A) ? 1.0f : 0.0f);
	MoveDirection += FVector::UpVector * (PlayerController->IsInputKeyDown(EKeys::E) ? 1.0f : 0.0f);
	MoveDirection -= FVector::UpVector * (PlayerController->IsInputKeyDown(EKeys::Q) ? 1.0f : 0.0f);

	if (!MoveDirection.IsNearlyZero())
	{
		DeckLinkInputScreenActor->AddActorWorldOffset(MoveDirection.GetSafeNormal() * MoveSpeed * DeltaSeconds, false);
	}

	FRotator RotationDelta = FRotator::ZeroRotator;
	RotationDelta.Pitch += PlayerController->IsInputKeyDown(EKeys::Up) ? -1.0f : 0.0f;
	RotationDelta.Pitch += PlayerController->IsInputKeyDown(EKeys::Down) ? 1.0f : 0.0f;
	RotationDelta.Yaw += PlayerController->IsInputKeyDown(EKeys::Right) ? 1.0f : 0.0f;
	RotationDelta.Yaw += PlayerController->IsInputKeyDown(EKeys::Left) ? -1.0f : 0.0f;
	RotationDelta.Roll += PlayerController->IsInputKeyDown(EKeys::C) ? 1.0f : 0.0f;
	RotationDelta.Roll += PlayerController->IsInputKeyDown(EKeys::Z) ? -1.0f : 0.0f;

	if (!RotationDelta.IsNearlyZero())
	{
		DeckLinkInputScreenMeshComponent->AddRelativeRotation(RotationDelta * RotateSpeed * DeltaSeconds);
	}

	float ScaleDirection = 0.0f;
	ScaleDirection += (PlayerController->IsInputKeyDown(EKeys::Equals) || PlayerController->IsInputKeyDown(EKeys::Add)) ? 1.0f : 0.0f;
	ScaleDirection -= (PlayerController->IsInputKeyDown(EKeys::Hyphen) || PlayerController->IsInputKeyDown(EKeys::Subtract)) ? 1.0f : 0.0f;
	if (!FMath::IsNearlyZero(ScaleDirection))
	{
		DeckLinkInputPlateScaleFactor = FMath::Clamp(
			DeckLinkInputPlateScaleFactor + ScaleDirection * ScaleSpeed * DeltaSeconds,
			DeckLinkInputPlateMinScale,
			DeckLinkInputPlateMaxScale);
		DeckLinkInputScreenMeshComponent->SetRelativeScale3D(DeckLinkInputPlateDefaultScale * DeckLinkInputPlateScaleFactor);
	}

	if (PlayerController->WasInputKeyJustPressed(EKeys::R))
	{
		ResetDeckLinkInputPlate();
		ShowDeckLinkInputPlateControlStatus();
	}
}

void AGGGGameModeBase::ResetDeckLinkInputPlate()
{
	if (DeckLinkInputScreenActor)
	{
		DeckLinkInputScreenActor->SetActorLocation(DeckLinkInputPlateDefaultLocation);
		DeckLinkInputScreenActor->SetActorRotation(FRotator::ZeroRotator);
	}

	if (DeckLinkInputScreenMeshComponent)
	{
		DeckLinkInputScreenMeshComponent->SetRelativeRotation(DeckLinkInputPlateDefaultRotation);
		DeckLinkInputScreenMeshComponent->SetRelativeScale3D(DeckLinkInputPlateDefaultScale);
	}

	DeckLinkInputPlateScaleFactor = 1.0f;
}

void AGGGGameModeBase::ShowCameraControlStatus() const
{
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(1001, 2.0f, FColor::Cyan, FString::Printf(TEXT("Controlling camera %d"), SelectedCameraIndex + 1));
	}
}

void AGGGGameModeBase::ShowDeckLinkInputPlateControlStatus() const
{
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(1001, 2.0f, FColor::Cyan, TEXT("Controlling DeckLink input plate"));
	}
}

void AGGGGameModeBase::UpdateSelectedViewportCamera()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	if (!MonitorViewportCamera)
	{
		FActorSpawnParameters SpawnParameters;
		SpawnParameters.Name = TEXT("MonitorViewportCamera");
		MonitorViewportCamera = World->SpawnActor<ACameraActor>(FVector(0.0f, 0.0f, 100000.0f), FRotator::ZeroRotator, SpawnParameters);
		if (UCameraComponent* CameraComponent = MonitorViewportCamera ? MonitorViewportCamera->GetCameraComponent() : nullptr)
		{
			CameraComponent->SetFieldOfView(5.0f);
		}
	}

	APlayerController* PlayerController = UGameplayStatics::GetPlayerController(World, 0);
	if (MonitorViewportCamera && PlayerController)
	{
		PlayerController->SetViewTarget(MonitorViewportCamera);
		PlayerController->SetViewTargetWithBlend(MonitorViewportCamera, 0.0f);
	}
}

void AGGGPreviewHUD::DrawHUD()
{
	Super::DrawHUD();

	if (!Canvas || !GetWorld())
	{
		return;
	}

	const AGGGGameModeBase* GameMode = Cast<AGGGGameModeBase>(UGameplayStatics::GetGameMode(GetWorld()));
	if (!GameMode)
	{
		return;
	}

	const TArray<TObjectPtr<UTextureRenderTarget2D>>& PreviewTargets = GameMode->GetDeckLinkPreviewTargets();
	if (PreviewTargets.Num() < 4)
	{
		return;
	}

	const float HalfWidth = Canvas->ClipX * 0.5f;
	const float HalfHeight = Canvas->ClipY * 0.5f;
	for (int32 Index = 0; Index < 4; ++Index)
	{
		UTextureRenderTarget2D* Texture = PreviewTargets[Index].Get();
		if (!Texture)
		{
			continue;
		}

		const float X = (Index % 2) * HalfWidth;
		const float Y = (Index / 2) * HalfHeight;
		DrawTexture(Texture, X, Y, HalfWidth, HalfHeight, 0.0f, 0.0f, 1.0f, 1.0f, FLinearColor::White, BLEND_Opaque);
	}

	constexpr float SeparatorThickness = 2.0f;
	DrawRect(FLinearColor::Black, HalfWidth - SeparatorThickness * 0.5f, 0.0f, SeparatorThickness, Canvas->ClipY);
	DrawRect(FLinearColor::Black, 0.0f, HalfHeight - SeparatorThickness * 0.5f, Canvas->ClipX, SeparatorThickness);
}

void AGGGGameModeBase::SetupDeckLinkInputScreen()
{
	static constexpr int32 DeckLinkInputDeviceId = 5;

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	FMediaIOConfiguration InputConfiguration;
	if (!FindDeckLinkInputConfiguration(DeckLinkInputDeviceId, InputConfiguration))
	{
		UE_LOG(LogTemp, Warning, TEXT("DeckLink input screen skipped: no Blackmagic input configuration found for device %d."), DeckLinkInputDeviceId);
		return;
	}

	DeckLinkInputMediaSource = NewObject<UBlackmagicMediaSource>(this, TEXT("DeckLinkInputDevice5Source"));
	DeckLinkInputMediaSource->MediaConfiguration = InputConfiguration;
	DeckLinkInputMediaSource->AutoDetectableTimecodeFormat = EMediaIOAutoDetectableTimecodeFormat::None;
	DeckLinkInputMediaSource->bCaptureAudio = true;
	DeckLinkInputMediaSource->AudioChannels = EBlackmagicMediaAudioChannel::Stereo2;
	DeckLinkInputMediaSource->MaxNumAudioFrameBuffer = 32;
	DeckLinkInputMediaSource->bCaptureVideo = true;
	DeckLinkInputMediaSource->ColorFormat = EBlackmagicMediaSourceColorFormat::YUV8;
	DeckLinkInputMediaSource->MaxNumVideoFrameBuffer = 8;
	DeckLinkInputMediaSource->bLogDropFrame = true;

	DeckLinkInputMediaPlayer = NewObject<UMediaPlayer>(this, TEXT("DeckLinkInputDevice5Player"));
	DeckLinkInputMediaPlayer->PlayOnOpen = true;

	DeckLinkInputMediaTexture = NewObject<UMediaTexture>(this, TEXT("DeckLinkInputDevice5Texture"));
	DeckLinkInputMediaTexture->SetMediaPlayer(DeckLinkInputMediaPlayer);
	DeckLinkInputMediaTexture->UpdateResource();

	FActorSpawnParameters SpawnParameters;
	SpawnParameters.Name = TEXT("DeckLinkInputDevice5Screen");
	SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	DeckLinkInputScreenActor = World->SpawnActor<AActor>(AActor::StaticClass(), DeckLinkInputPlateDefaultLocation, FRotator::ZeroRotator, SpawnParameters);
	if (!DeckLinkInputScreenActor)
	{
		UE_LOG(LogTemp, Warning, TEXT("DeckLink input screen skipped: could not spawn the screen actor."));
		return;
	}

	USceneComponent* DisplayRootComponent = NewObject<USceneComponent>(DeckLinkInputScreenActor, TEXT("DeckLinkInputScreenRoot"));
	DeckLinkInputScreenActor->SetRootComponent(DisplayRootComponent);
	DeckLinkInputScreenActor->AddInstanceComponent(DisplayRootComponent);
	DisplayRootComponent->SetMobility(EComponentMobility::Movable);
	DisplayRootComponent->RegisterComponent();

	DeckLinkInputMediaSoundComponent = NewObject<UMediaSoundComponent>(DeckLinkInputScreenActor, TEXT("DeckLinkInputDevice5Audio"));
	if (DeckLinkInputMediaSoundComponent)
	{
		DeckLinkInputMediaSoundComponent->SetupAttachment(DisplayRootComponent);
		DeckLinkInputMediaSoundComponent->SetMediaPlayer(DeckLinkInputMediaPlayer);
		DeckLinkInputMediaSoundComponent->Channels = EMediaSoundChannels::Stereo;
		DeckLinkInputMediaSoundComponent->DynamicRateAdjustment = true;
		DeckLinkInputMediaSoundComponent->bAllowSpatialization = false;
		DeckLinkInputMediaSoundComponent->bIsUISound = true;
		DeckLinkInputMediaSoundComponent->SetAutoActivate(true);
		DeckLinkInputScreenActor->AddInstanceComponent(DeckLinkInputMediaSoundComponent);
		DeckLinkInputMediaSoundComponent->RegisterComponent();
		DeckLinkInputMediaSoundComponent->CreateAudioComponent();
		DeckLinkInputMediaSoundComponent->Initialize(48000);
		DeckLinkInputMediaSoundComponent->SetVolumeMultiplier(1.0f);
		DeckLinkInputMediaSoundComponent->Start();
		DeckLinkInputMediaSoundComponent->Activate(true);
		DeckLinkInputMediaSoundComponent->AddClockSink();
	}

	if (!DeckLinkInputDisplayMesh || !DeckLinkInputScreenMaterial)
	{
		UE_LOG(LogTemp, Warning, TEXT("DeckLink input screen skipped: engine display mesh or video material is unavailable."));
		return;
	}

	AddDeckLinkInputScreenMesh(DeckLinkInputScreenActor, DeckLinkInputMediaTexture);

	if (!DeckLinkInputMediaPlayer->OpenSource(DeckLinkInputMediaSource))
	{
		UE_LOG(LogTemp, Warning, TEXT("DeckLink input device %d failed to open. Check SDI signal, Desktop Video driver, and that device 5 is not in use."), DeckLinkInputDeviceId);
		return;
	}

	DeckLinkInputAudioKickAttempts = 0;
	KickDeckLinkInputAudio();
	World->GetTimerManager().SetTimer(DeckLinkInputAudioKickTimerHandle, this, &AGGGGameModeBase::KickDeckLinkInputAudio, 0.25f, true);

	UE_LOG(LogTemp, Display, TEXT("DeckLink input device %d is placed in the scene as a visible video screen with stereo audio routed into Unreal."), DeckLinkInputDeviceId);
}

void AGGGGameModeBase::KickDeckLinkInputAudio()
{
	++DeckLinkInputAudioKickAttempts;

	bool bAudioTrackReady = false;
	int32 NumAudioTracks = 0;
	int32 SelectedAudioTrack = INDEX_NONE;
	float MediaRate = 0.0f;
	bool bMediaSoundPlaying = false;

	if (DeckLinkInputMediaPlayer)
	{
		NumAudioTracks = DeckLinkInputMediaPlayer->GetNumTracks(EMediaPlayerTrack::Audio);
		if (NumAudioTracks > 0)
		{
			SelectedAudioTrack = DeckLinkInputMediaPlayer->GetSelectedTrack(EMediaPlayerTrack::Audio);
			if (SelectedAudioTrack == INDEX_NONE)
			{
				DeckLinkInputMediaPlayer->SelectTrack(EMediaPlayerTrack::Audio, 0);
				SelectedAudioTrack = DeckLinkInputMediaPlayer->GetSelectedTrack(EMediaPlayerTrack::Audio);
			}
			bAudioTrackReady = SelectedAudioTrack != INDEX_NONE;
		}

		DeckLinkInputMediaPlayer->Play();
		DeckLinkInputMediaPlayer->SetRate(1.0f);
		MediaRate = DeckLinkInputMediaPlayer->GetRate();
	}

	if (DeckLinkInputMediaSoundComponent)
	{
		DeckLinkInputMediaSoundComponent->Start();
		DeckLinkInputMediaSoundComponent->Activate(true);
		DeckLinkInputMediaSoundComponent->UpdatePlayer();
		bMediaSoundPlaying = DeckLinkInputMediaSoundComponent->IsPlaying();
	}

	if (bAudioTrackReady && !FMath::IsNearlyZero(MediaRate))
	{
		if (UWorld* World = GetWorld())
		{
			World->GetTimerManager().ClearTimer(DeckLinkInputAudioKickTimerHandle);
		}

		UE_LOG(LogTemp, Display, TEXT("DeckLink input audio track %d selected from %d track(s), media rate %.2f, media sound playing: %s."), SelectedAudioTrack, NumAudioTracks, MediaRate, bMediaSoundPlaying ? TEXT("yes") : TEXT("no"));
		return;
	}

	if (DeckLinkInputAudioKickAttempts >= 20)
	{
		if (UWorld* World = GetWorld())
		{
			World->GetTimerManager().ClearTimer(DeckLinkInputAudioKickTimerHandle);
		}

		UE_LOG(LogTemp, Warning, TEXT("DeckLink input audio track was not reported after %d attempts. Output audio may be silent until the source exposes audio."), DeckLinkInputAudioKickAttempts);
	}
}

void AGGGGameModeBase::AddDeckLinkInputScreenMesh(AActor* ScreenActor, UMediaTexture* MediaTexture)
{
	if (!ScreenActor || !ScreenActor->GetRootComponent() || !MediaTexture || !DeckLinkInputDisplayMesh || !DeckLinkInputScreenMaterial)
	{
		return;
	}

	DeckLinkInputScreenMeshComponent = NewObject<UStaticMeshComponent>(ScreenActor, TEXT("DeckLinkInputScreenMesh"));
	DeckLinkInputScreenMeshComponent->SetupAttachment(ScreenActor->GetRootComponent());
	DeckLinkInputScreenMeshComponent->SetStaticMesh(DeckLinkInputDisplayMesh);
	DeckLinkInputScreenMeshComponent->SetRelativeRotation(DeckLinkInputPlateDefaultRotation);
	DeckLinkInputScreenMeshComponent->SetRelativeScale3D(DeckLinkInputPlateDefaultScale);
	DeckLinkInputScreenMeshComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	DeckLinkInputScreenMeshComponent->SetGenerateOverlapEvents(false);
	DeckLinkInputScreenMeshComponent->SetCastShadow(false);
	DeckLinkInputScreenMeshComponent->bReceivesDecals = false;

	ScreenActor->AddInstanceComponent(DeckLinkInputScreenMeshComponent);
	DeckLinkInputScreenMeshComponent->RegisterComponent();

	UMaterialInstanceDynamic* DynamicMaterial = DeckLinkInputScreenMeshComponent->CreateAndSetMaterialInstanceDynamicFromMaterial(0, DeckLinkInputScreenMaterial);
	if (DynamicMaterial)
	{
		DynamicMaterial->SetTextureParameterValue(TEXT("SlateUI"), MediaTexture);
		DynamicMaterial->SetVectorParameterValue(TEXT("TintColorAndOpacity"), FLinearColor::White);
		DynamicMaterial->SetScalarParameterValue(TEXT("OpacityFromTexture"), 1.0f);
	}
}

void AGGGGameModeBase::SyncDeckLinkCaptures()
{
	const int32 CaptureCount = FMath::Min(ControlledCameras.Num(), DeckLinkSceneCaptures.Num());
	for (int32 Index = 0; Index < CaptureCount; ++Index)
	{
		SyncDeckLinkCapture(Index, false);
	}
}

void AGGGGameModeBase::SyncDeckLinkCapture(int32 CameraIndex, bool bCaptureScene)
{
	ACameraActor* CameraActor = ControlledCameras.IsValidIndex(CameraIndex) ? ControlledCameras[CameraIndex].Get() : nullptr;
	ASceneCapture2D* SceneCaptureActor = DeckLinkSceneCaptureActors.IsValidIndex(CameraIndex) ? DeckLinkSceneCaptureActors[CameraIndex].Get() : nullptr;
	USceneCaptureComponent2D* SceneCapture = DeckLinkSceneCaptures.IsValidIndex(CameraIndex) ? DeckLinkSceneCaptures[CameraIndex].Get() : nullptr;
	if (!CameraActor || !SceneCaptureActor || !SceneCapture)
	{
		return;
	}

	UCameraComponent* CameraComponent = CameraActor->GetCameraComponent();
	if (CameraComponent)
	{
		SceneCaptureActor->SetActorLocationAndRotation(CameraComponent->GetComponentLocation(), CameraComponent->GetComponentRotation());
		SceneCapture->FOVAngle = CameraComponent->FieldOfView;
	}
	else
	{
		SceneCaptureActor->SetActorLocationAndRotation(CameraActor->GetActorLocation(), CameraActor->GetActorRotation());
	}

	if (bCaptureScene)
	{
		SceneCapture->CaptureScene();
	}
}

void AGGGGameModeBase::SetupDeckLinkOutputs(const TArray<AActor*>& OrderedCameras)
{
	static constexpr int32 DesiredOutputCount = 4;
	static constexpr int32 DeckLinkDeviceIds[DesiredOutputCount] = { 1, 2, 3, 4 };
	static constexpr int32 OutputWidth = 1920;
	static constexpr int32 OutputHeight = 1080;

	DeckLinkRenderTargets.Reset();
	DeckLinkSceneCaptureActors.Reset();
	DeckLinkSceneCaptures.Reset();
	DeckLinkMediaOutputs.Reset();
	DeckLinkMediaCaptures.Reset();
	DeckLinkRenderTargets.SetNumZeroed(DesiredOutputCount);
	DeckLinkSceneCaptureActors.SetNumZeroed(DesiredOutputCount);
	DeckLinkSceneCaptures.SetNumZeroed(DesiredOutputCount);
	DeckLinkMediaOutputs.SetNumZeroed(DesiredOutputCount);
	DeckLinkMediaCaptures.SetNumZeroed(DesiredOutputCount);
	LogDeckLinkOutputConfigurations();

	if (OrderedCameras.Num() < DesiredOutputCount)
	{
		UE_LOG(LogTemp, Warning, TEXT("DeckLink output skipped: expected 4 ordered cameras, got %d."), OrderedCameras.Num());
		return;
	}

	FMediaCaptureOptions CaptureOptions;

	for (int32 Index = 0; Index < DesiredOutputCount; ++Index)
	{
		ACameraActor* CameraActor = Cast<ACameraActor>(OrderedCameras[Index]);
		if (!CameraActor)
		{
			UE_LOG(LogTemp, Warning, TEXT("DeckLink output %d skipped: camera actor is invalid."), Index + 1);
			continue;
		}

		UTextureRenderTarget2D* RenderTarget = NewObject<UTextureRenderTarget2D>(this);
		RenderTarget->RenderTargetFormat = RTF_RGB10A2;
		RenderTarget->ClearColor = FLinearColor::Black;
		RenderTarget->InitAutoFormat(OutputWidth, OutputHeight);
		RenderTarget->UpdateResourceImmediate(true);
		DeckLinkRenderTargets[Index] = RenderTarget;

		FActorSpawnParameters SpawnParameters;
		SpawnParameters.Name = FName(*FString::Printf(TEXT("DeckLinkSceneCapture%d"), Index + 1));
		ASceneCapture2D* SceneCaptureActor = GetWorld()->SpawnActor<ASceneCapture2D>(CameraActor->GetActorLocation(), CameraActor->GetActorRotation(), SpawnParameters);
		if (!SceneCaptureActor)
		{
			UE_LOG(LogTemp, Warning, TEXT("DeckLink output %d skipped: could not spawn scene capture actor."), Index + 1);
			continue;
		}

		DeckLinkSceneCaptureActors[Index] = SceneCaptureActor;
		if (UCameraComponent* CameraComponent = CameraActor->GetCameraComponent())
		{
			SceneCaptureActor->AttachToComponent(CameraComponent, FAttachmentTransformRules::SnapToTargetNotIncludingScale);
		}
		else
		{
			SceneCaptureActor->AttachToActor(CameraActor, FAttachmentTransformRules::SnapToTargetNotIncludingScale);
		}

		USceneCaptureComponent2D* SceneCapture = SceneCaptureActor->GetCaptureComponent2D();
		if (!SceneCapture)
		{
			UE_LOG(LogTemp, Warning, TEXT("DeckLink output %d skipped: scene capture actor has no capture component."), Index + 1);
			continue;
		}

		SceneCapture->TextureTarget = RenderTarget;
		SceneCapture->CaptureSource = ESceneCaptureSource::SCS_FinalColorLDR;
		SceneCapture->bCaptureEveryFrame = true;
		SceneCapture->bCaptureOnMovement = true;
		SceneCapture->bAlwaysPersistRenderingState = true;
		SceneCapture->FOVAngle = CameraActor->GetCameraComponent() ? CameraActor->GetCameraComponent()->FieldOfView : 90.0f;
		DeckLinkSceneCaptures[Index] = SceneCapture;
		SyncDeckLinkCapture(Index, false);

		UBlackmagicMediaOutput* MediaOutput = NewObject<UBlackmagicMediaOutput>(this);
		const int32 DeckLinkDeviceId = DeckLinkDeviceIds[Index];
		if (!FindDeckLinkOutputConfiguration(DeckLinkDeviceId, MediaOutput->OutputConfiguration))
		{
			UE_LOG(LogTemp, Warning, TEXT("DeckLink output %d skipped: no Blackmagic output configuration found for device %d."), Index + 1, DeckLinkDeviceId);
			continue;
		}

		MediaOutput->PixelFormat = EBlackmagicMediaOutputPixelFormat::PF_10BIT_YUV;
		MediaOutput->TimecodeFormat = EMediaIOTimecodeFormat::None;
		MediaOutput->AudioSampleRate = EBlackmagicMediaOutputAudioSampleRate::SR_48k;
		MediaOutput->OutputChannelCount = EBlackmagicMediaAudioOutputChannelCount::CH_2;
		MediaOutput->AudioBitDepth = EBlackmagicMediaOutputAudioBitDepth::Signed_16Bits;
		MediaOutput->bOutputAudio = true;
		MediaOutput->bLogDropFrame = true;
		MediaOutput->bUseMultithreadedScheduling = false;
		MediaOutput->bWaitForSyncEvent = false;
		MediaOutput->NumberOfBlackmagicBuffers = 4;
		DeckLinkMediaOutputs[Index] = MediaOutput;

		UMediaCapture* MediaCapture = MediaOutput->CreateMediaCapture();
		if (!MediaCapture)
		{
			UE_LOG(LogTemp, Warning, TEXT("DeckLink output %d failed: could not create media capture."), Index + 1);
			continue;
		}

		DeckLinkMediaCaptures[Index] = MediaCapture;
		if (!MediaCapture->CaptureTextureRenderTarget2D(RenderTarget, CaptureOptions))
		{
			UE_LOG(LogTemp, Warning, TEXT("DeckLink output %d failed to start. Check Blackmagic Desktop Video driver, card availability, and 1080i50 support."), Index + 1);
			continue;
		}

		UE_LOG(LogTemp, Display, TEXT("Camera %d is outputting to DeckLink device %d as 1080i50 10-bit YUV with stereo audio."), Index + 1, DeckLinkDeviceId);
	}
}

bool AGGGGameModeBase::FindDeckLinkInputConfiguration(int32 DeviceIdentifier, FMediaIOConfiguration& OutConfiguration) const
{
	static constexpr int32 InputWidth = 1920;
	static constexpr int32 InputHeight = 1080;
	static constexpr int32 InputFrameRateNumerator = 50;
	static constexpr int32 InputFrameRateDenominator = 1;

	FBlackmagicDeviceProvider Provider;
	const TArray<FMediaIOConfiguration> Configurations = Provider.GetConfigurations(true, false);
	static bool bLoggedInputConfigurations = false;
	if (!bLoggedInputConfigurations)
	{
		bLoggedInputConfigurations = true;
		UE_LOG(LogTemp, Display, TEXT("Blackmagic reports %d input configurations."), Configurations.Num());
		for (const FMediaIOConfiguration& Configuration : Configurations)
		{
			const FMediaIOConnection& Connection = Configuration.MediaConnection;
			const FMediaIOMode& Mode = Configuration.MediaMode;
			UE_LOG(LogTemp, Display, TEXT("Blackmagic input: device %d '%s', port %d, transport %s, mode '%s', %dx%d, rate %d/%d, standard %s, mode id %d")
				, Connection.Device.DeviceIdentifier
				, *Connection.Device.DeviceName.ToString()
				, Connection.PortIdentifier
				, MediaIOTransportToString(Connection.TransportType)
				, *Mode.GetModeName().ToString()
				, Mode.Resolution.X
				, Mode.Resolution.Y
				, Mode.FrameRate.Numerator
				, Mode.FrameRate.Denominator
				, MediaIOStandardToString(Mode.Standard)
				, Mode.DeviceModeIdentifier);
		}
	}

	const FMediaIOConfiguration* FirstForDevice = nullptr;
	for (const FMediaIOConfiguration& Configuration : Configurations)
	{
		if (Configuration.MediaConnection.Device.DeviceIdentifier != DeviceIdentifier)
		{
			continue;
		}

		if (!FirstForDevice)
		{
			FirstForDevice = &Configuration;
		}

		if (Configuration.MediaMode.Resolution == FIntPoint(InputWidth, InputHeight)
			&& IsEquivalentFrameRate(Configuration.MediaMode.FrameRate, InputFrameRateNumerator, InputFrameRateDenominator)
			&& Configuration.MediaMode.Standard == EMediaIOStandardType::Interlaced)
		{
			OutConfiguration = Configuration;
			OutConfiguration.bIsInput = true;
			UE_LOG(LogTemp, Display, TEXT("DeckLink input device %d: using exact 1080i50 interlaced mode."), DeviceIdentifier);
			return true;
		}
	}

	if (FirstForDevice)
	{
		OutConfiguration = *FirstForDevice;
		OutConfiguration.bIsInput = true;
		const FMediaIOMode& FallbackMode = FirstForDevice->MediaMode;
		UE_LOG(LogTemp, Warning, TEXT("DeckLink input device %d: 1080i50 was not found; using fallback %s %dx%d %d/%d %s mode id %d.")
			, DeviceIdentifier
			, *FallbackMode.GetModeName().ToString()
			, FallbackMode.Resolution.X
			, FallbackMode.Resolution.Y
			, FallbackMode.FrameRate.Numerator
			, FallbackMode.FrameRate.Denominator
			, MediaIOStandardToString(FallbackMode.Standard)
			, FallbackMode.DeviceModeIdentifier);
		return true;
	}

	return false;
}

bool AGGGGameModeBase::FindDeckLinkOutputConfiguration(int32 DeviceIdentifier, FMediaIOOutputConfiguration& OutConfiguration) const
{
	static constexpr int32 OutputWidth = 1920;
	static constexpr int32 OutputHeight = 1080;
	static constexpr int32 OutputFrameRateNumerator = 50;
	static constexpr int32 OutputFrameRateDenominator = 1;

	FBlackmagicDeviceProvider Provider;
	const TArray<FMediaIOOutputConfiguration> Configurations = Provider.GetOutputConfigurations();
	static bool bLoggedOutputConfigurations = false;
	if (!bLoggedOutputConfigurations)
	{
		bLoggedOutputConfigurations = true;
		for (const FMediaIOOutputConfiguration& Configuration : Configurations)
		{
			const FMediaIOConfiguration& MediaConfiguration = Configuration.MediaConfiguration;
			UE_LOG(LogTemp, Display, TEXT("Available DeckLink output: device id %d '%s', port %d, key port %d, ref %s, output %s, transport %s, mode '%s' %dx%d %d/%d %s mode id %d.")
				, MediaConfiguration.MediaConnection.Device.DeviceIdentifier
				, *MediaConfiguration.MediaConnection.Device.DeviceName.ToString()
				, MediaConfiguration.MediaConnection.PortIdentifier
				, Configuration.KeyPortIdentifier
				, MediaIOReferenceToString(Configuration.OutputReference)
				, MediaIOOutputTypeToString(Configuration.OutputType)
				, MediaIOTransportToString(MediaConfiguration.MediaConnection.TransportType)
				, *MediaConfiguration.MediaMode.GetModeName().ToString()
				, MediaConfiguration.MediaMode.Resolution.X
				, MediaConfiguration.MediaMode.Resolution.Y
				, MediaConfiguration.MediaMode.FrameRate.Numerator
				, MediaConfiguration.MediaMode.FrameRate.Denominator
				, MediaIOStandardToString(MediaConfiguration.MediaMode.Standard)
				, MediaConfiguration.MediaMode.DeviceModeIdentifier);
		}
	}

	const FMediaIOOutputConfiguration* FirstFillForDevice = nullptr;
	const FMediaIOOutputConfiguration* ExactExternalConfiguration = nullptr;
	const FMediaIOOutputConfiguration* ExactFreeRunConfiguration = nullptr;
	for (const FMediaIOOutputConfiguration& Configuration : Configurations)
	{
		const FMediaIOConfiguration& MediaConfiguration = Configuration.MediaConfiguration;
		if (MediaConfiguration.MediaConnection.Device.DeviceIdentifier != DeviceIdentifier)
		{
			continue;
		}

		if (Configuration.OutputType != EMediaIOOutputType::Fill)
		{
			continue;
		}

		if (!FirstFillForDevice)
		{
			FirstFillForDevice = &Configuration;
		}

		if (MediaConfiguration.MediaMode.Resolution == FIntPoint(OutputWidth, OutputHeight)
			&& IsEquivalentFrameRate(MediaConfiguration.MediaMode.FrameRate, OutputFrameRateNumerator, OutputFrameRateDenominator)
			&& MediaConfiguration.MediaMode.Standard == EMediaIOStandardType::Interlaced)
		{
			if (Configuration.OutputReference == EMediaIOReferenceType::FreeRun && !ExactFreeRunConfiguration)
			{
				ExactFreeRunConfiguration = &Configuration;
			}

			if (Configuration.OutputReference == EMediaIOReferenceType::External && !ExactExternalConfiguration)
			{
				ExactExternalConfiguration = &Configuration;
			}
		}
	}

	if (ExactFreeRunConfiguration)
	{
		OutConfiguration = *ExactFreeRunConfiguration;
		UE_LOG(LogTemp, Display, TEXT("DeckLink device %d: using exact 1080i50 interlaced mode with FreeRun reference."), DeviceIdentifier);
		return true;
	}

	if (ExactExternalConfiguration)
	{
		OutConfiguration = *ExactExternalConfiguration;
		UE_LOG(LogTemp, Warning, TEXT("DeckLink device %d: exact 1080i50 FreeRun reference was not found; using External 1080i50 interlaced mode."), DeviceIdentifier);
		return true;
	}

	if (FirstFillForDevice)
	{
		OutConfiguration = *FirstFillForDevice;
		const FMediaIOConfiguration& FallbackMediaConfiguration = FirstFillForDevice->MediaConfiguration;
		UE_LOG(LogTemp, Warning, TEXT("DeckLink device %d: 1080i50 was not found; using fallback %s %dx%d %d/%d %s mode id %d on port %d.")
			, DeviceIdentifier
			, *FallbackMediaConfiguration.MediaMode.GetModeName().ToString()
			, FallbackMediaConfiguration.MediaMode.Resolution.X
			, FallbackMediaConfiguration.MediaMode.Resolution.Y
			, FallbackMediaConfiguration.MediaMode.FrameRate.Numerator
			, FallbackMediaConfiguration.MediaMode.FrameRate.Denominator
			, MediaIOStandardToString(FallbackMediaConfiguration.MediaMode.Standard)
			, FallbackMediaConfiguration.MediaMode.DeviceModeIdentifier
			, FallbackMediaConfiguration.MediaConnection.PortIdentifier);
		return true;
	}

	return false;
}

void AGGGGameModeBase::LogDeckLinkOutputConfigurations() const
{
	static bool bHasLoggedDeckLinkConfigurations = false;
	if (bHasLoggedDeckLinkConfigurations)
	{
		return;
	}

	bHasLoggedDeckLinkConfigurations = true;

	FBlackmagicDeviceProvider Provider;
	const TArray<FMediaIOOutputConfiguration> Configurations = Provider.GetOutputConfigurations();
	UE_LOG(LogTemp, Display, TEXT("Blackmagic reports %d output configurations."), Configurations.Num());

	for (const FMediaIOOutputConfiguration& Configuration : Configurations)
	{
		const FMediaIOConfiguration& MediaConfiguration = Configuration.MediaConfiguration;
		const FMediaIOConnection& Connection = MediaConfiguration.MediaConnection;
		const FMediaIOMode& Mode = MediaConfiguration.MediaMode;
		UE_LOG(LogTemp, Display, TEXT("Blackmagic output: device %d '%s', port %d, transport %s, output %s, ref %s, mode '%s', %dx%d, rate %d/%d, standard %s, mode id %d")
			, Connection.Device.DeviceIdentifier
			, *Connection.Device.DeviceName.ToString()
			, Connection.PortIdentifier
			, MediaIOTransportToString(Connection.TransportType)
			, MediaIOOutputTypeToString(Configuration.OutputType)
			, MediaIOReferenceToString(Configuration.OutputReference)
			, *Mode.GetModeName().ToString()
			, Mode.Resolution.X
			, Mode.Resolution.Y
			, Mode.FrameRate.Numerator
			, Mode.FrameRate.Denominator
			, MediaIOStandardToString(Mode.Standard)
			, Mode.DeviceModeIdentifier);
	}
}
