// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class BLACKMAGICMEDIA_API FBlackmagicMediaAudioBridge
{
public:
	static void PushInputAudio(int32 DeviceIndex, const int32* AudioBuffer, int32 AudioValueCount, int32 NumChannels, int32 SampleRate);
	static bool PullAudioForOutput(const void* ReaderId, int32 NumFrames, int32 NumOutputChannels, TArray<int32>& OutAudioBuffer);
	static void UnregisterOutputReader(const void* ReaderId);
};
