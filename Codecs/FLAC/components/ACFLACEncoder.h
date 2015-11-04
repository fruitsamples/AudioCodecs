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
	ACFLACEncoder.h

=============================================================================*/
#if !defined(__ACFLACEncoder_h__)
#define __ACFLACEncoder_h__

//=============================================================================
//	Includes
//=============================================================================

#define kFLACNumberSupportedChannelTotals kMaxChannels

#include "ACFLACCodec.h"
#include "stream_encoder.h"
#include "CACFDictionary.h"
#include "CACFString.h"
#include "CACFArray.h"

enum
{
	kFLACMaxChannels	= 8
};

//=============================================================================
//	ACFLACEncoder
//
//	This class encodes raw 16 bit signed integer data into an FLAC stream.
//=============================================================================

class ACFLACEncoder
:
	public ACFLACCodec
{

//	Construction/Destruction
public:
					ACFLACEncoder(OSType theSubType);
	virtual			~ACFLACEncoder();

	virtual void	GetPropertyInfo(AudioCodecPropertyID inPropertyID, UInt32& outPropertyDataSize, Boolean& outWritable);
	virtual void	GetProperty(AudioCodecPropertyID inPropertyID, UInt32& ioPropertyDataSize, void* outPropertyData);
	virtual void	SetProperty(AudioCodecPropertyID inPropertyID, UInt32 inPropertyDataSize, const void* inPropertyData);

//	Format Information
public:
	virtual void	SetCurrentInputFormat(const AudioStreamBasicDescription& inInputFormat);
	virtual void	SetCurrentOutputFormat(const AudioStreamBasicDescription& inOutputFormat);
	virtual UInt32	GetVersion() const;
	void            Initialize(const AudioStreamBasicDescription* inInputFormat, const AudioStreamBasicDescription* inOutputFormat, const void* inMagicCookie, UInt32 inMagicCookieByteSize);
    virtual void		AppendInputData(const void* inInputData, UInt32& ioInputDataByteSize, UInt32& ioNumberPackets, const AudioStreamPacketDescription* inPacketDescription);
	virtual UInt32		GetInputBufferByteSize() const;
	virtual UInt32		GetUsedInputBufferByteSize() const;
	virtual void		ReallocateInputBuffer(UInt32 inInputBufferByteSize);
	void			Uninitialize();
	virtual UInt32	ProduceOutputPackets(void* outOutputData, UInt32& ioOutputDataByteSize, UInt32& ioNumberPackets, AudioStreamPacketDescription* outPacketDescription);
	void Reset();

public:
	// call backs
	static FLAC__StreamEncoderWriteStatus stream_encoder_write_callback(const FLAC__StreamEncoder *encoder, const FLAC__byte buffer[], size_t bytes, unsigned samples, unsigned current_frame, void *client_data);
	static void stream_encoder_metadata_callback(const FLAC__StreamEncoder *encoder, const FLAC__StreamMetadata *metadata, void *client_data);	
//	Implementation
private:
	virtual	void	SetCompressionLevel(UInt32 theCompressionLevel);
	virtual OSStatus	BuildSettingsDictionary(CFDictionaryRef * theSettings);
	virtual OSStatus	ParseSettingsDictionary(CFDictionaryRef theSettings);
	UInt32 mSupportedChannelTotals[kFLACNumberSupportedChannelTotals];

	UInt32 mInputBufferBytesUsed;

#if TARGET_CPU_PPC || TARGET_CPU_PPC64
	Byte mInputBuffer[kInputBufferPackets * kFLACNumberSupportedChannelTotals * sizeof(SInt32)] __attribute__ ((aligned (16)));
	SInt32 mConvertedBuffer[kInputBufferPackets * kFLACNumberSupportedChannelTotals] __attribute__ ((aligned (16)));
#else
	Byte mInputBuffer[kInputBufferPackets * kFLACNumberSupportedChannelTotals * sizeof(SInt32)];
	SInt32 mConvertedBuffer[kInputBufferPackets * kFLACNumberSupportedChannelTotals];
#endif

	// FLAC encoder parameters
	OSType					mFormat;

	// encoding statistics
	UInt32					mTotalBytesGenerated;	
	static UInt32			mOutputBytes;
	// We need a "global" pointer for the output data callback
	static Byte * mOutputBuffer;
	
	UInt32					mQuality;
	UInt32					mTrailingFrames;
	bool mPacketInInputBuffer;
	bool mFlushPacket;
	bool mFinished;
	FLAC__StreamEncoder * mEncoder;
	FLAC__StreamEncoderState mEncoderState;
};

#endif
