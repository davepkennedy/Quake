//----------------------------------------------------------------------------------------------------------------------------
//
// "QMediaScan.h"
//
// Written by:	Axel 'awe' Wefers			[mailto:awe@fruitz-of-dojo.de].
//				©2001-2012 Fruitz Of Dojo 	[http://www.fruitz-of-dojo.de].
//
//----------------------------------------------------------------------------------------------------------------------------

#import <Cocoa/Cocoa.h>

//----------------------------------------------------------------------------------------------------------------------------

@interface QMediaScan : NSWindowController
{
@private
    IBOutlet NSTextField*           mTextField;
    IBOutlet NSProgressIndicator*   mProgressIndicator;
    
    NSConditionLock*                mStopConditionLock;
    NSString*                       mFolder;
}

+ (BOOL) scanFolder: (NSString*) folder callback:(void(^)(void)) callback;

- (IBAction) stop: (id) sender;

@end

//----------------------------------------------------------------------------------------------------------------------------
