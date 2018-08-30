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
	
	Soy::AutoReleasePtr<IDeckLinkInput>		GetDevice(const std::string& MatchSerial);
	void									EnumDevices(std::function<void(Soy::AutoReleasePtr<IDeckLinkInput>&,const std::string&)> EnumDevice);
	
public:
	Soy::AutoReleasePtr<IDeckLinkIterator>	GetIterator();
};




class Decklink::TExtractor : public TMediaExtractor
{
public:
	TExtractor(const TMediaExtractorParams& Params);
	
	virtual void									GetStreams(ArrayBridge<TStreamMeta>&& Streams) override;
	virtual std::shared_ptr<Platform::TMediaFormat>	GetStreamFormat(size_t StreamIndex) override		{	return nullptr;	}
	virtual void									GetMeta(TJsonWriter& Json) override;
	
protected:
	virtual std::shared_ptr<TMediaPacket>			ReadNextPacket() override;
	
public:
	Soy::AutoReleasePtr<IDeckLinkInput>				mInput;
};

