//----------------------------------------------------------------------------------------------------------------------------
//
// "FDHIDDevice.m"
//
// Written by:	Axel 'awe' Wefers			[mailto:awe@fruitz-of-dojo.de].
//				©2001-2012 Fruitz Of Dojo 	[http://www.fruitz-of-dojo.de].
//
//----------------------------------------------------------------------------------------------------------------------------

#import "FDDebug.h"
#import "FDDefines.h"
#import "FDHIDActuator.h"
#import "FDHIDDevice.h"

#import <Cocoa/Cocoa.h>
#import <IOKit/hid/IOHIDLib.h>
#import <IOKit/hidsystem/IOHIDLib.h>

//----------------------------------------------------------------------------------------------------------------------------

@interface FDHIDManager ()

- (void)pushEvent:(const FDHIDEvent*)pEvent;

@end

//----------------------------------------------------------------------------------------------------------------------------

@implementation FDHIDDevice {
    IOHIDDeviceRef mpIOHIDDevice;
    NSString* mVendorName;
    NSString* mProductName;
    FDHIDActuator* mActuator;
    const FDHIDDeviceDesc* mpDeviceDesc;
    FDHIDManager* mDelegate;
}

//----------------------------------------------------------------------------------------------------------------------------

- (BOOL)hasActuator;
{
    return [self actuator] != nil;
}

//----------------------------------------------------------------------------------------------------------------------------

+ (NSDictionary*)matchingDictionarForUsageMap:(const FDHIDUsageToDevice*)pUsageMap
{
    FD_ASSERT(pUsageMap);

    NSString* pageKey = [NSString stringWithCString:kIOHIDPrimaryUsagePageKey encoding:NSASCIIStringEncoding];
    NSString* usageKey = [NSString stringWithCString:kIOHIDPrimaryUsageKey encoding:NSASCIIStringEncoding];
    NSNumber* pageVal = [NSNumber numberWithInt:pUsageMap->mUsagePage];
    NSNumber* usageVal = [NSNumber numberWithInt:pUsageMap->mUsage];

    return [NSDictionary dictionaryWithObjectsAndKeys:pageVal, pageKey, usageVal, usageKey, nil];
}

//----------------------------------------------------------------------------------------------------------------------------

+ (NSArray*)matchingDictionaries
{
    [self doesNotRecognizeSelector:_cmd];
    return nil;
}

+ (NSArray*)matchingDictionaries:(const FDHIDUsageToDevice*)pUsageMap withCount:(NSUInteger)numUsages
{
    FD_ASSERT(pUsageMap);

    NSMutableArray* dictionaries = [NSMutableArray arrayWithCapacity:numUsages];

    for (NSUInteger i = 0; i < numUsages; ++i) {
        [dictionaries addObject:[FDHIDDevice matchingDictionarForUsageMap:&(pUsageMap[i])]];
    }

    return dictionaries;
}

//----------------------------------------------------------------------------------------------------------------------------

+ (FDHIDDevice*)deviceWithDevice:(IOHIDDeviceRef)pDevice
                        usageMap:(const FDHIDUsageToDevice*)pUsageMap
                           count:(NSUInteger)numUsages
{
    FD_ASSERT(pDevice);
    FD_ASSERT(pUsageMap);

    FDHIDDevice* device = nil;

    for (NSUInteger i = 0; i < numUsages; ++i) {
        if (IOHIDDeviceConformsTo(pDevice, pUsageMap[i].mUsagePage, pUsageMap[i].mUsage)) {
            device = [[[self class] alloc] initWithDevice:pDevice deviceDescriptors:pUsageMap[i].mDeviceDesc];

            break;
        }
    }

    return device;
}

//----------------------------------------------------------------------------------------------------------------------------

- (id)initWithDevice:(IOHIDDeviceRef)pDevice deviceDescriptors:(const FDHIDDeviceDesc*)pDeviceDesc
{
    self = [super init];

    if (self != nil) {
        if (pDeviceDesc != nil) {
            mpIOHIDDevice = pDevice;

            mVendorName = [self getDevicePropertyStringForKey:CFSTR(kIOHIDManufacturerKey)];
            mProductName = [self getDevicePropertyStringForKey:CFSTR(kIOHIDProductKey)];

            FDLog(@"Found %@ by %@\n", mProductName, mVendorName);

            const uint32_t vendorId = [self getDevicePropertyForKey:CFSTR(kIOHIDVendorIDKey)];
            const uint32_t productId = [self getDevicePropertyForKey:CFSTR(kIOHIDProductIDKey)];

            while (1) {
                if ((pDeviceDesc->mVendorId == -1) && (pDeviceDesc->mProductId == -1)) {
                    break;
                }

                if ((pDeviceDesc->mVendorId == vendorId) && (pDeviceDesc->mProductId == productId)) {
                    break;
                }

                ++pDeviceDesc;
            }

            mpDeviceDesc = pDeviceDesc;
            mActuator = [[FDHIDActuator alloc] initWithDevice:self];
        }
        else {
            self = nil;
        }
    }

    return self;
}

//----------------------------------------------------------------------------------------------------------------------------

- (void)dealloc
{
    FDLog(@"Lost %@ by %@\n", [self productName], [self vendorName]);
}

//----------------------------------------------------------------------------------------------------------------------------

- (void)setDelegate:(FDHIDManager*)delegate;
{
    mDelegate = delegate;
}

//----------------------------------------------------------------------------------------------------------------------------

- (void)pushEvent:(const FDHIDEvent*)pEvent
{
    if (mDelegate != nil) {
        [mDelegate pushEvent:pEvent];
    }
}

//----------------------------------------------------------------------------------------------------------------------------

- (NSUInteger)vendorId
{
    return mpDeviceDesc->mVendorId;
}

//----------------------------------------------------------------------------------------------------------------------------

- (NSUInteger)productId
{
    return mpDeviceDesc->mProductId;
}

//----------------------------------------------------------------------------------------------------------------------------

- (NSString*)vendorName
{
    return mVendorName;
}

//----------------------------------------------------------------------------------------------------------------------------

- (NSString*)productName
{
    return mProductName;
}

//----------------------------------------------------------------------------------------------------------------------------

- (NSString*)deviceType
{
    return NSStringFromClass([self class]);
}

//----------------------------------------------------------------------------------------------------------------------------

- (IOHIDDeviceRef)iohidDeviceRef
{
    return mpIOHIDDevice;
}

//----------------------------------------------------------------------------------------------------------------------------

- (uint32_t)getDevicePropertyForKey:(CFStringRef)pKey
{
    IOHIDDeviceRef pDevice = [self iohidDeviceRef];
    BOOL success = (pDevice != nil);
    CFTypeRef pProperty = NULL;
    SInt32 value = -1;

    if (success) {
        pProperty = IOHIDDeviceGetProperty(pDevice, pKey);
        success = (pProperty != NULL);
    }

    if (success) {
        success = (CFNumberGetTypeID() == CFGetTypeID(pProperty));
    }

    if (success) {
        success = CFNumberGetValue((CFNumberRef)pProperty, kCFNumberSInt32Type, &value);
    }
    else {
        value = -1;
    }

    return value;
}

//----------------------------------------------------------------------------------------------------------------------------

- (NSString*)getDevicePropertyStringForKey:(CFStringRef)pKey
{
    IOHIDDeviceRef pDevice = [self iohidDeviceRef];
    BOOL success = (pDevice != nil);
    CFTypeRef pProperty = nil;
    NSString* string = nil;

    if (success) {
        pProperty = IOHIDDeviceGetProperty(pDevice, pKey);
        success = (pProperty != NULL);
    }

    if (success) {
        success = (CFStringGetTypeID() == CFGetTypeID(pProperty));
    }

    if (success) {
        string = (__bridge NSString*)pProperty;
        string = [string copy];
    }

    return string;
}

//----------------------------------------------------------------------------------------------------------------------------

- (void)handleInput:(IOHIDValueRef)pValue
{
    IOHIDElementRef pElement = IOHIDValueGetElement(pValue);
    const uint32_t type = IOHIDElementGetType(pElement);
    const FDHIDElementMap* pElements = [self elementMap];
    const uint32_t typeOffset = type - pElements[0].mType;

    if (typeOffset < [self elementCount]) {
        pElements = &(pElements[typeOffset]);

        FD_ASSERT(pElements->mType == type);

        if (pElements->mpButtons) {
            const uint32_t usage = IOHIDElementGetUsage(pElement);
            const uint32_t usageOffset = usage - pElements->mpButtons[0].mUsage;

            if (usageOffset < pElements->mNumButtons) {
                const FDHIDButtonMap* pButton = &(pElements->mpButtons[usageOffset]);

                FD_ASSERT(pButton->mUsage == usage);

                if (pButton->mpEventHandler) {
                    pButton->mpEventHandler(self, pButton->mButton, pValue, pElement);
                }
            }
        }
    }
}

//----------------------------------------------------------------------------------------------------------------------------

- (FDHIDElementMap*)elementMap
{
    return mpDeviceDesc->mpElements;
}

//----------------------------------------------------------------------------------------------------------------------------

- (NSUInteger)elementCount
{
    return mpDeviceDesc->mNumElements;
}

//----------------------------------------------------------------------------------------------------------------------------

- (void)flush
{
}

//----------------------------------------------------------------------------------------------------------------------------

- (FDHIDActuator*)actuator
{
    return mActuator;
}

+ (FDHIDDevice*)deviceWithDevice:(IOHIDDeviceRef)pDevice
{
    [self doesNotRecognizeSelector:_cmd];
    return nil;
}

@end

//----------------------------------------------------------------------------------------------------------------------------
