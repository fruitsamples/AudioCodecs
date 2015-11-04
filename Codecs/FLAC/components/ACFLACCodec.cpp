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
	ACFLACCodec.cpp

=============================================================================*/

//=============================================================================
//	Includes
//=============================================================================

#include "ACFLACCodec.h"
#include "CABundleLocker.h"
#include "ACCompatibility.h"

#if TARGET_OS_WIN32
	#include "CAWin32StringResources.h"
#endif

typedef UInt32 QTSoundFormatFlags;

// Linear PCM (dataFormat == 'lpcm') specific QTSoundFormatFlags
enum
{
	kQTSoundLPCMFlagLittleEndian	= 1L<<0,	// stored least-significant-byte first (Intel-style)
	kQTSoundLPCMFlagFloatingPoint	= 1L<<1,	// samples are floats, not integers
	kQTSoundLPCMFlagUnsignedInteger	= 1L<<2,	// samples are 0..max (eg. 8-bit where 128 is "silence")
};

struct AtomHeader {
  SInt32              size;                   /* = sizeof(AudioFormatAtom)*/
  OSType              type;               /* = kAudioFormatAtomType*/
};
typedef struct AtomHeader          AtomHeader;

struct FullAtomHeader {
  SInt32              size;                   /* = sizeof(AudioFormatAtom)*/
  OSType              type;               /* = kAudioFormatAtomType*/
  OSType              versionFlags;
};
typedef struct FullAtomHeader          FullAtomHeader;

struct AudioFormatAtom {
  SInt32                size;                   /* = sizeof(AudioFormatAtom)*/
  OSType              atomType;               /* = kAudioFormatAtomType*/
  OSType              format;
};
typedef struct AudioFormatAtom          AudioFormatAtom;

struct AudioTerminatorAtom {
  SInt32                size;                   /* = sizeof(AudioTerminatorAtom)*/
  OSType              atomType;               /* = kAudioTerminatorAtomType*/
};
typedef struct AudioTerminatorAtom      AudioTerminatorAtom;

enum
{
	kFLACCodecFormat		= 'flac',
	kFLACVersion			= 0,
	kFLACCompatibleVersion	= kFLACVersion,
	kFLACDefaultFrameSize	= 4608 // 1152 * 4
};

/*audio atom types*/
enum {
  kAudioFormatAtomType          = 'frma',
  kAudioTerminatorAtomType      = 0
};

#define VERBOSE 0
//=============================================================================
//	ACFLACCodec
//=============================================================================
FLAC__StreamMetadata_StreamInfo ACFLACCodec::mStreamInfo = {0};
UInt32 ACFLACCodec::mCookieDefined = false;

ACFLACCodec::ACFLACCodec(UInt32 inInputBufferByteSize, OSType theSubType)
:
	ACBaseCodec(theSubType)
{
	mBitDepth = 0;
	mAvgBitRate = 0;
	mMaxFrameBytes = 0;
	mMagicCookieLength = 0;
	
	memset( mMagicCookie, 0, 256 );
	mCookieSet = 0;
}

ACFLACCodec::~ACFLACCodec()
{
}

void	ACFLACCodec::Initialize(const AudioStreamBasicDescription* inInputFormat, const AudioStreamBasicDescription* inOutputFormat, const void* inMagicCookie, UInt32 inMagicCookieByteSize)
{
	//	use the given arguments, if necessary
	if(inInputFormat != NULL)
	{
		SetCurrentInputFormat(*inInputFormat);
	}

	if(inOutputFormat != NULL)
	{
		SetCurrentOutputFormat(*inOutputFormat);
	}
	
	//	make sure the sample rate and number of channels match between the input format and the output format
	if( (mInputFormat.mSampleRate != mOutputFormat.mSampleRate) ||
		(mInputFormat.mChannelsPerFrame != mOutputFormat.mChannelsPerFrame))
	{
#if VERBOSE	
		printf("The channels and sample rates don't match, mInputFormat.mSampleRate == %f, mOutputFormat.mSampleRate == %f, mInputFormat.mChannelsPerFrame == %lu, mOutputFormat.mChannelsPerFrame == %lu\n", 
				mInputFormat.mSampleRate, mOutputFormat.mSampleRate, mInputFormat.mChannelsPerFrame, mOutputFormat.mChannelsPerFrame);
#endif
		CODEC_THROW(kAudioCodecUnsupportedFormatError);
	}
	
	if(inMagicCookie != NULL)
	{
		SetMagicCookie(inMagicCookie, inMagicCookieByteSize);
	}
	ACBaseCodec::Initialize(inInputFormat, inOutputFormat, inMagicCookie, inMagicCookieByteSize);
}

void	ACFLACCodec::Uninitialize()
{
	//	clean up the internal state	
	mAvgBitRate = 0;
	mMaxFrameBytes = 0;
	//	let our base class clean up it's internal state
	ACBaseCodec::Uninitialize();
}

void	ACFLACCodec::Reset()
{
	//	clean up the internal state	
	mAvgBitRate = 0;
	mMaxFrameBytes = 0;
	//	let our base class clean up it's internal state
	ACBaseCodec::Reset();
}

void	ACFLACCodec::GetPropertyInfo(AudioCodecPropertyID inPropertyID, UInt32& outPropertyDataSize, Boolean& outWritable)
{
	switch(inPropertyID)
	{
		case kAudioCodecPropertyCurrentInputSampleRate:
			outPropertyDataSize = sizeof(Float64);
			outWritable = true;
			break;

 		case kAudioCodecPropertyCurrentOutputSampleRate:
			outPropertyDataSize = sizeof(Float64);
			outWritable = false;
			break;
			
		case kAudioCodecPropertyMaximumPacketByteSize:
			outPropertyDataSize = sizeof(UInt32);
			outWritable = false;
			break;
		
		case kAudioCodecPropertyRequiresPacketDescription:
			outPropertyDataSize = sizeof(UInt32);
			outWritable = false;
			break;

		case kAudioCodecPropertyHasVariablePacketByteSizes:
			outPropertyDataSize = sizeof(UInt32);
			outWritable = false;
			break;
            
		case kAudioCodecPropertyPacketFrameSize:
			outPropertyDataSize = sizeof(UInt32);
			outWritable = false;
			break;
            		
		case kAudioCodecPropertyInputChannelLayout:
		case kAudioCodecPropertyOutputChannelLayout:
			outPropertyDataSize = sizeof(AudioChannelLayout);
			outWritable = false;
			break;

		case kAudioCodecPropertyAvailableInputChannelLayouts:
		case kAudioCodecPropertyAvailableOutputChannelLayouts:
			outPropertyDataSize = kMaxChannels * sizeof(AudioChannelLayoutTag);
			outWritable = false;
			break;

		case kAudioCodecPropertyFormatInfo:
			outPropertyDataSize = sizeof(AudioFormatInfo);
			outWritable = false;
			break;

		default:
			ACBaseCodec::GetPropertyInfo(inPropertyID, outPropertyDataSize, outWritable);
			break;
			
	};
}

void	ACFLACCodec::GetProperty(AudioCodecPropertyID inPropertyID, UInt32& ioPropertyDataSize, void* outPropertyData)
{	
	// kAudioCodecPropertyMaximumPacketByteSize is handled in the Encoder or Decoder
	
	switch(inPropertyID)
	{
		case kAudioCodecPropertyFormatCFString:
		{
			if (ioPropertyDataSize != sizeof(CFStringRef))
			{
				CODEC_THROW(kAudioCodecBadPropertySizeError);
			}
			
			CABundleLocker lock;
			CFStringRef name = CFCopyLocalizedStringFromTableInBundle(CFSTR("FLAC"), CFSTR("CodecNames"), GetCodecBundle(), CFSTR(""));
			*(CFStringRef*)outPropertyData = name;
			break; 
		}

       case kAudioCodecPropertyRequiresPacketDescription:
  			if(ioPropertyDataSize == sizeof(UInt32))
			{
                *reinterpret_cast<UInt32*>(outPropertyData) = 1; 
            }
			else
			{
				CODEC_THROW(kAudioCodecBadPropertySizeError);
			}
            break;
			
        case kAudioCodecPropertyHasVariablePacketByteSizes:
  			if(ioPropertyDataSize == sizeof(UInt32))
			{
                *reinterpret_cast<UInt32*>(outPropertyData) = 1; // We are variable bitrate
            }
			else
			{
				CODEC_THROW(kAudioCodecBadPropertySizeError);
			}
            break;
			
		case kAudioCodecPropertyPacketFrameSize:
			if(ioPropertyDataSize == sizeof(UInt32))
			{
                *reinterpret_cast<UInt32*>(outPropertyData) = kFramesPerPacket;
            }
			else
			{
				CODEC_THROW(kAudioCodecBadPropertySizeError);
			}
			break;
			
		case kAudioCodecPropertyMagicCookie:
			if(ioPropertyDataSize >= GetMagicCookieByteSize())
			{
				GetMagicCookie(outPropertyData, ioPropertyDataSize);
				mMagicCookieLength = ioPropertyDataSize;
			}
			else
			{
				CODEC_THROW(kAudioCodecIllegalOperationError);
			}
			break;
			
        case kAudioCodecPropertyCurrentInputSampleRate:
  			if(ioPropertyDataSize == sizeof(Float64))
			{
                *reinterpret_cast<Float64*>(outPropertyData) = (Float64)(mInputFormat.mSampleRate);
			}
			else
			{
				CODEC_THROW(kAudioCodecBadPropertySizeError);
			}
            break;
			
		case kAudioCodecPropertyCurrentOutputSampleRate:
  			if(ioPropertyDataSize == sizeof(Float64))
			{
				*reinterpret_cast<Float64*>(outPropertyData) = (Float64)(mOutputFormat.mSampleRate);
			}
			else
			{
				CODEC_THROW(kAudioCodecBadPropertySizeError);
			}
			break;
			
		case kAudioCodecPropertyInputChannelLayout:
		case kAudioCodecPropertyOutputChannelLayout:
			AudioChannelLayout temp1AudioChannelLayout;
			memset(&temp1AudioChannelLayout, 0, sizeof(AudioChannelLayout));
  			if(ioPropertyDataSize == sizeof(AudioChannelLayout))
			{
				temp1AudioChannelLayout.mChannelLayoutTag = sChannelLayoutTags[mInputFormat.mChannelsPerFrame - 1];
				memcpy(outPropertyData, &temp1AudioChannelLayout, ioPropertyDataSize);
			}
			else
			{
				CODEC_THROW(kAudioCodecBadPropertySizeError);
			}
			break;
			
		case kAudioCodecPropertyAvailableInputChannelLayouts:
		case kAudioCodecPropertyAvailableOutputChannelLayouts:
  			if(ioPropertyDataSize == kMaxChannels * sizeof(AudioChannelLayoutTag))
			{
				if(mIsInitialized)
				{
					AudioChannelLayoutTag temp2AudioChannelLayout[1];
					temp2AudioChannelLayout[0] = sChannelLayoutTags[mInputFormat.mChannelsPerFrame - 1];
					ioPropertyDataSize = sizeof(AudioChannelLayoutTag);
					memcpy(reinterpret_cast<AudioChannelLayoutTag*>(outPropertyData), temp2AudioChannelLayout, ioPropertyDataSize);
				}
				else
				{
					AudioChannelLayoutTag tempAudioChannelLayout[kMaxChannels];
					tempAudioChannelLayout[0] = kAudioChannelLayoutTag_Mono;
					tempAudioChannelLayout[1] = kAudioChannelLayoutTag_Stereo;
					tempAudioChannelLayout[2] = kAudioChannelLayoutTag_MPEG_3_0_B;
					tempAudioChannelLayout[3] = kAudioChannelLayoutTag_MPEG_4_0_B;
					tempAudioChannelLayout[4] = kAudioChannelLayoutTag_MPEG_5_0_D;
					tempAudioChannelLayout[5] = kAudioChannelLayoutTag_MPEG_5_1_D;
					tempAudioChannelLayout[6] = kAudioChannelLayoutTag_AAC_6_1;
					tempAudioChannelLayout[7] = kAudioChannelLayoutTag_MPEG_7_1_B;
					memcpy(reinterpret_cast<AudioChannelLayoutTag*>(outPropertyData), tempAudioChannelLayout, ioPropertyDataSize);
				}
			}
			else
			{
				CODEC_THROW(kAudioCodecBadPropertySizeError);
			}
			break;
			
		case kAudioCodecPropertyFormatInfo:
			if(ioPropertyDataSize == sizeof(AudioFormatInfo))
			{
				AudioFormatInfo& formatInfo = *(AudioFormatInfo*)outPropertyData;
				
				// Check for cookie existence
				if((NULL != formatInfo.mMagicCookie) && (formatInfo.mMagicCookieSize > 0))
				{
					UInt32 theByteSize = formatInfo.mMagicCookieSize;
					
					FLAC__StreamMetadata_StreamInfo theConfig;
					memset (&theConfig, 0, sizeof(FLAC__StreamMetadata_StreamInfo));
					ParseMagicCookie(formatInfo.mMagicCookie, theByteSize, &theConfig);
					formatInfo.mASBD.mSampleRate = (Float64)theConfig.sample_rate;
					formatInfo.mASBD.mChannelsPerFrame = theConfig.channels;
					formatInfo.mASBD.mFramesPerPacket = theConfig.max_blocksize;
					formatInfo.mASBD.mBytesPerPacket = 0; // it's never CBR
					switch (theConfig.bits_per_sample)
					{
						case 16:
							formatInfo.mASBD.mFormatFlags = kFLACFormatFlag_16BitSourceData;
							break;
						case 20:
							formatInfo.mASBD.mFormatFlags = kFLACFormatFlag_20BitSourceData;
							break;
						case 24:
							formatInfo.mASBD.mFormatFlags = kFLACFormatFlag_24BitSourceData;
							break;
						case 32:
							formatInfo.mASBD.mFormatFlags = kFLACFormatFlag_32BitSourceData;
							break;
						default: // we don't support this
							formatInfo.mASBD.mFormatFlags = 0;
							break;						
					}
				}
				else
				{
					// We don't have a cookie, we have to check the ASBD 
					// according to the input formats
					UInt32 i;
					for(i = 0; i < GetNumberSupportedInputFormats(); ++i)
					{
						if(mInputFormatList[i].IsEqual(formatInfo.mASBD))
						{
							// IsEqual will treat 0 values as wildcards -- we can't have that with the format flags
							UInt32 tempFormatFlags = formatInfo.mASBD.mFormatFlags;
							// Fill out missing entries
							CAStreamBasicDescription::FillOutFormat(formatInfo.mASBD, mInputFormatList[i]);
							if (tempFormatFlags == 0)
							{
								formatInfo.mASBD.mFormatFlags = 0; // anything assigned here would be bad.
							}
							break;
						}
					}
					if(i == GetNumberSupportedInputFormats())
					{
						// No suitable settings found
						CODEC_THROW(kAudioCodecUnsupportedFormatError);						
					}
				}
			}
			else
			{
				CODEC_THROW(kAudioCodecBadPropertySizeError);
			}
			break;
			
		default:
			ACBaseCodec::GetProperty(inPropertyID, ioPropertyDataSize, outPropertyData);
	}
}

//FLAC__StreamMetadata_StreamInfo
void ACFLACCodec::GetMagicCookie(void* outMagicCookieData, UInt32& ioMagicCookieDataByteSize) const
{

	Byte *						buffer;
	Byte *						currPtr;
	AudioFormatAtom * frmaAtom;
	FullAtomHeader * flacAtom;
	AudioTerminatorAtom * termAtom;
	SInt32						atomSize;
	UInt32						flacSize;
	UInt32						chanSize;
	UInt32						frmaSize;
	UInt32						termSize;
	FLAC__StreamMetadata_StreamInfo *		config;
	OSStatus					status;
	UInt32						tempMaxFrameBytes;
	
	//RequireAction( sampleDesc != nil, return paramErr; );

	config		= nil;
	
	frmaSize = sizeof(AudioFormatAtom);
	flacSize		= sizeof(FullAtomHeader) + sizeof(FLAC__StreamMetadata_StreamInfo);
	chanSize		= 0;
	termSize = sizeof(AudioTerminatorAtom);

	// if we're encoding more than two channels, add an AudioChannelLayout atom to describe the layout
	if ( mOutputFormat.mChannelsPerFrame > 2 )
	{
		chanSize = sizeof(FullAtomHeader) + offsetof(AudioChannelLayout, mChannelDescriptions);
	}

	// create buffer of the required size
	atomSize = frmaSize + flacSize + chanSize + termSize;
	
	// Someone might have a stereo/mono cookie while we're trying to do surround.
	if ((UInt32)atomSize > ioMagicCookieDataByteSize)
	{
		CODEC_THROW(kAudioCodecBadPropertySizeError);
	}
	
	tempMaxFrameBytes = kInputBufferPackets * mOutputFormat.mChannelsPerFrame * ((10 + kMaxSampleSize) / 8) + 1;

	buffer = (Byte *)calloc( atomSize, 1 );
	currPtr = buffer;

	// fill in the atom stuff
	frmaAtom = (AudioFormatAtom *) currPtr;
	frmaAtom->size			= EndianU32_NtoB( frmaSize );
	frmaAtom->atomType		= EndianU32_NtoB( kAudioFormatAtomType );
	frmaAtom->format	= EndianU32_NtoB( 'flac' );
	currPtr += frmaSize;

	// fill in the FLAC config
	flacAtom = (FullAtomHeader *) currPtr;
	flacAtom->size				= EndianU32_NtoB( flacSize );
	flacAtom->type				= EndianU32_NtoB( 'flac' );
	flacAtom->versionFlags		= 0;
	currPtr += sizeof(FullAtomHeader);

/*
	unsigned min_blocksize, max_blocksize;
	unsigned min_framesize, max_framesize;
	unsigned sample_rate;
	unsigned channels;
	unsigned bits_per_sample;
	FLAC__uint64 total_samples;
	FLAC__byte md5sum[16];
*/
	config = (FLAC__StreamMetadata_StreamInfo *) currPtr;
	if (mCookieDefined)
	{
		config->min_blocksize	= EndianU32_NtoB( mStreamInfo.min_blocksize );
		config->max_blocksize	= EndianU32_NtoB( mStreamInfo.max_blocksize );
		config->min_framesize	= EndianU32_NtoB( mStreamInfo.min_framesize );
		config->max_framesize	= EndianU32_NtoB( mStreamInfo.max_framesize );
		config->sample_rate		= EndianU32_NtoB( mStreamInfo.sample_rate );
		config->channels		= EndianU32_NtoB( mStreamInfo.channels );
		config->bits_per_sample	= EndianU32_NtoB( mStreamInfo.bits_per_sample );
		config->total_samples	= EndianU64_NtoB( mStreamInfo.total_samples );
		config->md5sum[0]		= mStreamInfo.md5sum[0];
		config->md5sum[1]		= mStreamInfo.md5sum[1];
		config->md5sum[2]		= mStreamInfo.md5sum[2];
		config->md5sum[3]		= mStreamInfo.md5sum[3];
		config->md5sum[4]		= mStreamInfo.md5sum[4];
		config->md5sum[5]		= mStreamInfo.md5sum[5];
		config->md5sum[6]		= mStreamInfo.md5sum[6];
		config->md5sum[7]		= mStreamInfo.md5sum[7];
		config->md5sum[8]		= mStreamInfo.md5sum[8];
		config->md5sum[9]		= mStreamInfo.md5sum[9];
		config->md5sum[10]		= mStreamInfo.md5sum[10];
		config->md5sum[11]		= mStreamInfo.md5sum[11];
		config->md5sum[12]		= mStreamInfo.md5sum[12];
		config->md5sum[13]		= mStreamInfo.md5sum[13];
		config->md5sum[14]		= mStreamInfo.md5sum[14];
		config->md5sum[15]		= mStreamInfo.md5sum[15];
	}
	else
	{
		config->min_blocksize	= EndianU32_NtoB( kFLACDefaultFrameSize );
		config->max_blocksize	= EndianU32_NtoB( kFLACDefaultFrameSize );
		config->min_framesize	= EndianU32_NtoB( 0 );
		config->max_framesize	= EndianU32_NtoB( 0 );
		config->sample_rate		= EndianU32_NtoB( (UInt32)(mOutputFormat.mSampleRate) );
		config->channels		= EndianU32_NtoB(mOutputFormat.mChannelsPerFrame);
		config->bits_per_sample	= EndianU32_NtoB(mBitDepth);
		config->total_samples	= 0;
		config->md5sum[0]		= 0;
		config->md5sum[1]		= 0;
		config->md5sum[2]		= 0;
		config->md5sum[3]		= 0;
		config->md5sum[4]		= 0;
		config->md5sum[5]		= 0;
		config->md5sum[6]		= 0;
		config->md5sum[7]		= 0;
		config->md5sum[8]		= 0;
		config->md5sum[9]		= 0;
		config->md5sum[10]		= 0;
		config->md5sum[11]		= 0;
		config->md5sum[12]		= 0;
		config->md5sum[13]		= 0;
		config->md5sum[14]		= 0;
		config->md5sum[15]		= 0;
	}

	currPtr += sizeof(FLAC__StreamMetadata_StreamInfo);

	// if we're encoding more than two channels, add an AudioChannelLayout atom to describe the layout
	// Unfortunately there is no way to avoid dealing with an atom here
	if ( mOutputFormat.mChannelsPerFrame > 2 )
	{
		AudioChannelLayoutTag		tag;
		FullAtomHeader *			chan;
		AudioChannelLayout *		layout;
		
		chan = (FullAtomHeader *) currPtr;
		chan->size = EndianU32_NtoB( chanSize );
		chan->type = EndianU32_NtoB( AudioChannelLayoutAID );
		// version flags == 0
		currPtr += sizeof(FullAtomHeader);
		
		// we use a predefined set of layout tags so we don't need to write any channel descriptions
		layout = (AudioChannelLayout *) currPtr;
		tag = sChannelLayoutTags[mOutputFormat.mChannelsPerFrame - 1];
		layout->mChannelLayoutTag			= EndianU32_NtoB( tag );
		layout->mChannelBitmap				= 0;
		layout->mNumberChannelDescriptions	= 0;
		currPtr += offsetof(AudioChannelLayout, mChannelDescriptions);
	}
	
	// fill in Terminator atom header
	termAtom = (AudioTerminatorAtom *) currPtr;
	termAtom->size = EndianU32_NtoB( termSize );
	termAtom->atomType = EndianU32_NtoB( kAudioTerminatorAtomType );

	// all good, return the new description
	memcpy (outMagicCookieData, (const void *)(buffer), atomSize);
	ioMagicCookieDataByteSize = atomSize;
	status = noErr;
	
	// delete any memory we allocated
	if ( buffer != NULL )
	{
		delete buffer;
		buffer = NULL;
	}

}

UInt32	ACFLACCodec::GetMagicCookieByteSize() const
{
	UInt32 tempCookieSize = 256;

	if(mMagicCookieLength) // we have a cookie
	{
		tempCookieSize = mMagicCookieLength;
	}
	return tempCookieSize;
}


void ACFLACCodec::ParseMagicCookie(const void* inMagicCookieData, UInt32 inMagicCookieDataByteSize, FLAC__StreamMetadata_StreamInfo * theStreamInfo) const
{
	FLAC__StreamMetadata_StreamInfo * tempConfig;
	UInt32 cookieOffset = 0;
	
	// We might get a cookie with atoms -- strip them off
	if (inMagicCookieDataByteSize > sizeof(FLAC__StreamMetadata_StreamInfo))
	{
		if(EndianU32_BtoN(((AudioFormatAtom *)inMagicCookieData)->atomType) == 'frma')
		{
			cookieOffset = (sizeof(AudioFormatAtom) + sizeof(FullAtomHeader));
		}
	} 
	// Finally, parse the cookie for the bits we care about
	tempConfig = (FLAC__StreamMetadata_StreamInfo *)(&((Byte *)(inMagicCookieData))[cookieOffset]);
	theStreamInfo->min_blocksize	= EndianU32_BtoN( tempConfig->min_blocksize );
	theStreamInfo->max_blocksize	= EndianU32_BtoN( tempConfig->max_blocksize );
	theStreamInfo->min_framesize	= EndianU32_BtoN( tempConfig->min_framesize );
	theStreamInfo->max_framesize	= EndianU32_BtoN( tempConfig->max_framesize );
	theStreamInfo->sample_rate		= EndianU32_BtoN( tempConfig->sample_rate );
	theStreamInfo->channels		= EndianU32_BtoN( tempConfig->channels );
	theStreamInfo->bits_per_sample	= EndianU32_BtoN( tempConfig->bits_per_sample );
	theStreamInfo->total_samples	= EndianU64_BtoN( tempConfig->total_samples );
	theStreamInfo->md5sum[0]		= tempConfig->md5sum[0];
	theStreamInfo->md5sum[1]		= tempConfig->md5sum[1];
	theStreamInfo->md5sum[2]		= tempConfig->md5sum[2];
	theStreamInfo->md5sum[3]		= tempConfig->md5sum[3];
	theStreamInfo->md5sum[4]		= tempConfig->md5sum[4];
	theStreamInfo->md5sum[5]		= tempConfig->md5sum[5];
	theStreamInfo->md5sum[6]		= tempConfig->md5sum[6];
	theStreamInfo->md5sum[7]		= tempConfig->md5sum[7];
	theStreamInfo->md5sum[8]		= tempConfig->md5sum[8];
	theStreamInfo->md5sum[9]		= tempConfig->md5sum[9];
	theStreamInfo->md5sum[10]		= tempConfig->md5sum[10];
	theStreamInfo->md5sum[11]		= tempConfig->md5sum[11];
	theStreamInfo->md5sum[12]		= tempConfig->md5sum[12];
	theStreamInfo->md5sum[13]		= tempConfig->md5sum[13];
	theStreamInfo->md5sum[14]		= tempConfig->md5sum[14];
	theStreamInfo->md5sum[15]		= tempConfig->md5sum[15];
}
// FLAC__StreamMetadata_StreamInfo is our magic cookie;

void ACFLACCodec::SetMagicCookie(const void* inMagicCookieData, UInt32 inMagicCookieDataByteSize)
{

	if(mIsInitialized)
	{
		CODEC_THROW(kAudioCodecStateError);
	}
	if(inMagicCookieDataByteSize > 256) // the largest cookie we can store
	{
		CODEC_THROW(kAudioCodecBadPropertySizeError);
	}
	else // store the cookie
	{
		memcpy (mMagicCookie, (const void *)(inMagicCookieData), inMagicCookieDataByteSize);
		mMagicCookieLength = inMagicCookieDataByteSize;
		mCookieSet = 1;
	}
	
	ParseMagicCookie(inMagicCookieData, inMagicCookieDataByteSize, &mStreamInfo);
	
	if (inMagicCookieDataByteSize > 0)
	{
		mCookieDefined = true;
	}
	else
	{
		mCookieDefined = false;
	}
}

void ACFLACCodec::SetProperty(AudioCodecPropertyID inPropertyID, UInt32 inPropertyDataSize, const void* inPropertyData)
{
	switch(inPropertyID)
	{
        case kAudioCodecPropertyCurrentInputSampleRate:
			if(mIsInitialized)
			{
				CODEC_THROW(kAudioCodecIllegalOperationError);
			}
			if(inPropertyDataSize == sizeof(Float64))
			{
				mInputFormat.mSampleRate = *((Float64*)inPropertyData);
			}
			else
			{
				CODEC_THROW(kAudioCodecBadPropertySizeError);
			}
			break;

		case kAudioCodecPropertyFormatInfo:
		case kAudioCodecPropertyHasVariablePacketByteSizes:
		case kAudioCodecPropertyCurrentOutputSampleRate:
		case kAudioCodecPropertyAvailableInputChannelLayouts:
		case kAudioCodecPropertyAvailableOutputChannelLayouts:
		case kAudioCodecPropertyPacketFrameSize:
		case kAudioCodecPropertyMaximumPacketByteSize:
			CODEC_THROW(kAudioCodecIllegalOperationError);
			break;
		default:
			ACBaseCodec::SetProperty(inPropertyID, inPropertyDataSize, inPropertyData);
			break;            
	}
}
