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
	ACFLACEncoder.cpp

=============================================================================*/

//=============================================================================
//	Includes
//=============================================================================

#include "ACFLACEncoder.h"
#include "metadata.h"
#include "ACCodecDispatch.h"
#include "CAStreamBasicDescription.h"
#include "CASampleTools.h"
#include "CADebugMacros.h"
#include "CABundleLocker.h"

#if TARGET_OS_WIN32
	#include "CAWin32StringResources.h"
#endif

#ifndef MAX
	#define MAX(x, y) 			( (x)>(y) ?(x): (y) )
#endif //MAX

#define RequireNoErr(err, action)   if ((err) != noErr) { action }
#define RequireAction(condition, action)			if (!(condition)) { action }

// Note: in C you can't typecast to a 2-dimensional array pointer but that's what we need when
// picking which coefs to use so we declare this typedef b/c we *can* typecast to this type
#define kMaxWorkChannels 2

typedef enum
{

	ID_SCE = 0,						/* Single Channel Element   */
	ID_CPE = 1,						/* Channel Pair Element     */
	ID_CCE = 2,						/* Coupling Channel Element */
	ID_LFE = 3,						/* LFE Channel Element      */
	ID_DSE = 4,						/* not yet supported        */
	ID_PCE = 5,
	ID_FIL = 6,
	ID_END = 7
} ELEMENT_TYPE;

static const UInt32	sChannelMaps[kFLACMaxChannels] =
{
	ID_SCE,
	ID_CPE,
	(ID_CPE << 3) | (ID_SCE),
	(ID_SCE << 9) | (ID_CPE << 3) | (ID_SCE),
	(ID_CPE << 9) | (ID_CPE << 3) | (ID_SCE),
	(ID_SCE << 15) | (ID_CPE << 9) | (ID_CPE << 3) | (ID_SCE),
	(ID_SCE << 18) | (ID_SCE << 15) | (ID_CPE << 9) | (ID_CPE << 3) | (ID_SCE),
	(ID_SCE << 21) | (ID_CPE << 15) | (ID_CPE << 9) | (ID_CPE << 3) | (ID_SCE)
};

#define kFLACMaxCompressionQuality 8

#define VERBOSE 0 // This will spit out an amazing amout of stuff -- it wouldn't hurt to split this up a bit
//=============================================================================
//	ACFLACEncoder
//=============================================================================

// static data members
UInt32	ACFLACEncoder::mOutputBytes = 0;
Byte * ACFLACEncoder::mOutputBuffer = NULL;

ACFLACEncoder::ACFLACEncoder(OSType theSubType)
:
	ACFLACCodec(kInputBufferPackets * kFramesPerPacket * sizeof(SInt16), theSubType)
{	
	//	This encoder only accepts (16- or 24-bit) native endian signed integers as it's input,
	//	but can handle any sample rate and any number of channels
	CAStreamBasicDescription theInputFormat1(kAudioStreamAnyRate, kAudioFormatLinearPCM, 0, 1, 0, 0, 16, kAudioFormatFlagsNativeEndian | kAudioFormatFlagIsSignedInteger | kAudioFormatFlagIsPacked);
	AddInputFormat(theInputFormat1);
	
	CAStreamBasicDescription theInputFormat2(kAudioStreamAnyRate, kAudioFormatLinearPCM, 0, 1, 0, 0, 24, kAudioFormatFlagsNativeEndian | kAudioFormatFlagIsSignedInteger | kAudioFormatFlagIsPacked);
	AddInputFormat(theInputFormat2);

	// These are some additional formats that FLAC can support
	//CAStreamBasicDescription theInputFormat3(kAudioStreamAnyRate, kAudioFormatLinearPCM, 0, 1, 0, 0, 32, kAudioFormatFlagsNativeEndian | kAudioFormatFlagIsSignedInteger | kAudioFormatFlagIsPacked);
	//AddInputFormat(theInputFormat3);

	//CAStreamBasicDescription theInputFormat4(kAudioStreamAnyRate, kAudioFormatLinearPCM, 0, 1, 0, 0, 20, kAudioFormatFlagsNativeEndian | kAudioFormatFlagIsSignedInteger | kAudioFormatFlagIsAlignedHigh);
	//AddInputFormat(theInputFormat4);
	
	//	set our intial input format to stereo 32 bit native endian signed integer at a 44100 sample rate
	mInputFormat.mSampleRate = 44100;
	mInputFormat.mFormatID = kAudioFormatLinearPCM;
	mInputFormat.mFormatFlags = kAudioFormatFlagsNativeEndian | kAudioFormatFlagIsSignedInteger | kAudioFormatFlagIsPacked;
	mInputFormat.mBytesPerPacket = 4;
	mInputFormat.mFramesPerPacket = 1;
	mInputFormat.mBytesPerFrame = 4;
	mInputFormat.mChannelsPerFrame = 2;
	mInputFormat.mBitsPerChannel = 16;
	
	//	This encoder only puts out a FLAC stream
	CAStreamBasicDescription theOutputFormat1(kAudioStreamAnyRate, kAudioFormatFLAC, 0, kFramesPerPacket, 0, 0, 0, 0);
	AddOutputFormat(theOutputFormat1);

	//	set our intial output format to stereo FLAC at a 44100 sample rate -- note however the 16 bit bit depth
	mOutputFormat.mSampleRate = 44100;
	mOutputFormat.mFormatFlags = kFLACFormatFlag_16BitSourceData;
	mOutputFormat.mBytesPerPacket = 0;
	mOutputFormat.mFramesPerPacket = kFramesPerPacket;
	mOutputFormat.mFormatID = kAudioFormatFLAC;
	mOutputFormat.mBytesPerFrame = 0;
	mOutputFormat.mChannelsPerFrame = 2;
	mOutputFormat.mBitsPerChannel = 0;
	
	mSupportedChannelTotals[0] = 1;
	mSupportedChannelTotals[1] = 2;
	mSupportedChannelTotals[2] = 3;
	mSupportedChannelTotals[3] = 4;
	mSupportedChannelTotals[4] = 5;
	mSupportedChannelTotals[5] = 6;
	mSupportedChannelTotals[6] = 7;
	mSupportedChannelTotals[7] = 8;
	
	mPacketInInputBuffer = false;

	mFormat = 0;

	mTotalBytesGenerated = 0;
	
	mQuality = 0; // Compression Quality
	mInputBufferBytesUsed = 0;
	mFlushPacket = false;
	mFinished = false;
	mTrailingFrames = 0;
	mBitDepth = 16;
	mEncoder = FLAC__stream_encoder_new();
	mEncoderState = FLAC__stream_encoder_get_state(mEncoder);
}

ACFLACEncoder::~ACFLACEncoder()
{
	if (mEncoder != NULL)
	{
		FLAC__stream_encoder_delete(mEncoder);
		mEncoder = NULL;
	}
}

void	ACFLACEncoder::GetPropertyInfo(AudioCodecPropertyID inPropertyID, UInt32& outPropertyDataSize, Boolean& outWritable)
{
	switch(inPropertyID)
	{
		case kAudioCodecPropertyAvailableNumberChannels:
			outPropertyDataSize = sizeof(UInt32) * kFLACNumberSupportedChannelTotals;
			outWritable = false;
			break;

		case kAudioCodecPropertyAvailableInputSampleRates:
			outPropertyDataSize = sizeof(AudioValueRange);
			outWritable = false;
			break;
			
		case kAudioCodecPropertyAvailableOutputSampleRates:
			outPropertyDataSize = sizeof(AudioValueRange);
			outWritable = false;
			break;
            		            		
		case kAudioCodecPropertyQualitySetting:
			outPropertyDataSize = sizeof(UInt32);
			outWritable = true;
			break;

		case kAudioCodecPropertyZeroFramesPadded:
			outPropertyDataSize = sizeof(UInt32);
			outWritable = false;
			break;
		
		case kAudioCodecPropertySettings:
			outPropertyDataSize = sizeof(CFDictionaryRef *);
			outWritable = true;
			break;
		
		default:
			ACFLACCodec::GetPropertyInfo(inPropertyID, outPropertyDataSize, outWritable);
			break;
			
	};
}

void	ACFLACEncoder::GetProperty(AudioCodecPropertyID inPropertyID, UInt32& ioPropertyDataSize, void* outPropertyData)
{
	switch(inPropertyID)
	{
		case kAudioCodecPropertyNameCFString:
		{
			if (ioPropertyDataSize != sizeof(CFStringRef))
			{
				CODEC_THROW(kAudioCodecBadPropertySizeError);
			}
			CABundleLocker lock;
			CFStringRef name = CFCopyLocalizedStringFromTableInBundle(CFSTR("FLAC encoder"), CFSTR("CodecNames"), GetCodecBundle(), CFSTR(""));
			*(CFStringRef*)outPropertyData = name;
			break; 
		}
		
		case kAudioCodecPropertyAvailableNumberChannels:
  			if(ioPropertyDataSize == sizeof(UInt32) * kFLACNumberSupportedChannelTotals)
			{
				memcpy(reinterpret_cast<UInt32*>(outPropertyData), mSupportedChannelTotals, ioPropertyDataSize);
			}
			else
			{
				CODEC_THROW(kAudioCodecBadPropertySizeError);
			}
			break;

		case kAudioCodecPropertyAvailableInputSampleRates:
  			if(ioPropertyDataSize == sizeof(AudioValueRange) )
			{
				(reinterpret_cast<AudioValueRange*>(outPropertyData))->mMinimum = 0.0;
				(reinterpret_cast<AudioValueRange*>(outPropertyData))->mMaximum = 0.0;
			}
			else
			{
				CODEC_THROW(kAudioCodecBadPropertySizeError);
			}
			break;

		case kAudioCodecPropertyAvailableOutputSampleRates:
  			if(ioPropertyDataSize == sizeof(AudioValueRange) )
			{
				(reinterpret_cast<AudioValueRange*>(outPropertyData))->mMinimum = 0.0;
				(reinterpret_cast<AudioValueRange*>(outPropertyData))->mMaximum = 0.0;
			}
			else
			{
				CODEC_THROW(kAudioCodecBadPropertySizeError);
			}
			break;

        case kAudioCodecPropertyPrimeMethod:
  			if(ioPropertyDataSize == sizeof(UInt32))
			{
				*reinterpret_cast<UInt32*>(outPropertyData) = (UInt32)kAudioCodecPrimeMethod_None;
			}
			else
			{
				CODEC_THROW(kAudioCodecBadPropertySizeError);
			}
			break;

		case kAudioCodecPropertyPrimeInfo:
  			if(ioPropertyDataSize == sizeof(AudioCodecPrimeInfo) )
			{
				(reinterpret_cast<AudioCodecPrimeInfo*>(outPropertyData))->leadingFrames = 0;
				(reinterpret_cast<AudioCodecPrimeInfo*>(outPropertyData))->trailingFrames = mTrailingFrames;
			}
			else
			{
				CODEC_THROW(kAudioCodecBadPropertySizeError);
			}
			break;

        case kAudioCodecPropertyQualitySetting:
  			if(ioPropertyDataSize == sizeof(UInt32))
			{
                *reinterpret_cast<UInt32*>(outPropertyData) = mQuality;
			}
			else
			{
				CODEC_THROW(kAudioCodecBadPropertySizeError);
			}
			break;

       case kAudioCodecPropertyMaximumPacketByteSize:
			if(ioPropertyDataSize == sizeof(UInt32))
			{
				if (mMaxFrameBytes)
				{
					*reinterpret_cast<UInt32*>(outPropertyData) = mMaxFrameBytes;
				}
				else // default case
				{
					*reinterpret_cast<UInt32*>(outPropertyData) = mMaxFrameBytes = kInputBufferPackets * mOutputFormat.mChannelsPerFrame * (mBitDepth >> 3) + kMaxEscapeHeaderBytes;
				}
			#if VERBOSE
				printf("Max packet size == %lu, mBitDepth == %lu\n", mMaxFrameBytes, mBitDepth);
			#endif
			}
			else
			{
				CODEC_THROW(kAudioCodecBadPropertySizeError);
			}
			break;

		case kAudioCodecInputFormatsForOutputFormat:
			if(ioPropertyDataSize >= sizeof(AudioStreamBasicDescription))
			{
				UInt32 bitDepth, numFormats = 1, tempSize;
				switch ( ( ( (AudioStreamBasicDescription*)(outPropertyData) )[0].mFormatFlags) & 0x00000007)
				{
					case kFLACFormatFlag_16BitSourceData:
						bitDepth = 16;
						break;
					case kFLACFormatFlag_20BitSourceData:
						bitDepth = 24;
						break;
					case kFLACFormatFlag_24BitSourceData:
						bitDepth = 24;
						break;						
					case kFLACFormatFlag_32BitSourceData:
						bitDepth = 32;
						break;
					default: // Check the currently set input format bit depth
						bitDepth = mInputFormat.mBitsPerChannel;
						numFormats = 2;
						break;
				}
				AudioStreamBasicDescription theInputFormat = {kAudioStreamAnyRate, kAudioFormatLinearPCM, kAudioFormatFlagsNativeEndian | kAudioFormatFlagIsSignedInteger | kAudioFormatFlagIsPacked, 0, 1, 0, 0, bitDepth, 0};
				tempSize = sizeof(AudioStreamBasicDescription) * numFormats;
				if ( tempSize <= ioPropertyDataSize )
				{
					ioPropertyDataSize = tempSize;
				}
				else
				{
					CODEC_THROW(kAudioCodecBadPropertySizeError);
				}
				if ( numFormats == 1 )
				{
					memcpy(outPropertyData, &theInputFormat, ioPropertyDataSize);
				}
				else // numFormats == 2
				{
					theInputFormat.mBitsPerChannel = 16;
					memcpy(outPropertyData, &theInputFormat, sizeof(AudioStreamBasicDescription));
					theInputFormat.mBitsPerChannel = 24;
					memcpy((void *)((Byte *)outPropertyData + sizeof(AudioStreamBasicDescription)), &theInputFormat, sizeof(AudioStreamBasicDescription));
				}
			}
			else
			{
				CODEC_THROW(kAudioCodecBadPropertySizeError);
			}
			break;

        case kAudioCodecPropertyZeroFramesPadded:
			if(ioPropertyDataSize == sizeof(UInt32))
			{
				*reinterpret_cast<UInt32*>(outPropertyData) = 0; // we never append any extra zeros
			}
			else
			{
				CODEC_THROW(kAudioCodecBadPropertySizeError);
			}
			break;

		case kAudioCodecPropertySettings:
  			if(ioPropertyDataSize == sizeof(CFDictionaryRef *) )
			{
				BuildSettingsDictionary(reinterpret_cast<CFDictionaryRef *>(outPropertyData) );
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

void ACFLACEncoder::SetProperty(AudioCodecPropertyID inPropertyID, UInt32 inPropertyDataSize, const void* inPropertyData)
{
	switch(inPropertyID)
	{
		case kAudioCodecPropertyCurrentOutputFormat:
			if(mIsInitialized)
			{
				CODEC_THROW(kAudioCodecIllegalOperationError);
			}
			if(inPropertyDataSize == sizeof(AudioStreamBasicDescription))
			{
				SetCurrentOutputFormat(*reinterpret_cast<const AudioStreamBasicDescription*>(inPropertyData));
			}
			else
			{
				CODEC_THROW(kAudioCodecBadPropertySizeError);
			}
			break;

		// We could certainly use a private property here instead
		case kAudioCodecPropertyQualitySetting:
			if(mIsInitialized)
			{
				CODEC_THROW(kAudioCodecIllegalOperationError);
			}
			if(inPropertyDataSize == sizeof(UInt32))
			{
				UInt32 tempQuality = *(UInt32*)inPropertyData;
				// QuickTime will slam us with a render setting that we don't want -- we ignore it.
				if (tempQuality <= kFLACMaxCompressionQuality)
				{
					mQuality = tempQuality;
				}
			}
			else
			{
				CODEC_THROW(kAudioCodecBadPropertySizeError);
			}
            break;

		case kAudioCodecPropertySettings:
			if(inPropertyDataSize == sizeof(CFDictionaryRef *) )
			{
				ParseSettingsDictionary( *( (CFDictionaryRef*)(inPropertyData) ) );
			}
			break;

		case kAudioCodecPropertyZeroFramesPadded:
		case kAudioCodecPropertyAvailableInputSampleRates:
		case kAudioCodecPropertyAvailableOutputSampleRates:
			CODEC_THROW(kAudioCodecIllegalOperationError);
			break;
		default:
			ACFLACCodec::SetProperty(inPropertyID, inPropertyDataSize, inPropertyData);
			break;            
	}
}

void ACFLACEncoder::SetCurrentInputFormat(const AudioStreamBasicDescription& inInputFormat)
{
	if(!mIsInitialized)
	{
		//	check to make sure the input format is legal
		if(	(inInputFormat.mFormatID != kAudioFormatLinearPCM) ||
			( (inInputFormat.mFormatFlags != (kAudioFormatFlagsNativeEndian | kAudioFormatFlagIsSignedInteger | kAudioFormatFlagIsPacked) ) &&
			  (inInputFormat.mFormatFlags != (kAudioFormatFlagsNativeEndian | kAudioFormatFlagIsSignedInteger | kAudioFormatFlagIsAlignedHigh) ) ) )
		{
	#if VERBOSE
			DebugMessage("ACFLACEncoder::SetCurrentInputFormat: only supports native endian signed integers for input");
	#endif
			CODEC_THROW(kAudioCodecUnsupportedFormatError);
		}
		if ( !( (inInputFormat.mBitsPerChannel == 16) || (inInputFormat.mBitsPerChannel == 24) ) )
		{
	#if VERBOSE
			DebugMessage("ACFLACEncoder::SetCurrentInputFormat: only supports 16 or 24 bit integers for input");
	#endif
			CODEC_THROW(kAudioCodecUnsupportedFormatError);
		}
		
		//	tell our base class about the new format
		ACFLACCodec::SetCurrentInputFormat(inInputFormat);
		
		// Now, since lossless requires number of channels and sample rates to match between
		// input and output formats, set the output ones here in case the output format is left to default
		mOutputFormat.mChannelsPerFrame = mInputFormat.mChannelsPerFrame;
		if (inInputFormat.mSampleRate == 0.0)
		{
			mInputFormat.mSampleRate = mOutputFormat.mSampleRate;
		}
		else
		{
			mOutputFormat.mSampleRate = mInputFormat.mSampleRate;
		}
		mOutputFormat.mFormatFlags &= 0xfffffff8;
		mBitDepth = inInputFormat.mBitsPerChannel;
		switch (inInputFormat.mBitsPerChannel)
		{
			case 16:
				mOutputFormat.mFormatFlags |= kFLACFormatFlag_16BitSourceData;
				break;
			case 24:
				mOutputFormat.mFormatFlags |= kFLACFormatFlag_24BitSourceData;
				break;
			default: // avoids a warning -- this will never be hit
				break;
		}
		// Zero out everything that has to be zero
		mInputFormat.mReserved = 0;
	}
	else
	{
		CODEC_THROW(kAudioCodecStateError);
	}
}

void ACFLACEncoder::SetCurrentOutputFormat(const AudioStreamBasicDescription& inOutputFormat)
{
	if(!mIsInitialized)
	{
		//	check to make sure the output format is legal
		if(!(inOutputFormat.mFormatID == 'flac' /* kAudioFormatFLAC */ ))
		{
	#if VERBOSE
			DebugMessage("ACFLACEncoder::SetCurrentOutputFormat: only support FLAC for output");
	#endif
			CODEC_THROW(kAudioCodecUnsupportedFormatError);
		}
		
		//	tell our base class about the new format
		ACFLACCodec::SetCurrentOutputFormat(inOutputFormat);
		if (mOutputFormat.mFramesPerPacket == 0 && inOutputFormat.mFormatID == 'flac') // make sure this field is still valid
		{
			mOutputFormat.mFramesPerPacket = kFramesPerPacket;
		}
		if (mOutputFormat.mSampleRate == 0.0)
		{
			mOutputFormat.mSampleRate = mInputFormat.mSampleRate;
		}
		// Zero out everything that has to be zero
		mOutputFormat.mBytesPerFrame = 0;
		mOutputFormat.mBitsPerChannel = 0;
		mOutputFormat.mBytesPerPacket = 0;
		mOutputFormat.mReserved = 0;
	}
	else
	{
		CODEC_THROW(kAudioCodecStateError);
	}
}

// There's a lot more to do!
void ACFLACEncoder::Initialize(const AudioStreamBasicDescription* inInputFormat, const AudioStreamBasicDescription* inOutputFormat, const void* inMagicCookie, UInt32 inMagicCookieByteSize)
{
	if(!mIsInitialized)
	{
		// Yes, do this here and not at the end -- we need to set the MagicCookie before
		// we continue on
		ACFLACCodec::Initialize(inInputFormat, inOutputFormat, inMagicCookie, inMagicCookieByteSize);

	#if VERBOSE	
		printf("mOutputFormat.mFormatFlags == %x\n", mOutputFormat.mFormatFlags);
	#endif
		if ( (mOutputFormat.mFormatFlags & 0x00000007) != 0) 
		{
			UInt32 outputBitDepth = 0;
			switch ( (mOutputFormat.mFormatFlags & 0x00000007) )
			{
				case kFLACFormatFlag_16BitSourceData:
				#if VERBOSE	
					printf("kFLACFormatFlag_16BitSourceData\n");
				#endif
					outputBitDepth = 16;
					break;
				case kFLACFormatFlag_24BitSourceData:
				#if VERBOSE	
					printf("kFLACFormatFlag_24BitSourceData\n");
				#endif
					outputBitDepth = 24;
					break;						
				default:
					// guarantees failure as we have a non-zero flags value and that's not allowed 
					break;
			}
		#if VERBOSE	
			printf("mInputFormat.mBitsPerChannel == %lu\n", mInputFormat.mBitsPerChannel);
		#endif
			if (outputBitDepth != mInputFormat.mBitsPerChannel)
			{
				ACFLACCodec::Uninitialize();
				CODEC_THROW(kAudioCodecUnsupportedFormatError);
			}
		}

		mInputFormat.mBytesPerFrame = ((mInputFormat.mBitsPerChannel) >> 3) * mInputFormat.mChannelsPerFrame;

		mBitDepth = mInputFormat.mBitsPerChannel;
	#if VERBOSE	
		printf ("mBitDepth == %lu\n", mBitDepth);
	#endif
		if (mBitDepth == 0)
		{
			mBitDepth = mInputFormat.mBitsPerChannel;
		}
		mMaxFrameBytes = kInputBufferPackets * mOutputFormat.mChannelsPerFrame * ((mBitDepth) >> 3) + kMaxEscapeHeaderBytes;
		
		// Fill out the output format flags so if someone needs to see them
		if(mOutputFormat.mFormatFlags == 0) // fill out the flags if they aren't filled out already.
		{
			switch (mBitDepth)
			{
				case 16:
					mOutputFormat.mFormatFlags = kFLACFormatFlag_16BitSourceData;
					break;
				case 24:
					mOutputFormat.mFormatFlags = kFLACFormatFlag_24BitSourceData;
					break;						
				default:
					// guarantees failure as we have a non-zero flags value that's not allowed 
					mOutputFormat.mFormatFlags = 0;
					break;
			}
		}
		mEncoderState = FLAC__stream_encoder_get_state(mEncoder);
		if (mEncoderState != FLAC__STREAM_ENCODER_UNINITIALIZED)
		{
			FLAC__stream_encoder_finish(mEncoder);
			mEncoderState = FLAC__stream_encoder_get_state(mEncoder);
		}
		// Now set up the encoder
		FLAC__stream_encoder_set_streamable_subset(mEncoder, false);
		FLAC__stream_encoder_set_channels(mEncoder, mInputFormat.mChannelsPerFrame);
		
		FLAC__stream_encoder_set_bits_per_sample(mEncoder, mBitDepth);
		FLAC__stream_encoder_set_sample_rate(mEncoder, (UInt32)mInputFormat.mSampleRate);
		FLAC__stream_encoder_set_blocksize(mEncoder, kFramesPerPacket);

		// Now, we set the compression level. We used the kAudioCodecPropertyQualitySetting to determine this. Min 0, max 8
		SetCompressionLevel(mQuality);	

		// we're going to toss this anyway -- this is only for the initialization writes
		mOutputBuffer = mInputBuffer;

		// Finally, initialize the encoder
		FLAC__stream_encoder_init_stream(mEncoder,
										 stream_encoder_write_callback,
										 NULL,
										 NULL,
										 stream_encoder_metadata_callback,
										 NULL);
										 
		mEncoderState = FLAC__stream_encoder_get_state(mEncoder);
		if (mEncoderState != FLAC__STREAM_ENCODER_OK)
		{
			// Right now we treat all errors as equally bad
		#if VERBOSE	
			printf("mEncoderState == %i\n", mEncoderState);
		#endif
			ACFLACCodec::Uninitialize();
			CODEC_THROW(kAudioCodecUnsupportedFormatError);
		}
	}
	else
	{
		CODEC_THROW(kAudioCodecStateError);
	}
}

// We take one flac "packet" at a time of PCM data
// We will need 4608 frames at a time but we could very well get more.
void ACFLACEncoder::AppendInputData(const void* inInputData, UInt32& ioInputDataByteSize, UInt32& ioNumberPackets, const AudioStreamPacketDescription* inPacketDescription)
{
	if(!mIsInitialized)
	{
		CODEC_THROW(kAudioCodecStateError);
	}

	Boolean packetAdded = false;
	UInt32 requiredNumberOfBytes, currentlyNeededNumberOfBytes;

	requiredNumberOfBytes = kInputBufferPackets * mInputFormat.mChannelsPerFrame * ((mInputFormat.mBitsPerChannel) >> 3);
	
	// We may be getting partial packets.
	currentlyNeededNumberOfBytes = requiredNumberOfBytes - mInputBufferBytesUsed;
#if VERBOSE
	printf("currentlyNeededNumberOfBytes == %lu, mInputFormat.mBitsPerChannel == %lu, mInputFormat.mBytesPerFrame == %lu\n", currentlyNeededNumberOfBytes, mInputFormat.mBitsPerChannel, mInputFormat.mBytesPerFrame);
#endif
	// For some reason we're getting called more than once before a call to ProduceOutputPackets
	if (currentlyNeededNumberOfBytes == 0) // there's a packet already there
	{
		mPacketInInputBuffer = true; // mark that we have a packet in the buffer
	#if VERBOSE
		printf("we have a packet in the buffer and copy ioInputDataByteSize = %d to the mLeftoverBuffer \n", ioInputDataByteSize);
	#endif
	}
	else if (!mPacketInInputBuffer) // we don't have a packet
	{

		if ( (ioInputDataByteSize >= currentlyNeededNumberOfBytes) && ( currentlyNeededNumberOfBytes <= GetInputBufferByteSize() - GetUsedInputBufferByteSize() ) ) // we will have a full packet
		{
			mPacketInInputBuffer = true;
		#if VERBOSE
			printf("mInputBufferBytesUsed == %lu\n", mInputBufferBytesUsed);
		#endif
			memcpy( (Byte *)mInputBuffer + mInputBufferBytesUsed, (Byte *)inInputData, currentlyNeededNumberOfBytes );
			mInputBufferBytesUsed += currentlyNeededNumberOfBytes;
			// Now, this part is not fun -- we need to blit the input into a low aligned 32-bit buffer
			if (mInputFormat.mBitsPerChannel == 16)
			{
				for (unsigned int i = 0; i < kInputBufferPackets * mInputFormat.mChannelsPerFrame; ++i)
				{
					mConvertedBuffer[i] = ((SInt16 *)mInputBuffer)[i];
				}
			}
			else // 24
			{
				for (unsigned int i = 0; i < kInputBufferPackets * mInputFormat.mChannelsPerFrame; ++i)
				{
					mConvertedBuffer[i] = (((UInt32)(mInputBuffer[3 * i])) << 16) | (((UInt32)(mInputBuffer[3 * i + 1])) << 8) | ((UInt32)(mInputBuffer[3 * i + 2]));
				}
			}
		#if VERBOSE
			printf("Append mInputBufferBytesUsed == %lu\n", mInputBufferBytesUsed);
		#endif
			// Useful for dealing with some input issues
			//printf ("The first 32 SInt32's == \n");
			//for (int i = 0; i < 32; ++i)
			//{
			//	printf ("%li ", ((SInt32 *)(mInputBuffer))[i]);
			//}
			//printf ("\n");
		}
		else // we won't have a full packet
		{
			if (ioInputDataByteSize == 0)
			{
				mFlushPacket = true;
			#if VERBOSE
				printf("Flushing last packet, mInputBufferBytesUsed == %lu\n", mInputBufferBytesUsed);
			#endif
				// We still have stuff in the input buffer that needs to be blitted in to the converted buffer
				if (mInputFormat.mBitsPerChannel == 16)
				{
					for (unsigned int i = 0; i < mInputBufferBytesUsed/((mInputFormat.mBitsPerChannel) >> 3); ++i)
					{
						mConvertedBuffer[i] = ((SInt16 *)mInputBuffer)[i];
					}
				}
				else // 24
				{
					for (unsigned int i = 0; i < mInputBufferBytesUsed/((mInputFormat.mBitsPerChannel) >> 3); ++i)
					{
						mConvertedBuffer[i] = (((UInt32)(mInputBuffer[3 * i])) << 16) | (((UInt32)(mInputBuffer[3 * i + 1])) << 8) | ((UInt32)(mInputBuffer[3 * i + 2]));
					}
				}
			}
			else
			{
				if (ioInputDataByteSize <= GetInputBufferByteSize() - GetUsedInputBufferByteSize())
				{
					memcpy( (Byte *)mInputBuffer + mInputBufferBytesUsed, (Byte *)inInputData, ioInputDataByteSize );
					mInputBufferBytesUsed += ioInputDataByteSize;
				#if VERBOSE
					printf("reading what we can\n");
				#endif
				}
				else
				{
					// We're screwed -- our buffer is too small
					CODEC_THROW(kAudioCodecStateError);
				}
			}
		}
		packetAdded = true;
	}
	// We should never get here, but if for some reason we do, things are bad --
	// we think we have a packet in the inputBuffer but not enough data.
	else 
	{
		CODEC_THROW(kAudioCodecStateError);
	}
    
	if (!packetAdded)
	{
		ioNumberPackets = 0;
		ioInputDataByteSize = 0;
	#if VERBOSE
		printf("no packetAdded -- ioInputDataByteSize == %lu, mInputBufferBytesUsed == %lu\n", ioInputDataByteSize, mInputBufferBytesUsed);
	#endif
	}
	else
	{
		if (mPacketInInputBuffer)
		{
			ioInputDataByteSize = currentlyNeededNumberOfBytes;
		}
		ioNumberPackets = ioInputDataByteSize / mInputFormat.mBytesPerFrame;
	#if VERBOSE
		printf("packetAdded -- ioInputDataByteSize == %lu, mInputBufferBytesUsed == %lu, ioNumberPackets == %lu\n", ioInputDataByteSize, mInputBufferBytesUsed, ioNumberPackets);
	#endif
    }

}

// We can only do 1 packet at a time
UInt32	ACFLACEncoder::ProduceOutputPackets(
				void* 							outOutputData, 
				UInt32& 						ioOutputDataByteSize, 
				UInt32& 						ioNumberPackets, 
				AudioStreamPacketDescription* 	outPacketDescription)
{

	//	setup the return value, by assuming that everything is going to work
	UInt32 theAnswer = kAudioCodecProduceOutputPacketSuccess;

	UInt32				numFrames = kInputBufferPackets;
	
	if(!mIsInitialized)
	{
		CODEC_THROW(kAudioCodecStateError);
	}
		
	//	clamp the number of packets to produce based on what is available in the input buffer
	// mBytesPerFrame had better be 2 or 4 -- not sure if anything else works
	UInt32 inputPacketSize = mInputFormat.mBytesPerFrame * kFramesPerPacket;  
	UInt32 numberOfInputPackets = GetUsedInputBufferByteSize() / inputPacketSize;
	if (numberOfInputPackets > 1)
	{
		numberOfInputPackets = 1;
	}
	if (ioNumberPackets < numberOfInputPackets) // only if ioNumberPackets == 0
	{
		numberOfInputPackets = ioNumberPackets;
	}
	else if (ioNumberPackets > numberOfInputPackets)
	{
		ioNumberPackets = numberOfInputPackets;
		
		//	this also means we need more input to satisfy the request so set the return value
		theAnswer = kAudioCodecProduceOutputPacketNeedsMoreInputData;
	}
	if (mFlushPacket && !mPacketInInputBuffer && !mFinished) 
	{
		numFrames = GetUsedInputBufferByteSize()/(mInputFormat.mBytesPerFrame);
		mPacketInInputBuffer = true;
		theAnswer = kAudioCodecProduceOutputPacketAtEOF;
		ioNumberPackets = 1;
		inputPacketSize = GetUsedInputBufferByteSize();
	#if VERBOSE
		printf("inputPacketSize == %lu, numFrames = %lu\n", inputPacketSize, numFrames);
	#endif
	}
	else
	{
		numFrames = kFramesPerPacket;
	}
	if(ioNumberPackets > 0 && mPacketInInputBuffer && !mFinished) // ioNumberPackets == 1
	{
		// Process a packet!
		
		mOutputBuffer = reinterpret_cast<Byte*>(outOutputData);
		// The call back sets mOutputBytes and mOutputBuffer
		mOutputBytes = 0; // set this value to 0 now. It may get incremented more than once by the write call back
		if (!FLAC__stream_encoder_process_interleaved(mEncoder, (const FLAC__int32 *)mConvertedBuffer, numFrames))
		{
			mEncoderState = FLAC__stream_encoder_get_state(mEncoder);
		#if VERBOSE
			printf ("mEncoderState == %i\n", mEncoderState);
		#endif
			return kAudioCodecProduceOutputPacketFailure;
		}			
		if (theAnswer == kAudioCodecProduceOutputPacketAtEOF)
		{
			FLAC__stream_encoder_finish(mEncoder); // flushes the last packet
		}
		// gather encoding stats
		mTotalBytesGenerated += mOutputBytes;
		mMaxFrameBytes = MAX( mMaxFrameBytes, mOutputBytes );

		//	make sure that there is enough space in the output buffer for the encoded data
		//	it is an error to ask for more output than you pass in buffer space for
	#if VERBOSE
		printf ("ioOutputDataByteSize == %lu, mOutputBytes == %lu\n", ioOutputDataByteSize, mOutputBytes);	
	#endif
		ThrowIf(ioOutputDataByteSize < mOutputBytes, static_cast<ComponentResult>(kAudioCodecNotEnoughBufferSpaceError), "ACFLACEncoder::ProduceOutputPackets: not enough space in the output buffer");
		
		//	set the return value
		ioOutputDataByteSize = mOutputBytes;
		if (mOutputBytes == 0)
		{
			ioNumberPackets = 0;
			if (outPacketDescription != NULL)
			{
				outPacketDescription->mStartOffset = 0;
				outPacketDescription->mVariableFramesInPacket = 0; // 4608 except for last packet
				outPacketDescription->mDataByteSize = ioOutputDataByteSize;
			#if VERBOSE
				printf("inputPacketSize / mInputFormat.mBytesPerFrame == %lu, ioOutputDataByteSize == %lu\n", inputPacketSize / mInputFormat.mBytesPerFrame, ioOutputDataByteSize);
			#endif
				mTrailingFrames = kFramesPerPacket - numFrames;
			}
		}
		else
		{
			if (outPacketDescription != NULL)
			{
				outPacketDescription->mStartOffset = 0;
				outPacketDescription->mVariableFramesInPacket = numFrames; // 4608 except for last packet
				outPacketDescription->mDataByteSize = ioOutputDataByteSize;
			#if VERBOSE
				printf("inputPacketSize / mInputFormat.mBytesPerFrame == %lu, ioOutputDataByteSize == %lu\n", inputPacketSize / mInputFormat.mBytesPerFrame, ioOutputDataByteSize);
			#endif
				mTrailingFrames = kFramesPerPacket - numFrames;
			}
		}
		
		//	encode the input data for each channel
	#if VERBOSE
		printf("Consuming %lu bytes\n", inputPacketSize);
	#endif
		mInputBufferBytesUsed -= inputPacketSize;
	#if VERBOSE
		printf("Produce mInputBufferBytesUsed == %lu\n", mInputBufferBytesUsed);
	#endif
		mPacketInInputBuffer = false;
		if (mFlushPacket && !mFinished) // there's only one last packet
		{
			mFinished = true;
		}
	}
	else
	{
		//	set the return value since we're not actually doing any work
		ioNumberPackets = 0;
		ioOutputDataByteSize = 0;
		theAnswer = kAudioCodecProduceOutputPacketNeedsMoreInputData;
	#if VERBOSE
		printf("Encoder needs more data\n");
	#endif
		if (outPacketDescription != NULL)
		{
			outPacketDescription->mStartOffset = 0;
			outPacketDescription->mVariableFramesInPacket = 0; 
			outPacketDescription->mDataByteSize = 0;
		#if VERBOSE
			printf("inputPacketSize / mInputFormat.mBytesPerFrame = %lu, ioOutputDataByteSize == %lu\n", inputPacketSize / mInputFormat.mBytesPerFrame, ioOutputDataByteSize);
		#endif
		}
	}
	
	if((theAnswer == kAudioCodecProduceOutputPacketSuccess) && (GetUsedInputBufferByteSize() >= inputPacketSize) && (inputPacketSize > 0) )
	{
		//	we satisfied the request, and there's at least one more full packet of data we can encode
		//	so set the return value
		theAnswer = kAudioCodecProduceOutputPacketSuccessHasMore;
	}
	
	return theAnswer;
}


UInt32	ACFLACEncoder::GetVersion() const
{
	return kFLACaencVersion;
}

UInt32	ACFLACEncoder::GetInputBufferByteSize() const
{
	return kInputBufferPackets * kFLACNumberSupportedChannelTotals * sizeof(SInt32);
}

UInt32	ACFLACEncoder::GetUsedInputBufferByteSize() const
{
	return mInputBufferBytesUsed;
}

void ACFLACEncoder::ReallocateInputBuffer(UInt32 inInputBufferByteSize)
{
}

void	ACFLACEncoder::Reset()
{
	//	clean up the internal state
	mFlushPacket = false;
	mFinished = false;
	mTrailingFrames = 0;
	mInputBufferBytesUsed = 0;
	mTotalBytesGenerated = 0;
	mOutputBytes = 0;
	if (mIsInitialized)
	{
		// This call is safe since if it's already uninitialized it'll just return
		FLAC__stream_encoder_finish(mEncoder);
		// Now set up the encoder -- yes, we must do all of this again
		FLAC__stream_encoder_set_streamable_subset(mEncoder, false);
		FLAC__stream_encoder_set_channels(mEncoder, mInputFormat.mChannelsPerFrame);
		
		FLAC__stream_encoder_set_bits_per_sample(mEncoder, mBitDepth);
		FLAC__stream_encoder_set_sample_rate(mEncoder, (UInt32)mInputFormat.mSampleRate);
		FLAC__stream_encoder_set_blocksize(mEncoder, kFramesPerPacket);

		// Now, we set the compression level. We used the kAudioCodecPropertyQualitySetting to determine this. Min 0, max 8
		SetCompressionLevel(mQuality);	
		// These must be set up again
		FLAC__stream_encoder_init_stream(mEncoder,
										 stream_encoder_write_callback,
										 NULL,
										 NULL,
										 stream_encoder_metadata_callback,
										 NULL);
	}
	
	//	let our base class clean up it's internal state
	ACFLACCodec::Reset();
}

void ACFLACEncoder::Uninitialize()
{
	//	clean up the internal state
	mFlushPacket = false;
	mFinished = false;
	mTrailingFrames = 0;
	mInputBufferBytesUsed = 0;
	mTotalBytesGenerated = 0;
	mOutputBytes = 0;
	mEncoderState = FLAC__stream_encoder_get_state(mEncoder);
	if (mEncoderState != FLAC__STREAM_ENCODER_UNINITIALIZED)
	{
		FLAC__stream_encoder_finish(mEncoder);
		mEncoderState = FLAC__stream_encoder_get_state(mEncoder);
	}
	ACFLACCodec::Uninitialize();
}

// Implements FLAC__stream_encoder_set_compression_level()
// FLAC__stream_encoder_set_apodization() is not set
void ACFLACEncoder::SetCompressionLevel(UInt32 theCompressionLevel)
{
	Boolean theStatus1, theStatus2, theStatus3, theStatus4, theStatus5;

	switch(theCompressionLevel)
	{
		case 0:
			theStatus1 = FLAC__stream_encoder_set_do_mid_side_stereo(mEncoder, false);
			theStatus2 = FLAC__stream_encoder_set_loose_mid_side_stereo(mEncoder, false);
			theStatus3 = FLAC__stream_encoder_set_max_lpc_order(mEncoder, 0);
			theStatus4 = FLAC__stream_encoder_set_do_exhaustive_model_search(mEncoder, false);
			theStatus5 = FLAC__stream_encoder_set_max_residual_partition_order(mEncoder, 3);
			break;
		case 1:
			theStatus1 = FLAC__stream_encoder_set_do_mid_side_stereo(mEncoder, true);
			theStatus2 = FLAC__stream_encoder_set_loose_mid_side_stereo(mEncoder, true);
			theStatus3 = FLAC__stream_encoder_set_max_lpc_order(mEncoder, 0);
			theStatus4 = FLAC__stream_encoder_set_do_exhaustive_model_search(mEncoder, false);
			theStatus5 = FLAC__stream_encoder_set_max_residual_partition_order(mEncoder, 3);
			break;
		case 2:
			theStatus1 = FLAC__stream_encoder_set_do_mid_side_stereo(mEncoder, true);
			theStatus2 = FLAC__stream_encoder_set_loose_mid_side_stereo(mEncoder, false);
			theStatus3 = FLAC__stream_encoder_set_max_lpc_order(mEncoder, 0);
			theStatus4 = FLAC__stream_encoder_set_do_exhaustive_model_search(mEncoder, false);
			theStatus5 = FLAC__stream_encoder_set_max_residual_partition_order(mEncoder, 3);
			break;
		case 3:
			theStatus1 = FLAC__stream_encoder_set_do_mid_side_stereo(mEncoder, false);
			theStatus2 = FLAC__stream_encoder_set_loose_mid_side_stereo(mEncoder, false);
			theStatus3 = FLAC__stream_encoder_set_max_lpc_order(mEncoder, 6);
			theStatus4 = FLAC__stream_encoder_set_do_exhaustive_model_search(mEncoder, false);
			theStatus5 = FLAC__stream_encoder_set_max_residual_partition_order(mEncoder, 4);
			break;
		case 4:
			theStatus1 = FLAC__stream_encoder_set_do_mid_side_stereo(mEncoder, true);
			theStatus2 = FLAC__stream_encoder_set_loose_mid_side_stereo(mEncoder, true);
			theStatus3 = FLAC__stream_encoder_set_max_lpc_order(mEncoder, 8);
			theStatus4 = FLAC__stream_encoder_set_do_exhaustive_model_search(mEncoder, false);
			theStatus5 = FLAC__stream_encoder_set_max_residual_partition_order(mEncoder, 4);
			break;
		case 5:
			theStatus1 = FLAC__stream_encoder_set_do_mid_side_stereo(mEncoder, true);
			theStatus2 = FLAC__stream_encoder_set_loose_mid_side_stereo(mEncoder, false);
			theStatus3 = FLAC__stream_encoder_set_max_lpc_order(mEncoder, 8);
			theStatus4 = FLAC__stream_encoder_set_do_exhaustive_model_search(mEncoder, false);
			theStatus5 = FLAC__stream_encoder_set_max_residual_partition_order(mEncoder, 5);
			break;
		case 6:
			theStatus1 = FLAC__stream_encoder_set_do_mid_side_stereo(mEncoder, true);
			theStatus2 = FLAC__stream_encoder_set_loose_mid_side_stereo(mEncoder, false);
			theStatus3 = FLAC__stream_encoder_set_max_lpc_order(mEncoder, 8);
			theStatus4 = FLAC__stream_encoder_set_do_exhaustive_model_search(mEncoder, false);
			theStatus5 = FLAC__stream_encoder_set_max_residual_partition_order(mEncoder, 6);
			break;
		case 7:
			theStatus1 = FLAC__stream_encoder_set_do_mid_side_stereo(mEncoder, true);
			theStatus2 = FLAC__stream_encoder_set_loose_mid_side_stereo(mEncoder, false);
			theStatus3 = FLAC__stream_encoder_set_max_lpc_order(mEncoder, 8);
			theStatus4 = FLAC__stream_encoder_set_do_exhaustive_model_search(mEncoder, true);
			theStatus5 = FLAC__stream_encoder_set_max_residual_partition_order(mEncoder, 6);
			break;
		case 8:
			theStatus1 = FLAC__stream_encoder_set_do_mid_side_stereo(mEncoder, true);
			theStatus2 = FLAC__stream_encoder_set_loose_mid_side_stereo(mEncoder, false);
			theStatus3 = FLAC__stream_encoder_set_max_lpc_order(mEncoder, 12);
			theStatus4 = FLAC__stream_encoder_set_do_exhaustive_model_search(mEncoder, true);
			theStatus5 = FLAC__stream_encoder_set_max_residual_partition_order(mEncoder, 6);
			break;
	}
#if VERBOSE
	printf("theCompressionLevel == %lu, theStatus1 == %i, theStatus2 == %i, theStatus3 == %i, theStatus4 == %i, theStatus5 == %i\n", theCompressionLevel, theStatus1, theStatus2, theStatus3, theStatus4, theStatus5);
#endif
}

// Used by GetProperty when called with kAudioCodecPropertySettings
OSStatus ACFLACEncoder::BuildSettingsDictionary(CFDictionaryRef * theSettingsRef)
{
	CABundleLocker lock;
	
	// Constants for kAudioCodecPropertySettings
	const CFStringRef kTopLevelKey = CFSTR(kAudioSettings_TopLevelKey);
	const CFStringRef kParameters = CFSTR(kAudioSettings_Parameters);
	const CFStringRef kSettingKey = CFSTR(kAudioSettings_SettingKey);
	const CFStringRef kSettingName = CFSTR(kAudioSettings_SettingName); // localizeable
	const CFStringRef kValueType = CFSTR(kAudioSettings_ValueType);
	const CFStringRef kAvailableValues = CFSTR(kAudioSettings_AvailableValues);
	const CFStringRef kCurrentValue = CFSTR(kAudioSettings_CurrentValue);
	const CFStringRef kSummary = CFSTR(kAudioSettings_Summary);
	const CFStringRef kHint = CFSTR(kAudioSettings_Hint);
	const CFStringRef kUnit = CFSTR(kAudioSettings_Unit);

	CACFArray theParameters(false);
	CACFDictionary	theCompressionLevel(false),
					theSettings(false);
	unsigned int i = 0;

	// Build the parameter dictionaries
	CACFArray theCompressionLevelValues(false);


	// Compression Level
	for (i = 0; i <= kFLACMaxCompressionQuality; i++)
	{
		if (!theCompressionLevelValues.AppendUInt32(i) ) goto cleanup;
	}
				
	if (!theCompressionLevel.AddString(kSettingKey, CFSTR("Compression Level") ) ) goto cleanup;
	if (!theCompressionLevel.AddString(kSettingName, CFCopyLocalizedStringFromTableInBundle( CFSTR("Compression Level"), CFSTR("CodecNames"), GetCodecBundle(), CFSTR("") ) ) ) goto cleanup;
	if (!theCompressionLevel.AddUInt32(kValueType, CFNumberGetTypeID() ) ) goto cleanup;
	if (!theCompressionLevel.AddArray(kAvailableValues, theCompressionLevelValues.GetCFArray() ) ) goto cleanup;
	theCompressionLevelValues.ShouldRelease(true);

	if (!theCompressionLevel.AddUInt32(kCurrentValue, mQuality) ) goto cleanup;
	if (!theCompressionLevel.AddSInt32(kHint, 0) ) goto cleanup;
	if (!theCompressionLevel.AddString(kSummary, CFCopyLocalizedStringFromTableInBundle( CFSTR("The compression level of the FLAC encoder"), CFSTR("CodecNames"), GetCodecBundle(), CFSTR("") ) ) ) goto cleanup;
	if (!theCompressionLevel.AddString(kUnit, CFCopyLocalizedStringFromTableInBundle( CFSTR(""), CFSTR("CodecNames"), GetCodecBundle(), CFSTR("") ) ) ) goto cleanup;
		
	
	// Now build the top level Settings dictionary
	// Add the codec name
	if (!theSettings.AddString(kTopLevelKey, CFCopyLocalizedStringFromTableInBundle( CFSTR("FLAC Encoder"), CFSTR("CodecNames"), GetCodecBundle(), CFSTR("") )) ) goto cleanup;
	
	// Build the parameters array 
	if (!theParameters.AppendDictionary(theCompressionLevel.GetCFDictionary() ) ) goto cleanup;
	theCompressionLevel.ShouldRelease(true);
	
	// Add the parameters to the dictionary
	if (!theSettings.AddArray(kParameters, theParameters.GetCFArray() ) ) goto cleanup;	
	theParameters.ShouldRelease(true);
	
	// Assign the freshly built dictionary to what theSettingsRef points to
	*theSettingsRef = theSettings.GetCFDictionary();
	
	return noErr;

cleanup:
	theSettings.ShouldRelease(true);
	return -1;
}

// Used by SetProperty when called with kAudioCodecPropertySettings
OSStatus ACFLACEncoder::ParseSettingsDictionary(CFDictionaryRef theNewSettings)
{
	// Constants for kAudioCodecPropertySettings
	const CFStringRef kParameters = CFSTR(kAudioSettings_Parameters);
	const CFStringRef kCurrentValue = CFSTR(kAudioSettings_CurrentValue);
	const CFStringRef kSettingKey = CFSTR(kAudioSettings_SettingKey);

	CACFDictionary	theSettings(theNewSettings, false);
	UInt32 theCurrentValue, theSettingIndex = 0;
	
	// Get the Parameters array
	CFArrayRef tmpParamArray;
	if (!theSettings.GetArray(kParameters, tmpParamArray) ) return -1;
	CACFArray theParameters(tmpParamArray, false);
	
	// Assume order independence
	CFDictionaryRef tmpDictRef;
	while (theParameters.GetDictionary(theSettingIndex, tmpDictRef))
	{
		CACFDictionary theCurrentSetting(tmpDictRef, false);
		CFStringRef currentKeyCFString;
		
		if (theCurrentSetting.GetString(kSettingKey, currentKeyCFString))
		{
			// don't release the key string: we're just inspecting the dictionary
			CACFString currentKey = CACFString(currentKeyCFString, false);
			
			if (currentKey == CACFString(CFSTR("Channel Configuration"), false ) )
			{
				// Get the current value of the Channel Configuration setting
			}
			else if (currentKey == CACFString(CFSTR("Sample Rate"), false ) )
			{
				// Get the current value of the Output Sample Rate setting
			}
			else if (currentKey == CACFString(CFSTR("Compression Level"), false ) )
			{	
				// Get the current value of the Compression Level setting
				if (!theCurrentSetting.GetUInt32(kCurrentValue, (UInt32&)theCurrentValue) ) return -1;
				mQuality = theCurrentValue; // we're a 0-based index, makes this simple
			}
		}
		
		++theSettingIndex;
	}

	return noErr;
}

// Call backs
FLAC__StreamEncoderWriteStatus ACFLACEncoder::stream_encoder_write_callback(const FLAC__StreamEncoder *encoder, const FLAC__byte buffer[], size_t bytes, unsigned samples, unsigned current_frame, void *client_data)
{
	(void)encoder, (void)samples, (void)current_frame, (void)client_data;
#if VERBOSE	
	printf("Writing %lu bytes at offset %lu\n", bytes, mOutputBytes);
#endif
	memcpy (mOutputBuffer + mOutputBytes, buffer, bytes);
	mOutputBytes += bytes;
	return FLAC__STREAM_ENCODER_WRITE_STATUS_OK;
}

void ACFLACEncoder::stream_encoder_metadata_callback(const FLAC__StreamEncoder *encoder, const FLAC__StreamMetadata *metadata, void *client_data)
{
	(void)encoder, (void)client_data;
	if (metadata->type == FLAC__METADATA_TYPE_STREAMINFO)
	{
		mCookieDefined = true;
		memcpy(&mStreamInfo, &(metadata->data), sizeof(FLAC__StreamMetadata_StreamInfo));
	}
}

extern "C"
ComponentResult ACFLACEncoderEntry(ComponentParameters* inParameters, ACFLACEncoder* inThis)
{
	return	ACCodecDispatch(inParameters, inThis);
}
