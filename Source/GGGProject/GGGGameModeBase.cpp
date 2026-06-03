#include "GGGGameModeBase.h"

#include "Async/Async.h"
#include "BlackmagicMediaOutput.h"
#include "BlackmagicMediaSource.h"
#include "BlackmagicDeviceProvider.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Components/SceneComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Containers/StringConv.h"
#include "Dom/JsonObject.h"
#include "Engine/Engine.h"
#include "Engine/Canvas.h"
#include "Engine/GameViewportClient.h"
#include "Engine/PointLight.h"
#include "Engine/SceneCapture2D.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "Engine/TextureRenderTarget2D.h"
#include "FileMediaSource.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/GameUserSettings.h"
#include "GenlockedFixedRateCustomTimeStep.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "HttpResultCallback.h"
#include "HttpServerModule.h"
#include "HttpServerRequest.h"
#include "HttpServerResponse.h"
#include "IHttpRouter.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "ImageUtils.h"
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
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/ScopeLock.h"
#include "Modules/ModuleManager.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
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

const FVector StudioCameraFocusPoint(0.0f, 610.0f, 250.0f);
const FVector DeckLinkInputPlateDefaultLocation(-430.0f, 610.0f, 255.0f);
const FRotator DeckLinkInputPlateDefaultRotation(0.0f, 0.0f, 90.0f);
const FVector DeckLinkInputPlateDefaultScale(-4.2f, 2.3625f, 1.0f);
constexpr float DeckLinkInputPlateMinScale = 0.2f;
constexpr float DeckLinkInputPlateMaxScale = 5.0f;
const TCHAR* ChromaKeyPlateRelativePath = TEXT("media/mixkit-female-reporter-reporting-with-microphone-in-hand-on-a-chroma-28293-full-hd_i.mp4");
const FVector ChromaKeyPlateDefaultLocation(0.0f, 320.0f, 255.0f);
const FRotator ChromaKeyPlateDefaultRotation(0.0f, 0.0f, 90.0f);
const FVector ChromaKeyPlateDefaultScale(-5.6f, 1.575f, 1.0f);
constexpr float ChromaKeyPlateMinScale = 0.2f;
constexpr float ChromaKeyPlateMaxScale = 5.0f;
constexpr bool bDefaultChromaKeyEnabled = false;
constexpr float DefaultChromaKeyTolerance = 0.12f;
constexpr float DefaultChromaKeySoftness = 0.22f;
constexpr float DefaultChromaKeyDespill = 0.75f;
const FName ChromaKeyMediaPlayerName(TEXT("ElectraPlayer"));
constexpr double ChromaKeyMediaStallSeconds = 1.25;
constexpr double ChromaKeyMediaRestartCooldownSeconds = 1.75;
constexpr double ChromaKeyMediaCommandCooldownSeconds = 2.0;
const TCHAR* ExpressLoopMediaRelativePath = TEXT("media/go1080p25.mp4");
const FName ExpressLoopMediaPlayerName(TEXT("WmfMedia"));
const FVector ExpressLoopMediaPlateLocation(430.0f, 610.0f, 255.0f);
const FRotator ExpressLoopMediaPlateRotation(0.0f, 0.0f, 90.0f);
const FVector ExpressLoopMediaPlateScale(-4.2f, 2.3625f, 1.0f);
constexpr float ExpressLoopMediaPlateMinScale = 0.2f;
constexpr float ExpressLoopMediaPlateMaxScale = 5.0f;
constexpr int64 ExpressLoopMediaPrecacheMaxBytes = 256LL * 1024LL * 1024LL;
constexpr double ExpressLoopMediaLoopLeadMilliseconds = 2000.0;
constexpr float ExpressLoopMediaWatchdogSeconds = 0.05f;
constexpr float ExpressLoopMediaSwapWarmupSeconds = 0.35f;
constexpr float ExpressLoopMediaSwapRetrySeconds = 0.15f;
constexpr int32 ExpressLoopMediaSwapMaxWarmupAttempts = 30;
constexpr double ExpressLoopMediaMinWarmFrameSeconds = 0.05;
constexpr float ExpressLoopFrameAnimationFps = 25.0f;

float ClampControlDeltaSeconds(float DeltaSeconds)
{
	return FMath::Clamp(DeltaSeconds, 0.0f, 0.25f);
}

bool ShouldPrecacheExpressLoopMediaFile(const FString& FilePath)
{
	const int64 FileSize = IFileManager::Get().FileSize(*FilePath);
	return FileSize >= 0 && FileSize <= ExpressLoopMediaPrecacheMaxBytes;
}

float SmoothStepClamped(float Edge0, float Edge1, float Value)
{
	const float Range = FMath::Max(Edge1 - Edge0, KINDA_SMALL_NUMBER);
	const float Alpha = FMath::Clamp((Value - Edge0) / Range, 0.0f, 1.0f);
	return Alpha * Alpha * (3.0f - 2.0f * Alpha);
}

float ReadJsonNumber(const TSharedPtr<FJsonObject>& Object, const TCHAR* FieldName, float DefaultValue = 0.0f)
{
	if (!Object.IsValid())
	{
		return DefaultValue;
	}

	double Value = DefaultValue;
	return Object->TryGetNumberField(FieldName, Value) ? static_cast<float>(Value) : DefaultValue;
}

FString BuildVectorJson(const FVector& Value)
{
	return FString::Printf(TEXT("{\"x\":%.3f,\"y\":%.3f,\"z\":%.3f}"), Value.X, Value.Y, Value.Z);
}

FString BuildRotatorJson(const FRotator& Value)
{
	return FString::Printf(TEXT("{\"pitch\":%.3f,\"yaw\":%.3f,\"roll\":%.3f}"), Value.Pitch, Value.Yaw, Value.Roll);
}

FString BuildJsonString(const FString& Value)
{
	FString EscapedValue;
	EscapedValue.Reserve(Value.Len() + 8);
	for (const TCHAR Character : Value)
	{
		switch (Character)
		{
		case TEXT('"'):
			EscapedValue += TEXT("\\\"");
			break;
		case TEXT('\\'):
			EscapedValue += TEXT("\\\\");
			break;
		case TEXT('\n'):
			EscapedValue += TEXT("\\n");
			break;
		case TEXT('\r'):
			EscapedValue += TEXT("\\r");
			break;
		case TEXT('\t'):
			EscapedValue += TEXT("\\t");
			break;
		default:
			if (Character < 0x20)
			{
				EscapedValue += FString::Printf(TEXT("\\u%04x"), static_cast<uint32>(Character));
			}
			else
			{
				EscapedValue.AppendChar(Character);
			}
			break;
		}
	}

	return FString::Printf(TEXT("\"%s\""), *EscapedValue);
}

void AddSingleHttpHeader(FHttpServerResponse& Response, const TCHAR* Key, const TCHAR* Value)
{
	TArray<FString> HeaderValues;
	HeaderValues.Add(Value);
	Response.Headers.Add(Key, MoveTemp(HeaderValues));
}

void AddNoCacheHeaders(FHttpServerResponse& Response)
{
	AddSingleHttpHeader(Response, TEXT("Cache-Control"), TEXT("no-store, no-cache, must-revalidate, max-age=0"));
	AddSingleHttpHeader(Response, TEXT("Pragma"), TEXT("no-cache"));
	AddSingleHttpHeader(Response, TEXT("Expires"), TEXT("0"));
}

FString SanitizeUploadedFileName(const FString& UploadedFileName)
{
	FString CleanFileName = FPaths::GetCleanFilename(UploadedFileName);
	CleanFileName.TrimStartAndEndInline();
	if (CleanFileName.IsEmpty())
	{
		CleanFileName = TEXT("selected.mp4");
	}

	FString SanitizedFileName;
	SanitizedFileName.Reserve(CleanFileName.Len());
	for (const TCHAR Character : CleanFileName)
	{
		const bool bAllowed =
			(Character >= TEXT('a') && Character <= TEXT('z'))
			|| (Character >= TEXT('A') && Character <= TEXT('Z'))
			|| (Character >= TEXT('0') && Character <= TEXT('9'))
			|| Character == TEXT('.')
			|| Character == TEXT('_')
			|| Character == TEXT('-');

		SanitizedFileName.AppendChar(bAllowed ? Character : TEXT('_'));
	}

	if (!SanitizedFileName.EndsWith(TEXT(".mp4"), ESearchCase::IgnoreCase))
	{
		SanitizedFileName += TEXT(".mp4");
	}

	return SanitizedFileName;
}

void ApplyMediaTextureToMaterial(UMaterialInstanceDynamic* DynamicMaterial, UTexture* MediaTexture)
{
	if (!DynamicMaterial || !MediaTexture)
	{
		return;
	}

	DynamicMaterial->SetTextureParameterValue(TEXT("InputTexture"), MediaTexture);
	DynamicMaterial->SetTextureParameterValue(TEXT("MediaTexture"), MediaTexture);
	DynamicMaterial->SetTextureParameterValue(TEXT("SlateUI"), MediaTexture);
	DynamicMaterial->SetVectorParameterValue(TEXT("Color"), FLinearColor::White);
	DynamicMaterial->SetVectorParameterValue(TEXT("TintColorAndOpacity"), FLinearColor::White);
	DynamicMaterial->SetScalarParameterValue(TEXT("Opacity"), 1.0f);
	DynamicMaterial->SetScalarParameterValue(TEXT("OpacityFromTexture"), 1.0f);
}

void ApplyChromaKeyTextureToMaterial(UMaterialInstanceDynamic* DynamicMaterial, UTexture* PlateTexture)
{
	if (!DynamicMaterial || !PlateTexture)
	{
		return;
	}

	ApplyMediaTextureToMaterial(DynamicMaterial, PlateTexture);
	DynamicMaterial->SetTextureParameterValue(TEXT("Texture"), PlateTexture);
	DynamicMaterial->SetScalarParameterValue(TEXT("Opacity"), 1.0f);
}

void ApplyChromaKeySettingsToMaterial(UMaterialInstanceDynamic* DynamicMaterial, bool bEnabled, float Tolerance, float Softness, float Despill)
{
	if (!DynamicMaterial)
	{
		return;
	}

	DynamicMaterial->SetScalarParameterValue(TEXT("KeyEnabled"), bEnabled ? 1.0f : 0.0f);
	DynamicMaterial->SetScalarParameterValue(TEXT("Tolerance"), FMath::Clamp(Tolerance, 0.0f, 1.0f));
	DynamicMaterial->SetScalarParameterValue(TEXT("Softness"), FMath::Clamp(Softness, 0.001f, 1.0f));
	DynamicMaterial->SetScalarParameterValue(TEXT("Despill"), FMath::Clamp(Despill, 0.0f, 1.0f));
}
}

AGGGGameModeBase::AGGGGameModeBase()
{
	DefaultPawnClass = nullptr;
	HUDClass = AGGGPreviewHUD::StaticClass();
	PrimaryActorTick.bCanEverTick = true;

	static ConstructorHelpers::FObjectFinder<UStaticMesh> DisplayMeshFinder(TEXT("/Engine/BasicShapes/Plane.Plane"));
	DeckLinkInputDisplayMesh = DisplayMeshFinder.Object;

	static ConstructorHelpers::FObjectFinder<UMaterialInterface> MediaPlateMaterialFinder(TEXT("/MediaPlate/M_MediaPlate.M_MediaPlate"));
	DeckLinkInputScreenMaterial = MediaPlateMaterialFinder.Object;
	if (!DeckLinkInputScreenMaterial)
	{
		static ConstructorHelpers::FObjectFinder<UMaterialInterface> MediaPlaneMaterialFinder(TEXT("/MediaCompositing/DefaultMediaPlaneMaterial.DefaultMediaPlaneMaterial"));
		DeckLinkInputScreenMaterial = MediaPlaneMaterialFinder.Object;
	}
	if (!DeckLinkInputScreenMaterial)
	{
		static ConstructorHelpers::FObjectFinder<UMaterialInterface> ScreenMaterialFinder(TEXT("/Engine/EngineMaterials/Widget3DPassThrough_Opaque.Widget3DPassThrough_Opaque"));
		DeckLinkInputScreenMaterial = ScreenMaterialFinder.Object;
	}

	static ConstructorHelpers::FObjectFinder<UMaterialInterface> ChromaKeyMaterialFinder(TEXT("/Game/Materials/M_LiveChromaKey.M_LiveChromaKey"));
	ChromaKeyScreenMaterial = ChromaKeyMaterialFinder.Object;
	if (!ChromaKeyScreenMaterial)
	{
		static ConstructorHelpers::FObjectFinder<UMaterialInterface> ChromaKeyFallbackMaterialFinder(TEXT("/Engine/EngineMaterials/Widget3DPassThrough_Masked_OneSided.Widget3DPassThrough_Masked_OneSided"));
		ChromaKeyScreenMaterial = ChromaKeyFallbackMaterialFinder.Object;
	}
	if (!ChromaKeyScreenMaterial)
	{
		static ConstructorHelpers::FObjectFinder<UMaterialInterface> ChromaKeyFallbackMaterialFinder(TEXT("/Engine/EngineMaterials/Widget3DPassThrough_Masked.Widget3DPassThrough_Masked"));
		ChromaKeyScreenMaterial = ChromaKeyFallbackMaterialFinder.Object;
	}

	static ConstructorHelpers::FObjectFinder<UStaticMesh> RoomMeshFinder(TEXT("/Engine/BasicShapes/Cube.Cube"));
	StudioRoomMesh = RoomMeshFinder.Object;

	static ConstructorHelpers::FObjectFinder<UMaterialInterface> RoomMaterialFinder(TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	StudioRoomMaterial = RoomMaterialFinder.Object;
}

const TArray<TObjectPtr<UTextureRenderTarget2D>>& AGGGGameModeBase::GetDeckLinkPreviewTargets() const
{
	return DeckLinkRenderTargets;
}

void AGGGGameModeBase::BeginPlay()
{
	Super::BeginPlay();

	ConfigureDeckLinkTiming();
	ConfigureWindowAndMouse();
	StartWebControlServer();
	UpdateCachedWebControlState();

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimerForNextTick(this, &AGGGGameModeBase::SetupSplitScreenCameras);
	}
}

void AGGGGameModeBase::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(DeckLinkInputAudioKickTimerHandle);
		World->GetTimerManager().ClearTimer(ExpressLoopMediaWatchdogTimerHandle);
		World->GetTimerManager().ClearTimer(ExpressLoopMediaSwapTimerHandle);
	}

	StopWebControlServer();
	Super::EndPlay(EndPlayReason);
}

void AGGGGameModeBase::ConfigureWindowAndMouse()
{
	if (GEngine)
	{
		if (UGameUserSettings* GameUserSettings = GEngine->GetGameUserSettings())
		{
			GameUserSettings->SetFullscreenMode(EWindowMode::Windowed);
			GameUserSettings->SetScreenResolution(FIntPoint(1280, 720));
			GameUserSettings->ApplySettings(false);
		}
	}

	if (GEngine && GEngine->GameViewport)
	{
		GEngine->GameViewport->SetMouseCaptureMode(EMouseCaptureMode::NoCapture);
		GEngine->GameViewport->SetMouseLockMode(EMouseLockMode::DoNotLock);
		GEngine->GameViewport->SetHideCursorDuringCapture(false);
	}

	UWorld* World = GetWorld();
	APlayerController* PlayerController = World ? UGameplayStatics::GetPlayerController(World, 0) : nullptr;
	if (PlayerController)
	{
		PlayerController->bShowMouseCursor = true;
		PlayerController->bEnableClickEvents = true;
		PlayerController->bEnableMouseOverEvents = true;

		FInputModeGameAndUI InputMode;
		InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
		InputMode.SetHideCursorDuringCapture(false);
		PlayerController->SetInputMode(InputMode);
	}

	static bool bLoggedWindowMouseSetup = false;
	if (!bLoggedWindowMouseSetup)
	{
		bLoggedWindowMouseSetup = true;
		UE_LOG(LogTemp, Display, TEXT("Window/mouse: forced 1280x720 windowed mode with an unlocked visible cursor."));
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

void AGGGGameModeBase::SetupStudioRoom()
{
	if (StudioRoomActors.Num() > 0)
	{
		return;
	}

	UWorld* World = GetWorld();
	if (!World || !StudioRoomMesh)
	{
		UE_LOG(LogTemp, Warning, TEXT("Studio room skipped: room mesh is unavailable."));
		return;
	}

	AddStudioRoomSurface(TEXT("StudioBottomFloor"), FVector(0.0f, -250.0f, -12.0f), FVector(18.0f, 20.0f, 0.08f), FLinearColor(0.22f, 0.25f, 0.31f, 1.0f));
	AddStudioRoomSurface(TEXT("StudioFrontScreenWall"), FVector(0.0f, 710.0f, 315.0f), FVector(18.0f, 0.08f, 6.6f), FLinearColor(0.85f, 0.03f, 0.02f, 1.0f));
	AddStudioRoomSurface(TEXT("StudioCameraSideWall"), FVector(0.0f, -1250.0f, 315.0f), FVector(18.0f, 0.08f, 6.6f), FLinearColor(0.35f, 0.29f, 0.12f, 1.0f));
	AddStudioRoomSurface(TEXT("StudioLeftWall"), FVector(-900.0f, -250.0f, 315.0f), FVector(0.08f, 20.0f, 6.6f), FLinearColor(0.11f, 0.24f, 0.43f, 1.0f));
	AddStudioRoomSurface(TEXT("StudioRightWall"), FVector(900.0f, -250.0f, 315.0f), FVector(0.08f, 20.0f, 6.6f), FLinearColor(0.13f, 0.34f, 0.22f, 1.0f));
	AddStudioRoomSurface(TEXT("StudioTopCeiling"), FVector(0.0f, -250.0f, 660.0f), FVector(18.0f, 20.0f, 0.08f), FLinearColor(0.30f, 0.21f, 0.39f, 1.0f));

	AddStudioRoomLight(TEXT("StudioKeyLightLeft"), FVector(-360.0f, -430.0f, 560.0f), 9000.0f, 1800.0f);
	AddStudioRoomLight(TEXT("StudioKeyLightRight"), FVector(360.0f, -430.0f, 560.0f), 9000.0f, 1800.0f);
	AddStudioRoomLight(TEXT("StudioBackWallFill"), FVector(0.0f, 250.0f, 520.0f), 4500.0f, 1200.0f);

	UE_LOG(LogTemp, Display, TEXT("Studio room: large color-coded room spawned with separate bottom, top, left, right, screen/front, and camera-side walls."));
}

void AGGGGameModeBase::AddStudioRoomSurface(const FName& SurfaceName, const FVector& Location, const FVector& Scale, const FLinearColor& Color)
{
	UWorld* World = GetWorld();
	if (!World || !StudioRoomMesh)
	{
		return;
	}

	FActorSpawnParameters SpawnParameters;
	SpawnParameters.Name = SurfaceName;
	SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AActor* SurfaceActor = World->SpawnActor<AActor>(AActor::StaticClass(), Location, FRotator::ZeroRotator, SpawnParameters);
	if (!SurfaceActor)
	{
		return;
	}

	USceneComponent* SurfaceRootComponent = NewObject<USceneComponent>(SurfaceActor, FName(*FString::Printf(TEXT("%sRoot"), *SurfaceName.ToString())));
	SurfaceActor->SetRootComponent(SurfaceRootComponent);
	SurfaceActor->AddInstanceComponent(SurfaceRootComponent);
	SurfaceRootComponent->SetMobility(EComponentMobility::Static);
	SurfaceRootComponent->SetWorldLocation(Location);
	SurfaceRootComponent->SetWorldRotation(FRotator::ZeroRotator);
	SurfaceRootComponent->RegisterComponent();

	UStaticMeshComponent* SurfaceMeshComponent = NewObject<UStaticMeshComponent>(SurfaceActor, FName(*FString::Printf(TEXT("%sMesh"), *SurfaceName.ToString())));
	SurfaceMeshComponent->SetupAttachment(SurfaceRootComponent);
	SurfaceMeshComponent->SetStaticMesh(StudioRoomMesh);
	SurfaceMeshComponent->SetRelativeScale3D(Scale);
	SurfaceMeshComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	SurfaceMeshComponent->SetGenerateOverlapEvents(false);
	SurfaceMeshComponent->SetCastShadow(false);
	SurfaceMeshComponent->bReceivesDecals = false;
	SurfaceActor->AddInstanceComponent(SurfaceMeshComponent);
	SurfaceMeshComponent->RegisterComponent();

	if (StudioRoomMaterial)
	{
		UMaterialInstanceDynamic* DynamicMaterial = SurfaceMeshComponent->CreateAndSetMaterialInstanceDynamicFromMaterial(0, StudioRoomMaterial);
		if (DynamicMaterial)
		{
			DynamicMaterial->SetVectorParameterValue(TEXT("Color"), Color);
			DynamicMaterial->SetVectorParameterValue(TEXT("BaseColor"), Color);
		}
	}

	StudioRoomActors.Add(SurfaceActor);
}

void AGGGGameModeBase::AddStudioRoomLight(const FName& LightName, const FVector& Location, float Intensity, float Radius)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	FActorSpawnParameters SpawnParameters;
	SpawnParameters.Name = LightName;
	SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	APointLight* LightActor = World->SpawnActor<APointLight>(Location, FRotator::ZeroRotator, SpawnParameters);
	if (!LightActor)
	{
		return;
	}

	if (UPointLightComponent* LightComponent = LightActor->PointLightComponent)
	{
		LightComponent->SetIntensity(Intensity);
		LightComponent->SetAttenuationRadius(Radius);
		LightComponent->SetLightColor(FLinearColor(1.0f, 0.96f, 0.90f).ToFColor(true));
		LightComponent->SetCastShadows(false);
	}

	StudioRoomActors.Add(LightActor);
}

void AGGGGameModeBase::ArrangeStudioCameras(const TArray<AActor*>& OrderedCameras)
{
	static const FVector CameraLocations[] = {
		FVector(0.0f, -1060.0f, 250.0f),
		FVector(-280.0f, -980.0f, 245.0f),
		FVector(280.0f, -980.0f, 245.0f),
		FVector(0.0f, -880.0f, 430.0f)
	};

	for (int32 Index = 0; Index < UE_ARRAY_COUNT(CameraLocations); ++Index)
	{
		ACameraActor* CameraActor = OrderedCameras.IsValidIndex(Index) ? Cast<ACameraActor>(OrderedCameras[Index]) : nullptr;
		if (!CameraActor)
		{
			continue;
		}

		const FVector& CameraLocation = CameraLocations[Index];
		CameraActor->SetActorLocation(CameraLocation);
		CameraActor->SetActorRotation(UKismetMathLibrary::FindLookAtRotation(CameraLocation, StudioCameraFocusPoint));

		if (UCameraComponent* CameraComponent = CameraActor->GetCameraComponent())
		{
			CameraComponent->SetFieldOfView(62.0f);
			CameraComponent->SetAspectRatio(16.0f / 9.0f);
			CameraComponent->bConstrainAspectRatio = false;
			CameraComponent->PostProcessSettings.bOverride_AutoExposureMethod = true;
			CameraComponent->PostProcessSettings.AutoExposureMethod = AEM_Manual;
			CameraComponent->PostProcessSettings.bOverride_AutoExposureBias = true;
			CameraComponent->PostProcessSettings.AutoExposureBias = 0.0f;
			CameraComponent->PostProcessSettings.bOverride_AutoExposureApplyPhysicalCameraExposure = true;
			CameraComponent->PostProcessSettings.AutoExposureApplyPhysicalCameraExposure = false;
		}
	}

	UE_LOG(LogTemp, Display, TEXT("Studio cameras: cameras 1-4 placed in front of the back wall with slightly different angles."));
}

void AGGGGameModeBase::SetupSplitScreenCameras()
{
	static constexpr int32 DesiredViewCount = 4;

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	SetupStudioRoom();

	TArray<AActor*> Cameras;
	UGameplayStatics::GetAllActorsOfClass(World, ACameraActor::StaticClass(), Cameras);
	if (Cameras.Num() < DesiredViewCount)
	{
		UE_LOG(LogTemp, Warning, TEXT("Expected 4 camera actors, found %d."), Cameras.Num());
		return;
	}

	for (AActor* Camera : Cameras)
	{
		if (Camera)
		{
			Camera->SetActorRotation(UKismetMathLibrary::FindLookAtRotation(Camera->GetActorLocation(), StudioCameraFocusPoint));
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

	ArrangeStudioCameras(OrderedCameras);

	ControlledCameras.Reset();
	for (AActor* OrderedCamera : OrderedCameras)
	{
		ControlledCameras.Add(Cast<ACameraActor>(OrderedCamera));
	}
	SelectedCameraIndex = 0;
	bControllingDeckLinkInputPlate = false;
	bControllingChromaKeyPlate = false;
	bControllingExpressLoopMediaPlate = false;
	ShowCameraControlStatus();
	UpdateSelectedViewportCamera();
	ConfigureWindowAndMouse();
	UE_LOG(LogTemp, Display, TEXT("Viewport monitor grid is showing cameras 1-4 from the DeckLink render targets."));

	SetupDeckLinkInputScreen();
	SetupChromaKeyPlate();
	SetupExpressLoopMediaPlate();
	SetupDeckLinkOutputs(OrderedCameras);
}

void AGGGGameModeBase::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	HandleCameraControl(DeltaSeconds);
	ProcessWebControlCommands();
	if (bUsingExpressLoopFrameAnimation)
	{
		UpdateExpressLoopFrameAnimation(DeltaSeconds);
	}
	else
	{
		KeepExpressLoopMediaLooping();
	}
	KeepChromaKeyMediaLooping();
	SyncDeckLinkCaptures();
	UpdateCachedWebControlState();
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
		SelectDeckLinkInputPlateControl(true);
	}

	if (PlayerController->WasInputKeyJustPressed(EKeys::Six) && ExpressLoopMediaPlateActor && ExpressLoopMediaPlateMeshComponent)
	{
		SelectExpressLoopMediaPlateControl(true);
	}

	if (PlayerController->WasInputKeyJustPressed(EKeys::Seven) && ChromaKeyPlateActor && ChromaKeyPlateMeshComponent)
	{
		SelectChromaKeyPlateControl(true);
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
			SelectCameraControl(CameraSelectKey.Value, true);
		}
	}

	if (bControllingDeckLinkInputPlate)
	{
		HandleDeckLinkInputPlateControl(PlayerController, DeltaSeconds);
		return;
	}

	if (bControllingChromaKeyPlate)
	{
		HandleChromaKeyPlateControl(PlayerController, DeltaSeconds);
		return;
	}

	if (bControllingExpressLoopMediaPlate)
	{
		HandleExpressLoopMediaPlateControl(PlayerController, DeltaSeconds);
		return;
	}

	ACameraActor* SelectedCamera = ControlledCameras.IsValidIndex(SelectedCameraIndex) ? ControlledCameras[SelectedCameraIndex].Get() : nullptr;
	if (!SelectedCamera)
	{
		return;
	}

	const bool bFastMove = PlayerController->IsInputKeyDown(EKeys::LeftShift) || PlayerController->IsInputKeyDown(EKeys::RightShift);

	const FVector MoveInput(
		(PlayerController->IsInputKeyDown(EKeys::W) ? 1.0f : 0.0f) - (PlayerController->IsInputKeyDown(EKeys::S) ? 1.0f : 0.0f),
		(PlayerController->IsInputKeyDown(EKeys::D) ? 1.0f : 0.0f) - (PlayerController->IsInputKeyDown(EKeys::A) ? 1.0f : 0.0f),
		(PlayerController->IsInputKeyDown(EKeys::E) ? 1.0f : 0.0f) - (PlayerController->IsInputKeyDown(EKeys::Q) ? 1.0f : 0.0f));

	FRotator RotationDelta = FRotator::ZeroRotator;
	RotationDelta.Pitch += PlayerController->IsInputKeyDown(EKeys::Up) ? -1.0f : 0.0f;
	RotationDelta.Pitch += PlayerController->IsInputKeyDown(EKeys::Down) ? 1.0f : 0.0f;
	RotationDelta.Yaw += PlayerController->IsInputKeyDown(EKeys::Right) ? 1.0f : 0.0f;
	RotationDelta.Yaw += PlayerController->IsInputKeyDown(EKeys::Left) ? -1.0f : 0.0f;

	ApplyCameraControl(SelectedCameraIndex, MoveInput, RotationDelta, bFastMove, DeltaSeconds);

	if (PlayerController->WasInputKeyJustPressed(EKeys::R))
	{
		AimCameraAtFocusPoint(SelectedCameraIndex);
	}
}

void AGGGGameModeBase::HandleDeckLinkInputPlateControl(APlayerController* PlayerController, float DeltaSeconds)
{
	if (!PlayerController || !DeckLinkInputScreenActor || !DeckLinkInputScreenMeshComponent)
	{
		return;
	}

	const bool bFastMove = PlayerController->IsInputKeyDown(EKeys::LeftShift) || PlayerController->IsInputKeyDown(EKeys::RightShift);
	const FVector MoveInput(
		(PlayerController->IsInputKeyDown(EKeys::W) ? 1.0f : 0.0f) - (PlayerController->IsInputKeyDown(EKeys::S) ? 1.0f : 0.0f),
		(PlayerController->IsInputKeyDown(EKeys::D) ? 1.0f : 0.0f) - (PlayerController->IsInputKeyDown(EKeys::A) ? 1.0f : 0.0f),
		(PlayerController->IsInputKeyDown(EKeys::E) ? 1.0f : 0.0f) - (PlayerController->IsInputKeyDown(EKeys::Q) ? 1.0f : 0.0f));

	FRotator RotationDelta = FRotator::ZeroRotator;
	RotationDelta.Pitch += PlayerController->IsInputKeyDown(EKeys::Up) ? -1.0f : 0.0f;
	RotationDelta.Pitch += PlayerController->IsInputKeyDown(EKeys::Down) ? 1.0f : 0.0f;
	RotationDelta.Yaw += PlayerController->IsInputKeyDown(EKeys::Right) ? 1.0f : 0.0f;
	RotationDelta.Yaw += PlayerController->IsInputKeyDown(EKeys::Left) ? -1.0f : 0.0f;
	RotationDelta.Roll += PlayerController->IsInputKeyDown(EKeys::C) ? 1.0f : 0.0f;
	RotationDelta.Roll += PlayerController->IsInputKeyDown(EKeys::Z) ? -1.0f : 0.0f;

	float ScaleDirection = 0.0f;
	ScaleDirection += (PlayerController->IsInputKeyDown(EKeys::Equals) || PlayerController->IsInputKeyDown(EKeys::Add)) ? 1.0f : 0.0f;
	ScaleDirection -= (PlayerController->IsInputKeyDown(EKeys::Hyphen) || PlayerController->IsInputKeyDown(EKeys::Subtract)) ? 1.0f : 0.0f;
	ApplyDeckLinkInputPlateControl(MoveInput, RotationDelta, ScaleDirection, bFastMove, DeltaSeconds);

	if (PlayerController->WasInputKeyJustPressed(EKeys::R))
	{
		ResetDeckLinkInputPlate();
		ShowDeckLinkInputPlateControlStatus();
	}
}

void AGGGGameModeBase::HandleChromaKeyPlateControl(APlayerController* PlayerController, float DeltaSeconds)
{
	if (!PlayerController || !ChromaKeyPlateActor || !ChromaKeyPlateMeshComponent)
	{
		return;
	}

	const bool bFastMove = PlayerController->IsInputKeyDown(EKeys::LeftShift) || PlayerController->IsInputKeyDown(EKeys::RightShift);
	const FVector MoveInput(
		(PlayerController->IsInputKeyDown(EKeys::W) ? 1.0f : 0.0f) - (PlayerController->IsInputKeyDown(EKeys::S) ? 1.0f : 0.0f),
		(PlayerController->IsInputKeyDown(EKeys::D) ? 1.0f : 0.0f) - (PlayerController->IsInputKeyDown(EKeys::A) ? 1.0f : 0.0f),
		(PlayerController->IsInputKeyDown(EKeys::E) ? 1.0f : 0.0f) - (PlayerController->IsInputKeyDown(EKeys::Q) ? 1.0f : 0.0f));

	FRotator RotationDelta = FRotator::ZeroRotator;
	RotationDelta.Pitch += PlayerController->IsInputKeyDown(EKeys::Up) ? -1.0f : 0.0f;
	RotationDelta.Pitch += PlayerController->IsInputKeyDown(EKeys::Down) ? 1.0f : 0.0f;
	RotationDelta.Yaw += PlayerController->IsInputKeyDown(EKeys::Right) ? 1.0f : 0.0f;
	RotationDelta.Yaw += PlayerController->IsInputKeyDown(EKeys::Left) ? -1.0f : 0.0f;
	RotationDelta.Roll += PlayerController->IsInputKeyDown(EKeys::C) ? 1.0f : 0.0f;
	RotationDelta.Roll += PlayerController->IsInputKeyDown(EKeys::Z) ? -1.0f : 0.0f;

	float ScaleDirection = 0.0f;
	ScaleDirection += (PlayerController->IsInputKeyDown(EKeys::Equals) || PlayerController->IsInputKeyDown(EKeys::Add)) ? 1.0f : 0.0f;
	ScaleDirection -= (PlayerController->IsInputKeyDown(EKeys::Hyphen) || PlayerController->IsInputKeyDown(EKeys::Subtract)) ? 1.0f : 0.0f;
	ApplyChromaKeyPlateControl(MoveInput, RotationDelta, ScaleDirection, bFastMove, DeltaSeconds);

	if (PlayerController->WasInputKeyJustPressed(EKeys::R))
	{
		ResetChromaKeyPlate();
		ShowChromaKeyPlateControlStatus();
	}
}

void AGGGGameModeBase::HandleExpressLoopMediaPlateControl(APlayerController* PlayerController, float DeltaSeconds)
{
	if (!PlayerController || !ExpressLoopMediaPlateActor || !ExpressLoopMediaPlateMeshComponent)
	{
		return;
	}

	const bool bFastMove = PlayerController->IsInputKeyDown(EKeys::LeftShift) || PlayerController->IsInputKeyDown(EKeys::RightShift);
	const FVector MoveInput(
		(PlayerController->IsInputKeyDown(EKeys::W) ? 1.0f : 0.0f) - (PlayerController->IsInputKeyDown(EKeys::S) ? 1.0f : 0.0f),
		(PlayerController->IsInputKeyDown(EKeys::D) ? 1.0f : 0.0f) - (PlayerController->IsInputKeyDown(EKeys::A) ? 1.0f : 0.0f),
		(PlayerController->IsInputKeyDown(EKeys::E) ? 1.0f : 0.0f) - (PlayerController->IsInputKeyDown(EKeys::Q) ? 1.0f : 0.0f));

	FRotator RotationDelta = FRotator::ZeroRotator;
	RotationDelta.Pitch += PlayerController->IsInputKeyDown(EKeys::Up) ? -1.0f : 0.0f;
	RotationDelta.Pitch += PlayerController->IsInputKeyDown(EKeys::Down) ? 1.0f : 0.0f;
	RotationDelta.Yaw += PlayerController->IsInputKeyDown(EKeys::Right) ? 1.0f : 0.0f;
	RotationDelta.Yaw += PlayerController->IsInputKeyDown(EKeys::Left) ? -1.0f : 0.0f;
	RotationDelta.Roll += PlayerController->IsInputKeyDown(EKeys::C) ? 1.0f : 0.0f;
	RotationDelta.Roll += PlayerController->IsInputKeyDown(EKeys::Z) ? -1.0f : 0.0f;

	float ScaleDirection = 0.0f;
	ScaleDirection += (PlayerController->IsInputKeyDown(EKeys::Equals) || PlayerController->IsInputKeyDown(EKeys::Add)) ? 1.0f : 0.0f;
	ScaleDirection -= (PlayerController->IsInputKeyDown(EKeys::Hyphen) || PlayerController->IsInputKeyDown(EKeys::Subtract)) ? 1.0f : 0.0f;
	ApplyExpressLoopMediaPlateControl(MoveInput, RotationDelta, ScaleDirection, bFastMove, DeltaSeconds);

	if (PlayerController->WasInputKeyJustPressed(EKeys::R))
	{
		ResetExpressLoopMediaPlate();
		ShowExpressLoopMediaPlateControlStatus();
	}
}

void AGGGGameModeBase::SelectCameraControl(int32 CameraIndex, bool bShowStatus)
{
	if (!ControlledCameras.IsValidIndex(CameraIndex))
	{
		return;
	}

	bControllingDeckLinkInputPlate = false;
	bControllingChromaKeyPlate = false;
	bControllingExpressLoopMediaPlate = false;
	SelectedCameraIndex = CameraIndex;
	UpdateSelectedViewportCamera();

	if (bShowStatus)
	{
		ShowCameraControlStatus();
	}
}

void AGGGGameModeBase::SelectDeckLinkInputPlateControl(bool bShowStatus)
{
	if (!DeckLinkInputScreenActor || !DeckLinkInputScreenMeshComponent)
	{
		return;
	}

	bControllingDeckLinkInputPlate = true;
	bControllingChromaKeyPlate = false;
	bControllingExpressLoopMediaPlate = false;

	if (bShowStatus)
	{
		ShowDeckLinkInputPlateControlStatus();
	}
}

void AGGGGameModeBase::SelectChromaKeyPlateControl(bool bShowStatus)
{
	if (!ChromaKeyPlateActor || !ChromaKeyPlateMeshComponent)
	{
		return;
	}

	bControllingDeckLinkInputPlate = false;
	bControllingChromaKeyPlate = true;
	bControllingExpressLoopMediaPlate = false;

	if (bShowStatus)
	{
		ShowChromaKeyPlateControlStatus();
	}
}

void AGGGGameModeBase::SelectExpressLoopMediaPlateControl(bool bShowStatus)
{
	if (!ExpressLoopMediaPlateActor || !ExpressLoopMediaPlateMeshComponent)
	{
		return;
	}

	bControllingDeckLinkInputPlate = false;
	bControllingChromaKeyPlate = false;
	bControllingExpressLoopMediaPlate = true;

	if (bShowStatus)
	{
		ShowExpressLoopMediaPlateControlStatus();
	}
}

void AGGGGameModeBase::ApplyCameraControl(int32 CameraIndex, const FVector& MoveInput, const FRotator& RotationInput, bool bFastMove, float DeltaSeconds)
{
	ACameraActor* SelectedCamera = ControlledCameras.IsValidIndex(CameraIndex) ? ControlledCameras[CameraIndex].Get() : nullptr;
	if (!SelectedCamera)
	{
		return;
	}

	const float ClampedDeltaSeconds = ClampControlDeltaSeconds(DeltaSeconds);
	const float MoveSpeed = bFastMove ? 1200.0f : 300.0f;
	const float RotateSpeed = bFastMove ? 90.0f : 30.0f;

	FVector MoveDirection = FVector::ZeroVector;
	MoveDirection += SelectedCamera->GetActorForwardVector() * MoveInput.X;
	MoveDirection += SelectedCamera->GetActorRightVector() * MoveInput.Y;
	MoveDirection += FVector::UpVector * MoveInput.Z;

	if (!MoveDirection.IsNearlyZero())
	{
		SelectedCamera->AddActorWorldOffset(MoveDirection.GetSafeNormal() * MoveSpeed * ClampedDeltaSeconds, false);
	}

	if (!RotationInput.IsNearlyZero())
	{
		SelectedCamera->AddActorWorldRotation(RotationInput * RotateSpeed * ClampedDeltaSeconds);
	}
}

void AGGGGameModeBase::AimCameraAtFocusPoint(int32 CameraIndex)
{
	ACameraActor* SelectedCamera = ControlledCameras.IsValidIndex(CameraIndex) ? ControlledCameras[CameraIndex].Get() : nullptr;
	if (!SelectedCamera)
	{
		return;
	}

	SelectedCamera->SetActorRotation(UKismetMathLibrary::FindLookAtRotation(SelectedCamera->GetActorLocation(), StudioCameraFocusPoint));
}

void AGGGGameModeBase::ApplyDeckLinkInputPlateControl(const FVector& MoveInput, const FRotator& RotationInput, float ScaleDirection, bool bFastMove, float DeltaSeconds)
{
	if (!DeckLinkInputScreenActor || !DeckLinkInputScreenMeshComponent)
	{
		return;
	}

	const float ClampedDeltaSeconds = ClampControlDeltaSeconds(DeltaSeconds);
	const float MoveSpeed = bFastMove ? 900.0f : 250.0f;
	const float RotateSpeed = bFastMove ? 120.0f : 40.0f;
	const float ScaleSpeed = bFastMove ? 1.5f : 0.5f;

	const FRotator PlateYaw(0.0f, DeckLinkInputScreenMeshComponent->GetComponentRotation().Yaw, 0.0f);
	const FVector PlateForward = FRotationMatrix(PlateYaw).GetUnitAxis(EAxis::X);
	const FVector PlateRight = FRotationMatrix(PlateYaw).GetUnitAxis(EAxis::Y);

	FVector MoveDirection = FVector::ZeroVector;
	MoveDirection += PlateForward * MoveInput.X;
	MoveDirection += PlateRight * MoveInput.Y;
	MoveDirection += FVector::UpVector * MoveInput.Z;

	if (!MoveDirection.IsNearlyZero())
	{
		DeckLinkInputScreenActor->AddActorWorldOffset(MoveDirection.GetSafeNormal() * MoveSpeed * ClampedDeltaSeconds, false);
	}

	if (!RotationInput.IsNearlyZero())
	{
		DeckLinkInputScreenMeshComponent->AddRelativeRotation(RotationInput * RotateSpeed * ClampedDeltaSeconds);
	}

	if (!FMath::IsNearlyZero(ScaleDirection))
	{
		DeckLinkInputPlateScaleFactor = FMath::Clamp(
			DeckLinkInputPlateScaleFactor + ScaleDirection * ScaleSpeed * ClampedDeltaSeconds,
			DeckLinkInputPlateMinScale,
			DeckLinkInputPlateMaxScale);
		DeckLinkInputScreenMeshComponent->SetRelativeScale3D(DeckLinkInputPlateDefaultScale * DeckLinkInputPlateScaleFactor);
	}
}

void AGGGGameModeBase::ApplyChromaKeyPlateControl(const FVector& MoveInput, const FRotator& RotationInput, float ScaleDirection, bool bFastMove, float DeltaSeconds)
{
	if (!ChromaKeyPlateActor || !ChromaKeyPlateMeshComponent)
	{
		return;
	}

	const float ClampedDeltaSeconds = ClampControlDeltaSeconds(DeltaSeconds);
	const float MoveSpeed = bFastMove ? 900.0f : 250.0f;
	const float RotateSpeed = bFastMove ? 120.0f : 40.0f;
	const float ScaleSpeed = bFastMove ? 1.5f : 0.5f;

	const FRotator PlateYaw(0.0f, ChromaKeyPlateMeshComponent->GetComponentRotation().Yaw, 0.0f);
	const FVector PlateForward = FRotationMatrix(PlateYaw).GetUnitAxis(EAxis::X);
	const FVector PlateRight = FRotationMatrix(PlateYaw).GetUnitAxis(EAxis::Y);

	FVector MoveDirection = FVector::ZeroVector;
	MoveDirection += PlateForward * MoveInput.X;
	MoveDirection += PlateRight * MoveInput.Y;
	MoveDirection += FVector::UpVector * MoveInput.Z;

	if (!MoveDirection.IsNearlyZero())
	{
		ChromaKeyPlateActor->AddActorWorldOffset(MoveDirection.GetSafeNormal() * MoveSpeed * ClampedDeltaSeconds, false);
	}

	if (!RotationInput.IsNearlyZero())
	{
		ChromaKeyPlateMeshComponent->AddRelativeRotation(RotationInput * RotateSpeed * ClampedDeltaSeconds);
	}

	if (!FMath::IsNearlyZero(ScaleDirection))
	{
		ChromaKeyPlateScaleFactor = FMath::Clamp(
			ChromaKeyPlateScaleFactor + ScaleDirection * ScaleSpeed * ClampedDeltaSeconds,
			ChromaKeyPlateMinScale,
			ChromaKeyPlateMaxScale);
		ChromaKeyPlateMeshComponent->SetRelativeScale3D(ChromaKeyPlateDefaultScale * ChromaKeyPlateScaleFactor);
	}
}

void AGGGGameModeBase::ApplyExpressLoopMediaPlateControl(const FVector& MoveInput, const FRotator& RotationInput, float ScaleDirection, bool bFastMove, float DeltaSeconds)
{
	if (!ExpressLoopMediaPlateActor || !ExpressLoopMediaPlateMeshComponent)
	{
		return;
	}

	const float ClampedDeltaSeconds = ClampControlDeltaSeconds(DeltaSeconds);
	const float MoveSpeed = bFastMove ? 900.0f : 250.0f;
	const float RotateSpeed = bFastMove ? 120.0f : 40.0f;
	const float ScaleSpeed = bFastMove ? 1.5f : 0.5f;

	const FRotator PlateYaw(0.0f, ExpressLoopMediaPlateMeshComponent->GetComponentRotation().Yaw, 0.0f);
	const FVector PlateForward = FRotationMatrix(PlateYaw).GetUnitAxis(EAxis::X);
	const FVector PlateRight = FRotationMatrix(PlateYaw).GetUnitAxis(EAxis::Y);

	FVector MoveDirection = FVector::ZeroVector;
	MoveDirection += PlateForward * MoveInput.X;
	MoveDirection += PlateRight * MoveInput.Y;
	MoveDirection += FVector::UpVector * MoveInput.Z;

	if (!MoveDirection.IsNearlyZero())
	{
		ExpressLoopMediaPlateActor->AddActorWorldOffset(MoveDirection.GetSafeNormal() * MoveSpeed * ClampedDeltaSeconds, false);
	}

	if (!RotationInput.IsNearlyZero())
	{
		ExpressLoopMediaPlateMeshComponent->AddRelativeRotation(RotationInput * RotateSpeed * ClampedDeltaSeconds);
	}

	if (!FMath::IsNearlyZero(ScaleDirection))
	{
		ExpressLoopMediaPlateScaleFactor = FMath::Clamp(
			ExpressLoopMediaPlateScaleFactor + ScaleDirection * ScaleSpeed * ClampedDeltaSeconds,
			ExpressLoopMediaPlateMinScale,
			ExpressLoopMediaPlateMaxScale);
		ExpressLoopMediaPlateMeshComponent->SetRelativeScale3D(ExpressLoopMediaPlateScale * ExpressLoopMediaPlateScaleFactor);
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

void AGGGGameModeBase::ResetChromaKeyPlate()
{
	if (ChromaKeyPlateActor)
	{
		ChromaKeyPlateActor->SetActorLocation(ChromaKeyPlateDefaultLocation);
		ChromaKeyPlateActor->SetActorRotation(FRotator::ZeroRotator);
	}

	if (ChromaKeyPlateMeshComponent)
	{
		ChromaKeyPlateMeshComponent->SetRelativeRotation(ChromaKeyPlateDefaultRotation);
		ChromaKeyPlateMeshComponent->SetRelativeScale3D(ChromaKeyPlateDefaultScale);
	}

	ChromaKeyPlateScaleFactor = 1.0f;
}

void AGGGGameModeBase::ResetExpressLoopMediaPlate()
{
	if (ExpressLoopMediaPlateActor)
	{
		ExpressLoopMediaPlateActor->SetActorLocation(ExpressLoopMediaPlateLocation);
		ExpressLoopMediaPlateActor->SetActorRotation(FRotator::ZeroRotator);
	}

	if (ExpressLoopMediaPlateMeshComponent)
	{
		ExpressLoopMediaPlateMeshComponent->SetRelativeRotation(ExpressLoopMediaPlateRotation);
		ExpressLoopMediaPlateMeshComponent->SetRelativeScale3D(ExpressLoopMediaPlateScale);
	}

	ExpressLoopMediaPlateScaleFactor = 1.0f;
}

FString AGGGGameModeBase::ResolveExpressLoopMediaFilePath(const FString& FilePath) const
{
	FString CleanPath = FilePath;
	CleanPath.TrimStartAndEndInline();
	if (CleanPath.StartsWith(TEXT("\"")) && CleanPath.EndsWith(TEXT("\"")) && CleanPath.Len() >= 2)
	{
		CleanPath = CleanPath.Mid(1, CleanPath.Len() - 2);
		CleanPath.TrimStartAndEndInline();
	}

	if (CleanPath.IsEmpty())
	{
		CleanPath = ExpressLoopMediaRelativePath;
	}

	FString ResolvedPath = FPaths::IsRelative(CleanPath)
		? FPaths::Combine(FPaths::ProjectContentDir(), CleanPath)
		: CleanPath;

	ResolvedPath = FPaths::ConvertRelativePathToFull(ResolvedPath);
	FPaths::NormalizeFilename(ResolvedPath);
	return ResolvedPath;
}

bool AGGGGameModeBase::LoadChromaKeySourceImage(const FString& ImagePath)
{
	TArray<uint8> CompressedBytes;
	if (!FFileHelper::LoadFileToArray(CompressedBytes, *ImagePath) || CompressedBytes.Num() == 0)
	{
		return false;
	}

	IImageWrapperModule& ImageWrapperModule = FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));
	const EImageFormat ImageFormat = ImageWrapperModule.DetectImageFormat(CompressedBytes.GetData(), CompressedBytes.Num());
	if (ImageFormat == EImageFormat::Invalid)
	{
		return false;
	}

	TSharedPtr<IImageWrapper> ImageWrapper = ImageWrapperModule.CreateImageWrapper(ImageFormat);
	if (!ImageWrapper.IsValid() || !ImageWrapper->SetCompressed(CompressedBytes.GetData(), CompressedBytes.Num()))
	{
		return false;
	}

	TArray64<uint8> RawPixels;
	if (!ImageWrapper->GetRaw(ERGBFormat::BGRA, 8, RawPixels))
	{
		return false;
	}

	ChromaKeyTextureWidth = ImageWrapper->GetWidth();
	ChromaKeyTextureHeight = ImageWrapper->GetHeight();
	const int32 PixelCount = ChromaKeyTextureWidth * ChromaKeyTextureHeight;
	if (PixelCount <= 0 || RawPixels.Num() != PixelCount * static_cast<int64>(sizeof(FColor)))
	{
		ChromaKeyTextureWidth = 0;
		ChromaKeyTextureHeight = 0;
		return false;
	}

	ChromaKeySourcePixels.SetNumUninitialized(PixelCount);
	FMemory::Memcpy(ChromaKeySourcePixels.GetData(), RawPixels.GetData(), RawPixels.Num());

	ChromaKeyPlateTexture = UTexture2D::CreateTransient(ChromaKeyTextureWidth, ChromaKeyTextureHeight, PF_B8G8R8A8, TEXT("ChromaKeyPlateTexture"));
	if (!ChromaKeyPlateTexture)
	{
		ChromaKeySourcePixels.Reset();
		ChromaKeyTextureWidth = 0;
		ChromaKeyTextureHeight = 0;
		return false;
	}

	ChromaKeyPlateTexture->SRGB = true;
	ChromaKeyPlateTexture->NeverStream = true;
	RebuildChromaKeyTexture();
	return true;
}

void AGGGGameModeBase::RebuildChromaKeyTexture()
{
	if (!ChromaKeyPlateTexture || ChromaKeySourcePixels.Num() == 0 || ChromaKeyTextureWidth <= 0 || ChromaKeyTextureHeight <= 0)
	{
		return;
	}

	const int32 PixelCount = ChromaKeyTextureWidth * ChromaKeyTextureHeight;
	if (ChromaKeySourcePixels.Num() != PixelCount)
	{
		return;
	}

	TArray<FColor> KeyedPixels;
	KeyedPixels.SetNumUninitialized(PixelCount);
	const float Tolerance = FMath::Clamp(ChromaKeyTolerance, 0.0f, 1.0f);
	const float Softness = FMath::Clamp(ChromaKeySoftness, 0.001f, 1.0f);
	const float Despill = FMath::Clamp(ChromaKeyDespill, 0.0f, 1.0f);

	for (int32 PixelIndex = 0; PixelIndex < PixelCount; ++PixelIndex)
	{
		const FColor SourcePixel = ChromaKeySourcePixels[PixelIndex];
		float Red = SourcePixel.R / 255.0f;
		float Green = SourcePixel.G / 255.0f;
		float Blue = SourcePixel.B / 255.0f;
		float Alpha = SourcePixel.A / 255.0f;

		if (bChromaKeyEnabled)
		{
			const float GreenDominance = Green - FMath::Max(Red, Blue);
			const float KeyAmount = SmoothStepClamped(Tolerance, Tolerance + Softness, GreenDominance);
			const float SpillAmount = FMath::Clamp(GreenDominance * 3.0f, 0.0f, 1.0f) * Despill * (1.0f - KeyAmount * 0.65f);
			Green = FMath::Lerp(Green, FMath::Max(Red, Blue), SpillAmount);
			Alpha *= 1.0f - KeyAmount;
		}

		KeyedPixels[PixelIndex] = FColor(
			static_cast<uint8>(FMath::Clamp(FMath::RoundToInt(Red * 255.0f), 0, 255)),
			static_cast<uint8>(FMath::Clamp(FMath::RoundToInt(Green * 255.0f), 0, 255)),
			static_cast<uint8>(FMath::Clamp(FMath::RoundToInt(Blue * 255.0f), 0, 255)),
			static_cast<uint8>(FMath::Clamp(FMath::RoundToInt(Alpha * 255.0f), 0, 255)));
	}

	FTexturePlatformData* PlatformData = ChromaKeyPlateTexture->GetPlatformData();
	if (!PlatformData || PlatformData->Mips.Num() == 0)
	{
		return;
	}

	FTexture2DMipMap& Mip = PlatformData->Mips[0];
	void* TextureData = Mip.BulkData.Lock(LOCK_READ_WRITE);
	if (TextureData)
	{
		FMemory::Memcpy(TextureData, KeyedPixels.GetData(), KeyedPixels.Num() * sizeof(FColor));
	}
	Mip.BulkData.Unlock();
	ChromaKeyPlateTexture->UpdateResource();

	if (ChromaKeyPlateMaterial)
	{
		ApplyChromaKeyTextureToMaterial(ChromaKeyPlateMaterial, ChromaKeyPlateTexture);
		ApplyChromaKeySettingsToMaterial(ChromaKeyPlateMaterial, bChromaKeyEnabled, ChromaKeyTolerance, ChromaKeySoftness, ChromaKeyDespill);
	}
}

void AGGGGameModeBase::ApplyChromaKeySettings(const FGGGWebControlCommand& Command, bool bShowStatus)
{
	if (Command.bHasChromaKeyEnabled)
	{
		bChromaKeyEnabled = Command.bChromaKeyEnabled;
	}
	if (Command.ChromaKeyTolerance >= 0.0f)
	{
		ChromaKeyTolerance = FMath::Clamp(Command.ChromaKeyTolerance, 0.0f, 1.0f);
	}
	if (Command.ChromaKeySoftness >= 0.0f)
	{
		ChromaKeySoftness = FMath::Clamp(Command.ChromaKeySoftness, 0.001f, 1.0f);
	}
	if (Command.ChromaKeyDespill >= 0.0f)
	{
		ChromaKeyDespill = FMath::Clamp(Command.ChromaKeyDespill, 0.0f, 1.0f);
	}

	RebuildChromaKeyTexture();
	ApplyChromaKeySettingsToMaterial(ChromaKeyPlateMaterial, bChromaKeyEnabled, ChromaKeyTolerance, ChromaKeySoftness, ChromaKeyDespill);
	if (GEngine && bShowStatus)
	{
		GEngine->AddOnScreenDebugMessage(
			1003,
			2.0f,
			FColor::Green,
			FString::Printf(TEXT("Chroma key: %s tol %.2f soft %.2f despill %.2f"),
				bChromaKeyEnabled ? TEXT("on") : TEXT("off"),
				ChromaKeyTolerance,
				ChromaKeySoftness,
				ChromaKeyDespill));
	}
}

void AGGGGameModeBase::KeepChromaKeyMediaLooping()
{
	if (!ChromaKeyMediaPlayer || !ChromaKeyMediaSource || ChromaKeyMediaFilePath.IsEmpty())
	{
		return;
	}

	const double NowSeconds = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
	const FTimespan Duration = ChromaKeyMediaPlayer->GetDuration();
	const FTimespan CurrentTime = ChromaKeyMediaPlayer->GetTime();
	const bool bHasPlayableDuration = Duration > FTimespan::FromSeconds(1.0);
	const bool bTimeChanged = !bChromaKeyMediaTimeInitialized || CurrentTime != ChromaKeyLastMediaTime;
	if (bTimeChanged)
	{
		bChromaKeyMediaTimeInitialized = true;
		ChromaKeyLastMediaTime = CurrentTime;
		ChromaKeyLastAdvanceWorldTimeSeconds = NowSeconds;
	}

	if (bHasPlayableDuration && CurrentTime >= Duration - FTimespan::FromMilliseconds(250.0))
	{
		if (NowSeconds - ChromaKeyLastPlaybackCommandWorldTimeSeconds < ChromaKeyMediaCommandCooldownSeconds)
		{
			return;
		}

		ChromaKeyLastPlaybackCommandWorldTimeSeconds = NowSeconds;
		ChromaKeyMediaPlayer->Seek(FTimespan::Zero());
		ChromaKeyMediaPlayer->Play();
		ChromaKeyMediaPlayer->SetRate(1.0f);
		bChromaKeyMediaTimeInitialized = false;
		ChromaKeyLastAdvanceWorldTimeSeconds = NowSeconds;
		return;
	}

	if (!ChromaKeyMediaPlayer->IsPlaying())
	{
		if (NowSeconds - ChromaKeyLastPlaybackCommandWorldTimeSeconds < ChromaKeyMediaCommandCooldownSeconds)
		{
			return;
		}

		if (bHasPlayableDuration && CurrentTime >= Duration - FTimespan::FromMilliseconds(500.0))
		{
			ChromaKeyMediaPlayer->Seek(FTimespan::Zero());
		}
		ChromaKeyLastPlaybackCommandWorldTimeSeconds = NowSeconds;
		ChromaKeyMediaPlayer->Play();
		ChromaKeyMediaPlayer->SetRate(1.0f);
		if (!ChromaKeyMediaPlayer->IsPlaying())
		{
			RestartChromaKeyMediaPlayback(TEXT("player stopped"));
		}
		return;
	}

	const bool bInMiddleOfClip = bHasPlayableDuration
		&& CurrentTime > FTimespan::FromMilliseconds(250.0)
		&& CurrentTime < Duration - FTimespan::FromMilliseconds(500.0);
	if (bInMiddleOfClip
		&& bChromaKeyMediaTimeInitialized
		&& NowSeconds - ChromaKeyLastAdvanceWorldTimeSeconds > ChromaKeyMediaStallSeconds)
	{
		RestartChromaKeyMediaPlayback(TEXT("media time stalled"));
	}
}

void AGGGGameModeBase::RestartChromaKeyMediaPlayback(const TCHAR* Reason)
{
	if (!ChromaKeyMediaPlayer || !ChromaKeyMediaSource || ChromaKeyMediaFilePath.IsEmpty())
	{
		return;
	}

	const double NowSeconds = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
	if (NowSeconds - ChromaKeyLastRestartWorldTimeSeconds < ChromaKeyMediaRestartCooldownSeconds)
	{
		return;
	}

	ChromaKeyLastRestartWorldTimeSeconds = NowSeconds;
	ChromaKeyLastPlaybackCommandWorldTimeSeconds = NowSeconds;
	++ChromaKeyMediaRestartCounter;
	bChromaKeyMediaTimeInitialized = false;
	ChromaKeyLastMediaTime = FTimespan::Zero();
	ChromaKeyLastAdvanceWorldTimeSeconds = NowSeconds;

	ChromaKeyMediaPlayer->Close();
	ChromaKeyMediaSource->SetFilePath(ChromaKeyMediaFilePath);
	ChromaKeyMediaSource->PrecacheFile = false;
	ChromaKeyMediaPlayer->PlayOnOpen = true;
	ChromaKeyMediaPlayer->SetDesiredPlayerName(ChromaKeyMediaPlayerName);
	ChromaKeyMediaPlayer->SetLooping(false);

	const bool bOpened = ChromaKeyMediaPlayer->OpenSource(ChromaKeyMediaSource);
	if (bOpened)
	{
		ChromaKeyMediaPlayer->Play();
		ChromaKeyMediaPlayer->SetRate(1.0f);
	}

	UE_LOG(LogTemp, Display, TEXT("Chroma key reporter video restart %d (%s): open=%s player=%s."),
		ChromaKeyMediaRestartCounter,
		Reason ? Reason : TEXT("watchdog"),
		bOpened ? TEXT("yes") : TEXT("no"),
		*ChromaKeyMediaPlayer->GetDesiredPlayerName().ToString());
}

bool AGGGGameModeBase::OpenExpressLoopMediaFile(const FString& FilePath, bool bShowStatus)
{
	if (!ExpressLoopMediaSource || !ExpressLoopMediaPlayer || !ExpressLoopStandbyMediaSource || !ExpressLoopStandbyMediaPlayer)
	{
		ExpressLoopMediaStatus = TEXT("MP4 player is not ready yet.");
		UE_LOG(LogTemp, Warning, TEXT("%s"), *ExpressLoopMediaStatus);
		return false;
	}

	const FString FullMediaPath = ResolveExpressLoopMediaFilePath(FilePath);
	if (!FPaths::FileExists(FullMediaPath))
	{
		ExpressLoopMediaStatus = FString::Printf(TEXT("MP4 file not found: %s"), *FullMediaPath);
		UE_LOG(LogTemp, Warning, TEXT("%s"), *ExpressLoopMediaStatus);
		if (GEngine && bShowStatus)
		{
			GEngine->AddOnScreenDebugMessage(1002, 3.0f, FColor::Red, ExpressLoopMediaStatus);
		}
		return false;
	}

	StopExpressLoopFrameAnimation();
	bExpressLoopMediaRestarting = false;
	ExpressLoopMediaSwapWarmupAttempts = 0;
	ExpressLoopMediaLoopCounter = 0;
	ExpressLoopKnownDurationSeconds = 0.0;
	if (UWorld* World = GetWorld())
	{
		ExpressLoopLastSwitchWorldTimeSeconds = World->GetTimeSeconds();
	}

	RefreshExpressLoopMediaPlayerDelegates();
	ConfigureExpressLoopMediaSource(ExpressLoopMediaSource, FullMediaPath);
	ConfigureExpressLoopMediaPlayer(ExpressLoopMediaPlayer, true);
	ExpressLoopMediaPlayer->Close();
	ExpressLoopStandbyMediaPlayer->Close();
	if (ExpressLoopMediaPlateMaterial && ExpressLoopMediaTexture)
	{
		ApplyMediaTextureToMaterial(ExpressLoopMediaPlateMaterial, ExpressLoopMediaTexture);
	}

	const bool bOpened = ExpressLoopMediaPlayer->OpenSource(ExpressLoopMediaSource);
	if (!bOpened)
	{
		if (TryOpenExpressLoopFrameAnimation(FullMediaPath, bShowStatus))
		{
			return true;
		}

		ExpressLoopMediaStatus = FString::Printf(TEXT("MP4 failed to open: %s"), *FullMediaPath);
		UE_LOG(LogTemp, Warning, TEXT("%s"), *ExpressLoopMediaStatus);
		if (GEngine && bShowStatus)
		{
			GEngine->AddOnScreenDebugMessage(1002, 3.0f, FColor::Red, ExpressLoopMediaStatus);
		}
		return false;
	}

	ExpressLoopMediaPlayer->Play();
	ExpressLoopMediaPlayer->SetRate(1.0f);
	StartExpressLoopMediaWatchdog();
	ExpressLoopMediaFilePath = FullMediaPath;
	ExpressLoopMediaStatus = FString::Printf(TEXT("Playing %s"), *FullMediaPath);
	UE_LOG(LogTemp, Display, TEXT("Express loop media plate is playing %s in loop using %s."), *FullMediaPath, *ExpressLoopMediaPlayer->GetDesiredPlayerName().ToString());
	if (GEngine && bShowStatus)
	{
		GEngine->AddOnScreenDebugMessage(1002, 3.0f, FColor::Cyan, FString::Printf(TEXT("MP4 loaded: %s"), *FPaths::GetCleanFilename(FullMediaPath)));
	}
	return true;
}

void AGGGGameModeBase::ConfigureExpressLoopMediaPlayer(UMediaPlayer* MediaPlayer, bool bPlayOnOpen) const
{
	if (!MediaPlayer)
	{
		return;
	}

	MediaPlayer->PlayOnOpen = bPlayOnOpen;
	MediaPlayer->SetDesiredPlayerName(ExpressLoopMediaPlayerName);
	MediaPlayer->SetLooping(false);
}

void AGGGGameModeBase::ConfigureExpressLoopMediaSource(UFileMediaSource* MediaSource, const FString& FilePath) const
{
	if (!MediaSource)
	{
		return;
	}

	MediaSource->SetFilePath(FilePath);
	MediaSource->PrecacheFile = ShouldPrecacheExpressLoopMediaFile(FilePath);
}

bool AGGGGameModeBase::TryOpenExpressLoopFrameAnimation(const FString& FilePath, bool bShowStatus)
{
	if (!ExpressLoopMediaPlateMaterial)
	{
		return false;
	}

	const FString FrameDirectory = FPaths::Combine(FPaths::GetPath(FilePath), FPaths::GetBaseFilename(FilePath) + TEXT("_frames"));
	if (!FPaths::DirectoryExists(FrameDirectory))
	{
		return false;
	}

	TArray<FString> FrameFileNames;
	IFileManager::Get().FindFiles(FrameFileNames, *FPaths::Combine(FrameDirectory, TEXT("*.jpg")), true, false);
	if (FrameFileNames.Num() == 0)
	{
		IFileManager::Get().FindFiles(FrameFileNames, *FPaths::Combine(FrameDirectory, TEXT("*.png")), true, false);
	}
	FrameFileNames.Sort();
	if (FrameFileNames.Num() == 0)
	{
		return false;
	}

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(ExpressLoopMediaWatchdogTimerHandle);
		World->GetTimerManager().ClearTimer(ExpressLoopMediaSwapTimerHandle);
	}

	if (ExpressLoopMediaPlayer)
	{
		ExpressLoopMediaPlayer->Close();
	}
	if (ExpressLoopStandbyMediaPlayer)
	{
		ExpressLoopStandbyMediaPlayer->Close();
	}

	ExpressLoopFrameTextures.Reset();
	ExpressLoopFrameTextures.Reserve(FrameFileNames.Num());
	for (const FString& FrameFileName : FrameFileNames)
	{
		const FString FramePath = FPaths::Combine(FrameDirectory, FrameFileName);
		UTexture2D* FrameTexture = FImageUtils::ImportFileAsTexture2D(FramePath);
		if (!FrameTexture)
		{
			UE_LOG(LogTemp, Warning, TEXT("Express loop frame animation skipped bad frame: %s"), *FramePath);
			continue;
		}

		FrameTexture->SRGB = true;
		FrameTexture->NeverStream = true;
		ExpressLoopFrameTextures.Add(FrameTexture);
	}

	if (ExpressLoopFrameTextures.Num() == 0)
	{
		return false;
	}

	bUsingExpressLoopFrameAnimation = true;
	bExpressLoopMediaRestarting = false;
	ExpressLoopMediaFilePath = FilePath;
	ExpressLoopFrameAnimationTimeSeconds = 0.0f;
	ExpressLoopFrameAnimationFrameIndex = INDEX_NONE;
	ApplyExpressLoopFrameTexture(0);

	ExpressLoopMediaStatus = FString::Printf(TEXT("Playing frame loop %s (%d frames)"), *FilePath, ExpressLoopFrameTextures.Num());
	UE_LOG(LogTemp, Display, TEXT("Express loop media plate is using frame animation from %s with %d frames at %.2f fps."),
		*FrameDirectory,
		ExpressLoopFrameTextures.Num(),
		ExpressLoopFrameAnimationFps);
	if (GEngine && bShowStatus)
	{
		GEngine->AddOnScreenDebugMessage(1002, 3.0f, FColor::Cyan, FString::Printf(TEXT("Frame loop loaded: %s"), *FPaths::GetCleanFilename(FilePath)));
	}
	return true;
}

void AGGGGameModeBase::UpdateExpressLoopFrameAnimation(float DeltaSeconds)
{
	if (!bUsingExpressLoopFrameAnimation || ExpressLoopFrameTextures.Num() == 0 || ExpressLoopFrameAnimationFps <= 0.0f)
	{
		return;
	}

	const float FrameDurationSeconds = 1.0f / ExpressLoopFrameAnimationFps;
	const float LoopDurationSeconds = FrameDurationSeconds * static_cast<float>(ExpressLoopFrameTextures.Num());
	if (LoopDurationSeconds <= 0.0f)
	{
		return;
	}

	ExpressLoopFrameAnimationTimeSeconds = FMath::Fmod(
		ExpressLoopFrameAnimationTimeSeconds + FMath::Max(0.0f, DeltaSeconds),
		LoopDurationSeconds);

	const int32 FrameIndex = FMath::Clamp(
		FMath::FloorToInt(ExpressLoopFrameAnimationTimeSeconds * ExpressLoopFrameAnimationFps),
		0,
		ExpressLoopFrameTextures.Num() - 1);
	ApplyExpressLoopFrameTexture(FrameIndex);
}

void AGGGGameModeBase::ApplyExpressLoopFrameTexture(int32 FrameIndex)
{
	if (!ExpressLoopMediaPlateMaterial || !ExpressLoopFrameTextures.IsValidIndex(FrameIndex) || FrameIndex == ExpressLoopFrameAnimationFrameIndex)
	{
		return;
	}

	ApplyMediaTextureToMaterial(ExpressLoopMediaPlateMaterial, ExpressLoopFrameTextures[FrameIndex]);
	ExpressLoopFrameAnimationFrameIndex = FrameIndex;
}

void AGGGGameModeBase::StopExpressLoopFrameAnimation()
{
	bUsingExpressLoopFrameAnimation = false;
	ExpressLoopFrameAnimationTimeSeconds = 0.0f;
	ExpressLoopFrameAnimationFrameIndex = INDEX_NONE;
	ExpressLoopFrameTextures.Reset();
}

void AGGGGameModeBase::RefreshExpressLoopMediaPlayerDelegates()
{
	if (ExpressLoopMediaPlayer)
	{
		ExpressLoopMediaPlayer->OnEndReached.AddUniqueDynamic(this, &AGGGGameModeBase::HandleExpressLoopMediaEndReached);
		ExpressLoopMediaPlayer->OnMediaOpened.AddUniqueDynamic(this, &AGGGGameModeBase::HandleExpressLoopMediaOpened);
	}

	if (ExpressLoopStandbyMediaPlayer)
	{
		ExpressLoopStandbyMediaPlayer->OnEndReached.RemoveDynamic(this, &AGGGGameModeBase::HandleExpressLoopMediaEndReached);
		ExpressLoopStandbyMediaPlayer->OnMediaOpened.RemoveDynamic(this, &AGGGGameModeBase::HandleExpressLoopMediaOpened);
	}
}

bool AGGGGameModeBase::PrepareExpressLoopStandbyMediaPlayer(const FString& FilePath)
{
	if (!ExpressLoopStandbyMediaSource || !ExpressLoopStandbyMediaPlayer || FilePath.IsEmpty() || !FPaths::FileExists(FilePath))
	{
		return false;
	}

	ConfigureExpressLoopMediaSource(ExpressLoopStandbyMediaSource, FilePath);
	ConfigureExpressLoopMediaPlayer(ExpressLoopStandbyMediaPlayer, false);
	ExpressLoopStandbyMediaPlayer->OnEndReached.RemoveDynamic(this, &AGGGGameModeBase::HandleExpressLoopMediaEndReached);
	ExpressLoopStandbyMediaPlayer->OnMediaOpened.RemoveDynamic(this, &AGGGGameModeBase::HandleExpressLoopMediaOpened);
	ExpressLoopStandbyMediaPlayer->Close();

	const bool bOpened = ExpressLoopStandbyMediaPlayer->OpenSource(ExpressLoopStandbyMediaSource);
	if (bOpened)
	{
		ExpressLoopStandbyMediaPlayer->Seek(FTimespan::Zero());
		ExpressLoopStandbyMediaPlayer->Pause();
	}

	UE_LOG(LogTemp, Display, TEXT("Express loop media plate standby prepare: open=%s file=%s."),
		bOpened ? TEXT("yes") : TEXT("no"),
		*FilePath);
	return bOpened;
}

void AGGGGameModeBase::HandleExpressLoopMediaEndReached()
{
	if (IsInGameThread())
	{
		UE_LOG(LogTemp, Display, TEXT("Express loop media plate end reached; switching media player."));
		LoopExpressLoopMediaPlayback(true);
		return;
	}

	TWeakObjectPtr<AGGGGameModeBase> WeakThis(this);
	AsyncTask(ENamedThreads::GameThread, [WeakThis]()
	{
		if (AGGGGameModeBase* GameMode = WeakThis.Get())
		{
			UE_LOG(LogTemp, Display, TEXT("Express loop media plate end reached; switching media player."));
			GameMode->LoopExpressLoopMediaPlayback(true);
		}
	});
}

void AGGGGameModeBase::HandleExpressLoopMediaOpened(FString OpenedUrl)
{
	if (IsInGameThread())
	{
		HandleExpressLoopMediaOpenedOnGameThread(OpenedUrl);
		return;
	}

	TWeakObjectPtr<AGGGGameModeBase> WeakThis(this);
	AsyncTask(ENamedThreads::GameThread, [WeakThis, OpenedUrl]()
	{
		if (AGGGGameModeBase* GameMode = WeakThis.Get())
		{
			GameMode->HandleExpressLoopMediaOpenedOnGameThread(OpenedUrl);
		}
	});
}

void AGGGGameModeBase::HandleExpressLoopMediaOpenedOnGameThread(const FString& OpenedUrl)
{
	if (!ExpressLoopMediaPlayer)
	{
		return;
	}

	ExpressLoopMediaPlayer->SetLooping(false);
	const bool bPlaying = ExpressLoopMediaPlayer->Play();
	ExpressLoopMediaPlayer->SetRate(1.0f);
	const FTimespan Duration = ExpressLoopMediaPlayer->GetDuration();
	if (Duration > FTimespan::Zero())
	{
		ExpressLoopKnownDurationSeconds = Duration.GetTotalSeconds();
	}
	if (UWorld* World = GetWorld())
	{
		ExpressLoopLastSwitchWorldTimeSeconds = World->GetTimeSeconds();
	}

	UE_LOG(LogTemp, Display, TEXT("Express loop media plate opened %s; play=%s duration=%.3fs."),
		*OpenedUrl,
		bPlaying ? TEXT("yes") : TEXT("no"),
		Duration.GetTotalSeconds());
}

void AGGGGameModeBase::LoopExpressLoopMediaPlayback(bool bFromEndReached)
{
	if (!ExpressLoopMediaSource || !ExpressLoopMediaPlayer || !ExpressLoopStandbyMediaSource || !ExpressLoopStandbyMediaPlayer || !ExpressLoopStandbyMediaTexture || ExpressLoopMediaFilePath.IsEmpty() || bExpressLoopMediaRestarting)
	{
		return;
	}

	bExpressLoopMediaRestarting = true;
	const FString LoopMediaFilePath = ExpressLoopMediaFilePath;
	const double LoopStartWorldTimeSeconds = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
	if (ExpressLoopKnownDurationSeconds <= 0.0 && ExpressLoopLastSwitchWorldTimeSeconds > 0.0 && LoopStartWorldTimeSeconds > ExpressLoopLastSwitchWorldTimeSeconds)
	{
		ExpressLoopKnownDurationSeconds = LoopStartWorldTimeSeconds - ExpressLoopLastSwitchWorldTimeSeconds;
	}

	if (!FPaths::FileExists(LoopMediaFilePath))
	{
		ExpressLoopMediaStatus = FString::Printf(TEXT("MP4 loop file not found: %s"), *LoopMediaFilePath);
		UE_LOG(LogTemp, Warning, TEXT("%s"), *ExpressLoopMediaStatus);
		bExpressLoopMediaRestarting = false;
		return;
	}

	ConfigureExpressLoopMediaSource(ExpressLoopStandbyMediaSource, LoopMediaFilePath);
	ConfigureExpressLoopMediaPlayer(ExpressLoopStandbyMediaPlayer, true);

	ExpressLoopStandbyMediaPlayer->Close();
	const bool bOpened = ExpressLoopStandbyMediaPlayer->OpenSource(ExpressLoopStandbyMediaSource);
	const bool bPlaying = ExpressLoopStandbyMediaPlayer->Play();
	ExpressLoopStandbyMediaPlayer->SetRate(1.0f);

	if (bOpened)
	{
		ExpressLoopMediaSwapWarmupAttempts = 0;
		UE_LOG(LogTemp, Display, TEXT("Express loop media plate loop%s: warming hidden player open=%s play=%s."),
			bFromEndReached ? TEXT(" after end") : TEXT(" from watchdog"),
			bOpened ? TEXT("yes") : TEXT("no"),
			bPlaying ? TEXT("yes") : TEXT("no"));

		if (UWorld* World = GetWorld())
		{
			FTimerDelegate SwapDelegate;
			SwapDelegate.BindUObject(this, &AGGGGameModeBase::CompleteExpressLoopMediaPlayerSwap, bFromEndReached);
			World->GetTimerManager().ClearTimer(ExpressLoopMediaSwapTimerHandle);
			World->GetTimerManager().SetTimer(ExpressLoopMediaSwapTimerHandle, SwapDelegate, ExpressLoopMediaSwapWarmupSeconds, false);
		}
		return;
	}

	UE_LOG(LogTemp, Warning, TEXT("Express loop media plate loop%s: standby open failed, reopening active player."),
		bFromEndReached ? TEXT(" after end") : TEXT(" from watchdog"));
	bExpressLoopMediaRestarting = false;
	ReopenExpressLoopMediaPlayback(bFromEndReached);
}

void AGGGGameModeBase::CompleteExpressLoopMediaPlayerSwap(bool bFromEndReached)
{
	if (!ExpressLoopMediaPlayer || !ExpressLoopMediaTexture || !ExpressLoopStandbyMediaPlayer || !ExpressLoopStandbyMediaTexture || ExpressLoopMediaFilePath.IsEmpty())
	{
		bExpressLoopMediaRestarting = false;
		return;
	}

	bool bPlaying = ExpressLoopStandbyMediaPlayer->IsPlaying();
	if (!bPlaying)
	{
		bPlaying = ExpressLoopStandbyMediaPlayer->Play();
		ExpressLoopStandbyMediaPlayer->SetRate(1.0f);
	}

	const double StandbyTimeSeconds = ExpressLoopStandbyMediaPlayer->GetTime().GetTotalSeconds();
	if ((!bPlaying || StandbyTimeSeconds < ExpressLoopMediaMinWarmFrameSeconds)
		&& ExpressLoopMediaSwapWarmupAttempts < ExpressLoopMediaSwapMaxWarmupAttempts)
	{
		++ExpressLoopMediaSwapWarmupAttempts;
		UE_LOG(LogTemp, Display, TEXT("Express loop media plate swap waiting: attempt=%d play=%s time=%.3fs."),
			ExpressLoopMediaSwapWarmupAttempts,
			bPlaying ? TEXT("yes") : TEXT("no"),
			StandbyTimeSeconds);

		if (UWorld* World = GetWorld())
		{
			FTimerDelegate SwapDelegate;
			SwapDelegate.BindUObject(this, &AGGGGameModeBase::CompleteExpressLoopMediaPlayerSwap, bFromEndReached);
			World->GetTimerManager().SetTimer(ExpressLoopMediaSwapTimerHandle, SwapDelegate, ExpressLoopMediaSwapRetrySeconds, false);
		}
		return;
	}

	if (StandbyTimeSeconds < ExpressLoopMediaMinWarmFrameSeconds)
	{
		UE_LOG(LogTemp, Warning, TEXT("Express loop media plate hidden player did not advance; reopening visible player instead of switching to blank."));
		ExpressLoopStandbyMediaPlayer->Close();
		bExpressLoopMediaRestarting = false;
		ReopenExpressLoopMediaPlayback(bFromEndReached);
		return;
	}

	if (ExpressLoopMediaPlateMaterial)
	{
		ApplyMediaTextureToMaterial(ExpressLoopMediaPlateMaterial, ExpressLoopStandbyMediaTexture);
	}

	TObjectPtr<UFileMediaSource> PreviousActiveSource = ExpressLoopMediaSource;
	TObjectPtr<UMediaPlayer> PreviousActivePlayer = ExpressLoopMediaPlayer;
	TObjectPtr<UMediaTexture> PreviousActiveTexture = ExpressLoopMediaTexture;

	ExpressLoopMediaSource = ExpressLoopStandbyMediaSource;
	ExpressLoopMediaPlayer = ExpressLoopStandbyMediaPlayer;
	ExpressLoopMediaTexture = ExpressLoopStandbyMediaTexture;

	ExpressLoopStandbyMediaSource = PreviousActiveSource;
	ExpressLoopStandbyMediaPlayer = PreviousActivePlayer;
	ExpressLoopStandbyMediaTexture = PreviousActiveTexture;
	RefreshExpressLoopMediaPlayerDelegates();

	++ExpressLoopMediaLoopCounter;
	const FTimespan NewActiveDuration = ExpressLoopMediaPlayer->GetDuration();
	if (NewActiveDuration > FTimespan::Zero())
	{
		ExpressLoopKnownDurationSeconds = NewActiveDuration.GetTotalSeconds();
	}
	if (UWorld* World = GetWorld())
	{
		ExpressLoopLastSwitchWorldTimeSeconds = World->GetTimeSeconds();
	}

	const FString LoopMediaFilePath = ExpressLoopMediaFilePath;
	ExpressLoopMediaStatus = FString::Printf(TEXT("Playing %s"), *LoopMediaFilePath);
	UE_LOG(LogTemp, Display, TEXT("Express loop media plate loop %d%s: switched warmed player play=%s time=%.3fs duration=%.3fs."),
		ExpressLoopMediaLoopCounter,
		bFromEndReached ? TEXT(" after end") : TEXT(" from watchdog"),
		bPlaying ? TEXT("yes") : TEXT("no"),
		StandbyTimeSeconds,
		ExpressLoopKnownDurationSeconds);

	ConfigureExpressLoopMediaPlayer(ExpressLoopStandbyMediaPlayer, false);
	ExpressLoopStandbyMediaPlayer->Close();
	bExpressLoopMediaRestarting = false;
}

void AGGGGameModeBase::ReopenExpressLoopMediaPlayback(bool bFromEndReached)
{
	if (!ExpressLoopMediaSource || !ExpressLoopMediaPlayer || ExpressLoopMediaFilePath.IsEmpty() || bExpressLoopMediaRestarting)
	{
		return;
	}

	bExpressLoopMediaRestarting = true;
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(ExpressLoopMediaSwapTimerHandle);
	}
	const FString LoopMediaFilePath = ExpressLoopMediaFilePath;
	if (!FPaths::FileExists(LoopMediaFilePath))
	{
		ExpressLoopMediaStatus = FString::Printf(TEXT("MP4 loop file not found: %s"), *LoopMediaFilePath);
		UE_LOG(LogTemp, Warning, TEXT("%s"), *ExpressLoopMediaStatus);
		bExpressLoopMediaRestarting = false;
		return;
	}

	ConfigureExpressLoopMediaSource(ExpressLoopMediaSource, LoopMediaFilePath);
	ConfigureExpressLoopMediaPlayer(ExpressLoopMediaPlayer, true);
	ExpressLoopMediaPlayer->Close();
	if (ExpressLoopMediaPlateMaterial && ExpressLoopMediaTexture)
	{
		ApplyMediaTextureToMaterial(ExpressLoopMediaPlateMaterial, ExpressLoopMediaTexture);
	}

	const bool bOpened = ExpressLoopMediaPlayer->OpenSource(ExpressLoopMediaSource);
	bool bPlaying = false;
	if (bOpened)
	{
		bPlaying = ExpressLoopMediaPlayer->Play();
		ExpressLoopMediaPlayer->SetRate(1.0f);
		ExpressLoopMediaStatus = FString::Printf(TEXT("Playing %s"), *LoopMediaFilePath);
		++ExpressLoopMediaLoopCounter;
		if (UWorld* World = GetWorld())
		{
			ExpressLoopLastSwitchWorldTimeSeconds = World->GetTimeSeconds();
		}
	}
	else
	{
		ExpressLoopMediaStatus = FString::Printf(TEXT("MP4 loop reopen failed: %s"), *LoopMediaFilePath);
	}

	if (bOpened)
	{
		UE_LOG(LogTemp, Display, TEXT("Express loop media plate reopen fallback%s: open=yes play=%s."),
			bFromEndReached ? TEXT(" after end") : TEXT(" from watchdog"),
			bPlaying ? TEXT("yes") : TEXT("no"));
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("Express loop media plate reopen fallback%s: open=no play=no."),
			bFromEndReached ? TEXT(" after end") : TEXT(" from watchdog"));
	}
	bExpressLoopMediaRestarting = false;

	if (bOpened)
	{
		ConfigureExpressLoopMediaPlayer(ExpressLoopStandbyMediaPlayer, false);
		ExpressLoopStandbyMediaPlayer->Close();
	}
}

void AGGGGameModeBase::KeepExpressLoopMediaLooping()
{
	if (!ExpressLoopMediaPlayer || ExpressLoopMediaFilePath.IsEmpty() || bExpressLoopMediaRestarting)
	{
		return;
	}

	const FTimespan Duration = ExpressLoopMediaPlayer->GetDuration();
	const FTimespan CurrentTime = ExpressLoopMediaPlayer->GetTime();
	const FTimespan LoopLeadTime = FTimespan::FromMilliseconds(ExpressLoopMediaLoopLeadMilliseconds);
	if (Duration > FTimespan::Zero())
	{
		ExpressLoopKnownDurationSeconds = Duration.GetTotalSeconds();
	}

	if (UWorld* World = GetWorld())
	{
		const double LeadSeconds = ExpressLoopMediaLoopLeadMilliseconds / 1000.0;
		const double SwitchAfterSeconds = ExpressLoopKnownDurationSeconds > LeadSeconds + 0.25
			? ExpressLoopKnownDurationSeconds - LeadSeconds
			: 0.0;
		if (SwitchAfterSeconds > 0.0
			&& ExpressLoopLastSwitchWorldTimeSeconds > 0.0
			&& World->GetTimeSeconds() - ExpressLoopLastSwitchWorldTimeSeconds >= SwitchAfterSeconds)
		{
			LoopExpressLoopMediaPlayback(false);
			return;
		}
	}

	if (Duration > LoopLeadTime && CurrentTime >= Duration - LoopLeadTime)
	{
		LoopExpressLoopMediaPlayback(false);
		return;
	}

	if (ExpressLoopMediaPlayer->IsPlaying())
	{
		return;
	}

	if (Duration <= FTimespan::Zero() || CurrentTime < Duration - FTimespan::FromMilliseconds(500.0))
	{
		ExpressLoopMediaPlayer->SetLooping(true);
		if (ExpressLoopMediaPlayer->Play())
		{
			ExpressLoopMediaPlayer->SetRate(1.0f);
		}
		return;
	}

	LoopExpressLoopMediaPlayback(false);
}

void AGGGGameModeBase::StartExpressLoopMediaWatchdog()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(ExpressLoopMediaWatchdogTimerHandle);
		World->GetTimerManager().SetTimer(
			ExpressLoopMediaWatchdogTimerHandle,
			this,
			&AGGGGameModeBase::KeepExpressLoopMediaLooping,
			ExpressLoopMediaWatchdogSeconds,
			true);
	}
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

void AGGGGameModeBase::ShowChromaKeyPlateControlStatus() const
{
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(1001, 2.0f, FColor::Green, TEXT("Controlling chroma key plate"));
	}
}

void AGGGGameModeBase::ShowExpressLoopMediaPlateControlStatus() const
{
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(1001, 2.0f, FColor::Cyan, TEXT("Controlling MP4 media plate"));
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

void AGGGGameModeBase::StartWebControlServer()
{
	if (bWebControlServerStarted)
	{
		return;
	}

	FHttpServerModule& HttpServerModule = FHttpServerModule::Get();
	HttpServerModule.StartAllListeners();

	WebControlRouter = HttpServerModule.GetHttpRouter(WebControlPort, true);
	if (!WebControlRouter.IsValid())
	{
		UE_LOG(LogTemp, Warning, TEXT("Web camera control could not bind to port %u. Is another app already using it?"), WebControlPort);
		return;
	}

	TWeakObjectPtr<AGGGGameModeBase> WeakThis(this);
	WebControlPageRouteHandle = WebControlRouter->BindRoute(FHttpPath(TEXT("/c")), EHttpServerRequestVerbs::VERB_GET,
		[WeakThis](const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
	{
		AGGGGameModeBase* GameMode = WeakThis.Get();
		if (!GameMode)
		{
			return false;
		}

		TUniquePtr<FHttpServerResponse> Response = FHttpServerResponse::Create(GameMode->BuildWebControlPage(), TEXT("text/html"));
		AddNoCacheHeaders(*Response);
		OnComplete(MoveTemp(Response));
		return true;
	});

	WebControlStateRouteHandle = WebControlRouter->BindRoute(FHttpPath(TEXT("/state")), EHttpServerRequestVerbs::VERB_GET,
		[WeakThis](const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
	{
		AGGGGameModeBase* GameMode = WeakThis.Get();
		if (!GameMode)
		{
			return false;
		}

		FString StateJson;
		{
			FScopeLock Lock(&GameMode->WebControlStateCriticalSection);
			StateJson = GameMode->CachedWebControlStateJson;
		}

		if (StateJson.IsEmpty())
		{
			StateJson = TEXT("{\"selected\":{\"target\":\"camera\",\"cameraIndex\":0,\"cameraNumber\":1},\"cameras\":[],\"plate\":{\"available\":false},\"key\":{\"available\":false},\"mp4\":{\"available\":false}}");
		}

		TUniquePtr<FHttpServerResponse> Response = FHttpServerResponse::Create(StateJson, TEXT("application/json"));
		AddNoCacheHeaders(*Response);
		OnComplete(MoveTemp(Response));
		return true;
	});

	WebControlCommandRouteHandle = WebControlRouter->BindRoute(FHttpPath(TEXT("/control")), EHttpServerRequestVerbs::VERB_POST,
		[WeakThis](const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
	{
		AGGGGameModeBase* GameMode = WeakThis.Get();
		if (!GameMode)
		{
			return false;
		}

		FString RequestBody;
		if (Request.Body.Num() > 0)
		{
			FUTF8ToTCHAR ConvertedBody(reinterpret_cast<const ANSICHAR*>(Request.Body.GetData()), Request.Body.Num());
			RequestBody = FString(ConvertedBody.Length(), ConvertedBody.Get());
		}

		FString ErrorMessage;
		if (!GameMode->EnqueueWebControlCommandFromJson(RequestBody, ErrorMessage))
		{
			OnComplete(FHttpServerResponse::Error(EHttpServerResponseCodes::BadRequest, TEXT("bad_request"), ErrorMessage));
			return true;
		}

		TUniquePtr<FHttpServerResponse> Response = FHttpServerResponse::Create(TEXT("{\"ok\":true}"), TEXT("application/json"));
		AddNoCacheHeaders(*Response);
		OnComplete(MoveTemp(Response));
		return true;
	});

	WebControlUploadRouteHandle = WebControlRouter->BindRoute(FHttpPath(TEXT("/uploadmp4")), EHttpServerRequestVerbs::VERB_POST,
		[WeakThis](const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
	{
		AGGGGameModeBase* GameMode = WeakThis.Get();
		if (!GameMode)
		{
			return false;
		}

		const FString* QueryFileName = Request.QueryParams.Find(TEXT("filename"));
		const FString UploadedFileName = QueryFileName ? *QueryFileName : TEXT("selected.mp4");

		FString SavedFilePath;
		FString ErrorMessage;
		if (!GameMode->SaveUploadedExpressLoopMediaFile(UploadedFileName, Request.Body, SavedFilePath, ErrorMessage))
		{
			OnComplete(FHttpServerResponse::Error(EHttpServerResponseCodes::BadRequest, TEXT("bad_request"), ErrorMessage));
			return true;
		}

		FGGGWebControlCommand Command;
		Command.Target = EGGGWebControlTarget::ExpressLoopMediaPlate;
		Command.bSetFile = true;
		Command.FilePath = SavedFilePath;
		GameMode->PendingWebControlCommands.Enqueue(Command);

		const FString ResponseJson = FString::Printf(
			TEXT("{\"ok\":true,\"filePath\":%s,\"fileName\":%s}"),
			*BuildJsonString(SavedFilePath),
			*BuildJsonString(FPaths::GetCleanFilename(SavedFilePath)));
		TUniquePtr<FHttpServerResponse> Response = FHttpServerResponse::Create(ResponseJson, TEXT("application/json"));
		AddNoCacheHeaders(*Response);
		OnComplete(MoveTemp(Response));
		return true;
	});

	if (!WebControlPageRouteHandle.IsValid() || !WebControlStateRouteHandle.IsValid() || !WebControlCommandRouteHandle.IsValid() || !WebControlUploadRouteHandle.IsValid())
	{
		UE_LOG(LogTemp, Warning, TEXT("Web camera control could not bind all routes on port %u."), WebControlPort);
		StopWebControlServer();
		return;
	}

	bWebControlServerStarted = true;
	UE_LOG(LogTemp, Display, TEXT("Web camera control is available at http://<this-pc-ip>:%u/c."), WebControlPort);
}

void AGGGGameModeBase::StopWebControlServer()
{
	if (WebControlRouter.IsValid())
	{
		if (WebControlPageRouteHandle.IsValid())
		{
			WebControlRouter->UnbindRoute(WebControlPageRouteHandle);
		}

		if (WebControlStateRouteHandle.IsValid())
		{
			WebControlRouter->UnbindRoute(WebControlStateRouteHandle);
		}

		if (WebControlCommandRouteHandle.IsValid())
		{
			WebControlRouter->UnbindRoute(WebControlCommandRouteHandle);
		}

		if (WebControlUploadRouteHandle.IsValid())
		{
			WebControlRouter->UnbindRoute(WebControlUploadRouteHandle);
		}
	}

	WebControlPageRouteHandle.Reset();
	WebControlStateRouteHandle.Reset();
	WebControlCommandRouteHandle.Reset();
	WebControlUploadRouteHandle.Reset();
	WebControlRouter.Reset();

	if (bWebControlServerStarted && FHttpServerModule::IsAvailable())
	{
		FHttpServerModule::Get().StopAllListeners();
	}

	bWebControlServerStarted = false;
}

void AGGGGameModeBase::ProcessWebControlCommands()
{
	FGGGWebControlCommand Command;
	while (PendingWebControlCommands.Dequeue(Command))
	{
		if (Command.Target == EGGGWebControlTarget::Camera)
		{
			if (!ControlledCameras.IsValidIndex(Command.CameraIndex))
			{
				continue;
			}

			SelectCameraControl(Command.CameraIndex, Command.bSelectOnly);
			if (Command.bLookAt || Command.bReset)
			{
				AimCameraAtFocusPoint(Command.CameraIndex);
			}

			if (!Command.bSelectOnly)
			{
				ApplyCameraControl(Command.CameraIndex, Command.Move, Command.Rotation, Command.bFast, Command.DeltaSeconds);
			}
		}
		else
		{
			if (Command.Target == EGGGWebControlTarget::DeckLinkInputPlate)
			{
				if (!DeckLinkInputScreenActor || !DeckLinkInputScreenMeshComponent)
				{
					continue;
				}

				SelectDeckLinkInputPlateControl(Command.bSelectOnly);
				if (Command.bReset)
				{
					ResetDeckLinkInputPlate();
					ShowDeckLinkInputPlateControlStatus();
					continue;
				}

				if (!Command.bSelectOnly)
				{
					ApplyDeckLinkInputPlateControl(Command.Move, Command.Rotation, Command.Scale, Command.bFast, Command.DeltaSeconds);
				}
			}
			else if (Command.Target == EGGGWebControlTarget::ChromaKeyPlate)
			{
				if (!ChromaKeyPlateActor || !ChromaKeyPlateMeshComponent)
				{
					continue;
				}

				SelectChromaKeyPlateControl(Command.bSelectOnly || Command.bSetChromaKeySettings);
				if (Command.bSetChromaKeySettings)
				{
					ApplyChromaKeySettings(Command, true);
				}

				if (Command.bReset)
				{
					ResetChromaKeyPlate();
					ShowChromaKeyPlateControlStatus();
					continue;
				}

				if (!Command.bSelectOnly)
				{
					ApplyChromaKeyPlateControl(Command.Move, Command.Rotation, Command.Scale, Command.bFast, Command.DeltaSeconds);
				}
			}
			else
			{
				if (!ExpressLoopMediaPlateActor || !ExpressLoopMediaPlateMeshComponent)
				{
					continue;
				}

				SelectExpressLoopMediaPlateControl(Command.bSelectOnly || Command.bSetFile);
				if (Command.bSetFile)
				{
					OpenExpressLoopMediaFile(Command.FilePath, true);
					continue;
				}

				if (Command.bReset)
				{
					ResetExpressLoopMediaPlate();
					ShowExpressLoopMediaPlateControlStatus();
					continue;
				}

				if (!Command.bSelectOnly)
				{
					ApplyExpressLoopMediaPlateControl(Command.Move, Command.Rotation, Command.Scale, Command.bFast, Command.DeltaSeconds);
				}
			}
		}
	}
}

bool AGGGGameModeBase::EnqueueWebControlCommandFromJson(const FString& RequestBody, FString& OutError)
{
	if (RequestBody.IsEmpty())
	{
		OutError = TEXT("Request body is empty.");
		return false;
	}

	TSharedPtr<FJsonObject> JsonObject;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(RequestBody);
	if (!FJsonSerializer::Deserialize(Reader, JsonObject) || !JsonObject.IsValid())
	{
		OutError = TEXT("Request body is not valid JSON.");
		return false;
	}

	FGGGWebControlCommand Command;

	FString TargetString;
	if (JsonObject->TryGetStringField(TEXT("target"), TargetString))
	{
		const FString NormalizedTarget = TargetString.ToLower();
		if (NormalizedTarget == TEXT("plate") || NormalizedTarget == TEXT("input") || NormalizedTarget == TEXT("decklinkinputplate"))
		{
			Command.Target = EGGGWebControlTarget::DeckLinkInputPlate;
		}
		else if (NormalizedTarget == TEXT("key") || NormalizedTarget == TEXT("chroma") || NormalizedTarget == TEXT("chromakey") || NormalizedTarget == TEXT("green") || NormalizedTarget == TEXT("chromakeyplate"))
		{
			Command.Target = EGGGWebControlTarget::ChromaKeyPlate;
		}
		else if (NormalizedTarget == TEXT("mp4") || NormalizedTarget == TEXT("media") || NormalizedTarget == TEXT("file") || NormalizedTarget == TEXT("express") || NormalizedTarget == TEXT("expressloopmediaplate"))
		{
			Command.Target = EGGGWebControlTarget::ExpressLoopMediaPlate;
		}
		else
		{
			Command.Target = EGGGWebControlTarget::Camera;
		}
	}
	else
	{
		Command.Target = bControllingDeckLinkInputPlate
			? EGGGWebControlTarget::DeckLinkInputPlate
			: (bControllingChromaKeyPlate ? EGGGWebControlTarget::ChromaKeyPlate : (bControllingExpressLoopMediaPlate ? EGGGWebControlTarget::ExpressLoopMediaPlate : EGGGWebControlTarget::Camera));
	}

	Command.CameraIndex = SelectedCameraIndex;
	int32 CameraIndex = INDEX_NONE;
	if (JsonObject->TryGetNumberField(TEXT("cameraIndex"), CameraIndex))
	{
		Command.CameraIndex = CameraIndex;
	}
	else if (JsonObject->TryGetNumberField(TEXT("index"), CameraIndex))
	{
		Command.CameraIndex = CameraIndex - 1;
	}

	if (Command.Target == EGGGWebControlTarget::Camera && (Command.CameraIndex < 0 || Command.CameraIndex > 3))
	{
		OutError = TEXT("Camera index must be 0-3, or index must be 1-4.");
		return false;
	}

	bool bFast = false;
	if (JsonObject->TryGetBoolField(TEXT("fast"), bFast))
	{
		Command.bFast = bFast;
	}

	double DeltaSeconds = Command.DeltaSeconds;
	if (JsonObject->TryGetNumberField(TEXT("dt"), DeltaSeconds) || JsonObject->TryGetNumberField(TEXT("deltaSeconds"), DeltaSeconds))
	{
		Command.DeltaSeconds = ClampControlDeltaSeconds(static_cast<float>(DeltaSeconds));
	}

	FString ActionString;
	if (JsonObject->TryGetStringField(TEXT("action"), ActionString))
	{
		const FString NormalizedAction = ActionString.ToLower();
		Command.bSelectOnly = NormalizedAction == TEXT("select");
		Command.bLookAt = NormalizedAction == TEXT("lookat") || NormalizedAction == TEXT("aim") || NormalizedAction == TEXT("aimcenter");
		Command.bReset = NormalizedAction == TEXT("reset");
		Command.bSetFile = NormalizedAction == TEXT("setfile") || NormalizedAction == TEXT("loadfile") || NormalizedAction == TEXT("setmp4") || NormalizedAction == TEXT("loadmp4");
		Command.bSetChromaKeySettings = NormalizedAction == TEXT("setchromakey") || NormalizedAction == TEXT("setkey") || NormalizedAction == TEXT("keysettings") || NormalizedAction == TEXT("chromakeysettings");
		if (Command.bSetFile)
		{
			Command.Target = EGGGWebControlTarget::ExpressLoopMediaPlate;
		}
		if (Command.bSetChromaKeySettings)
		{
			Command.Target = EGGGWebControlTarget::ChromaKeyPlate;
		}
		if (NormalizedAction == TEXT("resetchromakey") || NormalizedAction == TEXT("resetkeysettings") || NormalizedAction == TEXT("resetchromakeysettings"))
		{
			Command.Target = EGGGWebControlTarget::ChromaKeyPlate;
			Command.bSetChromaKeySettings = true;
			Command.bHasChromaKeyEnabled = true;
			Command.bChromaKeyEnabled = bDefaultChromaKeyEnabled;
			Command.ChromaKeyTolerance = DefaultChromaKeyTolerance;
			Command.ChromaKeySoftness = DefaultChromaKeySoftness;
			Command.ChromaKeyDespill = DefaultChromaKeyDespill;
		}
	}

	bool bBoolValue = false;
	if (JsonObject->TryGetBoolField(TEXT("select"), bBoolValue))
	{
		Command.bSelectOnly = bBoolValue;
	}
	if (JsonObject->TryGetBoolField(TEXT("lookAt"), bBoolValue))
	{
		Command.bLookAt = bBoolValue;
	}
	if (JsonObject->TryGetBoolField(TEXT("reset"), bBoolValue))
	{
		Command.bReset = bBoolValue;
	}
	if (JsonObject->TryGetBoolField(TEXT("setFile"), bBoolValue))
	{
		Command.bSetFile = bBoolValue;
		if (Command.bSetFile)
		{
			Command.Target = EGGGWebControlTarget::ExpressLoopMediaPlate;
		}
	}

	if (Command.bSetFile)
	{
		if (!JsonObject->TryGetStringField(TEXT("filePath"), Command.FilePath)
			&& !JsonObject->TryGetStringField(TEXT("path"), Command.FilePath)
			&& !JsonObject->TryGetStringField(TEXT("file"), Command.FilePath))
		{
			OutError = TEXT("MP4 filePath is required.");
			return false;
		}

		if (Command.FilePath.TrimStartAndEnd().IsEmpty())
		{
			OutError = TEXT("MP4 filePath is empty.");
			return false;
		}
	}

	const TSharedPtr<FJsonObject>* ChromaObject = nullptr;
	TSharedPtr<FJsonObject> ChromaSettingsObject = JsonObject;
	if (JsonObject->TryGetObjectField(TEXT("chroma"), ChromaObject) && ChromaObject && ChromaObject->IsValid())
	{
		ChromaSettingsObject = *ChromaObject;
		Command.Target = EGGGWebControlTarget::ChromaKeyPlate;
		Command.bSetChromaKeySettings = true;
	}

	if (ChromaSettingsObject.IsValid())
	{
		bool bChromaEnabled = false;
		if (ChromaSettingsObject->TryGetBoolField(TEXT("enabled"), bChromaEnabled)
			|| ChromaSettingsObject->TryGetBoolField(TEXT("keyEnabled"), bChromaEnabled)
			|| ChromaSettingsObject->TryGetBoolField(TEXT("chromaKeyEnabled"), bChromaEnabled))
		{
			Command.bHasChromaKeyEnabled = true;
			Command.bChromaKeyEnabled = bChromaEnabled;
			Command.Target = EGGGWebControlTarget::ChromaKeyPlate;
			Command.bSetChromaKeySettings = true;
		}

		double ChromaNumber = 0.0;
		if (ChromaSettingsObject->TryGetNumberField(TEXT("tolerance"), ChromaNumber))
		{
			Command.ChromaKeyTolerance = static_cast<float>(ChromaNumber);
			Command.Target = EGGGWebControlTarget::ChromaKeyPlate;
			Command.bSetChromaKeySettings = true;
		}
		if (ChromaSettingsObject->TryGetNumberField(TEXT("softness"), ChromaNumber))
		{
			Command.ChromaKeySoftness = static_cast<float>(ChromaNumber);
			Command.Target = EGGGWebControlTarget::ChromaKeyPlate;
			Command.bSetChromaKeySettings = true;
		}
		if (ChromaSettingsObject->TryGetNumberField(TEXT("despill"), ChromaNumber))
		{
			Command.ChromaKeyDespill = static_cast<float>(ChromaNumber);
			Command.Target = EGGGWebControlTarget::ChromaKeyPlate;
			Command.bSetChromaKeySettings = true;
		}
	}

	const TSharedPtr<FJsonObject>* MoveObject = nullptr;
	if (JsonObject->TryGetObjectField(TEXT("move"), MoveObject) && MoveObject && MoveObject->IsValid())
	{
		Command.Move = FVector(
			ReadJsonNumber(*MoveObject, TEXT("x")),
			ReadJsonNumber(*MoveObject, TEXT("y")),
			ReadJsonNumber(*MoveObject, TEXT("z")));
	}

	const TSharedPtr<FJsonObject>* RotationObject = nullptr;
	if (JsonObject->TryGetObjectField(TEXT("rotate"), RotationObject) && RotationObject && RotationObject->IsValid())
	{
		Command.Rotation = FRotator(
			ReadJsonNumber(*RotationObject, TEXT("pitch")),
			ReadJsonNumber(*RotationObject, TEXT("yaw")),
			ReadJsonNumber(*RotationObject, TEXT("roll")));
	}

	Command.Scale = ReadJsonNumber(JsonObject, TEXT("scale"));
	PendingWebControlCommands.Enqueue(Command);
	return true;
}

bool AGGGGameModeBase::SaveUploadedExpressLoopMediaFile(const FString& UploadedFileName, const TArray<uint8>& FileBytes, FString& OutSavedFilePath, FString& OutError) const
{
	if (FileBytes.Num() == 0)
	{
		OutError = TEXT("Selected MP4 upload is empty.");
		return false;
	}

	if (!UploadedFileName.EndsWith(TEXT(".mp4"), ESearchCase::IgnoreCase))
	{
		OutError = TEXT("Choose an .mp4 file.");
		return false;
	}

	const FString UploadDirectory = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("UploadedMedia")));
	if (!IFileManager::Get().MakeDirectory(*UploadDirectory, true))
	{
		OutError = FString::Printf(TEXT("Could not create upload folder: %s"), *UploadDirectory);
		return false;
	}

	const FString SavedFileName = SanitizeUploadedFileName(UploadedFileName);
	OutSavedFilePath = FPaths::ConvertRelativePathToFull(FPaths::Combine(UploadDirectory, SavedFileName));
	FPaths::NormalizeFilename(OutSavedFilePath);

	if (!FFileHelper::SaveArrayToFile(FileBytes, *OutSavedFilePath))
	{
		OutError = FString::Printf(TEXT("Could not save uploaded MP4: %s"), *OutSavedFilePath);
		return false;
	}

	UE_LOG(LogTemp, Display, TEXT("Uploaded MP4 saved to %s (%d byte(s))."), *OutSavedFilePath, FileBytes.Num());
	return true;
}

void AGGGGameModeBase::UpdateCachedWebControlState()
{
	const FString StateJson = BuildWebControlStateJson();
	FScopeLock Lock(&WebControlStateCriticalSection);
	CachedWebControlStateJson = StateJson;
}

FString AGGGGameModeBase::BuildWebControlPage() const
{
	FString Page;
	Page.Reserve(25000);
	Page += TEXT(R"HTML(<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>GGG Camera Control</title>
<style>
:root {
  color-scheme: dark;
  --bg: #101317;
  --panel: #171b20;
  --panel-2: #20262d;
  --line: #343d46;
  --text: #f2f5f7;
  --muted: #9ba8b4;
  --accent: #2f7dd3;
  --accent-2: #18a884;
  --danger: #d9634f;
}
* { box-sizing: border-box; }
body {
  margin: 0;
  min-height: 100vh;
  background: var(--bg);
  color: var(--text);
  font-family: Segoe UI, Roboto, Arial, sans-serif;
}
.app {
  min-height: 100vh;
  display: grid;
  grid-template-columns: minmax(400px, 460px) minmax(0, 1fr);
  gap: 14px;
  padding: 14px;
  overflow-x: hidden;
}
.panel {
  background: var(--panel);
  border: 1px solid var(--line);
  border-radius: 8px;
  padding: 14px;
  min-width: 0;
}
h1, h2 {
  margin: 0 0 12px;
  font-size: 18px;
  line-height: 1.2;
  letter-spacing: 0;
}
h2 { font-size: 14px; color: var(--muted); text-transform: uppercase; }
.targets, .row, .grid {
  display: grid;
  gap: 8px;
  min-width: 0;
}
.targets { grid-template-columns: repeat(3, minmax(0, 1fr)); }
.row { grid-template-columns: repeat(2, minmax(0, 1fr)); }
.grid { grid-template-columns: repeat(3, minmax(76px, 1fr)); }
.file-row { display: grid; grid-template-columns: minmax(0, 1fr) 84px; gap: 8px; }
.hidden-file { display: none; }
.spacer { visibility: hidden; }
.slider-row {
  display: grid;
  grid-template-columns: 84px minmax(0, 1fr) 48px;
  gap: 8px;
  align-items: center;
  color: var(--muted);
  font-size: 13px;
}
button, .toggle, input[type="text"] {
  min-height: 42px;
  border: 1px solid var(--line);
  border-radius: 6px;
  background: var(--panel-2);
  color: var(--text);
  font: inherit;
  font-size: 14px;
  cursor: pointer;
  user-select: none;
  touch-action: manipulation;
  min-width: 0;
}
input[type="text"] {
  width: 100%;
  cursor: text;
  user-select: text;
  padding: 0 10px;
}
input[type="range"] {
  width: 100%;
  min-width: 0;
  accent-color: var(--accent-2);
}
button:hover, .toggle:hover { border-color: #617283; }
input[type="text"]:focus { outline: 2px solid var(--accent); outline-offset: 1px; }
button.pressed { background: var(--accent-2); border-color: var(--accent-2); color: #061512; }
button.selected { background: var(--accent); border-color: var(--accent); }
button:disabled { opacity: 0.35; cursor: not-allowed; }
.toggle {
  margin-top: 12px;
  display: flex;
  align-items: center;
  gap: 10px;
  padding: 0 12px;
}
.toggle input { width: 18px; height: 18px; }
.stack { display: grid; gap: 14px; min-width: 0; }
.status {
  display: grid;
  gap: 8px;
  color: var(--muted);
  font-size: 13px;
  line-height: 1.35;
  min-width: 0;
}
.value {
  min-height: 28px;
  padding: 8px;
  border-radius: 6px;
  background: #11161b;
  border: 1px solid #27313a;
  color: var(--text);
  font-family: Consolas, monospace;
  white-space: pre-wrap;
  overflow-wrap: anywhere;
  word-break: break-word;
  min-width: 0;
}
.danger { background: #2a1714; border-color: #7b3328; color: #ffd7d0; }
@media (max-width: 1120px) {
  .app { grid-template-columns: 1fr; }
}
@media (max-width: 520px) {
  .app { padding: 8px; gap: 8px; }
  .panel { padding: 10px; }
  .targets { grid-template-columns: repeat(2, minmax(0, 1fr)); }
  .file-row { grid-template-columns: 1fr; }
}
</style>
)HTML");
	Page += TEXT(R"HTML(
</head>
<body>
<main class="app">
  <section class="panel stack">
    <div>
      <h1>GGG Control</h1>
      <div class="targets" id="targets">
        <button data-target="camera" data-index="0">Cam 1</button>
        <button data-target="camera" data-index="1">Cam 2</button>
        <button data-target="camera" data-index="2">Cam 3</button>
        <button data-target="camera" data-index="3">Cam 4</button>
        <button data-target="plate">Input</button>
        <button data-target="key">Key</button>
        <button data-target="mp4">MP4</button>
      </div>
      <label class="toggle"><input id="fast" type="checkbox"> Fast</label>
    </div>
    <div class="status">
      <div id="selectedText">Connecting...</div>
      <div class="value" id="transformText">Waiting for state</div>
      <div class="value" id="serverText">http://IP:8080/c</div>
    </div>
    <div class="stack">
      <h2>MP4 File</h2>
      <div class="file-row">
        <input id="mp4Path" type="text" spellcheck="false" placeholder="C:\path\video.mp4">
        <button id="setMp4File">Set</button>
      </div>
      <input id="mp4FileInput" class="hidden-file" type="file" accept=".mp4,video/mp4">
      <button id="chooseMp4File">Choose File</button>
      <div class="value" id="mp4FileText">Waiting for MP4 state</div>
    </div>
    <div class="stack">
      <h2>Chroma Key</h2>
      <label class="toggle"><input id="keyEnabled" type="checkbox"> Enable</label>
      <div class="slider-row">
        <span>Tolerance</span>
        <input id="keyTolerance" type="range" min="0" max="1" step="0.01">
        <span id="keyToleranceValue">0.12</span>
      </div>
      <div class="slider-row">
        <span>Softness</span>
        <input id="keySoftness" type="range" min="0.01" max="1" step="0.01">
        <span id="keySoftnessValue">0.22</span>
      </div>
      <div class="slider-row">
        <span>Despill</span>
        <input id="keyDespill" type="range" min="0" max="1" step="0.01">
        <span id="keyDespillValue">0.75</span>
      </div>
      <button id="resetKeySettings">Reset Key Settings</button>
    </div>
  </section>
  <section class="stack">
    <div class="panel stack">
      <h2>Move</h2>
      <div class="grid">
        <button data-hold='{"move":{"z":1}}'>Up</button>
        <button data-hold='{"move":{"x":1}}'>Forward</button>
        <button data-hold='{"move":{"z":-1}}'>Down</button>
        <button data-hold='{"move":{"y":-1}}'>Left</button>
        <button data-hold='{"move":{"x":-1}}'>Back</button>
        <button data-hold='{"move":{"y":1}}'>Right</button>
      </div>
    </div>
    <div class="panel stack">
      <h2>Rotate</h2>
      <div class="grid">
        <button data-hold='{"rotate":{"pitch":-1}}'>Pitch Up</button>
        <button id="aimReset">Aim Center</button>
        <button data-hold='{"rotate":{"pitch":1}}'>Pitch Down</button>
        <button data-hold='{"rotate":{"yaw":-1}}'>Yaw Left</button>
        <button data-hold='{"rotate":{"yaw":1}}'>Yaw Right</button>
        <button class="spacer" disabled></button>
        <button data-plate-only data-hold='{"rotate":{"roll":-1}}'>Roll Left</button>
        <button data-plate-only data-hold='{"scale":1}'>Larger</button>
        <button data-plate-only data-hold='{"rotate":{"roll":1}}'>Roll Right</button>
        <button class="spacer" disabled></button>
        <button data-plate-only data-hold='{"scale":-1}'>Smaller</button>
        <button class="spacer" disabled></button>
      </div>
    </div>
  </section>
</main>
)HTML");
	Page += TEXT(R"HTML(
<script>
const pulseMs = 80;
let selected = { target: "camera", cameraIndex: 0 };
let lastState = null;
let keySettingsSyncing = false;
let keySettingsTimer = null;
const defaultKeySettings = { enabled: false, tolerance: 0.12, softness: 0.22, despill: 0.75 };

function basePayload() {
  return {
    target: selected.target,
    cameraIndex: selected.cameraIndex,
    fast: document.getElementById("fast").checked,
    dt: pulseMs / 1000
  };
}

function isPlateLikeTarget() {
  return selected.target === "plate" || selected.target === "key" || selected.target === "mp4";
}

async function postControl(command) {
  const payload = Object.assign(basePayload(), command);
  try {
    await fetch("/control", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify(payload)
    });
  } catch (error) {
    document.getElementById("selectedText").textContent = "Control connection lost";
  }
}

function formatKeyValue(value) {
  return Number(value || 0).toFixed(2);
}

function collectKeySettings() {
  return {
    enabled: document.getElementById("keyEnabled").checked,
    tolerance: Number(document.getElementById("keyTolerance").value),
    softness: Number(document.getElementById("keySoftness").value),
    despill: Number(document.getElementById("keyDespill").value)
  };
}

function renderKeySettings(key) {
  const activeId = document.activeElement && document.activeElement.id;
  if (keySettingsTimer || activeId === "keyTolerance" || activeId === "keySoftness" || activeId === "keyDespill") {
    return;
  }

  const settings = (key && key.chroma) || defaultKeySettings;
  keySettingsSyncing = true;
  document.getElementById("keyEnabled").checked = settings.enabled !== false;
  document.getElementById("keyTolerance").value = settings.tolerance ?? defaultKeySettings.tolerance;
  document.getElementById("keySoftness").value = settings.softness ?? defaultKeySettings.softness;
  document.getElementById("keyDespill").value = settings.despill ?? defaultKeySettings.despill;
  document.getElementById("keyToleranceValue").textContent = formatKeyValue(document.getElementById("keyTolerance").value);
  document.getElementById("keySoftnessValue").textContent = formatKeyValue(document.getElementById("keySoftness").value);
  document.getElementById("keyDespillValue").textContent = formatKeyValue(document.getElementById("keyDespill").value);
  keySettingsSyncing = false;
}

function sendKeySettings(delay = 160) {
  if (keySettingsSyncing) return;
  const settings = collectKeySettings();
  document.getElementById("keyToleranceValue").textContent = formatKeyValue(settings.tolerance);
  document.getElementById("keySoftnessValue").textContent = formatKeyValue(settings.softness);
  document.getElementById("keyDespillValue").textContent = formatKeyValue(settings.despill);
  selected = { target: "key", cameraIndex: selected.cameraIndex || 0 };
  updateTargetButtons();
  clearTimeout(keySettingsTimer);
  keySettingsTimer = setTimeout(() => {
    keySettingsTimer = null;
    postControl({ action: "setChromaKey", target: "key", chroma: settings });
  }, delay);
}

function selectTarget(target, index) {
  selected = { target, cameraIndex: index || 0 };
  updateTargetButtons();
  postControl({ action: "select", target, cameraIndex: selected.cameraIndex });
}

function updateTargetButtons() {
  document.querySelectorAll("#targets button").forEach(button => {
    const buttonTarget = button.dataset.target;
    const isCamera = buttonTarget === "camera";
    const match = isCamera
      ? selected.target === "camera" && Number(button.dataset.index) === selected.cameraIndex
      : selected.target === buttonTarget;
    button.classList.toggle("selected", match);
  });

  const plateLike = isPlateLikeTarget();
  document.querySelectorAll("[data-plate-only]").forEach(button => button.disabled = !plateLike);
  document.getElementById("aimReset").textContent =
    selected.target === "camera" ? "Aim Center" : (selected.target === "mp4" ? "Reset MP4" : (selected.target === "key" ? "Reset Key" : "Reset Input"));
}

function setupHold(button, command) {
  let timer = null;
  const start = event => {
    if (button.disabled || timer) return;
    event.preventDefault();
    button.classList.add("pressed");
    postControl(command);
    timer = setInterval(() => postControl(command), pulseMs);
  };
  const stop = () => {
    if (!timer) return;
    clearInterval(timer);
    timer = null;
    button.classList.remove("pressed");
  };
  button.addEventListener("pointerdown", start);
  button.addEventListener("pointerup", stop);
  button.addEventListener("pointerleave", stop);
  button.addEventListener("pointercancel", stop);
}

function formatVector(value) {
  if (!value) return "n/a";
  return `X ${value.x.toFixed(1)}  Y ${value.y.toFixed(1)}  Z ${value.z.toFixed(1)}`;
}

function formatRotation(value) {
  if (!value) return "n/a";
  return `P ${value.pitch.toFixed(1)}  Y ${value.yaw.toFixed(1)}  R ${value.roll.toFixed(1)}`;
}

function renderMp4File(mp4) {
  const input = document.getElementById("mp4Path");
  const text = document.getElementById("mp4FileText");
  if (mp4 && mp4.filePath && document.activeElement !== input) {
    input.value = mp4.filePath;
  }

  const fileName = mp4 && mp4.fileName ? mp4.fileName : "No MP4 loaded";
  const status = mp4 && mp4.status ? mp4.status : "Waiting for MP4 player";
  text.textContent = `${fileName}\n${status}`;
  text.classList.toggle("danger", Boolean(mp4 && mp4.status && mp4.status.toLowerCase().includes("not found")));
}

async function uploadMp4File(file) {
  const text = document.getElementById("mp4FileText");
  const chooseButton = document.getElementById("chooseMp4File");
  if (!file) return;
  if (!file.name.toLowerCase().endsWith(".mp4")) {
    text.textContent = "Choose an .mp4 file";
    text.classList.add("danger");
    return;
  }

  selected = { target: "mp4", cameraIndex: selected.cameraIndex || 0 };
  updateTargetButtons();
  chooseButton.disabled = true;
  text.textContent = `Uploading ${file.name}...`;
  text.classList.remove("danger");

  try {
    const response = await fetch(`/uploadmp4?filename=${encodeURIComponent(file.name)}`, {
      method: "POST",
      headers: { "Content-Type": "video/mp4" },
      body: file
    });
    const result = await response.json();
    if (!response.ok || !result.ok) {
      throw new Error(result.errorMessage || "Upload failed");
    }

    document.getElementById("mp4Path").value = result.filePath || "";
    text.textContent = `Uploaded ${result.fileName || file.name}\nLoading on MP4 plate...`;
    await refreshState();
  } catch (error) {
    text.textContent = `Upload failed\n${error.message || error}`;
    text.classList.add("danger");
  } finally {
    chooseButton.disabled = false;
    document.getElementById("mp4FileInput").value = "";
  }
}

)HTML");
	Page += TEXT(R"HTML(
function renderState(state) {
  lastState = state;
  renderMp4File(state.mp4);
  renderKeySettings(state.key);

  if (state.selected) {
    selected.target = state.selected.target;
    selected.cameraIndex = state.selected.cameraIndex || 0;
  }
  updateTargetButtons();

  let source = null;
  let label = "";
  if (selected.target === "plate") {
    source = state.plate;
    label = "DeckLink input plate";
  } else if (selected.target === "key") {
    source = state.key;
    label = "Chroma key plate";
  } else if (selected.target === "mp4") {
    source = state.mp4;
    label = "MP4 media plate";
  } else {
    source = (state.cameras || []).find(camera => camera.index === selected.cameraIndex);
    label = `Camera ${selected.cameraIndex + 1}`;
  }

  document.getElementById("selectedText").textContent = `Selected: ${label}`;
  if (!source || source.available === false) {
    document.getElementById("transformText").textContent = "Target is not ready yet";
    document.getElementById("transformText").classList.add("danger");
    return;
  }

  document.getElementById("transformText").classList.remove("danger");
  document.getElementById("transformText").textContent =
    `Location  ${formatVector(source.location)}\nRotation  ${formatRotation(source.rotation)}\nScale     ${source.scale !== undefined ? source.scale.toFixed(2) : "n/a"}`;
}

async function refreshState() {
  try {
    const response = await fetch("/state", { cache: "no-store" });
    renderState(await response.json());
  } catch (error) {
    document.getElementById("selectedText").textContent = "Waiting for Unreal";
  }
}

document.querySelectorAll("#targets button").forEach(button => {
  button.addEventListener("click", () => selectTarget(button.dataset.target, Number(button.dataset.index || 0)));
});
document.querySelectorAll("[data-hold]").forEach(button => setupHold(button, JSON.parse(button.dataset.hold)));
document.getElementById("aimReset").addEventListener("click", () => {
  postControl(isPlateLikeTarget() ? { action: "reset" } : { action: "lookAt" });
});
document.getElementById("setMp4File").addEventListener("click", () => {
  const path = document.getElementById("mp4Path").value.trim();
  if (!path) return;
  selected = { target: "mp4", cameraIndex: selected.cameraIndex || 0 };
  updateTargetButtons();
  postControl({ action: "setFile", target: "mp4", filePath: path });
});
document.getElementById("mp4Path").addEventListener("keydown", event => {
  if (event.key === "Enter") {
    event.preventDefault();
    document.getElementById("setMp4File").click();
  }
});
document.getElementById("chooseMp4File").addEventListener("click", () => {
  document.getElementById("mp4FileInput").click();
});
document.getElementById("mp4FileInput").addEventListener("change", event => {
  uploadMp4File(event.target.files && event.target.files[0]);
});
document.getElementById("keyEnabled").addEventListener("change", () => sendKeySettings(0));
["keyTolerance", "keySoftness", "keyDespill"].forEach(id => {
  document.getElementById(id).addEventListener("input", () => sendKeySettings());
  document.getElementById(id).addEventListener("change", () => sendKeySettings(0));
});
document.getElementById("resetKeySettings").addEventListener("click", () => {
  keySettingsSyncing = true;
  document.getElementById("keyEnabled").checked = defaultKeySettings.enabled;
  document.getElementById("keyTolerance").value = defaultKeySettings.tolerance;
  document.getElementById("keySoftness").value = defaultKeySettings.softness;
  document.getElementById("keyDespill").value = defaultKeySettings.despill;
  keySettingsSyncing = false;
  sendKeySettings(0);
});
document.getElementById("serverText").textContent = `${window.location.protocol}//${window.location.host}/c`;

updateTargetButtons();
refreshState();
setInterval(refreshState, 500);
</script>
</body>
</html>)HTML");
	return Page;
}

FString AGGGGameModeBase::BuildWebControlStateJson() const
{
	FString CameraJson;
	for (int32 Index = 0; Index < 4; ++Index)
	{
		if (!CameraJson.IsEmpty())
		{
			CameraJson += TEXT(",");
		}

		const ACameraActor* Camera = ControlledCameras.IsValidIndex(Index) ? ControlledCameras[Index].Get() : nullptr;
		if (!Camera)
		{
			CameraJson += FString::Printf(TEXT("{\"index\":%d,\"number\":%d,\"available\":false}"), Index, Index + 1);
			continue;
		}

		CameraJson += FString::Printf(
			TEXT("{\"index\":%d,\"number\":%d,\"available\":true,\"location\":%s,\"rotation\":%s,\"scale\":1.0}"),
			Index,
			Index + 1,
			*BuildVectorJson(Camera->GetActorLocation()),
			*BuildRotatorJson(Camera->GetActorRotation()));
	}

	FString PlateJson = TEXT("\"available\":false");
	if (DeckLinkInputScreenActor && DeckLinkInputScreenMeshComponent)
	{
		PlateJson = FString::Printf(
			TEXT("\"available\":true,\"location\":%s,\"rotation\":%s,\"scale\":%.3f"),
			*BuildVectorJson(DeckLinkInputScreenActor->GetActorLocation()),
			*BuildRotatorJson(DeckLinkInputScreenMeshComponent->GetComponentRotation()),
			DeckLinkInputPlateScaleFactor);
	}

	FString KeyJson = TEXT("\"available\":false");
	if (ChromaKeyPlateActor && ChromaKeyPlateMeshComponent)
	{
		KeyJson = FString::Printf(
			TEXT("\"available\":true,\"location\":%s,\"rotation\":%s,\"scale\":%.3f,\"chroma\":{\"enabled\":%s,\"tolerance\":%.3f,\"softness\":%.3f,\"despill\":%.3f}"),
			*BuildVectorJson(ChromaKeyPlateActor->GetActorLocation()),
			*BuildRotatorJson(ChromaKeyPlateMeshComponent->GetComponentRotation()),
			ChromaKeyPlateScaleFactor,
			bChromaKeyEnabled ? TEXT("true") : TEXT("false"),
			ChromaKeyTolerance,
			ChromaKeySoftness,
			ChromaKeyDespill);
	}

	const FString Mp4FilePathJson = BuildJsonString(ExpressLoopMediaFilePath);
	const FString Mp4FileNameJson = BuildJsonString(FPaths::GetCleanFilename(ExpressLoopMediaFilePath));
	const FString Mp4StatusJson = BuildJsonString(ExpressLoopMediaStatus);
	FString Mp4Json = FString::Printf(
		TEXT("\"available\":false,\"filePath\":%s,\"fileName\":%s,\"status\":%s"),
		*Mp4FilePathJson,
		*Mp4FileNameJson,
		*Mp4StatusJson);
	if (ExpressLoopMediaPlateActor && ExpressLoopMediaPlateMeshComponent)
	{
		Mp4Json = FString::Printf(
			TEXT("\"available\":true,\"location\":%s,\"rotation\":%s,\"scale\":%.3f,\"filePath\":%s,\"fileName\":%s,\"status\":%s"),
			*BuildVectorJson(ExpressLoopMediaPlateActor->GetActorLocation()),
			*BuildRotatorJson(ExpressLoopMediaPlateMeshComponent->GetComponentRotation()),
			ExpressLoopMediaPlateScaleFactor,
			*Mp4FilePathJson,
			*Mp4FileNameJson,
			*Mp4StatusJson);
	}

	const TCHAR* SelectedTargetString = TEXT("camera");
	if (bControllingDeckLinkInputPlate)
	{
		SelectedTargetString = TEXT("plate");
	}
	else if (bControllingChromaKeyPlate)
	{
		SelectedTargetString = TEXT("key");
	}
	else if (bControllingExpressLoopMediaPlate)
	{
		SelectedTargetString = TEXT("mp4");
	}

	return FString::Printf(
		TEXT("{\"webPort\":%u,\"selected\":{\"target\":\"%s\",\"cameraIndex\":%d,\"cameraNumber\":%d},\"cameras\":[%s],\"plate\":{%s},\"key\":{%s},\"mp4\":{%s}}"),
		WebControlPort,
		SelectedTargetString,
		SelectedCameraIndex,
		SelectedCameraIndex + 1,
		*CameraJson,
		*PlateJson,
		*KeyJson,
		*Mp4Json);
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
	DeckLinkInputMediaSource->MaxNumVideoFrameBuffer = 32;
	DeckLinkInputMediaSource->bLogDropFrame = false;

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
	DeckLinkInputScreenActor->SetActorLocation(DeckLinkInputPlateDefaultLocation);
	DeckLinkInputScreenActor->SetActorRotation(FRotator::ZeroRotator);

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
	ResetDeckLinkInputPlate();

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

void AGGGGameModeBase::SetupChromaKeyPlate()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	if (!DeckLinkInputDisplayMesh || !DeckLinkInputScreenMaterial)
	{
		UE_LOG(LogTemp, Warning, TEXT("Chroma key plate skipped: engine display mesh or fallback material is unavailable."));
		return;
	}

	const FString ChromaKeyMediaPath = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectContentDir(), ChromaKeyPlateRelativePath));
	if (!FPaths::FileExists(ChromaKeyMediaPath))
	{
		UE_LOG(LogTemp, Warning, TEXT("Chroma key plate skipped: media file not found at %s."), *ChromaKeyMediaPath);
		return;
	}

	ChromaKeyMediaSource = NewObject<UFileMediaSource>(this, TEXT("ChromaKeyVideoSource"));
	ChromaKeyMediaSource->SetFilePath(ChromaKeyMediaPath);
	ChromaKeyMediaSource->PrecacheFile = false;

	ChromaKeyMediaPlayer = NewObject<UMediaPlayer>(this, TEXT("ChromaKeyVideoPlayer"));
	ChromaKeyMediaPlayer->PlayOnOpen = true;
	ChromaKeyMediaPlayer->SetDesiredPlayerName(ChromaKeyMediaPlayerName);
	ChromaKeyMediaPlayer->SetLooping(false);

	ChromaKeyMediaTexture = NewObject<UMediaTexture>(this, TEXT("ChromaKeyVideoTexture"));
	ChromaKeyMediaTexture->SetMediaPlayer(ChromaKeyMediaPlayer);
	ChromaKeyMediaTexture->UpdateResource();

	FActorSpawnParameters SpawnParameters;
	SpawnParameters.Name = TEXT("ChromaKeyPlate");
	SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ChromaKeyPlateActor = World->SpawnActor<AActor>(AActor::StaticClass(), ChromaKeyPlateDefaultLocation, FRotator::ZeroRotator, SpawnParameters);
	if (!ChromaKeyPlateActor)
	{
		UE_LOG(LogTemp, Warning, TEXT("Chroma key plate skipped: could not spawn the plate actor."));
		return;
	}

	USceneComponent* PlateRootComponent = NewObject<USceneComponent>(ChromaKeyPlateActor, TEXT("ChromaKeyPlateRoot"));
	ChromaKeyPlateActor->SetRootComponent(PlateRootComponent);
	ChromaKeyPlateActor->AddInstanceComponent(PlateRootComponent);
	PlateRootComponent->SetMobility(EComponentMobility::Movable);
	PlateRootComponent->RegisterComponent();
	ChromaKeyPlateActor->SetActorLocation(ChromaKeyPlateDefaultLocation);
	ChromaKeyPlateActor->SetActorRotation(FRotator::ZeroRotator);

	AddChromaKeyPlateMesh(ChromaKeyPlateActor, ChromaKeyMediaTexture);
	ResetChromaKeyPlate();

	const bool bOpened = ChromaKeyMediaPlayer->OpenSource(ChromaKeyMediaSource);
	if (bOpened)
	{
		ChromaKeyMediaPlayer->Play();
		ChromaKeyMediaPlayer->SetRate(1.0f);
		ChromaKeyMediaFilePath = ChromaKeyMediaPath;
		bChromaKeyMediaTimeInitialized = false;
		ChromaKeyLastMediaTime = FTimespan::Zero();
		ChromaKeyLastAdvanceWorldTimeSeconds = World->GetTimeSeconds();
		ChromaKeyLastRestartWorldTimeSeconds = World->GetTimeSeconds() - ChromaKeyMediaRestartCooldownSeconds;
		ChromaKeyLastPlaybackCommandWorldTimeSeconds = World->GetTimeSeconds();
		ChromaKeyMediaRestartCounter = 0;
		ApplyChromaKeyTextureToMaterial(ChromaKeyPlateMaterial, ChromaKeyMediaTexture);
		ApplyChromaKeySettingsToMaterial(ChromaKeyPlateMaterial, bChromaKeyEnabled, ChromaKeyTolerance, ChromaKeySoftness, ChromaKeyDespill);
		if (ChromaKeyPlateMeshComponent)
		{
			ChromaKeyPlateMeshComponent->MarkRenderStateDirty();
		}
	}

	UE_LOG(LogTemp, Display, TEXT("Chroma key plate loaded %s as a looping reporter video in front of the DeckLink input and MP4 plates; open=%s player=%s material=%s."),
		*ChromaKeyMediaPath,
		bOpened ? TEXT("yes") : TEXT("no"),
		*ChromaKeyMediaPlayer->GetDesiredPlayerName().ToString(),
		ChromaKeyScreenMaterial ? TEXT("runtime alpha key material") : TEXT("fallback media material"));
}

void AGGGGameModeBase::SetupExpressLoopMediaPlate()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	if (!DeckLinkInputDisplayMesh || !DeckLinkInputScreenMaterial)
	{
		UE_LOG(LogTemp, Warning, TEXT("Express loop media plate skipped: engine display mesh or video material is unavailable."));
		return;
	}

	const FString FullMediaPath = ResolveExpressLoopMediaFilePath(ExpressLoopMediaRelativePath);
	if (!FPaths::FileExists(FullMediaPath))
	{
		ExpressLoopMediaStatus = FString::Printf(TEXT("Express loop media plate skipped: file not found at %s."), *FullMediaPath);
		UE_LOG(LogTemp, Warning, TEXT("%s"), *ExpressLoopMediaStatus);
		return;
	}

	ExpressLoopMediaSource = NewObject<UFileMediaSource>(this, TEXT("ExpressLoopMediaSource"));
	ExpressLoopMediaSource->PrecacheFile = false;

	ExpressLoopMediaPlayer = NewObject<UMediaPlayer>(this, TEXT("ExpressLoopMediaPlayer"));
	ConfigureExpressLoopMediaPlayer(ExpressLoopMediaPlayer, true);
	ExpressLoopMediaPlayer->OnEndReached.AddUniqueDynamic(this, &AGGGGameModeBase::HandleExpressLoopMediaEndReached);
	ExpressLoopMediaPlayer->OnMediaOpened.AddUniqueDynamic(this, &AGGGGameModeBase::HandleExpressLoopMediaOpened);

	ExpressLoopMediaTexture = NewObject<UMediaTexture>(this, TEXT("ExpressLoopMediaTexture"));
	ExpressLoopMediaTexture->SetMediaPlayer(ExpressLoopMediaPlayer);
	ExpressLoopMediaTexture->UpdateResource();

	ExpressLoopStandbyMediaSource = NewObject<UFileMediaSource>(this, TEXT("ExpressLoopStandbyMediaSource"));
	ExpressLoopStandbyMediaSource->PrecacheFile = false;

	ExpressLoopStandbyMediaPlayer = NewObject<UMediaPlayer>(this, TEXT("ExpressLoopStandbyMediaPlayer"));
	ConfigureExpressLoopMediaPlayer(ExpressLoopStandbyMediaPlayer, false);

	ExpressLoopStandbyMediaTexture = NewObject<UMediaTexture>(this, TEXT("ExpressLoopStandbyMediaTexture"));
	ExpressLoopStandbyMediaTexture->SetMediaPlayer(ExpressLoopStandbyMediaPlayer);
	ExpressLoopStandbyMediaTexture->UpdateResource();
	RefreshExpressLoopMediaPlayerDelegates();

	FActorSpawnParameters SpawnParameters;
	SpawnParameters.Name = TEXT("ExpressLoopMediaPlate");
	SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ExpressLoopMediaPlateActor = World->SpawnActor<AActor>(AActor::StaticClass(), ExpressLoopMediaPlateLocation, FRotator::ZeroRotator, SpawnParameters);
	if (!ExpressLoopMediaPlateActor)
	{
		UE_LOG(LogTemp, Warning, TEXT("Express loop media plate skipped: could not spawn the plate actor."));
		return;
	}

	USceneComponent* PlateRootComponent = NewObject<USceneComponent>(ExpressLoopMediaPlateActor, TEXT("ExpressLoopMediaPlateRoot"));
	ExpressLoopMediaPlateActor->SetRootComponent(PlateRootComponent);
	ExpressLoopMediaPlateActor->AddInstanceComponent(PlateRootComponent);
	PlateRootComponent->SetMobility(EComponentMobility::Movable);
	PlateRootComponent->RegisterComponent();
	ExpressLoopMediaPlateActor->SetActorLocation(ExpressLoopMediaPlateLocation);
	ExpressLoopMediaPlateActor->SetActorRotation(FRotator::ZeroRotator);

	AddExpressLoopMediaPlateMesh(ExpressLoopMediaPlateActor, ExpressLoopMediaTexture);
	ResetExpressLoopMediaPlate();
	OpenExpressLoopMediaFile(FullMediaPath, false);
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

void AGGGGameModeBase::AddChromaKeyPlateMesh(AActor* PlateActor, UTexture* PlateTexture)
{
	if (!PlateActor || !PlateActor->GetRootComponent() || !PlateTexture || !DeckLinkInputDisplayMesh || !DeckLinkInputScreenMaterial)
	{
		return;
	}

	ChromaKeyPlateMeshComponent = NewObject<UStaticMeshComponent>(PlateActor, TEXT("ChromaKeyPlateMesh"));
	ChromaKeyPlateMeshComponent->SetupAttachment(PlateActor->GetRootComponent());
	ChromaKeyPlateMeshComponent->SetStaticMesh(DeckLinkInputDisplayMesh);
	ChromaKeyPlateMeshComponent->SetRelativeRotation(ChromaKeyPlateDefaultRotation);
	ChromaKeyPlateMeshComponent->SetRelativeScale3D(ChromaKeyPlateDefaultScale);
	ChromaKeyPlateMeshComponent->SetReverseCulling(true);
	ChromaKeyPlateMeshComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	ChromaKeyPlateMeshComponent->SetGenerateOverlapEvents(false);
	ChromaKeyPlateMeshComponent->SetCastShadow(false);
	ChromaKeyPlateMeshComponent->bReceivesDecals = false;
	ChromaKeyPlateMeshComponent->SetAffectDistanceFieldLighting(false);
	ChromaKeyPlateMeshComponent->SetAffectDynamicIndirectLighting(false);

	PlateActor->AddInstanceComponent(ChromaKeyPlateMeshComponent);
	ChromaKeyPlateMeshComponent->RegisterComponent();

	UMaterialInterface* MaterialBase = ChromaKeyScreenMaterial ? ChromaKeyScreenMaterial.Get() : DeckLinkInputScreenMaterial.Get();
	ChromaKeyPlateMaterial = ChromaKeyPlateMeshComponent->CreateAndSetMaterialInstanceDynamicFromMaterial(0, MaterialBase);
	ApplyChromaKeyTextureToMaterial(ChromaKeyPlateMaterial, PlateTexture);
	ApplyChromaKeySettingsToMaterial(ChromaKeyPlateMaterial, bChromaKeyEnabled, ChromaKeyTolerance, ChromaKeySoftness, ChromaKeyDespill);
}

void AGGGGameModeBase::AddExpressLoopMediaPlateMesh(AActor* PlateActor, UMediaTexture* MediaTexture)
{
	if (!PlateActor || !PlateActor->GetRootComponent() || !MediaTexture || !DeckLinkInputDisplayMesh || !DeckLinkInputScreenMaterial)
	{
		return;
	}

	ExpressLoopMediaPlateMeshComponent = NewObject<UStaticMeshComponent>(PlateActor, TEXT("ExpressLoopMediaPlateMesh"));
	ExpressLoopMediaPlateMeshComponent->SetupAttachment(PlateActor->GetRootComponent());
	ExpressLoopMediaPlateMeshComponent->SetStaticMesh(DeckLinkInputDisplayMesh);
	ExpressLoopMediaPlateMeshComponent->SetRelativeRotation(ExpressLoopMediaPlateRotation);
	ExpressLoopMediaPlateMeshComponent->SetRelativeScale3D(ExpressLoopMediaPlateScale);
		ExpressLoopMediaPlateMeshComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		ExpressLoopMediaPlateMeshComponent->SetGenerateOverlapEvents(false);
		ExpressLoopMediaPlateMeshComponent->SetCastShadow(false);
		ExpressLoopMediaPlateMeshComponent->bReceivesDecals = false;
		ExpressLoopMediaPlateMeshComponent->SetAffectDistanceFieldLighting(false);
		ExpressLoopMediaPlateMeshComponent->SetAffectDynamicIndirectLighting(false);

		PlateActor->AddInstanceComponent(ExpressLoopMediaPlateMeshComponent);
		ExpressLoopMediaPlateMeshComponent->RegisterComponent();

	ExpressLoopMediaPlateMaterial = ExpressLoopMediaPlateMeshComponent->CreateAndSetMaterialInstanceDynamicFromMaterial(0, DeckLinkInputScreenMaterial);
	ApplyMediaTextureToMaterial(ExpressLoopMediaPlateMaterial, MediaTexture);
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
	DeckLinkInputScreenMeshComponent->SetAffectDistanceFieldLighting(false);
	DeckLinkInputScreenMeshComponent->SetAffectDynamicIndirectLighting(false);

	ScreenActor->AddInstanceComponent(DeckLinkInputScreenMeshComponent);
	DeckLinkInputScreenMeshComponent->RegisterComponent();

	UMaterialInstanceDynamic* DynamicMaterial = DeckLinkInputScreenMeshComponent->CreateAndSetMaterialInstanceDynamicFromMaterial(0, DeckLinkInputScreenMaterial);
	ApplyMediaTextureToMaterial(DynamicMaterial, MediaTexture);
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
		SceneCapture->PostProcessBlendWeight = 1.0f;
		SceneCapture->PostProcessSettings.bOverride_AutoExposureMethod = true;
		SceneCapture->PostProcessSettings.AutoExposureMethod = AEM_Manual;
		SceneCapture->PostProcessSettings.bOverride_AutoExposureBias = true;
		SceneCapture->PostProcessSettings.AutoExposureBias = 0.0f;
		SceneCapture->PostProcessSettings.bOverride_AutoExposureApplyPhysicalCameraExposure = true;
		SceneCapture->PostProcessSettings.AutoExposureApplyPhysicalCameraExposure = false;
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
		MediaOutput->AudioBufferSize = 20 * 1024;
		MediaOutput->OutputChannelCount = EBlackmagicMediaAudioOutputChannelCount::CH_2;
		MediaOutput->AudioBitDepth = EBlackmagicMediaOutputAudioBitDepth::Signed_16Bits;
		MediaOutput->bOutputAudio = true;
		MediaOutput->bLogDropFrame = false;
		MediaOutput->bUseMultithreadedScheduling = true;
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
