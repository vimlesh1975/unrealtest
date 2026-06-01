#include "GGGGameModeBase.h"

#include "BlackmagicMediaOutput.h"
#include "BlackmagicDeviceProvider.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Engine/World.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Kismet/KismetMathLibrary.h"
#include "Kismet/GameplayStatics.h"
#include "MediaCapture.h"
#include "MediaIOCoreDefinitions.h"
#include "TimerManager.h"

AGGGGameModeBase::AGGGGameModeBase()
{
	DefaultPawnClass = nullptr;
}

void AGGGGameModeBase::BeginPlay()
{
	Super::BeginPlay();

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimerForNextTick(this, &AGGGGameModeBase::SetupSplitScreenCameras);
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

	for (int32 Index = 1; Index < DesiredViewCount; ++Index)
	{
		if (!UGameplayStatics::GetPlayerController(World, Index))
		{
			UGameplayStatics::CreatePlayer(World, Index, true);
		}
	}

	for (int32 Index = 0; Index < DesiredViewCount; ++Index)
	{
		APlayerController* PlayerController = UGameplayStatics::GetPlayerController(World, Index);
		if (!PlayerController)
		{
			UE_LOG(LogTemp, Warning, TEXT("Missing player controller for split-screen view %d."), Index);
			continue;
		}

		PlayerController->SetViewTarget(OrderedCameras[Index]);
		PlayerController->SetViewTargetWithBlend(OrderedCameras[Index], 0.0f);
	}

	SetupDeckLinkOutputs(OrderedCameras);
}

void AGGGGameModeBase::SetupDeckLinkOutputs(const TArray<AActor*>& OrderedCameras)
{
	static constexpr int32 DesiredOutputCount = 4;
	static constexpr int32 OutputWidth = 1920;
	static constexpr int32 OutputHeight = 1080;
	static constexpr int32 OutputFrameRate = 25;

	DeckLinkRenderTargets.Reset();
	DeckLinkSceneCaptures.Reset();
	DeckLinkMediaOutputs.Reset();
	DeckLinkMediaCaptures.Reset();

	if (OrderedCameras.Num() < DesiredOutputCount)
	{
		UE_LOG(LogTemp, Warning, TEXT("DeckLink output skipped: expected 4 ordered cameras, got %d."), OrderedCameras.Num());
		return;
	}

	FMediaCaptureOptions CaptureOptions;
	CaptureOptions.ResizeMethod = EMediaCaptureResizeMethod::None;
	CaptureOptions.Crop = EMediaCaptureCroppingType::None;
	CaptureOptions.bConvertToDesiredPixelFormat = true;
	CaptureOptions.bForceAlphaToOneOnConversion = true;

	for (int32 Index = 0; Index < DesiredOutputCount; ++Index)
	{
		ACameraActor* CameraActor = Cast<ACameraActor>(OrderedCameras[Index]);
		if (!CameraActor)
		{
			UE_LOG(LogTemp, Warning, TEXT("DeckLink output %d skipped: camera actor is invalid."), Index + 1);
			continue;
		}

		UTextureRenderTarget2D* RenderTarget = NewObject<UTextureRenderTarget2D>(this);
		RenderTarget->RenderTargetFormat = RTF_RGBA8;
		RenderTarget->ClearColor = FLinearColor::Black;
		RenderTarget->InitAutoFormat(OutputWidth, OutputHeight);
		RenderTarget->UpdateResourceImmediate(true);
		DeckLinkRenderTargets.Add(RenderTarget);

		USceneCaptureComponent2D* SceneCapture = NewObject<USceneCaptureComponent2D>(CameraActor);
		SceneCapture->TextureTarget = RenderTarget;
		SceneCapture->CaptureSource = ESceneCaptureSource::SCS_FinalColorLDR;
		SceneCapture->bCaptureEveryFrame = true;
		SceneCapture->bCaptureOnMovement = true;
		SceneCapture->FOVAngle = CameraActor->GetCameraComponent() ? CameraActor->GetCameraComponent()->FieldOfView : 90.0f;
		SceneCapture->RegisterComponent();
		SceneCapture->AttachToComponent(CameraActor->GetRootComponent(), FAttachmentTransformRules::SnapToTargetNotIncludingScale);
		DeckLinkSceneCaptures.Add(SceneCapture);

		UBlackmagicMediaOutput* MediaOutput = NewObject<UBlackmagicMediaOutput>(this);
		if (!FindDeckLinkOutputConfiguration(Index + 1, MediaOutput->OutputConfiguration))
		{
			UE_LOG(LogTemp, Warning, TEXT("DeckLink output %d skipped: no Blackmagic output configuration found for device %d."), Index + 1, Index + 1);
			continue;
		}

		MediaOutput->PixelFormat = EBlackmagicMediaOutputPixelFormat::PF_8BIT_YUV;
		MediaOutput->TimecodeFormat = EMediaIOTimecodeFormat::None;
		MediaOutput->bOutputAudio = false;
		DeckLinkMediaOutputs.Add(MediaOutput);

		UMediaCapture* MediaCapture = MediaOutput->CreateMediaCapture();
		if (!MediaCapture)
		{
			UE_LOG(LogTemp, Warning, TEXT("DeckLink output %d failed: could not create media capture."), Index + 1);
			continue;
		}

		DeckLinkMediaCaptures.Add(MediaCapture);
		if (!MediaCapture->CaptureTextureRenderTarget2D(RenderTarget, CaptureOptions))
		{
			UE_LOG(LogTemp, Warning, TEXT("DeckLink output %d failed to start. Check Blackmagic Desktop Video driver, card availability, and 1080i50 support."), Index + 1);
			continue;
		}

		UE_LOG(LogTemp, Display, TEXT("Camera %d is outputting to DeckLink device %d."), Index + 1, Index + 1);
	}
}

bool AGGGGameModeBase::FindDeckLinkOutputConfiguration(int32 DeviceIdentifier, FMediaIOOutputConfiguration& OutConfiguration) const
{
	static constexpr int32 OutputWidth = 1920;
	static constexpr int32 OutputHeight = 1080;
	static constexpr int32 OutputFrameRate = 25;

	FBlackmagicDeviceProvider Provider;
	const TArray<FMediaIOOutputConfiguration> Configurations = Provider.GetOutputConfigurations();

	const FMediaIOOutputConfiguration* FirstFillForDevice = nullptr;
	for (const FMediaIOOutputConfiguration& Configuration : Configurations)
	{
		const FMediaIOConfiguration& MediaConfiguration = Configuration.MediaConfiguration;
		if (MediaConfiguration.MediaConnection.Device.DeviceIdentifier != DeviceIdentifier)
		{
			continue;
		}

		if (Configuration.OutputType != EMediaIOOutputType::Fill || Configuration.OutputReference != EMediaIOReferenceType::FreeRun)
		{
			continue;
		}

		if (!FirstFillForDevice)
		{
			FirstFillForDevice = &Configuration;
		}

		if (MediaConfiguration.MediaMode.Resolution == FIntPoint(OutputWidth, OutputHeight)
			&& MediaConfiguration.MediaMode.FrameRate == FFrameRate(OutputFrameRate, 1)
			&& MediaConfiguration.MediaMode.Standard == EMediaIOStandardType::Interlaced)
		{
			OutConfiguration = Configuration;
			return true;
		}
	}

	if (FirstFillForDevice)
	{
		OutConfiguration = *FirstFillForDevice;
		UE_LOG(LogTemp, Warning, TEXT("DeckLink device %d: 1080i50 was not found; using the first available fill output mode instead."), DeviceIdentifier);
		return true;
	}

	return false;
}
