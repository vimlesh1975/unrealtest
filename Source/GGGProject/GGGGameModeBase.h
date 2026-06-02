#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/HUD.h"
#include "GGGGameModeBase.generated.h"

class AActor;
class ACameraActor;
class ASceneCapture2D;
class UBlackmagicMediaSource;
class UMaterialInterface;
class UMediaCapture;
class UBlackmagicMediaOutput;
class UEngineCustomTimeStep;
class UMediaPlayer;
class UMediaTexture;
class USceneCaptureComponent2D;
class UStaticMesh;
class UStaticMeshComponent;
class UTextureRenderTarget2D;
struct FMediaIOConfiguration;
struct FMediaIOOutputConfiguration;

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

private:
	void ConfigureDeckLinkTiming();
	void SetupSplitScreenCameras();
	void SetupDeckLinkInputScreen();
	void SetupDeckLinkOutputs(const TArray<AActor*>& OrderedCameras);
	void AddDeckLinkInputScreenMesh(AActor* ScreenActor, UMediaTexture* MediaTexture);
	void HandleCameraControl(float DeltaSeconds);
	void SyncDeckLinkCaptures();
	void SyncDeckLinkCapture(int32 CameraIndex, bool bCaptureScene);
	void UpdateSelectedViewportCamera();
	void LogDeckLinkOutputConfigurations() const;
	void ShowCameraControlStatus() const;
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
	TObjectPtr<AActor> DeckLinkInputScreenActor;

	UPROPERTY(Transient)
	TObjectPtr<UStaticMeshComponent> DeckLinkInputScreenMeshComponent;

	UPROPERTY()
	TObjectPtr<UStaticMesh> DeckLinkInputDisplayMesh;

	UPROPERTY()
	TObjectPtr<UMaterialInterface> DeckLinkInputScreenMaterial;

	int32 SelectedCameraIndex = 0;
};
