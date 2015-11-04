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
	ACFLACCodec.h

=============================================================================*/
#if !defined(__ACFLACCodec_h__)
#define __ACFLACCodec_h__

//=============================================================================
//	Includes
//=============================================================================

#include "ACBaseCodec.h"
#include <vector>
#include "ACFLACVersions.h"
#include "format.h"

const UInt32 kMaxChannels		= 8;			// max allowed number of channels is 8
const UInt32 kMaxSampleSize		= 32;			// max allowed bit width is 32
const UInt32 kMaxEscapeHeaderBytes	= 12;		// trial and error -- it's at least 11 bytes

enum
{
    kFLACFormatFlag_16BitSourceData    = 1,
    kFLACFormatFlag_20BitSourceData    = 2,
    kFLACFormatFlag_24BitSourceData    = 3,
    kFLACFormatFlag_32BitSourceData    = 4
};

enum
{
	kAudioFormatFLAC					= 'flac'
};

// tables
static const AudioChannelLayoutTag	sChannelLayoutTags[kMaxChannels] = 
{
	kAudioChannelLayoutTag_Mono,		// C
	kAudioChannelLayoutTag_Stereo,		// L R
	kAudioChannelLayoutTag_MPEG_3_0_B,	// C L R
	kAudioChannelLayoutTag_MPEG_4_0_B,	// C L R Cs
	kAudioChannelLayoutTag_MPEG_5_0_D,	// C L R Ls Rs
	kAudioChannelLayoutTag_MPEG_5_1_D,	// C L R Ls Rs LFE
	kAudioChannelLayoutTag_AAC_6_1,		// C L R Ls Rs Cs LFE
	kAudioChannelLayoutTag_MPEG_7_1_B	// C Lc Rc L R Ls Rs LFE    (doc: IS-13818-7 MPEG2-AAC)
};

enum
{
	AudioChannelLayoutAID = 'chan'
};

//=============================================================================
//	ACFLACCodec
//
//	This class encapsulates the common implementation of an Apple FLAC codec.
//=============================================================================

class ACFLACCodec
:
	public ACBaseCodec
{

//	Construction/Destruction
public:
						ACFLACCodec(UInt32 inInputBufferByteSize, OSType theSubType);
	virtual				~ACFLACCodec();

//	Data Handling
public:
	virtual void		Initialize(const AudioStreamBasicDescription* inInputFormat, const AudioStreamBasicDescription* inOutputFormat, const void* inMagicCookie, UInt32 inMagicCookieByteSize);
	virtual void		Uninitialize();
	virtual void		Reset();
	virtual UInt32		GetMagicCookieByteSize() const;
	virtual void		GetMagicCookie(void* outMagicCookieData, UInt32& ioMagicCookieDataByteSize) const;
	virtual void		ParseMagicCookie(const void* inMagicCookieData, UInt32 inMagicCookieDataByteSize, FLAC__StreamMetadata_StreamInfo * theStreamInfo) const;
	virtual void		SetMagicCookie(const void* inMagicCookieData, UInt32 inMagicCookieDataByteSize);

	virtual void		GetProperty(AudioCodecPropertyID inPropertyID, UInt32& ioPropertyDataSize, void* outPropertyData);
	virtual void		GetPropertyInfo(AudioCodecPropertyID inPropertyID, UInt32& outPropertyDataSize, Boolean& outWritable);
	virtual void		SetProperty(AudioCodecPropertyID inPropertyID, UInt32 inPropertyDataSize, const void* inPropertyData);

//	Implementation
protected:

//	Implementation Constants
protected:
	enum
	{
		kFramesPerPacket = 4608, // can also be 1152
		kBytesPerChannelPerPacket = 0, // no way of knowing
		kHeaderBytes = 12, // will need to check this
		kInputBufferPackets = kFramesPerPacket,
		kLosslessPacketBytes = kHeaderBytes + kBytesPerChannelPerPacket
	};
	SInt16					mBitDepth;
	UInt32					mAvgBitRate;
	UInt32					mMaxFrameBytes;
	
	Byte					mMagicCookie[256];
	UInt32					mMagicCookieLength;
	UInt32					mCookieSet;
	static FLAC__StreamMetadata_StreamInfo mStreamInfo;
	static UInt32 mCookieDefined;

};

#endif
