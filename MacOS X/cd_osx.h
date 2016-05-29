//----------------------------------------------------------------------------------------------------------------------------
//
// "cd_osx.h" - MacOS X audio CD driver.
//
// Written by:	Axel 'awe' Wefers			[mailto:awe@fruitz-of-dojo.de].
//				�2001-2012 Fruitz Of Dojo 	[http://www.fruitz-of-dojo.de].
//
// Quake� is copyrighted by id software		[http://www.idsoftware.com].
//
//----------------------------------------------------------------------------------------------------------------------------

#import <Cocoa/Cocoa.h>

//----------------------------------------------------------------------------------------------------------------------------

BOOL CDAudio_ScanForMedia(NSString* folder, NSConditionLock* stopConditionLock);

//----------------------------------------------------------------------------------------------------------------------------
