//----------------------------------------------------------------------------------------------------------------------------
//
// "FDHIDActuator.h"
//
// Written by:	Axel 'awe' Wefers			[mailto:awe@fruitz-of-dojo.de].
//				©2001-2012 Fruitz Of Dojo 	[http://www.fruitz-of-dojo.de].
//
//----------------------------------------------------------------------------------------------------------------------------

#import <Cocoa/Cocoa.h>

//----------------------------------------------------------------------------------------------------------------------------
@class FDHIDDevice;

@interface FDHIDActuator : NSObject {
}

- (id)initWithDevice:(FDHIDDevice*)device;
- (void)setIntensity:(float)intensity;
- (float)intensity;

- (void)setDuration:(float)duration;
- (float)duration;

- (BOOL)isActive;

- (void)start;
- (void)stop;

@end

//----------------------------------------------------------------------------------------------------------------------------
