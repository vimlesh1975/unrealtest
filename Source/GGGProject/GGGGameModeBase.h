#pragma once

#include "Containers/Queue.h"
#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/HUD.h"
#include "HAL/CriticalSection.h"
#include "HttpRouteHandle.h"
#include "TimerManager.h"
#include "GGGGameModeBase.generated.h"

class AActor;
class ACameraActor;
class APlayerController;
class ASceneCapture2D;
class UBlackmagicMediaSource;
class UFileMediaSource;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class UMediaCapture;
class UBlackmagicMediaOutput;
class UEngineCustomTimeStep;
class UMediaPlayer;
class UMediaSoundComponent;
class UMediaTexture;
class USceneCaptureComponent2D;
class UStaticMesh;
class UStaticMeshComponent;
class UTexture;
class UTexture2D;
class UTextureRenderTarget2D;
class IHttpRouter;
struct FMediaIOConfiguration;
struct FMediaIOOutputConfiguration;

enum class EGGGWebControlTarget : uint8
{
	Camera,
	DeckLinkInputPlate,
	ExpressLoopMediaPlate,
	ChromaKeyPlate
};

struct FGGGWebControlCommand
{
	EGGGWebControlTarget Target = EGGGWebControlTarget::Camera;
	int32 CameraIndex = 0;
	FVector Move = FVector::ZeroVector;
	FRotator Rotation = FRotator::ZeroRotator;
	float Scale = 0.0f;
	float DeltaSeconds = 0.08f;
	FString FilePath;
	float ChromaKeyTolerance = -1.0f;
	float ChromaKeySoftness = -1.0f;
	float ChromaKeyDespill = -1.0f;
	bool bFast = false;
	bool bSelectOnly = false;
	bool bLookAt = false;
	bool bReset = false;
	bool bSetFile = false;
	bool bSetChromaKeySettings = false;
	bool bHasChromaKeyEnabled = false;
	bool bChromaKeyEnabled = false;
};

UCLASS()
class GGGPROJECT_API AGGGPreviewHUD : public AHUD
{
	GENERATED_BODY()

public:
	virtual void DrawHUD() override;
};

UCLASS()
class GGGPROJECT_API AGGGGameModeBase : public AGameModeBase
{
	GENERATED_BODY()

public:
	AGGGGameModeBase();
	const TArray<TObjectPtr<UTextureRenderTarget2D>>& GetDeckLinkPreviewTargets() const;

protected:
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	void ConfigureDeckLinkTiming();
	void ConfigureWindowAndMouse();
	void SetupSplitScreenCameras();
	void SetupStudioRoom();
	void AddStudioRoomSurface(const FName& SurfaceName, const FVector& Location, const FVector& Scale, const FLinearColor& Color);
	void AddStudioRoomLight(const FName& LightName, const FVector& Location, float Intensity, float Radius);
	void ArrangeStudioCameras(const TArray<AActor*>& OrderedCameras);
	void SetupDeckLinkInputScreen();
	void SetupChromaKeyPlate();
	void SetupExpressLoopMediaPlate();
	void KickDeckLinkInputAudio();
	void SetupDeckLinkOutputs(const TArray<AActor*>& OrderedCameras);
	void AddDeckLinkInputScreenMesh(AActor* ScreenActor, UMediaTexture* MediaTexture);
	void AddChromaKeyPlateMesh(AActor* PlateActor, UTexture* PlateTexture);
	void AddExpressLoopMediaPlateMesh(AActor* PlateActor, UMediaTexture* MediaTexture);
	void HandleCameraControl(float DeltaSeconds);
	void HandleDeckLinkInputPlateControl(APlayerController* PlayerController, float DeltaSeconds);
	void HandleChromaKeyPlateControl(APlayerController* PlayerController, float DeltaSeconds);
	void HandleExpressLoopMediaPlateControl(APlayerController* PlayerController, float DeltaSeconds);
	void SelectCameraControl(int32 CameraIndex, bool bShowStatus);
	void SelectDeckLinkInputPlateControl(bool bShowStatus);
	void SelectChromaKeyPlateControl(bool bShowStatus);
	void SelectExpressLoopMediaPlateControl(bool bShowStatus);
	void ApplyCameraControl(int32 CameraIndex, const FVector& MoveInput, const FRotator& RotationInput, bool bFastMove, float DeltaSeconds);
	void AimCameraAtFocusPoint(int32 CameraIndex);
	void ApplyDeckLinkInputPlateControl(const FVector& MoveInput, const FRotator& RotationInput, float ScaleDirection, bool bFastMove, float DeltaSeconds);
	void ApplyChromaKeyPlateControl(const FVector& MoveInput, const FRotator& RotationInput, float ScaleDirection, bool bFastMove, float DeltaSeconds);
	void ApplyExpressLoopMediaPlateControl(const FVector& MoveInput, const FRotator& RotationInput, float ScaleDirection, bool bFastMove, float DeltaSeconds);
	void ResetDeckLinkInputPlate();
	void ResetChromaKeyPlate();
	void ResetExpressLoopMediaPlate();
	FString ResolveExpressLoopMediaFilePath(const FString& FilePath) const;
	bool OpenExpressLoopMediaFile(const FString& FilePath, bool bShowStatus);
	bool LoadChromaKeySourceImage(const FString& ImagePath);
	void RebuildChromaKeyTexture();
	void ApplyChromaKeySettings(const FGGGWebControlCommand& Command, bool bShowStatus);
	void KeepChromaKeyMediaLooping();
	void RestartChromaKeyMediaPlayback(const TCHAR* Reason);
	void ConfigureExpressLoopMediaPlayer(UMediaPlayer* MediaPlayer, bool bPlayOnOpen) const;
	void ConfigureExpressLoopMediaSource(UFileMediaSource* MediaSource, const FString& FilePath) const;
	void RefreshExpressLoopMediaPlayerDelegates();
	bool TryOpenExpressLoopFrameAnimation(const FString& FilePath, bool bShowStatus);
	void UpdateExpressLoopFrameAnimation(float DeltaSeconds);
	void ApplyExpressLoopFrameTexture(int32 FrameIndex);
	void StopExpressLoopFrameAnimation();
	bool PrepareExpressLoopStandbyMediaPlayer(const FString& FilePath);
	UFUNCTION()
	void HandleExpressLoopMediaEndReached();
	UFUNCTION()
	void HandleExpressLoopMediaOpened(FString OpenedUrl);
	void HandleExpressLoopMediaOpenedOnGameThread(const FString& OpenedUrl);
	void LoopExpressLoopMediaPlayback(bool bFromEndReached);
	void CompleteExpressLoopMediaPlayerSwap(bool bFromEndReached);
	void ReopenExpressLoopMediaPlayback(bool bFromEndReached);
	void KeepExpressLoopMediaLooping();
	void StartExpressLoopMediaWatchdog();
	void SyncDeckLinkCaptures();
	void SyncDeckLinkCapture(int32 CameraIndex, bool bCaptureScene);
	void UpdateSelectedViewportCamera();
	void StartWebControlServer();
	void StopWebControlServer();
	void ProcessWebControlCommands();
	bool EnqueueWebControlCommandFromJson(const FString& RequestBody, FString& OutError);
	bool SaveUploadedExpressLoopMediaFile(const FString& UploadedFileName, const TArray<uint8>& FileBytes, FString& OutSavedFilePath, FString& OutError) const;
	void UpdateCachedWebControlState();
	FString BuildWebControlPage() const;
	FString BuildWebControlStateJson() const;
	void LogDeckLinkOutputConfigurations() const;
	void ShowCameraControlStatus() const;
	void ShowDeckLinkInputPlateControlStatus() const;
	void ShowChromaKeyPlateControlStatus() const;
	void ShowExpressLoopMediaPlateControlStatus() const;
	bool FindDeckLinkInputConfiguration(int32 DeviceIdentifier, FMediaIOConfiguration& OutConfiguration) const;
	bool FindDeckLinkOutputConfiguration(int32 DeviceIdentifier, FMediaIOOutputConfiguration& OutConfiguration) const;

	UPROPERTY(Transient)
	TArray<TObjectPtr<ACameraActor>> ControlledCameras;

	UPROPERTY(Transient)
	TObjectPtr<ACameraActor> MonitorViewportCamera;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UTextureRenderTarget2D>> DeckLinkRenderTargets;

	UPROPERTY(Transient)
	TArray<TObjectPtr<ASceneCapture2D>> DeckLinkSceneCaptureActors;

	UPROPERTY(Transient)
	TArray<TObjectPtr<USceneCaptureComponent2D>> DeckLinkSceneCaptures;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UBlackmagicMediaOutput>> DeckLinkMediaOutputs;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UMediaCapture>> DeckLinkMediaCaptures;

	UPROPERTY(Transient)
	TObjectPtr<UEngineCustomTimeStep> DeckLinkCustomTimeStep;

	UPROPERTY(Transient)
	TObjectPtr<UBlackmagicMediaSource> DeckLinkInputMediaSource;

	UPROPERTY(Transient)
	TObjectPtr<UMediaPlayer> DeckLinkInputMediaPlayer;

	UPROPERTY(Transient)
	TObjectPtr<UMediaTexture> DeckLinkInputMediaTexture;

	UPROPERTY(Transient)
	TObjectPtr<UMediaSoundComponent> DeckLinkInputMediaSoundComponent;

	UPROPERTY(Transient)
	TObjectPtr<AActor> DeckLinkInputScreenActor;

	UPROPERTY(Transient)
	TObjectPtr<UStaticMeshComponent> DeckLinkInputScreenMeshComponent;

	UPROPERTY(Transient)
	TObjectPtr<UTexture2D> ChromaKeyPlateTexture;

	UPROPERTY(Transient)
	TObjectPtr<UFileMediaSource> ChromaKeyMediaSource;

	UPROPERTY(Transient)
	TObjectPtr<UMediaPlayer> ChromaKeyMediaPlayer;

	UPROPERTY(Transient)
	TObjectPtr<UMediaTexture> ChromaKeyMediaTexture;

	UPROPERTY(Transient)
	TObjectPtr<AActor> ChromaKeyPlateActor;

	UPROPERTY(Transient)
	TObjectPtr<UStaticMeshComponent> ChromaKeyPlateMeshComponent;

	UPROPERTY(Transient)
	TObjectPtr<UStaticMeshComponent> ChromaKeyPlateBackMeshComponent;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> ChromaKeyPlateMaterial;

	TArray<FColor> ChromaKeySourcePixels;
	int32 ChromaKeyTextureWidth = 0;
	int32 ChromaKeyTextureHeight = 0;
	bool bChromaKeyEnabled = false;
	float ChromaKeyTolerance = 0.12f;
	float ChromaKeySoftness = 0.22f;
	float ChromaKeyDespill = 0.75f;
	FString ChromaKeyMediaFilePath;
	FTimespan ChromaKeyLastMediaTime = FTimespan::Zero();
	double ChromaKeyLastAdvanceWorldTimeSeconds = 0.0;
	double ChromaKeyLastRestartWorldTimeSeconds = -1000.0;
	double ChromaKeyLastPlaybackCommandWorldTimeSeconds = -1000.0;
	int32 ChromaKeyMediaRestartCounter = 0;
	bool bChromaKeyMediaTimeInitialized = false;

	UPROPERTY(Transient)
	TObjectPtr<UFileMediaSource> ExpressLoopMediaSource;

	UPROPERTY(Transient)
	TObjectPtr<UMediaPlayer> ExpressLoopMediaPlayer;

	UPROPERTY(Transient)
	TObjectPtr<UMediaTexture> ExpressLoopMediaTexture;

	UPROPERTY(Transient)
	TObjectPtr<UFileMediaSource> ExpressLoopStandbyMediaSource;

	UPROPERTY(Transient)
	TObjectPtr<UMediaPlayer> ExpressLoopStandbyMediaPlayer;

	UPROPERTY(Transient)
	TObjectPtr<UMediaTexture> ExpressLoopStandbyMediaTexture;

	UPROPERTY(Transient)
	TObjectPtr<AActor> ExpressLoopMediaPlateActor;

	UPROPERTY(Transient)
	TObjectPtr<UStaticMeshComponent> ExpressLoopMediaPlateMeshComponent;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> ExpressLoopMediaPlateMaterial;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UTexture2D>> ExpressLoopFrameTextures;

	FString ExpressLoopMediaFilePath;
	FString ExpressLoopMediaStatus;

	UPROPERTY(Transient)
	TArray<TObjectPtr<AActor>> StudioRoomActors;

	FTimerHandle DeckLinkInputAudioKickTimerHandle;
	FTimerHandle ExpressLoopMediaWatchdogTimerHandle;
	FTimerHandle ExpressLoopMediaSwapTimerHandle;
	int32 DeckLinkInputAudioKickAttempts = 0;
	int32 ExpressLoopMediaLoopCounter = 0;
	int32 ExpressLoopMediaSwapWarmupAttempts = 0;
	int32 ExpressLoopFrameAnimationFrameIndex = INDEX_NONE;
	float ExpressLoopFrameAnimationTimeSeconds = 0.0f;
	double ExpressLoopKnownDurationSeconds = 0.0;
	double ExpressLoopLastSwitchWorldTimeSeconds = 0.0;
	bool bUsingExpressLoopFrameAnimation = false;

	TQueue<FGGGWebControlCommand, EQueueMode::Mpsc> PendingWebControlCommands;
	TSharedPtr<IHttpRouter> WebControlRouter;
	FHttpRouteHandle WebControlPageRouteHandle;
	FHttpRouteHandle WebControlStateRouteHandle;
	FHttpRouteHandle WebControlCommandRouteHandle;
	FHttpRouteHandle WebControlUploadRouteHandle;
	FCriticalSection WebControlStateCriticalSection;
	FString CachedWebControlStateJson;
	uint32 WebControlPort = 8080;
	bool bWebControlServerStarted = false;

	UPROPERTY()
	TObjectPtr<UStaticMesh> DeckLinkInputDisplayMesh;

	UPROPERTY()
	TObjectPtr<UMaterialInterface> DeckLinkInputScreenMaterial;

	UPROPERTY()
	TObjectPtr<UMaterialInterface> ChromaKeyScreenMaterial;

	UPROPERTY()
	TObjectPtr<UStaticMesh> StudioRoomMesh;

	UPROPERTY()
	TObjectPtr<UMaterialInterface> StudioRoomMaterial;

	int32 SelectedCameraIndex = 0;
	bool bControllingDeckLinkInputPlate = false;
	bool bControllingChromaKeyPlate = false;
	bool bControllingExpressLoopMediaPlate = false;
	bool bExpressLoopMediaRestarting = false;
	float DeckLinkInputPlateScaleFactor = 1.0f;
	float ChromaKeyPlateScaleFactor = 1.0f;
	float ExpressLoopMediaPlateScaleFactor = 1.0f;
};
