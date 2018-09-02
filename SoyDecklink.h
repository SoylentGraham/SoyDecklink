#pragma once

#include <SoyMedia.h>
#include <SoyAutoReleasePtr.h>

#if defined(TARGET_WINDOWS)

#include "DecklinkSdk\Windows\Include\DeckLinkAPI.h"
typedef BSTR DecklinkString;

#elif defined(TARGET_OSX)

#include "DecklinkSdk/Mac/include/DeckLinkAPI.h"
typedef CFStringRef DecklinkString;

#endif





namespace Decklink
{
	class TContext;
	class TExtractor;
	
	
	void								EnumDevices(std::function<void(const std::string&)> AppendName);
	std::shared_ptr<TMediaExtractor>	AllocExtractor(const TMediaExtractorParams& Params);
}



class Decklink::TContext
{
public:
	TContext();
	~TContext();
	
	Soy::AutoReleasePtr<IDeckLink>			GetDevice(const std::string& MatchSerial);
	void									EnumDevices(std::function<void(Soy::AutoReleasePtr<IDeckLink>&,const std::string&)> EnumDevice);
	
public:
	Soy::AutoReleasePtr<IDeckLinkIterator>	GetIterator();
};



//	based on Example CapturePreview
class Decklink::TExtractor : public TMediaExtractor, public IDeckLinkInputCallback, public IDeckLinkScreenPreviewCallback
{
public:
	TExtractor(const TMediaExtractorParams& Params);
	
	virtual void									GetStreams(ArrayBridge<TStreamMeta>&& Streams) override;
	virtual std::shared_ptr<Platform::TMediaFormat>	GetStreamFormat(size_t StreamIndex) override		{	return nullptr;	}
	virtual void									GetMeta(TJsonWriter& Json) override;
	
protected:
	//	IDeckLinkInputCallback
	virtual HRESULT VideoInputFormatChanged (/* in */ BMDVideoInputFormatChangedEvents notificationEvents, /* in */ IDeckLinkDisplayMode *newDisplayMode, /* in */ BMDDetectedVideoInputFormatFlags detectedSignalFlags) override;
	virtual HRESULT VideoInputFrameArrived (/* in */ IDeckLinkVideoInputFrame* videoFrame, /* in */ IDeckLinkAudioInputPacket* audioPacket) override;

	//	IDeckLinkScreenPreviewCallback
	virtual HRESULT DrawFrame (/* in */ IDeckLinkVideoFrame *theFrame) override;

	// IUnknown needs only a dummy implementation
	virtual HRESULT             QueryInterface (REFIID iid, LPVOID *ppv) override;
	virtual ULONG               AddRef() override;
	virtual ULONG               Release() override;
	
	std::atomic_int				mRefCount;
	
protected:
	void											StartCapture();
	virtual std::shared_ptr<TMediaPacket>			ReadNextPacket() override;
	
public:
	Soy::AutoReleasePtr<IDeckLink>					mDevice;
};

