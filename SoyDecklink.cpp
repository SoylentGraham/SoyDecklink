#include "SoyDecklink.h"
#include <SoyTypes.h>	//	Platform::IsOkay
#include <SoyCfString.h>

//	include the API source
#include "DecklinkSdk/Mac/include/DeckLinkAPIDispatch.cpp"


/*
 The DeckLink API can be accessed from a sandboxed applications if the following requirements are met:
 Application is built against Mac OS X 10.7 or later
 Ensure Enable App sandboxing is ticked in your applications Xcode project,
 Ensure you have selected a valid code signing identity,
 Insert the following property into your applications entitlements file: Refer to the Sandboxed Signal Generator target in the SignalGenerator sample application in the SDK.
 
 key
 com.apple.security.temporaryexception.mach-lookup.globalname
 value
 com.blackmagic-design.desktopvideo. DeckLinkHardwareXPCService
 
 Further information can be found in the App Sandbox Design Guide available on Apples Mac Developer Library website
 */


	
namespace Decklink
{
	TContext&					GetContext();
	std::string					GetSerial(IDeckLink& Device,size_t Index);
	
	std::shared_ptr<TContext>	gContext;




}

std::ostream& operator<<(std::ostream &out,const DecklinkString& in)
{
#if defined(TARGET_OSX)
	out << Soy::GetString(in);
#else
	out << in;
#endif
	return out;
}


std::shared_ptr<TMediaExtractor> Decklink::AllocExtractor(const TMediaExtractorParams& Params)
{
	//	let this return null if no match
	try
	{
		std::shared_ptr<TMediaExtractor> Extractor( new TExtractor(Params) );
		return Extractor;
	}
	catch(std::exception& e)
	{
		std::Debug << e.what() << std::endl;
		return nullptr;
	}
}


bool MatchSerial(const std::string& Serial,const std::string& Match)
{
	if ( Match == "*" )
		return true;
	
	if ( Soy::StringContains( Serial, Match, false ) )
		return true;
	
	return false;
}


void Decklink::EnumDevices(std::function<void(const std::string&)> AppendName)
{
	auto Filter = [&](Soy::AutoReleasePtr<IDeckLink>& Input,const std::string& Serial)
	{
		AppendName( Serial );
	};
	
	auto& Context = GetContext();
	Context.EnumDevices( Filter );
}


Decklink::TContext& Decklink::GetContext()
{
	if ( !gContext )
	gContext.reset( new TContext );
	
	return *gContext;
}


Decklink::TContext::TContext()
{
}

Decklink::TContext::~TContext()
{
}



Soy::AutoReleasePtr<IDeckLinkIterator> Decklink::TContext::GetIterator()
{
	//	gr: previously, for windows implementation, we created one at the start of the context
	//		on OSX a second traversal of the same iterator found nothing.
	//		need to create and dispose
	
	Soy::AutoReleasePtr<IDeckLinkIterator> Iterator;
#if defined(TARGET_WINDOWS)
	//	gr: judging from the samples, when this is missing, it's because the drivers are not installed. (verified by me)
	auto Result = CoCreateInstance( CLSID_CDeckLinkIterator, nullptr, CLSCTX_ALL, IID_IDeckLinkIterator, (void**)&mIterator.mObject );
	Platform::IsOkay( Result, "No decklink driver installed.");
#else
	Iterator.Set( CreateDeckLinkIteratorInstance(), true );
#endif
	if ( !Iterator )
		throw Soy::AssertException("Decklink iterator is null");
	
	return Iterator;
}

std::string Decklink::GetSerial(IDeckLink& Device,size_t Index)
{
	//	gr: argh overflow worries
	DecklinkString DisplayName = nullptr;
	DecklinkString ModelName = nullptr;
	
	
	std::stringstream Serial;
	auto Result = Device.GetDisplayName( &DisplayName );
	if ( Result == S_OK && DisplayName )
	{
		Serial << DisplayName;
		return Serial.str();
	}
	
	Result = Device.GetModelName( &ModelName );
	if ( Result == S_OK && ModelName )
	{
		Serial << ModelName;
		return Serial.str();
	}
	
	//	no serial...
	Serial << Index;
	return Serial.str();
}

void Decklink::TContext::EnumDevices(std::function<void(Soy::AutoReleasePtr<IDeckLink>&,const std::string&)> EnumDevice)
{
	auto Iterator = GetIterator();
	Soy::Assert( Iterator!=nullptr, "Iterator expected" );
	
	IDeckLink* CurrentDevice = nullptr;
	size_t CurrentDeviceIndex = 0;
	while ( true )
	{
		try
		{
			auto Result = Iterator->Next( &CurrentDevice );
			if ( CurrentDevice == nullptr )
				break;
			Platform::IsOkay( Result, "Enumerating devices");
			Soy::AutoReleasePtr<IDeckLink> Device( CurrentDevice, false );
			
			//	check it's an input
			Soy::AutoReleasePtr<IDeckLinkInput> DeviceInput;
			Result = Device->QueryInterface( IID_IDeckLinkInput, (void**)&DeviceInput.mObject );
			if ( Result != S_OK )
				continue;
			
			auto Serial = GetSerial( *Device, CurrentDeviceIndex );
			
			EnumDevice( Device, Serial );
		}
		catch(std::exception& e)
		{
			std::Debug << "Error enumerating device; " << e.what() << std::endl;
			break;
		}
	}
	
}

Soy::AutoReleasePtr<IDeckLink> Decklink::TContext::GetDevice(const std::string& MatchSerial)
{
	Soy::AutoReleasePtr<IDeckLink> MatchingDevice;
	
	auto Filter = [&](Soy::AutoReleasePtr<IDeckLink>& Input,const std::string& Serial)
	{
		if ( !::MatchSerial( Serial, MatchSerial ) )
			return;
		
		MatchingDevice = Input;
	};
	
	EnumDevices( Filter );
	
	if ( !MatchingDevice )
	{
		std::stringstream Error;
		Error << "No decklink devices matching " << MatchSerial;
		throw Soy::AssertException( Error.str() );
	}
	
	return MatchingDevice;
}





Decklink::TExtractor::TExtractor(const TMediaExtractorParams& Params) :
	TMediaExtractor			( Params ),
	mRefCount				( 1 )
{
	mDevice = GetContext().GetDevice( Params.mFilename );
	
	StartCapture();
	Start();
}


void Decklink::TExtractor::StartCapture()
{
	Soy::AutoReleasePtr<IDeckLinkInput> DeviceInput;
	auto Result = mDevice->QueryInterface( IID_IDeckLinkInput, (void**)&DeviceInput.mObject );
	if ( Result != S_OK )
		throw Soy::AssertException("Failed to get decklink input");
	
	//	get modes
	Array<IDeckLinkDisplayMode*> modeList;
	{
		Soy::AutoReleasePtr<IDeckLinkDisplayModeIterator> displayModeIterator;
		Result = DeviceInput->GetDisplayModeIterator(&displayModeIterator.mObject);
		if ( Result != S_OK )
			throw Soy::AssertException("Failed to get display mode iterator");
	
		//	needs release?
		IDeckLinkDisplayMode* displayMode = NULL;
		while (displayModeIterator->Next(&displayMode) == S_OK)
			modeList.PushBack(displayMode);
	}
	
	// Enable input video mode detection if the device supports it
	//BMDVideoInputFlags videoInputFlags = supportFormatDetection ? bmdVideoInputEnableFormatDetection : bmdVideoInputFlagDefault;
	BMDVideoInputFlags videoInputFlags = bmdVideoInputFlagDefault;

	// Set the screen preview
	DeviceInput->SetScreenPreviewCallback(this);
	
	// Set capture callback
	DeviceInput->SetCallback(this);
	

	BufferArray<_BMDPixelFormat,2> PixelFormats;
	PixelFormats.PushBack(bmdFormat8BitBGRA);
	PixelFormats.PushBack(bmdFormat8BitYUV);
	
	auto GetCompatibleVideoMode = [&](_BMDPixelFormat Format)
	{
		for ( int i=0;	i<modeList.GetSize();	i++ )
		{
			auto* VideoMode = modeList[i];
			
			// Set the video input mode
			//auto PixelFormat = bmdFormat10BitYUV;
			auto PixelFormat = bmdFormat8BitBGRA;
			Result = DeviceInput->EnableVideoInput(VideoMode->GetDisplayMode(), PixelFormat, videoInputFlags);
			if ( Result != S_OK)
			{
				continue;
				//throw Soy::AssertException("unable to select the video mode. Perhaps device is currently in-use");
			}
			
			return VideoMode;
		}
		throw Soy::AssertException("Failed to get video mode");
	};

	_BMDPixelFormat PixelFormat;
	IDeckLinkDisplayMode* VideoMode = nullptr;
	
	for ( int pm=0;	pm<PixelFormats.GetSize();	pm++ )
	{
		try
		{
			VideoMode = GetCompatibleVideoMode( PixelFormats[pm] );
			PixelFormat = PixelFormats[pm];
			break;
		}
		catch(std::exception& e)
		{
		}
	}
	
	if ( !VideoMode )
		throw Soy::AssertException("Failed to get video mode");
	
	// Start the capture
	Result = DeviceInput->StartStreams();
	if ( Result != S_OK)
		throw Soy::AssertException("Failed to start capture");
}

void Decklink::TExtractor::GetStreams(ArrayBridge<TStreamMeta>&& Streams)
{
	/*
	 TStreamMeta Meta;
	 Meta.mStreamIndex = 0;
	 Meta.mCodec = SoyMediaFormat::RGB;
	 Streams.PushBack( Meta );
	 */
}


std::shared_ptr<TMediaPacket> Decklink::TExtractor::ReadNextPacket()
{
	//	OnPacketExtracted( Packet.mTimecode, Packet.mMeta.mStreamIndex );
	return nullptr;
}


void Decklink::TExtractor::GetMeta(TJsonWriter& Json)
{
	TMediaExtractor::GetMeta( Json );
}


HRESULT Decklink::TExtractor::DrawFrame (/* in */ IDeckLinkVideoFrame *theFrame)
{
	std::Debug << __func__ << std::endl;
	return S_OK;
}



HRESULT Decklink::TExtractor::VideoInputFormatChanged (/* in */ BMDVideoInputFormatChangedEvents notificationEvents, /* in */ IDeckLinkDisplayMode *newDisplayMode, /* in */ BMDDetectedVideoInputFormatFlags detectedSignalFlags)
{
	std::Debug << __func__ << std::endl;
	return S_OK;
}

HRESULT Decklink::TExtractor::VideoInputFrameArrived (/* in */ IDeckLinkVideoInputFrame* videoFrame, /* in */ IDeckLinkAudioInputPacket* audioPacket)
{
	std::Debug << __func__ << std::endl;
	return S_OK;
}


HRESULT Decklink::TExtractor::QueryInterface (REFIID iid, LPVOID *ppv)
{
	CFUUIDBytes		iunknown;
	HRESULT			result = E_NOINTERFACE;
	
	// Initialise the return result
	*ppv = NULL;
	
	// Obtain the IUnknown interface and compare it the provided REFIID
	iunknown = CFUUIDGetUUIDBytes(IUnknownUUID);
	if (memcmp(&iid, &iunknown, sizeof(REFIID)) == 0)
	{
		*ppv = this;
		AddRef();
		result = S_OK;
	}
	else if (memcmp(&iid, &IID_IDeckLinkDeviceNotificationCallback, sizeof(REFIID)) == 0)
	{
		*ppv = (IDeckLinkDeviceNotificationCallback*)this;
		AddRef();
		result = S_OK;
	}
	
	return result;
}

ULONG Decklink::TExtractor::AddRef (void)
{
	return ++mRefCount;
}

ULONG Decklink::TExtractor::Release (void)
{
	auto NewValue = --mRefCount;
	if ( NewValue == 0 )
	{
		delete this;
		return 0;
	}
	
	return NewValue;
}
