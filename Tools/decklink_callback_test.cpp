#include <windows.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <thread>
#include <vector>

#include "DeckLinkAPI_h.h"

namespace
{
	constexpr BMDDisplayMode kDisplayMode = bmdModeHD1080i50;
	constexpr BMDPixelFormat kPixelFormat = bmdFormat10BitYUV;
	constexpr int kWidth = 1920;
	constexpr int kHeight = 1080;
	constexpr int kRowBytesV210 = ((kWidth + 47) / 48) * 128;

	const char* ResultName(BMDOutputFrameCompletionResult Result)
	{
		switch (Result)
		{
		case bmdOutputFrameCompleted:
			return "completed";
		case bmdOutputFrameDisplayedLate:
			return "displayed-late";
		case bmdOutputFrameDropped:
			return "dropped";
		case bmdOutputFrameFlushed:
			return "flushed";
		default:
			return "unknown";
		}
	}

	void PrintHr(const char* Label, HRESULT Hr)
	{
		std::printf("%s: 0x%08lx\n", Label, static_cast<unsigned long>(Hr));
	}

	void PrintBstr(BSTR Text)
	{
		if (!Text)
		{
			std::printf("(null)");
			return;
		}

		const int Required = WideCharToMultiByte(CP_UTF8, 0, Text, -1, nullptr, 0, nullptr, nullptr);
		std::vector<char> Buffer(static_cast<size_t>(Required));
		WideCharToMultiByte(CP_UTF8, 0, Text, -1, Buffer.data(), Required, nullptr, nullptr);
		std::printf("%s", Buffer.data());
	}

	class CompletionCallback final : public IDeckLinkVideoOutputCallback
	{
	public:
		CompletionCallback(IDeckLinkOutput* InOutput, BMDTimeValue InFrameDuration, BMDTimeScale InTimeScale, int InMaxFrames)
			: Output(InOutput)
			, FrameDuration(InFrameDuration)
			, TimeScale(InTimeScale)
			, MaxFrames(InMaxFrames)
		{
			if (Output)
			{
				Output->AddRef();
			}
		}

		~CompletionCallback()
		{
			if (Output)
			{
				Output->Release();
			}
		}

		HRESULT STDMETHODCALLTYPE QueryInterface(REFIID RiiD, void** OutObject) override
		{
			if (!OutObject)
			{
				return E_POINTER;
			}

			if (RiiD == __uuidof(IUnknown) || RiiD == __uuidof(IDeckLinkVideoOutputCallback))
			{
				*OutObject = static_cast<IDeckLinkVideoOutputCallback*>(this);
				AddRef();
				return S_OK;
			}

			*OutObject = nullptr;
			return E_NOINTERFACE;
		}

		ULONG STDMETHODCALLTYPE AddRef() override
		{
			return ++RefCount;
		}

		ULONG STDMETHODCALLTYPE Release() override
		{
			const ULONG NewCount = --RefCount;
			if (NewCount == 0)
			{
				delete this;
			}
			return NewCount;
		}

		HRESULT STDMETHODCALLTYPE ScheduledFrameCompleted(IDeckLinkVideoFrame* CompletedFrame, BMDOutputFrameCompletionResult Result) override
		{
			const int Count = ++CompletedCount;
			if (Result != bmdOutputFrameCompleted)
			{
				++ProblemCount;
			}

			unsigned int Buffered = 0;
			if (Output)
			{
				Output->GetBufferedVideoFrameCount(&Buffered);
			}

			if (Count <= 8 || Count % 25 == 0 || Result != bmdOutputFrameCompleted)
			{
				std::lock_guard<std::mutex> Lock(PrintMutex);
				std::printf("callback %d result=%s buffered=%u\n", Count, ResultName(Result), Buffered);
			}

			const int ScheduledSoFar = ScheduledCount.load();
			if (Output && CompletedFrame && ScheduledSoFar < MaxFrames)
			{
				const int FrameIndex = ScheduledCount.fetch_add(1);
				const BMDTimeValue DisplayTime = static_cast<BMDTimeValue>(FrameIndex) * FrameDuration;
				const HRESULT Hr = Output->ScheduleVideoFrame(CompletedFrame, DisplayTime, FrameDuration, TimeScale);
				if (Hr != S_OK)
				{
					++ProblemCount;
					std::lock_guard<std::mutex> Lock(PrintMutex);
					PrintHr("ScheduleVideoFrame from callback failed", Hr);
				}
			}

			return S_OK;
		}

		HRESULT STDMETHODCALLTYPE ScheduledPlaybackHasStopped() override
		{
			std::lock_guard<std::mutex> Lock(PrintMutex);
			std::printf("playback stopped callback\n");
			return S_OK;
		}

		void MarkScheduled(int Count)
		{
			ScheduledCount.store(Count);
		}

		int GetCompletedCount() const
		{
			return CompletedCount.load();
		}

		int GetProblemCount() const
		{
			return ProblemCount.load();
		}

	private:
		std::atomic<ULONG> RefCount{ 1 };
		IDeckLinkOutput* Output = nullptr;
		BMDTimeValue FrameDuration = 0;
		BMDTimeScale TimeScale = 0;
		int MaxFrames = 0;
		std::atomic<int> ScheduledCount{ 0 };
		std::atomic<int> CompletedCount{ 0 };
		std::atomic<int> ProblemCount{ 0 };
		std::mutex PrintMutex;
	};

	bool FillFrame(IDeckLinkMutableVideoFrame* Frame, int Seed)
	{
		void* Bytes = nullptr;
		if (!Frame || Frame->GetBytes(&Bytes) != S_OK || !Bytes)
		{
			return false;
		}

		unsigned int* Words = static_cast<unsigned int*>(Bytes);
		const int WordCount = (kRowBytesV210 * kHeight) / static_cast<int>(sizeof(unsigned int));
		const unsigned int Luma = 0x040 + static_cast<unsigned int>((Seed * 13) % 0x300);
		const unsigned int Chroma = 0x200;

		for (int Index = 0; Index < WordCount; ++Index)
		{
			Words[Index] = ((Chroma & 0x3ff) << 20) | ((Luma & 0x3ff) << 10) | (Chroma & 0x3ff);
		}

		return true;
	}
}

int main(int Argc, char** Argv)
{
	const int DeviceIndex = Argc > 1 ? std::atoi(Argv[1]) : 1;
	const int MaxFrames = Argc > 2 ? std::atoi(Argv[2]) : 150;

	std::printf("DeckLink callback test: device=%d mode=1080i50 pixel=v210 maxFrames=%d\n", DeviceIndex, MaxFrames);

	HRESULT Hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
	if (FAILED(Hr))
	{
		PrintHr("CoInitializeEx failed", Hr);
		return 1;
	}

	IDeckLinkIterator* Iterator = nullptr;
	Hr = CoCreateInstance(__uuidof(CDeckLinkIterator), nullptr, CLSCTX_ALL, __uuidof(IDeckLinkIterator), reinterpret_cast<void**>(&Iterator));
	if (FAILED(Hr) || !Iterator)
	{
		PrintHr("CoCreateInstance(CDeckLinkIterator) failed", Hr);
		CoUninitialize();
		return 1;
	}

	IDeckLink* SelectedDeckLink = nullptr;
	for (int Index = 1;; ++Index)
	{
		IDeckLink* DeckLink = nullptr;
		Hr = Iterator->Next(&DeckLink);
		if (Hr != S_OK || !DeckLink)
		{
			break;
		}

		BSTR DisplayName = nullptr;
		DeckLink->GetDisplayName(&DisplayName);
		std::printf("device %d: ", Index);
		PrintBstr(DisplayName);
		std::printf("\n");
		SysFreeString(DisplayName);

		if (Index == DeviceIndex)
		{
			SelectedDeckLink = DeckLink;
		}
		else
		{
			DeckLink->Release();
		}
	}

	Iterator->Release();

	if (!SelectedDeckLink)
	{
		std::printf("Device %d not found.\n", DeviceIndex);
		CoUninitialize();
		return 1;
	}

	IDeckLinkOutput* Output = nullptr;
	Hr = SelectedDeckLink->QueryInterface(__uuidof(IDeckLinkOutput), reinterpret_cast<void**>(&Output));
	SelectedDeckLink->Release();
	if (FAILED(Hr) || !Output)
	{
		PrintHr("QueryInterface(IDeckLinkOutput) failed", Hr);
		CoUninitialize();
		return 1;
	}

	BMDDisplayMode ActualMode = bmdModeUnknown;
	BOOL Supported = FALSE;
	Hr = Output->DoesSupportVideoMode(bmdVideoConnectionSDI, kDisplayMode, kPixelFormat, bmdNoVideoOutputConversion, bmdSupportedVideoModeDefault, &ActualMode, &Supported);
	PrintHr("DoesSupportVideoMode", Hr);
	std::printf("supported=%d actualMode=0x%08x\n", static_cast<int>(Supported), static_cast<unsigned int>(ActualMode));
	if (Hr != S_OK || !Supported)
	{
		Output->Release();
		CoUninitialize();
		return 1;
	}

	IDeckLinkDisplayMode* DisplayMode = nullptr;
	Hr = Output->GetDisplayMode(kDisplayMode, &DisplayMode);
	if (Hr != S_OK || !DisplayMode)
	{
		PrintHr("GetDisplayMode failed", Hr);
		Output->Release();
		CoUninitialize();
		return 1;
	}

	BMDTimeValue FrameDuration = 0;
	BMDTimeScale TimeScale = 0;
	DisplayMode->GetFrameRate(&FrameDuration, &TimeScale);
	std::printf("frameDuration=%lld timeScale=%lld fps=%.3f\n", static_cast<long long>(FrameDuration), static_cast<long long>(TimeScale), static_cast<double>(TimeScale) / static_cast<double>(FrameDuration));
	DisplayMode->Release();

	Hr = Output->EnableVideoOutput(kDisplayMode, bmdVideoOutputFlagDefault);
	if (Hr != S_OK)
	{
		PrintHr("EnableVideoOutput failed", Hr);
		Output->Release();
		CoUninitialize();
		return 1;
	}

	constexpr int InitialFrames = 4;
	std::vector<IDeckLinkMutableVideoFrame*> Frames;
	Frames.reserve(InitialFrames);
	for (int Index = 0; Index < InitialFrames; ++Index)
	{
		IDeckLinkMutableVideoFrame* Frame = nullptr;
		Hr = Output->CreateVideoFrame(kWidth, kHeight, kRowBytesV210, kPixelFormat, bmdFrameFlagDefault, &Frame);
		if (Hr != S_OK || !Frame)
		{
			PrintHr("CreateVideoFrame failed", Hr);
			Output->DisableVideoOutput();
			Output->Release();
			CoUninitialize();
			return 1;
		}

		FillFrame(Frame, Index);
		Frames.push_back(Frame);
	}

	CompletionCallback* Callback = new CompletionCallback(Output, FrameDuration, TimeScale, MaxFrames);
	Hr = Output->SetScheduledFrameCompletionCallback(Callback);
	if (Hr != S_OK)
	{
		PrintHr("SetScheduledFrameCompletionCallback failed", Hr);
		Callback->Release();
		for (IDeckLinkMutableVideoFrame* Frame : Frames)
		{
			Frame->Release();
		}
		Output->DisableVideoOutput();
		Output->Release();
		CoUninitialize();
		return 1;
	}

	for (int Index = 0; Index < InitialFrames; ++Index)
	{
		Hr = Output->ScheduleVideoFrame(Frames[Index], static_cast<BMDTimeValue>(Index) * FrameDuration, FrameDuration, TimeScale);
		if (Hr != S_OK)
		{
			PrintHr("initial ScheduleVideoFrame failed", Hr);
		}
	}
	Callback->MarkScheduled(InitialFrames);

	Hr = Output->StartScheduledPlayback(0, TimeScale, 1.0);
	if (Hr != S_OK)
	{
		PrintHr("StartScheduledPlayback failed", Hr);
	}

	const auto Start = std::chrono::steady_clock::now();
	int LastCompleted = -1;
	while (std::chrono::steady_clock::now() - Start < std::chrono::seconds(6))
	{
		const int Completed = Callback->GetCompletedCount();
		if (Completed >= MaxFrames)
		{
			break;
		}
		if (Completed != LastCompleted)
		{
			LastCompleted = Completed;
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(100));
	}

	BMDTimeValue ActualStop = 0;
	Output->StopScheduledPlayback(0, &ActualStop, TimeScale);
	Output->SetScheduledFrameCompletionCallback(nullptr);

	unsigned int Buffered = 0;
	Output->GetBufferedVideoFrameCount(&Buffered);
	std::printf("summary completed=%d problems=%d buffered=%u actualStop=%lld\n", Callback->GetCompletedCount(), Callback->GetProblemCount(), Buffered, static_cast<long long>(ActualStop));

	Callback->Release();
	for (IDeckLinkMutableVideoFrame* Frame : Frames)
	{
		Frame->Release();
	}

	Output->DisableVideoOutput();
	Output->Release();
	CoUninitialize();
	return 0;
}
