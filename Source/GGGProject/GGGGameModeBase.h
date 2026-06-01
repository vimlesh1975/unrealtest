#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "GGGGameModeBase.generated.h"

class ACameraActor;
class ASceneCapture2D;
class UMediaCapture;
class UBlackmagicMediaOutput;
class UEngineCustomTimeStep;
class USceneCaptureComponent2D;
class UTextureRenderTarget2D;
struct FMediaIOOutputConfiguration;

UCLASS()
class GGGPROJECT_API AGGGGameModeBase : public AGameModeBase
{
	GENERATED_BODY()

public:
	AGGGGameModeBase();

protected:
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

private:
	void ConfigureDeckLinkTiming();
	void SetupSplitScreenCameras();
	void SetupDeckLinkOutputs(const TArray<AActor*>& OrderedCameras);
	void HandleCameraControl(float DeltaSeconds);
	void SyncDeckLinkCaptures();
	void SyncDeckLinkCapture(int32 CameraIndex, bool bCaptureScene);
	void UpdateSelectedViewportCamera() const;
	void LogDeckLinkOutputConfigurations() const;
	void ShowCameraControlStatus() const;
	bool FindDeckLinkOutputConfiguration(int32 DeviceIdentifier, FMediaIOOutputConfiguration& OutConfiguration) const;

	UPROPERTY(Transient)
	TArray<TObjectPtr<ACameraActor>> ControlledCameras;

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

	int32 SelectedCameraIndex = 0;
};
