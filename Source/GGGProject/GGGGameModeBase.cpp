#include "GGGGameModeBase.h"

#include "BlackmagicMediaOutput.h"
#include "BlackmagicDeviceProvider.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Engine/Engine.h"
#include "Engine/SceneCapture2D.h"
#include "Engine/World.h"
#include "Engine/TextureRenderTarget2D.h"
#include "GameFramework/PlayerController.h"
#include "GenlockedFixedRateCustomTimeStep.h"
#include "HAL/IConsoleManager.h"
#include "InputCoreTypes.h"
#include "Kismet/KismetMathLibrary.h"
#include "Kismet/GameplayStatics.h"
#include "MediaCapture.h"
#include "MediaIOCoreDefinitions.h"
#include "TimerManager.h"

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
}

AGGGGameModeBase::AGGGGameModeBase()
{
	DefaultPawnClass = nullptr;
	PrimaryActorTick.bCanEverTick = true;
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
		GEngine->SetMaxFPS(0.0f);

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
	ShowCameraControlStatus();
	UpdateSelectedViewportCamera();
	UE_LOG(LogTemp, Display, TEXT("Viewport preview is using the selected camera; DeckLink outputs receive the four camera feeds."));

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
			SelectedCameraIndex = CameraSelectKey.Value;
			UpdateSelectedViewportCamera();
			ShowCameraControlStatus();
		}
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

void AGGGGameModeBase::ShowCameraControlStatus() const
{
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(1001, 2.0f, FColor::Cyan, FString::Printf(TEXT("Controlling camera %d"), SelectedCameraIndex + 1));
	}
}

void AGGGGameModeBase::UpdateSelectedViewportCamera() const
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	ACameraActor* SelectedCamera = ControlledCameras.IsValidIndex(SelectedCameraIndex) ? ControlledCameras[SelectedCameraIndex].Get() : nullptr;
	APlayerController* PlayerController = UGameplayStatics::GetPlayerController(World, 0);
	if (SelectedCamera && PlayerController)
	{
		PlayerController->SetViewTargetWithBlend(SelectedCamera, 0.0f);
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
		MediaOutput->bOutputAudio = false;
		MediaOutput->bLogDropFrame = true;
		MediaOutput->bUseMultithreadedScheduling = false;
		MediaOutput->bWaitForSyncEvent = false;
		MediaOutput->NumberOfBlackmagicBuffers = 3;
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

		UE_LOG(LogTemp, Display, TEXT("Camera %d is outputting to DeckLink device %d as 1080i50 10-bit YUV."), Index + 1, DeckLinkDeviceId);
	}
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
