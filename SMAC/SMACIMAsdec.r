/*	Copyright: 	� Copyright 2003 Apple Computer, Inc. All rights reserved.

	Disclaimer:	IMPORTANT:  This Apple software is supplied to you by Apple Computer, Inc.
			("Apple") in consideration of your agreement to the following terms, and your
			use, installation, modification or redistribution of this Apple software
			constitutes acceptance of these terms.  If you do not agree with these terms,
			please do not use, install, modify or redistribute this Apple software.

			In consideration of your agreement to abide by the following terms, and subject
			to these terms, Apple grants you a personal, non-exclusive license, under Apple�s
			copyrights in this original Apple software (the "Apple Software"), to use,
			reproduce, modify and redistribute the Apple Software, with or without
			modifications, in source and/or binary forms; provided that if you redistribute
			the Apple Software in its entirety and without modifications, you must retain
			this notice and the following text and disclaimers in all such redistributions of
			the Apple Software.  Neither the name, trademarks, service marks or logos of
			Apple Computer, Inc. may be used to endorse or promote products derived from the
			Apple Software without specific prior written permission from Apple.  Except as
			expressly stated in this notice, no other rights or licenses, express or implied,
			are granted by Apple herein, including but not limited to any patent rights that
			may be infringed by your derivative works or by other works in which the Apple
			Software may be incorporated.

			The Apple Software is provided by Apple on an "AS IS" basis.  APPLE MAKES NO
			WARRANTIES, EXPRESS OR IMPLIED, INCLUDING WITHOUT LIMITATION THE IMPLIED
			WARRANTIES OF NON-INFRINGEMENT, MERCHANTABILITY AND FITNESS FOR A PARTICULAR
			PURPOSE, REGARDING THE APPLE SOFTWARE OR ITS USE AND OPERATION ALONE OR IN
			COMBINATION WITH YOUR PRODUCTS.

			IN NO EVENT SHALL APPLE BE LIABLE FOR ANY SPECIAL, INDIRECT, INCIDENTAL OR
			CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
			GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
			ARISING IN ANY WAY OUT OF THE USE, REPRODUCTION, MODIFICATION AND/OR DISTRIBUTION
			OF THE APPLE SOFTWARE, HOWEVER CAUSED AND WHETHER UNDER THEORY OF CONTRACT, TORT
			(INCLUDING NEGLIGENCE), STRICT LIABILITY OR OTHERWISE, EVEN IF APPLE HAS BEEN
			ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/
/*=============================================================================
	SMACIMAsdec.r

=============================================================================*/

#define thng_RezTemplateVersion	2

//=============================================================================
//	Includes
//=============================================================================

#include "ConditionalMacros.r"
#include "MacTypes.r"
#include "Components.r"
#include "Sound.r"

#include "SMACResIDs.h"

//=============================================================================
//	Platform constants for the thng resources
//=============================================================================

#if !defined(ppc_YES) 
	#define ppc_YES 	0
#endif
#if !defined(RC_ppc_YES)
	#define RC_ppc_YES	0
#endif
#if ppc_YES || RC_ppc_YES
	#undef TARGET_REZ_MAC_PPC
	#define TARGET_REZ_MAC_PPC		1
#endif

#if TARGET_OS_MAC
	#if TARGET_REZ_CARBON_MACHO
		#define Target_PlatformType		platformPowerPCNativeEntryPoint
		#define Target_CodeResType		'dlle'
		#define TARGET_REZ_USE_DLLE		1
	#else
		error: you must use a valid platform spec
	#endif
#elif TARGET_OS_WIN32
	#define Target_PlatformType      platformWin32
	#define Target_CodeResType		'dlle'
	#define TARGET_REZ_USE_DLLE		1
#else
	#error platform unsupported
#endif // not TARGET_OS_MAC

#ifndef cmpThreadSafeOnMac	// so we don't need Panther headers to build
	#define cmpThreadSafeOnMac	0x10000000
#endif

#ifdef DEBUG
#define kSMACIMAsdecVersion			(0x70ff0004)
#else
#define kSMACIMAsdecVersion			(0x00020004)
#endif
//=============================================================================
//	Resources
//=============================================================================

#define kSMACIMAsdecThingFlags (k16BitIn | kStereoIn | k16BitOut | kStereoOut | kVMAwareness | cmpThreadSafeOnMac)
resource 'thng' (kIMA4DecompResID, "", purgeable)
{
	'sdec', 'ima4', 'appl',
	0, 0, 0, 0,
	'STR ', kIMA4DecompResID,
	'STR ', kIMA4StringResID,
	'ICON', -16557,
	kSMACIMAsdecVersion,
	componentHasMultiplePlatforms | componentDoAutoVersion | componentLoadResident,
	0,
	{
		kSMACIMAsdecThingFlags, Target_CodeResType, kIMA4DecompResID, Target_PlatformType,
	},
	0,
	0
};


#if TARGET_REZ_USE_DLLE
resource 'dlle' (kIMA4DecompResID)
{
	"SMACIMAsdecDispatch"
};
#endif

resource 'STR ' (kIMA4DecompResID, "", purgeable)
{
	"Apple IMA4 Audio"
};

resource 'STR ' (kIMA4StringResID, "", purgeable)
{
	"IMA4 Audio Decompressor component"
};
