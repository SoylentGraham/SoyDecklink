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
	auto Filter = [&](Soy::AutoReleasePtr<IDeckLinkInput>& Input,const std::string& Serial)
	{
		//if ( !MatchFilter( Serial, ))
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

void Decklink::TContext::EnumDevices(std::function<void(Soy::AutoReleasePtr<IDeckLinkInput>&,const std::string&)> EnumDevice)
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
			
			EnumDevice( DeviceInput, Serial );
		}
		catch(std::exception& e)
		{
			std::Debug << "Error enumerating device; " << e.what() << std::endl;
			break;
		}
	}
	
}

Soy::AutoReleasePtr<IDeckLinkInput> Decklink::TContext::GetDevice(const std::string& MatchSerial)
{
	Array<Soy::AutoReleasePtr<IDeckLinkInput>> MatchingDevices;
	
	auto Filter = [&](Soy::AutoReleasePtr<IDeckLinkInput>& Input,const std::string& Serial)
	{
		if ( !::MatchSerial( Serial, MatchSerial ) )
			return;
		
		MatchingDevices.PushBack( Input );
	};
	
	EnumDevices( Filter );
	
	if ( MatchingDevices.IsEmpty() )
	{
		std::stringstream Error;
		Error << "No decklink devices matching " << MatchSerial;
		throw Soy::AssertException( Error.str() );
	}
	
	return MatchingDevices[0];
}





Decklink::TExtractor::TExtractor(const TMediaExtractorParams& Params) :
	TMediaExtractor			( Params )
{
	mInput = GetContext().GetDevice( Params.mFilename );
	Start();
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

