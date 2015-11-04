/*	Copyright © 2007 Apple Inc. All Rights Reserved.
	
	Disclaimer: IMPORTANT:  This Apple software is supplied to you by 
			Apple Inc. ("Apple") in consideration of your agreement to the
			following terms, and your use, installation, modification or
			redistribution of this Apple software constitutes acceptance of these
			terms.  If you do not agree with these terms, please do not use,
			install, modify or redistribute this Apple software.
			
			In consideration of your agreement to abide by the following terms, and
			subject to these terms, Apple grants you a personal, non-exclusive
			license, under Apple's copyrights in this original Apple software (the
			"Apple Software"), to use, reproduce, modify and redistribute the Apple
			Software, with or without modifications, in source and/or binary forms;
			provided that if you redistribute the Apple Software in its entirety and
			without modifications, you must retain this notice and the following
			text and disclaimers in all such redistributions of the Apple Software. 
			Neither the name, trademarks, service marks or logos of Apple Inc. 
			may be used to endorse or promote products derived from the Apple
			Software without specific prior written permission from Apple.  Except
			as expressly stated in this notice, no other rights or licenses, express
			or implied, are granted by Apple herein, including but not limited to
			any patent rights that may be infringed by your derivative works or by
			other works in which the Apple Software may be incorporated.
			
			The Apple Software is provided by Apple on an "AS IS" basis.  APPLE
			MAKES NO WARRANTIES, EXPRESS OR IMPLIED, INCLUDING WITHOUT LIMITATION
			THE IMPLIED WARRANTIES OF NON-INFRINGEMENT, MERCHANTABILITY AND FITNESS
			FOR A PARTICULAR PURPOSE, REGARDING THE APPLE SOFTWARE OR ITS USE AND
			OPERATION ALONE OR IN COMBINATION WITH YOUR PRODUCTS.
			
			IN NO EVENT SHALL APPLE BE LIABLE FOR ANY SPECIAL, INDIRECT, INCIDENTAL
			OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
			SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
			INTERRUPTION) ARISING IN ANY WAY OUT OF THE USE, REPRODUCTION,
			MODIFICATION AND/OR DISTRIBUTION OF THE APPLE SOFTWARE, HOWEVER CAUSED
			AND WHETHER UNDER THEORY OF CONTRACT, TORT (INCLUDING NEGLIGENCE),
			STRICT LIABILITY OR OTHERWISE, EVEN IF APPLE HAS BEEN ADVISED OF THE
			POSSIBILITY OF SUCH DAMAGE.
*/
/*=============================================================================
	ACFLACDecoder.cpp

=============================================================================*/

//=============================================================================
//	Includes
//=============================================================================

#include "ACFLACDecoder.h"
#include "ACCodecDispatch.h"
#include "CAStreamBasicDescription.h"
#include "CASampleTools.h"
#include "CADebugMacros.h"
#include "CABundleLocker.h"

#if TARGET_OS_WIN32
	#include "CAWin32StringResources.h"
	#include <AudioFormat.h>
#else
	#include <AudioToolbox/AudioFormat.h>
#endif

#include "ACCompatibility.h"

#define RequireNoErr(err, action)   if ((err) != noErr) { action }
#define RequireAction(condition, action)			if (!(condition)) { action }
#define kAudioFormatCDLinearPCM (kLinearPCMFormatFlagIsSignedInteger | kAudioFormatFlagsNativeEndian | kAudioFormatFlagIsPacked)

static unsigned num_expected_;

#define VERBOSE 0 // It is often useful only enable some of what this enables

static FLAC__bool die_(const char *msg)
{
#if VERBOSE
	printf("ERROR: %s\n", msg);
#endif
	return false;
}

//=============================================================================
//	ACFLACDecoder
//=============================================================================
// Static variable

Byte * ACFLACDecoder::mOutputBufferPtr = NULL;
Byte * ACFLACDecoder::mInputBufferPtr = NULL;
UInt32 ACFLACDecoder::mInputBufferBytesUsed = 0;
UInt32 ACFLACDecoder::mFramesDecoded = 0;
UInt32 ACFLACDecoder::mInputBufferBytesRead = 0;

ACFLACDecoder::ACFLACDecoder(OSType theSubType)
:
	ACFLACCodec(kInputBufferPackets * kMaxChannels * sizeof(SInt32), theSubType)
{
	//	This decoder only takes an FLAC stream as it's input
	CAStreamBasicDescription theInputFormat1(kAudioStreamAnyRate, kAudioFormatFLAC, 0, kFramesPerPacket, 0, 0, 0, kFLACFormatFlag_16BitSourceData);
	AddInputFormat(theInputFormat1);

	CAStreamBasicDescription theInputFormat2(kAudioStreamAnyRate, kAudioFormatFLAC, 0, kFramesPerPacket, 0, 0, 0, kFLACFormatFlag_24BitSourceData);
	AddInputFormat(theInputFormat2);

	CAStreamBasicDescription theInputFormat3(kAudioStreamAnyRate, kAudioFormatFLAC, 0, kFramesPerPacket >> 2, 0, 0, 0, kFLACFormatFlag_16BitSourceData);
	AddInputFormat(theInputFormat3);

	CAStreamBasicDescription theInputFormat4(kAudioStreamAnyRate, kAudioFormatFLAC, 0, kFramesPerPacket >> 2, 0, 0, 0, kFLACFormatFlag_24BitSourceData);
	AddInputFormat(theInputFormat4);

	//	set our initial input format to stereo FLAC at a 44100 sample rate
	mInputFormat.mSampleRate = 44100;
	mInputFormat.mFormatFlags = kFLACFormatFlag_16BitSourceData; // the source will decode to a bit depth of 16 -- if it's not correct we'll default to 32 bits
	mInputFormat.mBytesPerPacket = 0;
	mInputFormat.mFormatID = 'flac' /* kAudioFormatFLAC */;
	mInputFormat.mFramesPerPacket = kFramesPerPacket;
	mInputFormat.mBytesPerFrame = 0;
	mInputFormat.mChannelsPerFrame = 2;
	mInputFormat.mBitsPerChannel = 0;
	
	//	This decoder produces 16 or 32 bit native endian signed integer as it's output,
	//	but can handle any sample rate and any number of channels
	CAStreamBasicDescription theOutputFormat1(kAudioStreamAnyRate, kAudioFormatLinearPCM, 0, 1, 0, 0, 16, kAudioFormatFlagsNativeEndian | kAudioFormatFlagIsSignedInteger | kAudioFormatFlagIsPacked);
	AddOutputFormat(theOutputFormat1);

	// For 32 bit ints we say we're aligned high, but it doesn't really matter
	//CAStreamBasicDescription theOutputFormat2(kAudioStreamAnyRate, kAudioFormatLinearPCM, 0, 1, 0, 0, 32, kAudioFormatFlagsNativeEndian | kAudioFormatFlagIsSignedInteger | kAudioFormatFlagIsPacked);
	//AddOutputFormat(theOutputFormat2);

	// For 24 and 20 bit ints we say we're aligned high as it has to be packed into 24 bits
	CAStreamBasicDescription theOutputFormat3(kAudioStreamAnyRate, kAudioFormatLinearPCM, 0, 1, 0, 0, 24, kAudioFormatFlagsNativeEndian | kAudioFormatFlagIsSignedInteger | kAudioFormatFlagIsPacked);
	AddOutputFormat(theOutputFormat3);

	//CAStreamBasicDescription theOutputFormat4(kAudioStreamAnyRate, kAudioFormatLinearPCM, 0, 1, 0, 0, 20, kAudioFormatFlagsNativeEndian | kAudioFormatFlagIsSignedInteger | kAudioFormatFlagIsAlignedHigh);
	//AddOutputFormat(theOutputFormat4);

	//	set our intial output format to stereo 16 bit native endian signed integers at a 44100 sample rate
	mOutputFormat.mSampleRate = 44100;
	mOutputFormat.mFormatID = kAudioFormatLinearPCM;
	mOutputFormat.mFormatFlags = (kLinearPCMFormatFlagIsSignedInteger | kAudioFormatFlagsNativeEndian | kAudioFormatFlagIsPacked);
	mOutputFormat.mBytesPerPacket = 4;
	mOutputFormat.mFramesPerPacket = 1;
	mOutputFormat.mBytesPerFrame = 4;
	mOutputFormat.mChannelsPerFrame = 2;
	mOutputFormat.mBitsPerChannel = 16;

	mInputBufferBytesUsed = 0;
	mPacketInInputBuffer = false;
	mDecoder = FLAC__stream_decoder_new();
	mDecoderState = FLAC__stream_decoder_get_state(mDecoder);
}

ACFLACDecoder::~ACFLACDecoder()
{
	if (mDecoder != NULL)
	{
		FLAC__stream_decoder_delete(mDecoder);
	}
}

void	ACFLACDecoder::GetPropertyInfo(AudioCodecPropertyID inPropertyID, UInt32& outPropertyDataSize, Boolean& outWritable)
{
	switch(inPropertyID)
	{
		case kAudioCodecPropertyFormatList:
			outPropertyDataSize = sizeof(AudioFormatListItem);
			outWritable = false;
			break;

		default:
			ACFLACCodec::GetPropertyInfo(inPropertyID, outPropertyDataSize, outWritable);
			break;
			
	}
}

void	ACFLACDecoder::GetProperty(AudioCodecPropertyID inPropertyID, UInt32& ioPropertyDataSize, void* outPropertyData)
{	
	switch(inPropertyID)
	{
		case kAudioCodecPropertyNameCFString:
		{
			if (ioPropertyDataSize != sizeof(CFStringRef)) CODEC_THROW(kAudioCodecBadPropertySizeError);
			
			CABundleLocker lock;
			CFStringRef name = CFCopyLocalizedStringFromTableInBundle(CFSTR("FLAC decoder"), CFSTR("CodecNames"), GetCodecBundle(), CFSTR(""));
			*(CFStringRef*)outPropertyData = name;
			break; 
		}
		
		case kAudioCodecPropertyMaximumPacketByteSize:
			if(ioPropertyDataSize == sizeof(UInt32))
			{
			#if VERBOSE	
				printf("Max packet size == %lu\n", kInputBufferPackets * mInputFormat.mChannelsPerFrame * ((mOutputFormat.mBitsPerChannel) >> 3) + kMaxEscapeHeaderBytes);
			#endif
				*reinterpret_cast<UInt32*>(outPropertyData) = kInputBufferPackets * mInputFormat.mChannelsPerFrame * ((mOutputFormat.mBitsPerChannel) >> 3) + kMaxEscapeHeaderBytes;
			}
			else
			{
				CODEC_THROW(kAudioCodecBadPropertySizeError);
			}
			break;
			
		case kAudioCodecOutputFormatsForInputFormat:
			if(ioPropertyDataSize >= sizeof(AudioStreamBasicDescription))
			{
				// There is no argument here if the cookie is set....
				UInt32 bitDepth = mStreamInfo.bits_per_sample;
			#if VERBOSE
				printf("kAudioCodecOutputFormatsForInputFormat mCookieSet == %lu, bitDepth == %lu\n", mCookieSet, bitDepth);
			#endif
				if (mCookieSet == 0) // the cookie has not been set
				{
					// First check the flag of the input format we're being told we're getting
					switch ( ( ( (AudioStreamBasicDescription*)(outPropertyData) )[0].mFormatFlags) & 0x00000007)
					{
						case kFLACFormatFlag_16BitSourceData:
							bitDepth = 16;
							break;
						case kFLACFormatFlag_20BitSourceData:
							bitDepth = 20;
							break;
						case kFLACFormatFlag_24BitSourceData:
							bitDepth = 24;
							break;						
						case kFLACFormatFlag_32BitSourceData:
							bitDepth = 32;
							break;
						default: // Check the currently set input format
							if ((mInputFormat.mFormatFlags & 0x00000007) == kFLACFormatFlag_16BitSourceData)
							{
								bitDepth = 16;
							}
							else if ((mInputFormat.mFormatFlags & 0x00000007) == kFLACFormatFlag_20BitSourceData)
							{
								bitDepth = 20; // we're putting 20 bits into 24 aligned high
							}
							else if ((mInputFormat.mFormatFlags & 0x00000007) == kFLACFormatFlag_24BitSourceData)
							{
								bitDepth = 24;
							}
							else if ((mInputFormat.mFormatFlags & 0x00000007) == kFLACFormatFlag_32BitSourceData)
							{
								bitDepth = 32;
							}
							else
							{
							#if VERBOSE
								printf("We don't know the bit depth of the source\n");
							#endif
								CODEC_THROW(kAudioCodecUnsupportedFormatError); // we don't know and it's dangerous to guess
							}
							break;
					}
				}
				if (bitDepth == 20)
				{
					AudioStreamBasicDescription theOutputFormat = { kAudioStreamAnyRate, kAudioFormatLinearPCM, 
						kAudioFormatFlagsNativeEndian | kAudioFormatFlagIsSignedInteger  | kAudioFormatFlagIsAlignedHigh,
						0, 1, 0, 0, bitDepth, 0 };
					ioPropertyDataSize = sizeof(AudioStreamBasicDescription);
					memcpy(outPropertyData, &theOutputFormat, ioPropertyDataSize);
				}
				else
				{
					AudioStreamBasicDescription theOutputFormat = { kAudioStreamAnyRate, kAudioFormatLinearPCM, 
						kAudioFormatFlagsNativeEndian | kAudioFormatFlagIsSignedInteger  | kAudioFormatFlagIsPacked,
						0, 1, 0, 0, bitDepth, 0 };
					ioPropertyDataSize = sizeof(AudioStreamBasicDescription);
					memcpy(outPropertyData, &theOutputFormat, ioPropertyDataSize);
				}
			}
			break;
			
		case kAudioCodecPropertyFormatList:
			if(ioPropertyDataSize >= sizeof(AudioFormatListItem))
			{
				AudioFormatListItem theInputFormat;
				FLAC__StreamMetadata_StreamInfo theConfig;

				theInputFormat.mASBD.mFormatID = mCodecSubType;
				theInputFormat.mASBD.mFormatFlags = 0;
				theInputFormat.mASBD.mBytesPerPacket = 0;
				theInputFormat.mASBD.mBytesPerFrame = 0;
				theInputFormat.mASBD.mBitsPerChannel = 0;
				theInputFormat.mASBD.mReserved = 0;
				
				AudioCodecMagicCookieInfo tempCookieInfo = *(AudioCodecMagicCookieInfo *)outPropertyData;
				
				if (tempCookieInfo.mMagicCookieSize > 0) // we have a cookie
				{
					ParseMagicCookie(tempCookieInfo.mMagicCookie, tempCookieInfo.mMagicCookieSize, &theConfig);
					theInputFormat.mASBD.mSampleRate = (Float64)theConfig.sample_rate;
					theInputFormat.mASBD.mFramesPerPacket = theConfig.max_blocksize;
					theInputFormat.mASBD.mChannelsPerFrame = theConfig.channels;
					theInputFormat.mChannelLayoutTag = sChannelLayoutTags[theInputFormat.mASBD.mChannelsPerFrame - 1];
					switch (theConfig.bits_per_sample)
					{
						case 16:
							theInputFormat.mASBD.mFormatFlags = kFLACFormatFlag_16BitSourceData;
							break;
						case 20:
							theInputFormat.mASBD.mFormatFlags = kFLACFormatFlag_20BitSourceData;
							break;
						case 24:
							theInputFormat.mASBD.mFormatFlags = kFLACFormatFlag_24BitSourceData;
							break;
						case 32:
							theInputFormat.mASBD.mFormatFlags = kFLACFormatFlag_32BitSourceData;
							break;
						default: // we don't support this
							theInputFormat.mASBD.mFormatFlags = 0;
							break;						
					}
				}
				else // we don't -- so we have no idea
				{
					theInputFormat.mASBD.mSampleRate = 0;
					theInputFormat.mASBD.mFramesPerPacket = 0;
					theInputFormat.mASBD.mChannelsPerFrame = 0;
					theInputFormat.mChannelLayoutTag = kAudioChannelLayoutTag_Unknown;
				}
				ioPropertyDataSize = sizeof(AudioFormatListItem);
				memcpy(outPropertyData, &theInputFormat, ioPropertyDataSize);
			}
			else
			{
				CODEC_THROW(kAudioCodecBadPropertySizeError);
			}
			break;
			
		default:
			ACFLACCodec::GetProperty(inPropertyID, ioPropertyDataSize, outPropertyData);
	}
}

void ACFLACDecoder::SetProperty(AudioCodecPropertyID inPropertyID, UInt32 inPropertyDataSize, const void* inPropertyData)
{
	if(mIsInitialized)
	{
		CODEC_THROW(kAudioCodecIllegalOperationError);
	}
	switch(inPropertyID)
	{
		case kAudioCodecPropertyFormatList:
			CODEC_THROW(kAudioCodecIllegalOperationError);
			break;
		default:
            ACFLACCodec::SetProperty(inPropertyID, inPropertyDataSize, inPropertyData);
            break;            
    }
}

void	ACFLACDecoder::SetCurrentInputFormat(const AudioStreamBasicDescription& inInputFormat)
{
	UInt32 bitDepthFlag = 0;
	
	if(!mIsInitialized)
	{
		//	check to make sure the input format is legal
		if(!(inInputFormat.mFormatID == 'flac' /* kAudioFormatFLAC */))
		{
#if VERBOSE
			DebugMessage("ACFLACDecoder::SetCurrentInputFormat: only support FLAC for output");
#endif
			CODEC_THROW(kAudioCodecUnsupportedFormatError);
		}
		
		//	tell our base class about the new format
		ACFLACCodec::SetCurrentInputFormat(inInputFormat);
		// Now, since lossless requires number of channels and sample rates to match between
		// input and output formats, set the output ones here in case the output format is left to default
		if (inInputFormat.mChannelsPerFrame == 0)
		{
			mInputFormat.mChannelsPerFrame = mOutputFormat.mChannelsPerFrame;
		}
		else
		{
			mOutputFormat.mChannelsPerFrame = mInputFormat.mChannelsPerFrame;
		}
		if (inInputFormat.mSampleRate == 0.0)
		{
			mInputFormat.mSampleRate = mOutputFormat.mSampleRate;
		}
		else
		{
			mOutputFormat.mSampleRate = mInputFormat.mSampleRate;
		}
		bitDepthFlag = mInputFormat.mFormatFlags & 0x00000007;
	#if VERBOSE
		printf("mInputFormat.mFormatFlags == %lu\n", mInputFormat.mFormatFlags);
	#endif
		switch (bitDepthFlag)
		{
			case kFLACFormatFlag_16BitSourceData:
				mOutputFormat.mBitsPerChannel = 16;
				break;
			case kFLACFormatFlag_20BitSourceData:
				mOutputFormat.mBitsPerChannel = 20; // 20 bits high aligned
				mOutputFormat.mFormatFlags = (kLinearPCMFormatFlagIsSignedInteger | kAudioFormatFlagsNativeEndian | kAudioFormatFlagIsAlignedHigh);
				break;
			case kFLACFormatFlag_24BitSourceData:
				mOutputFormat.mBitsPerChannel = 24;
				break;
			case kFLACFormatFlag_32BitSourceData:
				mOutputFormat.mBitsPerChannel = 32;
				break;
			default: // do nothing -- it's dangerous to guess
				break;
		}
		// Zero out everything that has to be zero
		mInputFormat.mBytesPerFrame = 0;
		mInputFormat.mBitsPerChannel = 0;
		mInputFormat.mBytesPerPacket = 0;
		mInputFormat.mReserved = 0;
	}
	else
	{
		CODEC_THROW(kAudioCodecStateError);
	}
}

void	ACFLACDecoder::SetCurrentOutputFormat(const AudioStreamBasicDescription& inOutputFormat)
{
	if(!mIsInitialized)
	{
		//	check to make sure the output format is legal
		if(	(inOutputFormat.mFormatID != kAudioFormatLinearPCM) /*||
			!( ( (inOutputFormat.mFormatFlags == (kLinearPCMFormatFlagIsSignedInteger | kAudioFormatFlagsNativeEndian | kAudioFormatFlagIsPacked) ) &&
			     (inOutputFormat.mBitsPerChannel == 16 || inOutputFormat.mBitsPerChannel == 24 || inOutputFormat.mBitsPerChannel == 32) ) ||
				 ( (inOutputFormat.mFormatFlags == (kLinearPCMFormatFlagIsSignedInteger | kAudioFormatFlagsNativeEndian | kAudioFormatFlagIsAlignedHigh) ) &&
			     ( inOutputFormat.mBitsPerChannel == 20) ) )*/ )
		{
#if VERBOSE
			DebugMessage("ACFLACDecoder::SetCurrentOutputFormat: only supports either 16 bit native endian signed integer or 32 bit native endian Core Audio floats for output");
#endif
			CODEC_THROW(kAudioCodecUnsupportedFormatError);
		}
		
		//	tell our base class about the new format
		ACFLACCodec::SetCurrentOutputFormat(inOutputFormat);
		if (mOutputFormat.mSampleRate == 0.0)
		{
			mOutputFormat.mSampleRate = mInputFormat.mSampleRate;
		}
		if (mOutputFormat.mChannelsPerFrame == 0)
		{
			mOutputFormat.mChannelsPerFrame = mInputFormat.mChannelsPerFrame;
		}		
		// Zero out everything that has to be zero
		mOutputFormat.mReserved = 0;
	}
	else
	{
		CODEC_THROW(kAudioCodecStateError);
	}
}

void	ACFLACDecoder::Initialize(const AudioStreamBasicDescription* inInputFormat, const AudioStreamBasicDescription* inOutputFormat, const void* inMagicCookie, UInt32 inMagicCookieByteSize)
{	
	if(!mIsInitialized)
	{
		// Do this here as it might be setting the magic cookie
		ACFLACCodec::Initialize(inInputFormat, inOutputFormat, inMagicCookie, inMagicCookieByteSize);
	#if VERBOSE
		printf("mOutputFormat.mBitsPerChannel == %lu, mInputFormat.mBitsPerChannel == %lu, mStreamInfo.bits_per_sample == %u\n", mOutputFormat.mBitsPerChannel, mInputFormat.mBitsPerChannel, mStreamInfo.bits_per_sample);
	#endif
	
		// We might only get a cookie! We have to make sure the formats are in sync
		if (mCookieSet == 1 && inInputFormat == NULL && inOutputFormat == NULL)
		{
			mOutputFormat.mSampleRate = mInputFormat.mSampleRate = (Float64)mStreamInfo.sample_rate;
			mOutputFormat.mChannelsPerFrame = mInputFormat.mChannelsPerFrame = mStreamInfo.channels;
			mInputFormat.mFormatFlags &= 0xfffffff8;
			switch (mStreamInfo.bits_per_sample)
			{
				case 16:
					mInputFormat.mFormatFlags |= kFLACFormatFlag_16BitSourceData;
					mOutputFormat.mFormatFlags = kAudioFormatFlagsNativeEndian | kAudioFormatFlagIsSignedInteger | kAudioFormatFlagIsPacked;
					break;
				case 20:
					mInputFormat.mFormatFlags |= kFLACFormatFlag_20BitSourceData;
					mOutputFormat.mFormatFlags = kAudioFormatFlagsNativeEndian | kAudioFormatFlagIsSignedInteger | kAudioFormatFlagIsAlignedHigh;
					break;
				case 24:
					mInputFormat.mFormatFlags |= kFLACFormatFlag_24BitSourceData;
					mOutputFormat.mFormatFlags = kAudioFormatFlagsNativeEndian | kAudioFormatFlagIsSignedInteger | kAudioFormatFlagIsPacked;
					break;
				case 32:
					mInputFormat.mFormatFlags |= kFLACFormatFlag_32BitSourceData;
					mOutputFormat.mFormatFlags = kAudioFormatFlagsNativeEndian | kAudioFormatFlagIsSignedInteger | kAudioFormatFlagIsPacked;
					break;
			}
			mOutputFormat.mBitsPerChannel = mStreamInfo.bits_per_sample;
			mInputFormat.mFramesPerPacket = mStreamInfo.max_blocksize;
		}

		// Set the callbacks
		// Initialize the decoder
		if(FLAC__stream_decoder_init_stream(mDecoder,
											stream_decoder_read_callback,
											NULL,
											NULL,
											NULL,
											NULL,
											stream_decoder_write_callback,
											stream_decoder_metadata_callback,
											stream_decoder_error_callback,
											&mClientDataStruct)
			!= FLAC__STREAM_DECODER_INIT_STATUS_OK)
		{
			ACFLACCodec::Uninitialize();
			CODEC_THROW(kAudioCodecStateError);
		}
		mDecoderState = FLAC__stream_decoder_get_state(mDecoder);
		mInputBufferPtr = mInputBuffer;
	}
	else
	{
		CODEC_THROW(kAudioCodecStateError);
	}
}

// We handle one packet at a time -- no more!
// get the AU from inInputData, store it in mBitBuffer
void ACFLACDecoder::AppendInputData(const void* inInputData, UInt32& ioInputDataByteSize, UInt32& ioNumberPackets, const AudioStreamPacketDescription* inPacketDescription)
{

	UInt8 * tempInput = (UInt8 *)inInputData;
	
	if (ioNumberPackets > 0 && ioInputDataByteSize > 0 && !mPacketInInputBuffer)
    {
		// See if we actually got an AudioStreamPacketDescription*
		if (inPacketDescription != NULL)
		{
			// and if it's filled out and passes a basic sanity check
			if ( (inPacketDescription[0]).mDataByteSize && (inPacketDescription[0]).mDataByteSize + (inPacketDescription[0]).mStartOffset <= ioInputDataByteSize )
			{
				// use it ...
				ioInputDataByteSize = (inPacketDescription[0]).mDataByteSize;
				// if we have enough space.
				if(GetInputBufferByteSize() - mInputBufferBytesUsed >= ioInputDataByteSize) // We have enough space
				{
					// increment past the offset
					tempInput += (inPacketDescription[0]).mStartOffset;
					memcpy(mInputBuffer + mInputBufferBytesUsed, (const unsigned char *)tempInput, (inPacketDescription[0]).mDataByteSize);
					mInputBufferBytesUsed = (inPacketDescription[0]).mDataByteSize;
					ioInputDataByteSize += (inPacketDescription[0]).mStartOffset;
				#if VERBOSE
					printf("Appending data: ioInputDataByteSize == %lu, mInputBufferBytesUsed == %lu, mStartOffset == %lu\n", ioInputDataByteSize, mInputBufferBytesUsed, (inPacketDescription[0]).mStartOffset);
				#endif
					// we may have received more than 1 packet, but we're only taking 1.
					ioNumberPackets = 1;
					mPacketInInputBuffer = true;
				}
				else
				{
				#if VERBOSE
					printf ("We think we have a partial packet\n");
				#endif
					// No partial packets
					ioInputDataByteSize = 0;
					ioNumberPackets = 0;
					mInputBufferBytesUsed = 0;
				}
			}
			else
			{
			#if VERBOSE
				printf("We have no data\n");
			#endif
				// We're either screwed or have no data
				ioNumberPackets = 0;
				ioInputDataByteSize = 0;
				mInputBufferBytesUsed = 0;
			}
		}
		else // we'd better have one packet
		{
		#if VERBOSE
			printf ("No Packet descriptions\n");
		#endif
			if(GetInputBufferByteSize() - mInputBufferBytesUsed >= ioInputDataByteSize) // We have enough space
			{
				memcpy(mInputBuffer + mInputBufferBytesUsed, (const unsigned char *)tempInput, ioInputDataByteSize);
				mInputBufferBytesUsed = ioInputDataByteSize;
				mPacketInInputBuffer = true;
			}
			else
			{
				// No partial packets
				ioInputDataByteSize = 0;
				ioNumberPackets = 0;
				mInputBufferBytesUsed = 0;
			}
		}
    }
	else
	{
        ioNumberPackets = 0;
        ioInputDataByteSize = 0;
	}

}

UInt32	ACFLACDecoder::ProduceOutputPackets(void* outOutputData, UInt32& ioOutputDataByteSize, UInt32& ioNumberPackets, AudioStreamPacketDescription* outPacketDescription)
{

	//	setup the return value, by assuming that everything is going to work
	UInt32 theAnswer = kAudioCodecProduceOutputPacketSuccess;
	
	if(!mIsInitialized)
	{
		CODEC_THROW(kAudioCodecStateError);
	}
	
	//	Note that the decoder doesn't suffer from the same problem the encoder
	//	does with not having enough data for a packet, since the encoded data
	//	is always going to be in whole packets.
	
	if(ioNumberPackets > 0 && mPacketInInputBuffer)
	{
		//	make sure that there is enough space in the output buffer for the encoded data
		//	it is an error to ask for more output than you pass in buffer space for
		UInt32	theOutputByteSize = mStreamInfo.max_blocksize * mOutputFormat.mBytesPerFrame;
	#if VERBOSE
		printf ("ioOutputDataByteSize == %lu, theOutputByteSize == %lu, mStreamInfo.max_blocksize = %lu, mOutputFormat.mBytesPerFrame = %lu\n", ioOutputDataByteSize, theOutputByteSize, mStreamInfo.max_blocksize, mOutputFormat.mBytesPerFrame);
	#endif
		ThrowIf(ioOutputDataByteSize < theOutputByteSize, static_cast<ComponentResult>(kAudioCodecNotEnoughBufferSpaceError), "ACFLACDecoder::ProduceOutputPackets: not enough space in the output buffer");

		// Process the packet
		mOutputBufferPtr = reinterpret_cast<Byte*>(outOutputData);
		mInputBufferBytesRead = 0; // reset this
		if (FLAC__stream_decoder_process_single(mDecoder))
		{
			ioNumberPackets = mFramesDecoded;
			ioOutputDataByteSize = mFramesDecoded * mOutputFormat.mBytesPerFrame;
			mInputBufferBytesUsed = 0;
			mPacketInInputBuffer = false;
			if (mFramesDecoded != kFramesPerPacket)
			{
				FLAC__stream_decoder_flush(mDecoder); // may not be necessary
				theAnswer = kAudioCodecProduceOutputPacketAtEOF;
			}
		}
		else
		{
			mDecoderState = FLAC__stream_decoder_get_state(mDecoder); // we'll do something with this eventually;
			ioNumberPackets = 0;
			theAnswer = kAudioCodecProduceOutputPacketFailure;
			ioOutputDataByteSize = 0;
		}
	#if VERBOSE	
		printf("producing packet, ioOutputDataByteSize == %lu\n", ioOutputDataByteSize);
	#endif
			//for (int i = 0; i < 32; ++i)
			//{
			//	printf ("%li ", ((SInt16 *)(outOutputData))[i]);
			//}
			//printf ("\n");
	}
	else
	{
		//	set the return value since we're not actually doing any work
		ioOutputDataByteSize = 0;
		ioNumberPackets = 0;
		theAnswer = kAudioCodecProduceOutputPacketNeedsMoreInputData;
	}
	
	return theAnswer;

}

UInt32	ACFLACDecoder::GetVersion() const
{
	return kFLACadecVersion;
}

UInt32	ACFLACDecoder::GetInputBufferByteSize() const
{
	return kInputBufferPackets * kMaxChannels * sizeof(SInt32);
}

UInt32	ACFLACDecoder::GetUsedInputBufferByteSize() const
{
	return mInputBufferBytesUsed;
}

void ACFLACDecoder::ReallocateInputBuffer(UInt32 inInputBufferByteSize)
{
}

void ACFLACDecoder::Uninitialize()
{
	if (mIsInitialized)
	{
		FLAC__stream_decoder_finish(mDecoder);
	}
	mDecoderState = FLAC__stream_decoder_get_state(mDecoder);
	mInputBufferBytesUsed = 0;
	mPacketInInputBuffer = false;
	mFramesDecoded = 0;
	ACFLACCodec::Uninitialize();
}

void ACFLACDecoder::Reset()
{
	mInputBufferBytesUsed = 0;
	mPacketInInputBuffer = false;
	mFramesDecoded = 0;
	FLAC__stream_decoder_reset(mDecoder);
	mDecoderState = FLAC__stream_decoder_get_state(mDecoder);
	ACFLACCodec::Reset();
}

FLAC__StreamDecoderReadStatus ACFLACDecoder::stream_decoder_read_callback(const FLAC__StreamDecoder *decoder, FLAC__byte buffer[], size_t *bytes, void *client_data)
{
	const unsigned requested_bytes = *bytes;

	(void)decoder, (void)client_data;

	if(requested_bytes > 0)
	{
		if (requested_bytes <= mInputBufferBytesUsed - mInputBufferBytesRead)
		{
			memcpy(buffer, mInputBufferPtr + mInputBufferBytesRead, requested_bytes);
		}
		else
		{
			*bytes = mInputBufferBytesUsed - mInputBufferBytesRead;
			memcpy(buffer, mInputBufferPtr + mInputBufferBytesRead, mInputBufferBytesUsed);
		}
		if(*bytes == 0)
		{
		#if VERBOSE
			printf("Read: FLAC__STREAM_DECODER_READ_STATUS_ABORT\n");
		#endif
			return FLAC__STREAM_DECODER_READ_STATUS_ABORT;
		}
		else
		{
		#if VERBOSE
			printf("Read: FLAC__STREAM_DECODER_READ_STATUS_CONTINUE\n");
		#endif
			mInputBufferBytesRead += *bytes;
			return FLAC__STREAM_DECODER_READ_STATUS_CONTINUE;
		}
	}
	else
	{
	#if VERBOSE
		printf("Read: FLAC__STREAM_DECODER_READ_STATUS_ABORT\n");
	#endif
		return FLAC__STREAM_DECODER_READ_STATUS_ABORT; /* abort to avoid a deadlock */
	}
}

FLAC__StreamDecoderWriteStatus ACFLACDecoder::stream_decoder_write_callback(const FLAC__StreamDecoder *decoder, const FLAC__Frame *frame, const FLAC__int32 * const buffer[], void *client_data)
{
	(void)decoder, (void)client_data;

	// deinterleave and convert 32-bit ints to 16-bit ints
	SInt16 * tempBuffer = (SInt16 *)mOutputBufferPtr;
#if VERBOSE
	printf ("frame->header.channels * frame->header.blocksize == %lu\n", frame->header.channels * frame->header.blocksize);
#endif
	if(frame->header.bits_per_sample == 16)
	{
		for (unsigned int i = 0; i < frame->header.blocksize; ++i)
		{
			for (unsigned int j = 0; j < frame->header.channels; ++j)
			{
				tempBuffer[i * frame->header.channels + j] = (SInt16)((buffer[j])[i]);
			}
		}
	}
	else // 24
	{
		for (unsigned int i = 0; i < frame->header.blocksize; ++i)
		{
			for (unsigned int j = 0; j < frame->header.channels; ++j)
			{
				mOutputBufferPtr[3 * (i * frame->header.channels + j)] = ((buffer[j][i]) >> 16) & 0xff ;
				mOutputBufferPtr[3 * (i * frame->header.channels + j) + 1] = ((buffer[j][i]) >> 8) & 0xff ;
				mOutputBufferPtr[3 * (i * frame->header.channels + j) + 2] = (buffer[j][i]) & 0xff ;
			}
		}
	}
	mFramesDecoded = frame->header.blocksize;

	return FLAC__STREAM_DECODER_WRITE_STATUS_CONTINUE;
}

void ACFLACDecoder::stream_decoder_metadata_callback(const FLAC__StreamDecoder *decoder, const FLAC__StreamMetadata *metadata, void *client_data)
{
	stream_decoder_client_data_struct *dcd = (stream_decoder_client_data_struct*)client_data;

	(void)decoder;

	if(0 == dcd)
	{
	#if VERBOSE
		printf("ERROR: client_data in metadata callback is NULL\n");
	#endif
		return;
	}

	if(dcd->error_occurred)
	{
		return;
	}

	fflush(stdout);

	if(dcd->current_metadata_number >= num_expected_)
	{
		(void)die_("got more metadata blocks than expected");
		dcd->error_occurred = true;
	}
	else
	{
	/*
		if(!mutils__compare_block(expected_metadata_sequence_[dcd->current_metadata_number], metadata))
		{
			(void)die_("metadata block mismatch");
			dcd->error_occurred = true;
		}
	*/
	}
	dcd->current_metadata_number++;
}

void ACFLACDecoder::stream_decoder_error_callback(const FLAC__StreamDecoder *decoder, FLAC__StreamDecoderErrorStatus status, void *client_data)
{
	stream_decoder_client_data_struct *dcd = (stream_decoder_client_data_struct*)client_data;

	(void)decoder;

	if(0 == dcd)
	{
	#if VERBOSE
		printf("ERROR: client_data in error callback is NULL\n");
	#endif
			return;
	}

	if(!dcd->ignore_errors)
	{
	#if VERBOSE	
		printf("ERROR: got error callback: err = %u (%s)\n", (unsigned)status, FLAC__StreamDecoderErrorStatusString[status]);
	#endif
		dcd->error_occurred = true;
	}
}

extern "C"
ComponentResult	ACFLACDecoderEntry(ComponentParameters* inParameters, ACFLACDecoder* inThis)
{	
	return	ACCodecDispatch(inParameters, inThis);
}

