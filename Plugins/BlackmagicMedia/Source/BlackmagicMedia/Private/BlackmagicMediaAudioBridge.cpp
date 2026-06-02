// Copyright Epic Games, Inc. All Rights Reserved.

#include "BlackmagicMediaAudioBridge.h"

#include "HAL/CriticalSection.h"
#include "Misc/ScopeLock.h"

namespace UE::BlackmagicMediaAudioBridge
{
	static constexpr int32 TargetInputDevice = 5;
	static constexpr int32 MaxBufferedSeconds = 4;

	static FCriticalSection CriticalSection;
	static TArray<int32> RingBuffer;
	static TMap<const void*, int64> ReaderFramePositions;
	static int64 WriteFramePosition = 0;
	static int32 NumInputChannels = 0;
	static int32 InputSampleRate = 0;
	static int32 CapacityFrames = 0;

	static void ResetLocked(int32 InNumChannels, int32 InSampleRate)
	{
		NumInputChannels = InNumChannels;
		InputSampleRate = InSampleRate;
		CapacityFrames = FMath::Max(InSampleRate * MaxBufferedSeconds, InSampleRate);
		RingBuffer.SetNumZeroed(CapacityFrames * NumInputChannels);
		ReaderFramePositions.Reset();
		WriteFramePosition = 0;
	}

	static bool HasValidAudioLocked()
	{
		return NumInputChannels > 0 && InputSampleRate > 0 && CapacityFrames > 0 && RingBuffer.Num() > 0;
	}
}

void FBlackmagicMediaAudioBridge::PushInputAudio(int32 DeviceIndex, const int32* AudioBuffer, int32 AudioValueCount, int32 InNumChannels, int32 InSampleRate)
{
	if (DeviceIndex != UE::BlackmagicMediaAudioBridge::TargetInputDevice || !AudioBuffer || AudioValueCount <= 0 || InNumChannels <= 0 || InSampleRate <= 0)
	{
		return;
	}

	const int32 NumFrames = AudioValueCount / InNumChannels;
	if (NumFrames <= 0)
	{
		return;
	}

	FScopeLock Lock(&UE::BlackmagicMediaAudioBridge::CriticalSection);

	if (!UE::BlackmagicMediaAudioBridge::HasValidAudioLocked() ||
		UE::BlackmagicMediaAudioBridge::NumInputChannels != InNumChannels ||
		UE::BlackmagicMediaAudioBridge::InputSampleRate != InSampleRate)
	{
		UE::BlackmagicMediaAudioBridge::ResetLocked(InNumChannels, InSampleRate);
	}

	for (int32 FrameIndex = 0; FrameIndex < NumFrames; ++FrameIndex)
	{
		const int32 RingFrameIndex = static_cast<int32>((UE::BlackmagicMediaAudioBridge::WriteFramePosition + FrameIndex) % UE::BlackmagicMediaAudioBridge::CapacityFrames);
		int32* RingFrame = UE::BlackmagicMediaAudioBridge::RingBuffer.GetData() + RingFrameIndex * InNumChannels;
		const int32* SourceFrame = AudioBuffer + FrameIndex * InNumChannels;
		FMemory::Memcpy(RingFrame, SourceFrame, InNumChannels * sizeof(int32));
	}

	UE::BlackmagicMediaAudioBridge::WriteFramePosition += NumFrames;
}

bool FBlackmagicMediaAudioBridge::PullAudioForOutput(const void* ReaderId, int32 NumFrames, int32 NumOutputChannels, TArray<int32>& OutAudioBuffer)
{
	OutAudioBuffer.Reset();

	if (!ReaderId || NumFrames <= 0 || NumOutputChannels <= 0)
	{
		return false;
	}

	FScopeLock Lock(&UE::BlackmagicMediaAudioBridge::CriticalSection);

	if (!UE::BlackmagicMediaAudioBridge::HasValidAudioLocked())
	{
		return false;
	}

	OutAudioBuffer.SetNumZeroed(NumFrames * NumOutputChannels);

	int64& ReaderFramePosition = UE::BlackmagicMediaAudioBridge::ReaderFramePositions.FindOrAdd(ReaderId);
	const int64 OldestFramePosition = FMath::Max<int64>(0, UE::BlackmagicMediaAudioBridge::WriteFramePosition - UE::BlackmagicMediaAudioBridge::CapacityFrames);

	if (ReaderFramePosition <= 0 || ReaderFramePosition < OldestFramePosition)
	{
		ReaderFramePosition = FMath::Max(OldestFramePosition, UE::BlackmagicMediaAudioBridge::WriteFramePosition - NumFrames);
	}

	const int64 AvailableFrames = UE::BlackmagicMediaAudioBridge::WriteFramePosition - ReaderFramePosition;
	const int32 FramesToCopy = static_cast<int32>(FMath::Clamp<int64>(AvailableFrames, 0, NumFrames));
	if (FramesToCopy <= 0)
	{
		return true;
	}

	const int32 ChannelsToCopy = FMath::Min(UE::BlackmagicMediaAudioBridge::NumInputChannels, NumOutputChannels);
	for (int32 FrameIndex = 0; FrameIndex < FramesToCopy; ++FrameIndex)
	{
		const int32 RingFrameIndex = static_cast<int32>((ReaderFramePosition + FrameIndex) % UE::BlackmagicMediaAudioBridge::CapacityFrames);
		const int32* RingFrame = UE::BlackmagicMediaAudioBridge::RingBuffer.GetData() + RingFrameIndex * UE::BlackmagicMediaAudioBridge::NumInputChannels;
		int32* OutputFrame = OutAudioBuffer.GetData() + FrameIndex * NumOutputChannels;
		FMemory::Memcpy(OutputFrame, RingFrame, ChannelsToCopy * sizeof(int32));
	}

	ReaderFramePosition += FramesToCopy;
	return true;
}

void FBlackmagicMediaAudioBridge::UnregisterOutputReader(const void* ReaderId)
{
	if (!ReaderId)
	{
		return;
	}

	FScopeLock Lock(&UE::BlackmagicMediaAudioBridge::CriticalSection);
	UE::BlackmagicMediaAudioBridge::ReaderFramePositions.Remove(ReaderId);
}
