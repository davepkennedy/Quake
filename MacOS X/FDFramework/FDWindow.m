//----------------------------------------------------------------------------------------------------------------------------
//
// "FDWindow.h"
//
// Written by:	Axel 'awe' Wefers			[mailto:awe@fruitz-of-dojo.de].
//				©2001-2012 Fruitz Of Dojo 	[http://www.fruitz-of-dojo.de].
//
//----------------------------------------------------------------------------------------------------------------------------

#import "FDDebug.h"
#import "FDDisplay.h"
#import "FDDisplayMode.h"
#import "FDView.h"
#import "FDWindow.h"

#import <Cocoa/Cocoa.h>

//----------------------------------------------------------------------------------------------------------------------------

#define FD_MINI_ICON_WIDTH (128)
#define FD_MINI_ICON_HEIGHT (128)

//----------------------------------------------------------------------------------------------------------------------------

@interface FDView ()

- (void)setResizeHandler:(FDResizeHandler)pResizeHandler forContext:(void*)pContext;
- (void)setOpenGLContext:(NSOpenGLContext*)openGLContext;
- (NSBitmapImageRep*)bitmapRepresentation;
- (void)drawGrowbox;

@end

//----------------------------------------------------------------------------------------------------------------------------

@implementation FDWindow {
@private
    NSImage* mMiniImage;
    NSCursor* mInvisibleCursor;
    FDView* mView;
    FDDisplay* mDisplay;
    BOOL mForceCusorVisible;
    BOOL mIsCursorVisible;
}

- (id)initWithContentRect:(NSRect)rect
{
    return [self initWithContentRect:rect samples:0];
}

//----------------------------------------------------------------------------------------------------------------------------

- (id)initForDisplay:(FDDisplay*)display samples:(NSUInteger)samples
{
    self = [super initWithContentRect:[display frame]
                            styleMask:NSBorderlessWindowMask
                              backing:NSBackingStoreBuffered
                                defer:NO];

    if (self != nil) {
        const NSUInteger bitsPerPixel = [[display displayMode] bitsPerPixel];
        const NSRect frameRect = [[self contentView] frame];
        NSOpenGLContext* glContext = [self createGLContextWithBitsPerPixel:bitsPerPixel samples:samples];

        mView = [[FDView alloc] initWithFrame:frameRect];
        mDisplay = display;

        [self initCursor];
        [self setContentView:mView];
        [self setLevel:CGShieldingWindowLevel()];
        [self setOpaque:YES];
        [self setHidesOnDeactivate:YES];
        [self setBackgroundColor:[NSColor blackColor]];
        [self setAcceptsMouseMovedEvents:YES];
        [self disableScreenUpdatesUntilFlush];
        [self setCursorVisible:NO];

        [mView setOpenGLContext:glContext];
        [mView setNeedsDisplay:YES];
    }

    return self;
}

//----------------------------------------------------------------------------------------------------------------------------

- (id)initForDisplay:(FDDisplay*)display
{
    return [self initForDisplay:display samples:0];
}

//----------------------------------------------------------------------------------------------------------------------------

- (id)initWithContentRect:(NSRect)rect samples:(NSUInteger)samples
{
    self = [super initWithContentRect:rect
                            styleMask:NSTitledWindowMask | NSClosableWindowMask | NSMiniaturizableWindowMask | NSResizableWindowMask
                              backing:NSBackingStoreBuffered
                                defer:NO];

    if (self != nil) {
        const NSUInteger bitsPerPixel = NSBitsPerPixelFromDepth([[self screen] depth]);
        NSOpenGLContext* glContext = [self createGLContextWithBitsPerPixel:bitsPerPixel samples:samples];

        mView = [[FDView alloc] initWithFrame:rect];

        [self initCursor];
        [self setDocumentEdited:YES];
        [self setMinSize:rect.size];
        [self setContentAspectRatio:rect.size];
        [self setShowsResizeIndicator:NO];
        [self setAcceptsMouseMovedEvents:YES];
        [self setBackgroundColor:[NSColor blackColor]];
        [self setContentView:mView];
        [self useOptimizedDrawing:NO];
        [self makeFirstResponder:mView];

        [self center];

        [mView setOpenGLContext:glContext];

        mMiniImage = [self createMiniImageWithSize:NSMakeSize(FD_MINI_ICON_WIDTH, FD_MINI_ICON_HEIGHT)];

        [[NSNotificationCenter defaultCenter] addObserver:self
                                                 selector:@selector(screenParametersDidChange:)
                                                     name:NSApplicationDidChangeScreenParametersNotification
                                                   object:nil];
    }

    return self;
}

//----------------------------------------------------------------------------------------------------------------------------

- (void)initCursor
{
    NSImage* image = [[NSImage alloc] initWithSize:NSMakeSize(16.0f, 16.0f)];

    mInvisibleCursor = [[NSCursor alloc] initWithImage:image hotSpot:NSMakePoint(8.0f, 8.0f)];

    [mInvisibleCursor setOnMouseEntered:YES];

    mIsCursorVisible = YES;
    mForceCusorVisible = NO;
}

//----------------------------------------------------------------------------------------------------------------------------

- (void)dealloc
{
    [[NSNotificationCenter defaultCenter] removeObserver:self
                                                    name:NSApplicationDidChangeScreenParametersNotification
                                                  object:nil];

    [self setCursorVisible:YES];
}

//----------------------------------------------------------------------------------------------------------------------------

- (void)setResizeHandler:(FDResizeHandler)pResizeHandler forContext:(void*)pContext
{
    [mView setResizeHandler:pResizeHandler forContext:pContext];
}

//----------------------------------------------------------------------------------------------------------------------------

- (NSOpenGLContext*)openGLContext
{
    return [mView openGLContext];
}

//----------------------------------------------------------------------------------------------------------------------------

- (void)centerForDisplay:(FDDisplay*)display
{
    const NSRect displayRect = [display frame];
    const NSRect windowRect = [self frame];
    NSPoint origin;

    origin.x = NSMidX(displayRect) - NSWidth(windowRect) * 0.5f;
    origin.y = NSMidY(displayRect) - NSHeight(windowRect) * 0.5f;

    [self setFrameOrigin:origin];
}

//----------------------------------------------------------------------------------------------------------------------------

- (void)updateCursor
{
    BOOL isVisible = mForceCusorVisible;

    if (!isVisible) {
        isVisible = mIsCursorVisible;
    }

    CGAssociateMouseAndMouseCursorPosition(isVisible);

    if (isVisible == YES) {
        [mView setCursor:[NSCursor arrowCursor]];
    }
    else {
        const NSRect nsRect = [self frame];
        const CGRect cgRect = CGDisplayBounds(CGMainDisplayID());
        const NSPoint nsCenter = NSMakePoint(NSMidX(nsRect), NSMidY(nsRect));
        const CGPoint cgCenter = CGPointMake(nsCenter.x, cgRect.size.height - nsCenter.y);

        [mView setCursor:mInvisibleCursor];

        CGWarpMouseCursorPosition(cgCenter);
    }
}

//----------------------------------------------------------------------------------------------------------------------------

- (void)setCursorVisible:(BOOL)state
{
    mIsCursorVisible = state;

    [self updateCursor];
}

//----------------------------------------------------------------------------------------------------------------------------

- (BOOL)isCursorVisible
{
    return mIsCursorVisible;
}

//----------------------------------------------------------------------------------------------------------------------------

- (void)setVsync:(BOOL)enabled
{
    [mView setVsync:enabled];
}

//----------------------------------------------------------------------------------------------------------------------------

- (BOOL)vsync
{
    return [mView vsync];
}

//----------------------------------------------------------------------------------------------------------------------------

- (BOOL)isFullscreen
{
    return mDisplay != nil;
}

//----------------------------------------------------------------------------------------------------------------------------

- (void)endFrame
{
    if ([self isMiniaturized] == YES) {
        [self drawMiniImage];
    }
    else {
        if ([self isFullscreen] == NO) {
            [mView drawGrowbox];
        }

        CGLFlushDrawable([[self openGLContext] CGLContextObj]);
    }
}

//----------------------------------------------------------------------------------------------------------------------------

- (BOOL)acceptsFirstResponder
{
    return YES;
}

//----------------------------------------------------------------------------------------------------------------------------

- (BOOL)canBecomeMainWindow
{
    return YES;
}

//----------------------------------------------------------------------------------------------------------------------------

- (BOOL)canBecomeKeyWindow
{
    return YES;
}

//----------------------------------------------------------------------------------------------------------------------------

- (BOOL)canHide
{
    return YES;
}

//----------------------------------------------------------------------------------------------------------------------------

- (BOOL)windowShouldClose:(id)sender
{
    const BOOL shouldClose = [self isFullscreen];

    if (shouldClose == NO) {
        [NSApp terminate:nil];
    }

    return shouldClose;
}

//----------------------------------------------------------------------------------------------------------------------------

- (NSOpenGLPixelFormat*)createGLPixelFormatWithBitsPerPixel:(NSUInteger)bitsPerPixel samples:(NSUInteger)samples
{
    NSOpenGLPixelFormat* pixelFormat = nil;
    NSOpenGLPixelFormatAttribute attributes[32];
    UInt16 i = 0;

    if (bitsPerPixel != 16) {
        bitsPerPixel = 32;
    }

    attributes[i++] = NSOpenGLPFANoRecovery;

    attributes[i++] = NSOpenGLPFAClosestPolicy;

    attributes[i++] = NSOpenGLPFAAccelerated;

    attributes[i++] = NSOpenGLPFADoubleBuffer;

    attributes[i++] = NSOpenGLPFADepthSize;
    attributes[i++] = 1;

    attributes[i++] = NSOpenGLPFAAlphaSize;
    attributes[i++] = 0;

    attributes[i++] = NSOpenGLPFAStencilSize;
    attributes[i++] = 0;

    attributes[i++] = NSOpenGLPFAAccumSize;
    attributes[i++] = 0;

    attributes[i++] = NSOpenGLPFAColorSize;
    attributes[i++] = (NSOpenGLPixelFormatAttribute)bitsPerPixel;

    if (samples > 0) {
        switch (samples) {
        case 4:
        case 8:
            break;

        default:
            samples = 8;
            break;
        }

        attributes[i++] = NSOpenGLPFASampleBuffers;
        attributes[i++] = 1;
        attributes[i++] = NSOpenGLPFASamples;
        attributes[i++] = (NSOpenGLPixelFormatAttribute)samples;
    }

    attributes[i++] = 0;

    pixelFormat = [[NSOpenGLPixelFormat alloc] initWithAttributes:attributes];

    if (pixelFormat == nil) {
        FDError(@"Unable to find a matching pixelformat. Please try other displaymode(s).");
    }

    return pixelFormat;
}

//----------------------------------------------------------------------------------------------------------------------------

- (NSOpenGLContext*)createGLContextWithBitsPerPixel:(NSUInteger)bitsPerPixel samples:(NSUInteger)samples
{
    NSOpenGLPixelFormat* pixelFormat = [self createGLPixelFormatWithBitsPerPixel:bitsPerPixel samples:samples];
    NSOpenGLContext* context = [[NSOpenGLContext alloc] initWithFormat:pixelFormat shareContext:nil];

    if (context == nil) {
        FDError(@"Unable to create an OpenGL context. Please try other displaymode(s).");
    }

    return context;
}

//----------------------------------------------------------------------------------------------------------------------------

- (NSImage*)createMiniImageWithSize:(NSSize)size
{
    NSGraphicsContext* graphicsContext = nil;
    NSImage* miniImage = [[NSImage alloc] initWithSize:size];

    [miniImage lockFocus];

    graphicsContext = [NSGraphicsContext currentContext];
    [graphicsContext setImageInterpolation:NSImageInterpolationNone];
    [graphicsContext setShouldAntialias:NO];

    [miniImage unlockFocus];

    return miniImage;
}

//----------------------------------------------------------------------------------------------------------------------------

- (void)drawMiniImage
{
    if ([self isMiniaturized] == YES) {
        if (mView != nil) {
            NSBitmapImageRep* bitmap = [mView bitmapRepresentation];

            if (bitmap != nil) {
                const NSSize size = [mMiniImage size];
                const NSRect contentRect = [mView frame];
                const float aspect = NSWidth(contentRect) / NSHeight(contentRect);
                const NSRect clearRect = NSMakeRect(0.0, 0.0, size.width, size.height);
                NSRect miniImageRect = clearRect;

                if (aspect >= 1.0f) {
                    miniImageRect.size.height /= aspect;
                    miniImageRect.origin.y = (size.height - NSHeight(miniImageRect)) * 0.5f;
                }
                else {
                    miniImageRect.size.width /= aspect;
                    miniImageRect.origin.x = (size.width - NSWidth(miniImageRect)) * 0.5f;
                }

                [mMiniImage lockFocus];
                [[NSColor clearColor] set];
                NSRectFill(clearRect);
                [bitmap drawInRect:miniImageRect];
                [mMiniImage unlockFocus];

                [self setMiniwindowImage:mMiniImage];
            }
        }
    }
}

//----------------------------------------------------------------------------------------------------------------------------

- (void)resignKeyWindow
{
    mForceCusorVisible = YES;

    [self updateCursor];
    [super resignKeyWindow];
}

//----------------------------------------------------------------------------------------------------------------------------

- (void)becomeKeyWindow
{
    mForceCusorVisible = NO;

    [self updateCursor];
    [super becomeKeyWindow];
}

//----------------------------------------------------------------------------------------------------------------------------

- (void)screenParametersDidChange:(NSNotification*)notification
{
    const NSRect frameRect = [self constrainFrameRect:[self frame] toScreen:[self screen]];

    [self setFrame:frameRect display:YES];
    [self center];
}

//----------------------------------------------------------------------------------------------------------------------------

- (void)keyDown:(NSEvent*)event
{
    // Already handled by FDHIDInput, implementation avoids the NSBeep() caused by unhandled key events.
}

@end

//----------------------------------------------------------------------------------------------------------------------------
