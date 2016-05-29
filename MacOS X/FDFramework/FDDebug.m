//----------------------------------------------------------------------------------------------------------------------------
//
// "FDDebug.m"
//
// Written by:	Axel 'awe' Wefers			[mailto:awe@fruitz-of-dojo.de].
//				©2001-2012 Fruitz Of Dojo 	[http://www.fruitz-of-dojo.de].
//
//----------------------------------------------------------------------------------------------------------------------------

#import "FDAlertPanel.h"
#import "FDDebug.h"
#import "FDDefines.h"

#import <Cocoa/Cocoa.h>
#import <stdarg.h>
#import <sys/sysctl.h>

//----------------------------------------------------------------------------------------------------------------------------

static dispatch_once_t sFDDebugPredicate = 0;
static FDDebug* sFDDebugInstance = nil;
static NSString* sFDDebugDefaultName = @"";

//----------------------------------------------------------------------------------------------------------------------------

@implementation FDDebug {
@private
    NSString* mName;
    NSString* mLogPrefix;
    FDDebugAssertHandler mpAssertHandler;
    FDDebugErrorHandler mpErrorHandler;
    FDDebugExceptionHandler mpExceptionHandler;
    FDDebugLogHandler mpLogHandler;
}

//----------------------------------------------------------------------------------------------------------------------------

+ (BOOL)isDebuggerAttached
{
    BOOL isAttached = NO;
    int mib[4] = { CTL_KERN, KERN_PROC, KERN_PROC_PID, getpid() };
    struct kinfo_proc info = { 0 };
    size_t size = sizeof(info);

    if (sysctl(mib, FD_SIZE_OF_ARRAY(mib), &info, &size, NULL, 0) == 0) {
        isAttached = ((info.kp_proc.p_flag & P_TRACED) != 0);
    }

    return isAttached;
}

//----------------------------------------------------------------------------------------------------------------------------

+ (FDDebug*)sharedDebug
{
    dispatch_once(&sFDDebugPredicate, ^{
        sFDDebugInstance = [[FDDebug alloc] initWithName:sFDDebugDefaultName];
    });

    return sFDDebugInstance;
}

//----------------------------------------------------------------------------------------------------------------------------

- (id)init
{
    self = [super init];

    if (self != nil) {
        [self setName:sFDDebugDefaultName];
    }

    return self;
}

//----------------------------------------------------------------------------------------------------------------------------

- (id)initWithName:(NSString*)name
{
    self = [super init];

    if (self != nil) {
        [self setName:name];
    }

    return self;
}

- (void)dealloc
{

    if (self == sFDDebugInstance) {
        sFDDebugInstance = nil;
    }
}

//----------------------------------------------------------------------------------------------------------------------------

- (void)setName:(NSString*)name
{

    mName = [[NSString alloc] initWithString:name];

    if ([name length]) {
        mLogPrefix = [[NSString alloc] initWithFormat:@"[%@] ", name];
    }
    else {
        mLogPrefix = [[NSString alloc] init];
    }
}

//----------------------------------------------------------------------------------------------------------------------------

- (NSString*)name
{
    return [NSString stringWithString:mName];
}

//----------------------------------------------------------------------------------------------------------------------------

- (void)setAssertHandler:(FDDebugAssertHandler)assertHandler
{
    mpAssertHandler = assertHandler;
}

//----------------------------------------------------------------------------------------------------------------------------

- (void)setErrorHandler:(FDDebugErrorHandler)errorHandler
{
    mpErrorHandler = errorHandler;
}

//----------------------------------------------------------------------------------------------------------------------------

- (void)setExceptionHandler:(FDDebugExceptionHandler)exceptionHandler
{
    mpExceptionHandler = exceptionHandler;
}

//----------------------------------------------------------------------------------------------------------------------------

- (void)setLogHandler:(FDDebugLogHandler)logHandler
{
    mpLogHandler = logHandler;
}

//----------------------------------------------------------------------------------------------------------------------------

- (void)logWithFormat:(NSString*)format arguments:(va_list)argList
{
    NSString* msg = [[NSString alloc] initWithFormat:format arguments:argList];

    if (mpLogHandler) {
        mpLogHandler([msg cStringUsingEncoding:NSUTF8StringEncoding]);
    }
    else {
        NSLog(@"%@%@", mLogPrefix, msg);
    }
}

//----------------------------------------------------------------------------------------------------------------------------

- (void)errorWithFormat:(NSString*)format arguments:(va_list)argList
{
    NSString* msg = [[NSString alloc] initWithFormat:format arguments:argList];

    if (mpErrorHandler) {
        mpErrorHandler([msg cStringUsingEncoding:NSUTF8StringEncoding]);
    }
    else {
        NSLog(@"%@An error has occured: %@\n", mLogPrefix, msg);
        FDRunCriticalAlertPanel(@"An error has ocurred:", nil, @"%@", msg);
    }

    exit(EXIT_FAILURE);
}

//---------------------------------------------------------------------------------------------------------------------------

- (void)exceptionWithFormat:(NSString*)format arguments:(va_list)argList
{
    NSString* msg = [[NSString alloc] initWithFormat:format arguments:argList];

    if (mpExceptionHandler) {
        mpExceptionHandler([msg cStringUsingEncoding:NSUTF8StringEncoding]);
    }
    else {
        NSLog(@"%@An exception has occured: %@\n", mLogPrefix, msg);
        FDRunCriticalAlertPanel(@"An exception has occured:", nil, @"%@", msg);
    }
}

//---------------------------------------------------------------------------------------------------------------------------

- (BOOL)assert:(NSString*)file line:(NSUInteger)line format:(NSString*)format arguments:(va_list)argList
{
    NSString* msg = [[NSString alloc] initWithFormat:format arguments:argList];
    BOOL resume = NO;

    if ([FDDebug isDebuggerAttached] == NO) {
        if (mpAssertHandler) {
            const char* pFile = [file cStringUsingEncoding:NSUTF8StringEncoding];
            const char* pMsg = [msg cStringUsingEncoding:NSUTF8StringEncoding];

            resume = mpAssertHandler(pFile, (unsigned int)line, pMsg);
        }
        else {
            NSString* dlg = [[NSString alloc] initWithFormat:@"\"%@\" (%lu): %@", file, (unsigned long)line, msg];

            NSLog(@"%@%@ (%d): Assertion failed: %@", mLogPrefix, file, (unsigned int)line, msg);

            resume = (FDRunCriticalAlertPanel(@"Assertion failed:", @[ @"Resume", @"Crash" ], @"%@", dlg) == NSAlertFirstButtonReturn);
        }
    }

    return resume;
}

//----------------------------------------------------------------------------------------------------------------------------

- (void)logWithFormat:(NSString*)format, ...
{
    va_list argList;

    va_start(argList, format);

    [self logWithFormat:format arguments:argList];

    va_end(argList);
}

//----------------------------------------------------------------------------------------------------------------------------

- (void)errorWithFormat:(NSString*)format, ...
{
    va_list argList;

    va_start(argList, format);

    [self errorWithFormat:format arguments:argList];

    va_end(argList);
}

//----------------------------------------------------------------------------------------------------------------------------

- (void)exception:(NSException*)exception
{
    NSString* reason = [exception reason];

    if (reason == nil) {
        reason = @"Unknown exception!";
    }

    [self exceptionWithFormat:reason];
}

//----------------------------------------------------------------------------------------------------------------------------

- (void)exceptionWithFormat:(NSString*)format, ...
{
    va_list argList;

    va_start(argList, format);

    [self errorWithFormat:format arguments:argList];

    va_end(argList);
}

//----------------------------------------------------------------------------------------------------------------------------

- (BOOL)assert:(NSString*)file line:(NSUInteger)line format:(NSString*)format, ...
{
    BOOL resume = NO;

    va_list argList;

    va_start(argList, format);

    resume = [self assert:file line:line format:format arguments:argList];

    va_end(argList);

    return resume;
}

@end

//----------------------------------------------------------------------------------------------------------------------------

void FDLog(NSString* format, ...)
{
    va_list argList;

    va_start(argList, format);

    [[FDDebug sharedDebug] logWithFormat:format arguments:argList];

    va_end(argList);
}

//----------------------------------------------------------------------------------------------------------------------------

void FDError(NSString* format, ...)
{
    va_list argList;

    va_start(argList, format);

    [[FDDebug sharedDebug] errorWithFormat:format arguments:argList];

    va_end(argList);
}

//---------------------------------------------------------------------------------------------------------------------------
