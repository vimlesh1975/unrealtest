#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "GGGGameModeBase.generated.h"

class ACameraActor;
class UMediaCapture;
class UBlackmagicMediaOutput;
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

private:
	void SetupSplitScreenCameras();
	void SetupDeckLinkOutputs(const TArray<AActor*>& OrderedCameras);
	bool FindDeckLinkOutputConfiguration(int32 DeviceIdentifier, FMediaIOOutputConfiguration& OutConfiguration) const;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UTextureRenderTarget2D>> DeckLinkRenderTargets;

	UPROPERTY(Transient)
	TArray<TObjectPtr<USceneCaptureComponent2D>> DeckLinkSceneCaptures;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UBlackmagicMediaOutput>> DeckLinkMediaOutputs;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UMediaCapture>> DeckLinkMediaCaptures;
};
