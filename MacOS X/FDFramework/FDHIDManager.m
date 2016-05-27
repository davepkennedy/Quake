//----------------------------------------------------------------------------------------------------------------------------
//
// "FDHIDManager.m"
//
// Written by:	Axel 'awe' Wefers			[mailto:awe@fruitz-of-dojo.de].
//				©2001-2012 Fruitz Of Dojo 	[http://www.fruitz-of-dojo.de].
//
//----------------------------------------------------------------------------------------------------------------------------

#import "FDHIDManager.h"
#import "FDDebug.h"
#import "FDDefines.h"
#import "FDPreferences.h"
#import "FDHIDDevice.h"

#import <IOKit/IOKitLib.h>
#import <IOKit/hidsystem/IOHIDLib.h>
#import <IOKit/hid/IOHIDLib.h>

//----------------------------------------------------------------------------------------------------------------------------

#define FD_HID_DEVICE_GAME_PAD      @"_FDHIDDeviceGamePad"
#define FD_HID_DEVICE_KEYBOARD      @"_FDHIDDeviceKeyboard"
#define FD_HID_DEVICE_MOUSE         @"_FDHIDDeviceMouse"

#define FD_HID_LCC_IDENTIFIER       @"com.Logitech.Control Center.Daemon"
#define FD_HID_LCC_SUPPRESS_WARNING @"LCCSuppressWarning"

//----------------------------------------------------------------------------------------------------------------------------

static NSString*        sDeviceFactories[]      = {
                                                    FD_HID_DEVICE_GAME_PAD,
                                                    FD_HID_DEVICE_KEYBOARD,
                                                    FD_HID_DEVICE_MOUSE
                                                  };

//----------------------------------------------------------------------------------------------------------------------------

NSString*               FDHIDDeviceGamePad      = FD_HID_DEVICE_GAME_PAD;
NSString*               FDHIDDeviceKeyboard     = FD_HID_DEVICE_KEYBOARD;
NSString*               FDHIDDeviceMouse        = FD_HID_DEVICE_MOUSE;

//----------------------------------------------------------------------------------------------------------------------------

static dispatch_once_t  sFDHIDManagerPredicate  = 0;
static FDHIDManager*    sFDHIDManagerInstance   = nil;

//----------------------------------------------------------------------------------------------------------------------------

static void             FDHIDManager_InputHandler (void*, IOReturn, void*, IOHIDValueRef);	
static void             FDHIDManager_DeviceMatchingCallback (void*, IOReturn, void*, IOHIDDeviceRef);
static void             FDHIDManager_DeviceRemovalCallback (void*, IOReturn, void*, IOHIDDeviceRef);

//----------------------------------------------------------------------------------------------------------------------------

//----------------------------------------------------------------------------------------------------------------------------

qvariant_t VARIANT_FLOAT(float f) {
    qvariant_t v = {.mFloatVal = f};
    return v;
}

qvariant_t VARIANT_INT(int i) {
    qvariant_t v = {.mIntVal = i};
    return v;
}

qvariant_t VARIANT_BOOL(BOOL b) {
    qvariant_t v = {.mBoolVal = b};
    return v;
}

@implementation FDHIDEvent

@synthesize mDevice, mType, mButton, mValue, mPadding;

@end

@implementation FDHIDManager
{
@private
    IOHIDManagerRef     mpIOHIDManager;
    NSMutableArray*     mDevices;
    
    // Not perfect - this is now an unbounded list…
    NSMutableArray*     mpEvents;
    NSUInteger          mReadEvent;
    NSUInteger          mWriteEvent;
    NSUInteger          mMaxEvents;
}

//----------------------------------------------------------------------------------------------------------------------------

+ (FDHIDManager*) sharedHIDManager
{
    dispatch_once (&sFDHIDManagerPredicate, ^{ sFDHIDManagerInstance = [[FDHIDManager alloc] initSharedHIDManager]; });
    
    return sFDHIDManagerInstance;
}


- (id) init
{
    self = [super init];
    
    if (self != nil)
    {
        [self doesNotRecognizeSelector: _cmd];
    }
    
    return nil;
}

//----------------------------------------------------------------------------------------------------------------------------

- (id) initSharedHIDManager
{
    self = [super init];
    
    if (self != nil)
    {
        BOOL success = YES;
        
        mpEvents = [NSMutableArray array];
        
        if (success)
        {
            mpIOHIDManager  = IOHIDManagerCreate (kCFAllocatorDefault, kIOHIDManagerOptionNone);
            success         = (mpIOHIDManager != NULL);
        }
        
        if (success)
        {
            success = (IOHIDManagerOpen (mpIOHIDManager, kIOHIDManagerOptionNone) == kIOReturnSuccess);
        }
        
        if (success)
        {
            mDevices = [[NSMutableArray alloc] initWithCapacity: 3];
            success  = (mDevices != nil);
        }
        
        if (success)
        {
            NSNotificationCenter* notificationCenter = [NSNotificationCenter defaultCenter];
            
            [notificationCenter addObserver: self
                                   selector: @selector (applicationWillResignActive:)
                                       name: NSApplicationWillResignActiveNotification
                                     object: nil];
        }
        
        if (!success)
        {
            self = nil;
        }
    }
    
    return self;
}

//----------------------------------------------------------------------------------------------------------------------------

- (void) dealloc
{
    [[NSNotificationCenter defaultCenter] removeObserver: self];
    
    if (mpIOHIDManager)
    {
        IOHIDManagerRegisterDeviceMatchingCallback (mpIOHIDManager, NULL, NULL);
        IOHIDManagerRegisterDeviceRemovalCallback (mpIOHIDManager, NULL, NULL);
        IOHIDManagerUnscheduleFromRunLoop (mpIOHIDManager, CFRunLoopGetCurrent(), kCFRunLoopDefaultMode);
        IOHIDManagerClose (mpIOHIDManager, kIOHIDManagerOptionNone);
    }
    
    sFDHIDManagerInstance = nil;
}

//----------------------------------------------------------------------------------------------------------------------------

- (void) applicationWillResignActive: (NSNotification*) notification
{
    FD_UNUSED (notification);
    
    for (FDHIDDevice* device in mDevices)
    {
        [device flush];
    }
    
    mReadEvent  = 0;
    mWriteEvent = 0;
}

//----------------------------------------------------------------------------------------------------------------------------

- (void) setDeviceFilter: (NSArray*) devices
{
    NSMutableArray* matchingArray = nil;
    
    if (devices != nil)
    {
        matchingArray = [NSMutableArray array];
        
        for (NSString* deviceName in devices)
        {
            Class   device = NSClassFromString (deviceName);
            
            if (device != nil)
            {
                NSArray* dicts = [device matchingDictionaries];
                
                if (dicts != nil)
                {
                    [matchingArray addObjectsFromArray: dicts];
                }
            }
        }
        
        if ([matchingArray count] == 0)
        {
            matchingArray = nil;
        }
    }
    
    IOHIDManagerSetDeviceMatchingMultiple (mpIOHIDManager, (CFMutableArrayRef) CFBridgingRetain(matchingArray));
    IOHIDManagerRegisterDeviceMatchingCallback (mpIOHIDManager, FDHIDManager_DeviceMatchingCallback, CFBridgingRetain(self));
    IOHIDManagerRegisterDeviceRemovalCallback (mpIOHIDManager, FDHIDManager_DeviceRemovalCallback, CFBridgingRetain(self));
    IOHIDManagerScheduleWithRunLoop (mpIOHIDManager, CFRunLoopGetCurrent (), kCFRunLoopDefaultMode);
}

//----------------------------------------------------------------------------------------------------------------------------

- (NSArray*) devices
{
    return mDevices;
}

//----------------------------------------------------------------------------------------------------------------------------

- (const FDHIDEvent*) nextEvent
{
    const FDHIDEvent* pEvent = nil;
    
    if ([mpEvents count] > 0) {
        pEvent = [mpEvents objectAtIndex:0];
        [mpEvents removeObjectAtIndex:0];
    }
    
    return pEvent;
}

//----------------------------------------------------------------------------------------------------------------------------

- (void) pushEvent: (const FDHIDEvent*) pEvent
{
    if ([NSApp isActive] == YES)
    {
        
        [mpEvents addObject:pEvent];
    }
}

//----------------------------------------------------------------------------------------------------------------------------

- (void) registerDevice: (IOHIDDeviceRef) pDevice
{
    for (NSUInteger i = 0; i < FD_SIZE_OF_ARRAY (sDeviceFactories); ++i)
    {
        Class factory = NSClassFromString (sDeviceFactories[i]);
        
        if (factory != nil)
        {
            FD_ASSERT ([factory isSubclassOfClass: [FDHIDDevice class]]);
            FD_ASSERT ([factory respondsToSelector: @selector (deviceWithDevice:)]);
            
            FDHIDDevice* device = [factory deviceWithDevice: pDevice];
            
            if (device != nil)
            {
                [device setDelegate: self];
                [mDevices addObject: device];
                
                IOHIDDeviceRegisterInputValueCallback (pDevice, &FDHIDManager_InputHandler, CFBridgingRetain(device));
                
                break;
            }
        }
    }
}

//----------------------------------------------------------------------------------------------------------------------------

- (void) unregisterDevice: (IOHIDDeviceRef) pDevice
{
    IOHIDDeviceRegisterInputValueCallback (pDevice, NULL, NULL);
    
    for (FDHIDDevice* device in mDevices)
    {
        if ([device iohidDeviceRef] == pDevice)
        {
            [mDevices removeObject: device];
            break;
        }
    }
}

//----------------------------------------------------------------------------------------------------------------------------

+ (void) checkForIncompatibleDevices
{
    [[FDPreferences sharedPrefs] registerDefaultObject: [NSNumber numberWithBool: NO] forKey: FD_HID_LCC_SUPPRESS_WARNING];
    
    // check for Logitech Control Center. LCC installs its own kext and blocks HID events from Logitech devices
    if ([[NSWorkspace sharedWorkspace] absolutePathForAppBundleWithIdentifier: FD_HID_LCC_IDENTIFIER] != nil)
    {
        if ([[FDPreferences sharedPrefs] boolForKey: FD_HID_LCC_SUPPRESS_WARNING] == NO)
        {
            NSAlert*    alert   = [[NSAlert alloc] init];
            NSString*   appName = [[NSRunningApplication currentApplication] localizedName];
            NSString*   message = [NSString stringWithFormat: @"An installation of the Logitech Control Center software "
                                                              @"has been detected. This software is not compatible with %@.",
                                                              appName];
            NSString*   informative = [NSString stringWithFormat: @"Please uninstall the Logitech Control Center software "
                                                                  @"if you want to use a Logitech input device with %@.",
                                                                  appName];
            
            [alert setMessageText: message];
            [alert setInformativeText: informative];
            [alert setAlertStyle: NSCriticalAlertStyle];
            [alert setShowsSuppressionButton: YES];
            [alert runModal];
            
            [[FDPreferences sharedPrefs] setObject: [alert suppressionButton] forKey: FD_HID_LCC_SUPPRESS_WARNING];
        }
    }
    else
    {
        // reset the warning in case LCC was uninstalled
        [[FDPreferences sharedPrefs] setObject: [NSNumber numberWithBool: NO] forKey: FD_HID_LCC_SUPPRESS_WARNING];
    }
}

@end

//----------------------------------------------------------------------------------------------------------------------------

void FDHIDManager_InputHandler (void* pContext, IOReturn result, void* pSender, IOHIDValueRef pValue)
{
    FD_UNUSED (result, pSender);
    FD_ASSERT (pContext != nil);
    FD_ASSERT (pValue != nil);
    
    if ([NSApp isActive] == YES)
    {
        FDHIDDevice* device = (__bridge FDHIDDevice*) pContext;
        
        [device handleInput: pValue];
    }
}

//----------------------------------------------------------------------------------------------------------------------------

void FDHIDManager_DeviceMatchingCallback (void* pContext, IOReturn result, void* pSender, IOHIDDeviceRef pDevice)
{
    FD_UNUSED (result, pSender);
    FD_ASSERT (pContext == (__bridge void *)(sFDHIDManagerInstance));
    FD_ASSERT (pDevice != nil);

    [((__bridge FDHIDManager*) pContext) registerDevice: pDevice];
}

//----------------------------------------------------------------------------------------------------------------------------

void FDHIDManager_DeviceRemovalCallback (void* pContext, IOReturn result, void* pSender, IOHIDDeviceRef pDevice) 
{
    FD_UNUSED (result, pSender);
    FD_ASSERT (pContext == (__bridge void *)(sFDHIDManagerInstance));
    FD_ASSERT (pDevice != nil);
    
    [((__bridge FDHIDManager*) pContext) unregisterDevice: pDevice];
}

//----------------------------------------------------------------------------------------------------------------------------
