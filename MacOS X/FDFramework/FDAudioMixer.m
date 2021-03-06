//----------------------------------------------------------------------------------------------------------------------------
//
// "FDAudioMixer.m" - Audio mixer.
//
// Written by:	Axel 'awe' Wefers			[mailto:awe@fruitz-of-dojo.de].
//				©2001-2012 Fruitz Of Dojo 	[http://www.fruitz-of-dojo.de].
//
//----------------------------------------------------------------------------------------------------------------------------

#import "FDAudioInternal.h"
#import "FDAudioMixer.h"
#import "FDDefines.h"

#import <AudioToolbox/AudioToolbox.h>
#import <Cocoa/Cocoa.h>
#import <CoreAudio/CoreAudio.h>

//----------------------------------------------------------------------------------------------------------------------------

static dispatch_once_t sFDAudioMixerPredicate = 0;
static FDAudioMixer* sFDAudioMixerShared = nil;

//----------------------------------------------------------------------------------------------------------------------------

@implementation FDAudioMixer {
@private
    AUGraph mAudioGraph;
    AudioUnit mMixerUnit;
    AUNode mMixerNode;
    NSMutableSet* mBusNumbers;
    NSMutableArray* mObservers;
}

//----------------------------------------------------------------------------------------------------------------------------

+ (FDAudioMixer*)sharedAudioMixer
{
    dispatch_once(&sFDAudioMixerPredicate, ^{
        sFDAudioMixerShared = [[FDAudioMixer alloc] init];
    });

    return sFDAudioMixerShared;
}

- (id)init
{
    self = [super init];

    if (self) {
        AUNode outputNode = 0;
        OSStatus err = NewAUGraph(&mAudioGraph);

        if (err == noErr) {
            AudioComponentDescription outputDesc = { 0 };

            outputDesc.componentType = kAudioUnitType_Output;
            outputDesc.componentSubType = kAudioUnitSubType_DefaultOutput;
            outputDesc.componentManufacturer = kAudioUnitManufacturer_Apple;

            err = AUGraphAddNode(mAudioGraph, &outputDesc, &outputNode);
        }

        if (err == noErr) {
            AudioComponentDescription mixerDesc = { 0 };

            mixerDesc.componentType = kAudioUnitType_Mixer;
            mixerDesc.componentSubType = kAudioUnitSubType_StereoMixer;
            mixerDesc.componentManufacturer = kAudioUnitManufacturer_Apple;

            err = AUGraphAddNode(mAudioGraph, &mixerDesc, &mMixerNode);
        }

        if (err == noErr) {
            err = AUGraphConnectNodeInput(mAudioGraph, mMixerNode, 0, outputNode, 0);
        }

        if (err == noErr) {
            err = AUGraphOpen(mAudioGraph);
        }

        if (err == noErr) {
            err = AUGraphInitialize(mAudioGraph);
        }

        if (err == noErr) {
            err = AUGraphNodeInfo(mAudioGraph, mMixerNode, 0, &mMixerUnit);
        }

        if (err == noErr) {
            mBusNumbers = [[NSMutableSet alloc] init];
            mObservers = [[NSMutableArray alloc] init];
        }

        if (err == noErr) {
            NSNotificationCenter* notificationCenter = [NSNotificationCenter defaultCenter];

            [notificationCenter addObserver:self
                                   selector:@selector(applicationWillHide:)
                                       name:NSApplicationWillHideNotification
                                     object:nil];

            [notificationCenter addObserver:self
                                   selector:@selector(applicationWillUnhide:)
                                       name:NSApplicationWillUnhideNotification
                                     object:nil];
        }

        if (err != noErr) {
            self = nil;
        }
    }

    return self;
}

//----------------------------------------------------------------------------------------------------------------------------

- (void)dealloc
{
    [[NSNotificationCenter defaultCenter] removeObserver:self];

    if (mAudioGraph != nil) {
        AUGraphStop(mAudioGraph);
        DisposeAUGraph(mAudioGraph);
    }

    if (self == sFDAudioMixerShared) {
        sFDAudioMixerShared = nil;
    }
}

//----------------------------------------------------------------------------------------------------------------------------

- (void)setVolume:(float)volume forBus:(AudioUnitElement)busNumber
{
    if (mMixerUnit != 0) {
        if (volume < 0.0f) {
            volume = 0.0f;
        }
        else if (volume > 1.0f) {
            volume = 1.0f;
        }

        AudioUnitSetParameter(mMixerUnit, kStereoMixerParam_Volume, kAudioUnitScope_Input, busNumber, volume, 0);
    }
}

//----------------------------------------------------------------------------------------------------------------------------

- (float)volumeForBus:(AudioUnitElement)busNumber
{
    AudioUnitParameterValue volume = 0.0f;

    AudioUnitGetParameter(mMixerUnit, kStereoMixerParam_Volume, kAudioUnitScope_Input, busNumber, &volume);

    return volume;
}

//----------------------------------------------------------------------------------------------------------------------------

- (AUGraph)audioGraph
{
    return mAudioGraph;
}

//----------------------------------------------------------------------------------------------------------------------------

- (AUNode)mixerNode
{
    return mMixerNode;
}

//----------------------------------------------------------------------------------------------------------------------------

- (void)start
{
    AUGraphStart(mAudioGraph);
}

//----------------------------------------------------------------------------------------------------------------------------

- (void)stop
{
    AUGraphStop(mAudioGraph);
}

//----------------------------------------------------------------------------------------------------------------------------

- (BOOL)isRunning
{
    Boolean isRunning = false;

    if (mAudioGraph != nil) {
        AUGraphIsRunning(mAudioGraph, &isRunning);
    }

    return isRunning;
}

//----------------------------------------------------------------------------------------------------------------------------

- (AudioUnitElement)allocateBus
{
    AudioUnitElement i = 0;

    while (1) {
        NSNumber* busNumber = [[NSNumber alloc] initWithInt:i];

        if ([mBusNumbers containsObject:busNumber] == NO) {
            [mBusNumbers addObject:busNumber];
            break;
        }

        ++i;
    }

    return i;
}

//----------------------------------------------------------------------------------------------------------------------------

- (void)deallocateBus:(AudioUnitElement)busNumber
{
    [mBusNumbers removeObject:[NSNumber numberWithInt:busNumber]];
}

//----------------------------------------------------------------------------------------------------------------------------

- (void)addObserver:(id)object
{
    [mObservers addObject:object];
}

//----------------------------------------------------------------------------------------------------------------------------

- (void)removeObserver:(id)object
{
    [mObservers removeObject:object];
}

//----------------------------------------------------------------------------------------------------------------------------

- (void)applicationWillHide:(NSNotification*)notification
{
    [mObservers makeObjectsPerformSelector:@selector(applicationWillHide:) withObject:notification];

    AUGraphStop(mAudioGraph);
}

//----------------------------------------------------------------------------------------------------------------------------

- (void)applicationWillUnhide:(NSNotification*)notification
{
    [mObservers makeObjectsPerformSelector:@selector(applicationWillUnhide:) withObject:notification];

    AUGraphStart(mAudioGraph);
}

@end

//----------------------------------------------------------------------------------------------------------------------------
