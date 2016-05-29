//----------------------------------------------------------------------------------------------------------------------------
//
// "vid_osx.m" - MacOS X Video driver
//
// Written by:	Axel 'awe' Wefers			[mailto:awe@fruitz-of-dojo.de].
//				©2001-2012 Fruitz Of Dojo 	[http://www.fruitz-of-dojo.de].
//
// Quakeª is copyrighted by id software		[http://www.idsoftware.com].
//
// Version History:
// v1.2.0: Replaced implementation with OpenGL texture due to CGDisplayBaseAddress() being deprecated with MacOS X v10.7.
// v1.1.0: Improved performance in windowed mode.
//         Window can be resized.
//	       Changed "minimized in Dock mode": now plays in the document miniwindow rather than inside the application icon.
//	       Screenshots are saved in PNG format now.
//		   Video mode list is getting sorted.
// v1.0.9: Moved functions for capturing and fading displays to "vid_shared_osx.m".
//	       Added "fade all displays" option.
//	       Added display selection.
// v1.0.7: Added variables "gl_fsaa" and "gl_pntriangles" for compatibility reasons.
// v1.0.5: Added windowed display modes.
//	       Added "minimized in Dock" mode.
//	       Displays are now catured manually due to a bug with CGReleaseAllDisplays().
//         Reduced the fade duration to 1.0s.
// v1.0.2: Added "DrawSprocket" style gamma fading.
//	       Fixed "Draw_Pic: bad coordinates" bug [for effective buffersizes < 320x240].
//         Default video mode is now: 640x480, 67hz.
//         Recognizes all supported [more than 15] video modes via console or config.cfg.
//         Some internal changes.
// v1.0.0: Initial release.
//
//----------------------------------------------------------------------------------------------------------------------------

#import <AppKit/AppKit.h>
#import <OpenGL/gl.h>

#import "FDFramework/FDFramework.h"

#import "QShared.h"
#import "d_local.h"
#import "in_osx.h"
#import "quakedef.h"
#import "vid_osx.h"

//----------------------------------------------------------------------------------------------------------------------------

#undef VID_CAPTURE_ALL_DISPLAYS

#define VID_NUM_WINDOWED_MODES 3
#define VID_NO_BUFFER_UPDATE 0
#define VID_BUFFER_UPDATE 1

//----------------------------------------------------------------------------------------------------------------------------

typedef enum {
    VID_BLIT_2X1 = 0,
    VID_BLIT_1X1,
    VID_BLIT_1X2,
    VID_BLIT_2X2,
    VID_BLIT_WIN
} vid_blitmode_t;

typedef struct {
    UInt16 Width;
    UInt16 Height;
    UInt16 OffWidth;
    UInt16 OffHeight;
    vid_blitmode_t BlitMode;
    UInt8* OffBuffer;
    short* ZBuffer;
    UInt8* SurfCache;
} vid_mode_t;

typedef struct {
    FDDisplayMode* mDisplayMode;
    UInt16 mWidth;
    UInt16 mHeight;
    char mDescription[128];
} vid_modedesc_t;

//----------------------------------------------------------------------------------------------------------------------------

extern viddef_t vid;

cvar_t vid_mode = { "vid_mode", "0", 0 };
cvar_t vid_redrawfull = { "vid_redrawfull", "0", 0 };
cvar_t vid_wait = { "vid_wait", "1", 1 };
cvar_t _vid_default_mode = { "_vid_default_mode", "0", 1 };
cvar_t _vid_default_blit_mode = { "_vid_default_blit_mode", "0", 1 };
cvar_t _windowed_mouse = { "_windowed_mouse", "0", 0 };
cvar_t gl_anisotropic = { "gl_anisotropic", "0", 1 };
cvar_t gl_truform = { "gl_truform", "-1", 1 };
cvar_t gl_multitexture = { "gl_multitexture", "0", 1 };

unsigned short d_8to16table[256] = { 0 };
unsigned d_8to24table[256] = { 0 };

qboolean gVidDisplayFullscreen = YES;
FDWindow* gVidWindow = nil;

static BOOL gVidFadeAllDisplays = NO;
static FDDisplay* gVidDisplay = nil;

static vid_mode_t gVidGraphMode = { 0 };
static vid_modedesc_t* gVidModeList = NULL;
static SInt16 gVidNumModes = 0;
static SInt16 gVidCurMode = 0;
static SInt16 gVidOldMode = 0;
static UInt16 gVidDefaultMode = 0;
static char* gVidBlitModeStr[] = { "2x1", "1x1", "1x2", "2x2", "" };
static SInt8 gVidMenuLine = 0;
static double gVidEndTestTime = 0.0;
static BOOL gVidIsInitialized = NO;
static BOOL gVidDefaultModeSet = NO;
static BOOL gVidTesting = NO;
static BOOL gVideoWait = NO;

static UInt32 gVidPalette[256] = { 0 };
static UInt32* gpVidBitmap = NULL;

static GLuint gVidTexture = 0;
static BOOL gVidTextureInitialized = NO;
static NSSize gVidTextureSize = { 0 };

//----------------------------------------------------------------------------------------------------------------------------

BOOL VID_HideFullscreen(BOOL);
qboolean VID_Screenshot(SInt8*, void*, UInt32, UInt32, UInt32);
void VID_MenuKey(int);
void VID_MenuDraw(void);

static void VID_InitializeTexture(void);
static void VID_ShutdownTexture(void);
static void VID_RenderTexture(void);
static void VID_ResizeHandler(id, void*);
static void VID_SetWait(UInt32);
static void VID_SetBlitMode(vid_blitmode_t, BOOL);
static void VID_GetBuffers(void);
static void VID_FlushBuffers(void);
static void VID_SetOriginalMode(float);
static BOOL VID_GetModeList(void);
static UInt16 VID_InsertMode(UInt16, UInt16, UInt16, FDDisplayMode*);
static UInt32 VID_GetNextPowerOfTwo(UInt32 val);
static BOOL VID_SetDisplay(void);
static void VID_DescribeCurrentMode_f(void);
static void VID_DescribeMode_f(void);
static void VID_DescribeModes_f(void);
static void VID_ForceMode_f(void);
static void VID_NumModes_f(void);
static void VID_TestMode_f(void);

//----------------------------------------------------------------------------------------------------------------------------

UInt32 VID_GetNextPowerOfTwo(UInt32 val)
{
    --val;

    val = (val >> 1) | val;
    val = (val >> 2) | val;
    val = (val >> 4) | val;
    val = (val >> 8) | val;
    val = (val >> 16) | val;

    return ++val;
}

//----------------------------------------------------------------------------------------------------------------------------

void VID_InitializeTexture(void)
{
    if (gVidTextureInitialized == NO) {
        const GLsizei texWidth = VID_GetNextPowerOfTwo(gVidGraphMode.OffWidth);
        const GLsizei texHeight = VID_GetNextPowerOfTwo(gVidGraphMode.OffHeight);
        GLint actualTexWidth = -1;
        GLint actualTexHeight = -1;
        GLenum error = 0;

        [[gVidWindow openGLContext] makeCurrentContext];

        glGenTextures(1, &gVidTexture);
        glBindTexture(GL_TEXTURE_2D, gVidTexture);
        glEnable(GL_TEXTURE_2D);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);

        glGetError();
        glTexImage2D(GL_PROXY_TEXTURE_2D, 0, GL_RGBA, texWidth, texHeight, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);

        error = glGetError();

        glGetTexLevelParameteriv(GL_PROXY_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &actualTexWidth);
        glGetTexLevelParameteriv(GL_PROXY_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &actualTexHeight);

        // now let's see if the width is equal to our requested value:
        if ((error == GL_NO_ERROR) && (texWidth == actualTexWidth) && (texHeight == actualTexHeight)) {
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, texWidth, texHeight, 0, GL_RGBA, GL_UNSIGNED_BYTE, gpVidBitmap);
        }
        else {
            Sys_Error("Out of video RAM. Please try a lower resolution and/or depth!");
        }

        gVidTextureSize = NSMakeSize(texWidth, texHeight);
        gVidTextureInitialized = YES;
    }
}

//----------------------------------------------------------------------------------------------------------------------------

void VID_ShutdownTexture(void)
{
    if (gVidTextureInitialized == YES) {
        [[gVidWindow openGLContext] makeCurrentContext];

        glDeleteTextures(1, &gVidTexture);
        gVidTextureInitialized = NO;
    }
}

//----------------------------------------------------------------------------------------------------------------------------

void VID_RenderTexture(void)
{
    if (gpVidBitmap != NULL) {
        const GLsizei width = gVidGraphMode.OffWidth;
        const GLsizei height = gVidGraphMode.OffHeight;
        const GLfloat s = gVidGraphMode.OffWidth / gVidTextureSize.width;
        const GLfloat t = gVidGraphMode.OffHeight / gVidTextureSize.height;
        const NSRect contentRect = [[gVidWindow contentView] frame];

        [[gVidWindow openGLContext] makeCurrentContext];

        glViewport(0, 0, (GLsizei)NSWidth(contentRect), (GLsizei)NSHeight(contentRect));
        glMatrixMode(GL_PROJECTION);
        glLoadIdentity();
        glOrtho(0.0, 1.0, 1.0, 0.0f, -1.0, 1.0);
        glMatrixMode(GL_MODELVIEW);
        glLoadIdentity();
        glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        glBindTexture(GL_TEXTURE_2D, gVidTexture);

        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, gpVidBitmap);
        glEnable(GL_TEXTURE_2D);

        glColor3f(1.0f, 1.0f, 1.0f);

        glBegin(GL_TRIANGLE_STRIP);
        glTexCoord2f(s, 0.0f);
        glVertex2f(1.0f, 0.0f);

        glTexCoord2f(0.0f, 0.0f);
        glVertex2f(0.0f, 0.0f);

        glTexCoord2f(s, t);
        glVertex2f(1.0f, 1.0f);

        glTexCoord2f(0.0f, t);
        glVertex2f(0.0f, 1.0f);
        glEnd();

        [gVidWindow endFrame];
    }
}

//----------------------------------------------------------------------------------------------------------------------------

void VID_ResizeHandler(id view, void* pContext)
{
    FD_UNUSED(view, pContext);

    VID_RenderTexture();
}

//----------------------------------------------------------------------------------------------------------------------------

void VID_GetBuffers(void)
{
    const size_t col32Bytes = gVidGraphMode.OffWidth * gVidGraphMode.OffHeight * sizeof(UInt32);
    const size_t colorBytes = gVidGraphMode.OffWidth * gVidGraphMode.OffHeight * sizeof(UInt8);
    const size_t depthBytes = gVidGraphMode.OffWidth * gVidGraphMode.OffHeight * sizeof(short);
    const size_t cacheBytes = D_SurfaceCacheForRes(gVidGraphMode.OffWidth, gVidGraphMode.OffHeight);
    const size_t totalBytes = col32Bytes + colorBytes + depthBytes + cacheBytes;
    void* pBuffer = malloc(totalBytes);

    if (pBuffer == NULL) {
        Sys_Error("Not enough memory for video buffers left!\n");
    }

    memset(pBuffer, 0x00, totalBytes);

    gpVidBitmap = pBuffer;
    gVidGraphMode.OffBuffer = ((UInt8*)pBuffer) + col32Bytes;
    gVidGraphMode.ZBuffer = (short*)(gVidGraphMode.OffBuffer + colorBytes);
    gVidGraphMode.SurfCache = gVidGraphMode.OffBuffer + colorBytes + depthBytes;

    VID_InitializeTexture();
}

//----------------------------------------------------------------------------------------------------------------------------

void VID_FlushBuffers(void)
{
    if (gpVidBitmap != NULL) {
        D_FlushCaches();
        free(gpVidBitmap);
        gpVidBitmap = NULL;
        gVidGraphMode.OffBuffer = NULL;
    }

    VID_ShutdownTexture();
}

//----------------------------------------------------------------------------------------------------------------------------

#if defined(QUAKE_WORLD)

void VID_LockBuffer(void)
{
}

#endif // QUAKE_WORLD

//----------------------------------------------------------------------------------------------------------------------------

#if defined(QUAKE_WORLD)

void VID_UnlockBuffer(void)
{
}

#endif // QUAKE_WORLD

//----------------------------------------------------------------------------------------------------------------------------

BOOL VID_HideFullscreen(BOOL hide)
{
    static BOOL isHidden = NO;

    if (isHidden == hide || gVidDisplayFullscreen == NO) {
        return YES;
    }

    if (hide == YES) {
        if (gVidFadeAllDisplays == YES) {
            [FDDisplay fadeOutAllDisplays:VID_FADE_DURATION];
        }
        else {
            [gVidDisplay fadeOutDisplay:VID_FADE_DURATION];
        }

        [gVidWindow orderOut:nil];
        [gVidDisplay setDisplayMode:[gVidDisplay originalMode]];

        if (gVidFadeAllDisplays == YES) {
            [FDDisplay releaseAllDisplays];
            [FDDisplay fadeInAllDisplays:VID_FADE_DURATION];
        }
        else {
            [gVidDisplay releaseDisplay];
            [gVidDisplay fadeInDisplay:VID_FADE_DURATION];
        }
    }
    else {
        if (gVidFadeAllDisplays == YES) {
            [FDDisplay fadeOutAllDisplays:VID_FADE_DURATION];
            [FDDisplay captureAllDisplays];
        }
        else {
            [gVidDisplay fadeOutDisplay:VID_FADE_DURATION];
            [gVidDisplay captureDisplay];
        }

        [gVidDisplay setDisplayMode:gVidModeList[gVidCurMode].mDisplayMode];
        [gVidWindow makeKeyAndOrderFront:nil];

        if (gVidFadeAllDisplays == YES) {
            [FDDisplay fadeInAllDisplays:VID_FADE_DURATION];
        }
        else {
            [gVidDisplay fadeInDisplay:VID_FADE_DURATION];
        }
    }

    isHidden = hide;

    return YES;
}

//----------------------------------------------------------------------------------------------------------------------------

UInt16 VID_InsertMode(UInt16 index, UInt16 width, UInt16 height, FDDisplayMode* displayMode)
{
    char resolution[16];

    // add values to the mode list:
    gVidModeList[index].mDisplayMode = [displayMode retain];
    gVidModeList[index].mWidth = width;
    gVidModeList[index].mHeight = height;

    // generate the description for the video menu:
    snprintf(resolution, 16, "%dx%d", width, height);

    if (displayMode != nil) {
        snprintf(gVidModeList[index].mDescription, 128, "%-9s - fullscreen", resolution);
    }
    else {
        snprintf(gVidModeList[index].mDescription, 128, "%-9s -   windowed", resolution);
    }

    return ++index;
}

//----------------------------------------------------------------------------------------------------------------------------

BOOL VID_GetModeList(void)
{
    NSArray* displayModes = [gVidDisplay displayModes];
    NSMutableArray* filteredModes = [[NSMutableArray alloc] init];
    UInt16 i = 0;

    if (displayModes == nil) {
        Sys_Error("Unable to get list of available display modes.");
    }

    gVidNumModes = VID_NUM_WINDOWED_MODES;

    for (FDDisplayMode* displayMode in [gVidDisplay displayModes]) {
        if ([displayMode bitsPerPixel] == 32) {
            [filteredModes addObject:displayMode];
        }
    }

    gVidNumModes = VID_NUM_WINDOWED_MODES + [filteredModes count];

    if (gVidNumModes == 0) {
        Sys_Error("Unable to get list of display modes.");
    }

    gVidModeList = malloc(gVidNumModes * sizeof(vid_modedesc_t));

    if (gVidModeList == NULL) {
        Sys_Error("Out of memory.");
    }

    // 320 x 240, windowed:
    i = VID_InsertMode(0, 320, 240, nil);

    // 640 x 480, windowed:
    i = VID_InsertMode(1, 640, 480, nil);

    // 800 x 600, windowed:
    i = VID_InsertMode(2, 800, 600, nil);

    // build the mode list:

    for (FDDisplayMode* displayMode in filteredModes) {
        const NSUInteger width = [displayMode width];
        const NSUInteger height = [displayMode height];

        if (i < gVidNumModes) {
            // insert the new mode into the list:
            i = VID_InsertMode(i, width, height, displayMode);
        }
    }

    gVidNumModes = i;

    return YES;
}

//----------------------------------------------------------------------------------------------------------------------------

void VID_SetPalette(unsigned char* pPalette)
{
    for (UInt32 i = 0; i < 256; ++i) {
        const UInt red = pPalette[i * 3 + 0];
        const UInt green = pPalette[i * 3 + 1];
        const UInt blue = pPalette[i * 3 + 2];
        const UInt alpha = 0xFF;

#ifdef __LITTLE_ENDIAN__
        const UInt color = (red << 0) + (green << 8) + (blue << 16) + (alpha << 24);
#else
        const UInt color = (red << 24) + (green << 16) + (blue << 8) + (alpha << 0);
#endif // __LITTLE_ENDIAN__

        gVidPalette[i] = color;
    }
}

//----------------------------------------------------------------------------------------------------------------------------

void VID_ShiftPalette(unsigned char* pPalette)
{
    VID_SetPalette(pPalette);
}

//----------------------------------------------------------------------------------------------------------------------------

void VID_SetBlitMode(vid_blitmode_t blitMode, BOOL bufferUpdate)
{
    const int tmp = scr_disabled_for_loading;

    // don't allow blitmodes other than 1x1 if our display size is < 640x480:
    if (gVidDisplayFullscreen == YES && (gVidGraphMode.Width < 640 || gVidGraphMode.Height < 480)) {
        if (gVidGraphMode.BlitMode == VID_BLIT_1X1) {
            return;
        }

        blitMode = VID_BLIT_1X1;
    }

    // return, if the new blitmode is equal to the old one:
    if (blitMode == gVidGraphMode.BlitMode) {
        return;
    }

    // free the buffers, if a buffer update is requested:
    if (bufferUpdate == VID_BUFFER_UPDATE) {
        scr_disabled_for_loading = 1;
        VID_FlushBuffers();
    }

    // just a check [in case someone played with config.cfg]:
    if (_vid_default_blit_mode.value < 0) {
        _vid_default_blit_mode.value = 0;
    }

    if (_vid_default_blit_mode.value > 4) {
        _vid_default_blit_mode.value = 3;
    }

    if (gVidDisplayFullscreen == YES) {
        if (blitMode == VID_BLIT_WIN) {
            blitMode = VID_BLIT_2X2;
        }
    }
    else {
        blitMode = VID_BLIT_WIN;
    }

    // setup the misc. blitmodes:
    switch (blitMode) {
    case VID_BLIT_1X2:
        gVidGraphMode.BlitMode = VID_BLIT_1X2;

        if (bufferUpdate == VID_NO_BUFFER_UPDATE) {
            return;
        }

        gVidGraphMode.OffWidth = gVidGraphMode.Width;
        gVidGraphMode.OffHeight = gVidGraphMode.Height >> 1;
        break;

    case VID_BLIT_2X1:
        gVidGraphMode.BlitMode = VID_BLIT_2X1;

        if (bufferUpdate == VID_NO_BUFFER_UPDATE) {
            return;
        }

        gVidGraphMode.OffWidth = gVidGraphMode.Width >> 1;
        gVidGraphMode.OffHeight = gVidGraphMode.Height;
        break;

    case VID_BLIT_2X2:
        gVidGraphMode.BlitMode = VID_BLIT_2X2;

        if (bufferUpdate == VID_NO_BUFFER_UPDATE) {
            return;
        }

        gVidGraphMode.OffWidth = gVidGraphMode.Width >> 1;
        gVidGraphMode.OffHeight = gVidGraphMode.Height >> 1;
        break;
    case VID_BLIT_WIN:
        gVidGraphMode.BlitMode = VID_BLIT_WIN;

        if (bufferUpdate == VID_NO_BUFFER_UPDATE) {
            return;
        }

        gVidGraphMode.OffWidth = gVidGraphMode.Width;
        gVidGraphMode.OffHeight = gVidGraphMode.Height;
        break;

    case VID_BLIT_1X1:
    default:
        gVidGraphMode.BlitMode = VID_BLIT_1X1;

        if (bufferUpdate == VID_NO_BUFFER_UPDATE) {
            return;
        }

        gVidGraphMode.OffWidth = gVidGraphMode.Width;
        gVidGraphMode.OffHeight = gVidGraphMode.Height;
        break;
    }

    // allocate buffers for the new blitmode:
    VID_GetBuffers();

    // setup vid struct for Quake:
    vid.maxwarpwidth = WARP_WIDTH;
    vid.width = gVidGraphMode.OffWidth;
    vid.conwidth = vid.width;
    vid.maxwarpheight = WARP_HEIGHT;
    vid.height = gVidGraphMode.OffHeight;
    vid.conheight = vid.height;
    vid.aspect = ((float)gVidGraphMode.OffHeight / (float)gVidGraphMode.OffWidth) * (320.0f / 240.0f);
    vid.numpages = 1;
    vid.colormap = host_colormap;
    vid.fullbright = 256 - LittleLong(*((int*)vid.colormap + 2048));
    vid.buffer = gVidGraphMode.OffBuffer;
    vid.conbuffer = vid.buffer;
    vid.rowbytes = gVidGraphMode.OffWidth;
    vid.conrowbytes = vid.rowbytes;
    vid.direct = 0;
    vid.recalc_refdef = 1;

    // get new buffers for Quake:
    d_pzbuffer = gVidGraphMode.ZBuffer;

    D_InitCaches(gVidGraphMode.SurfCache, D_SurfaceCacheForRes(gVidGraphMode.OffWidth, gVidGraphMode.OffHeight));

    scr_disabled_for_loading = tmp;
}

//----------------------------------------------------------------------------------------------------------------------------

BOOL VID_SetDisplay(void)
{
    NSString* displayName = [[FDPreferences sharedPrefs] stringForKey:QUAKE_PREFS_KEY_SW_DISPLAY];
    NSArray* displays = [FDDisplay displays];
    NSEnumerator* displayEnum = [displays objectEnumerator];
    FDDisplay* display = nil;

    while (display = [displayEnum nextObject]) {
        if ([[display description] isEqualToString:displayName] == YES) {
            break;
        }
    }

    if (display == nil) {
        display = [FDDisplay mainDisplay];
    }

    gVidDisplay = display;
    gVidFadeAllDisplays = [[FDPreferences sharedPrefs] boolForKey:QUAKE_PREFS_KEY_SW_FADE_ALL];

    return gVidDisplay != nil;
}

//----------------------------------------------------------------------------------------------------------------------------

void VID_Init(unsigned char* pPalette)
{
    // register variables:
    Cvar_RegisterVariable(&vid_mode);
    Cvar_RegisterVariable(&vid_redrawfull);
    Cvar_RegisterVariable(&vid_wait);
    Cvar_RegisterVariable(&_vid_default_mode);
    Cvar_RegisterVariable(&_vid_default_blit_mode);
    Cvar_RegisterVariable(&gl_anisotropic);
    Cvar_RegisterVariable(&gl_truform);
    Cvar_RegisterVariable(&gl_multitexture);

#ifndef QUAKE_WORLD
    Cvar_RegisterVariable(&_windowed_mouse);
#endif // QUAKE_WORLD

    // register console commands:
    Cmd_AddCommand("vid_describecurrentmode", VID_DescribeCurrentMode_f);
    Cmd_AddCommand("vid_describemode", VID_DescribeMode_f);
    Cmd_AddCommand("vid_describemodes", VID_DescribeModes_f);
    Cmd_AddCommand("vid_forcemode", VID_ForceMode_f);
    Cmd_AddCommand("vid_nummodes", VID_NumModes_f);
    Cmd_AddCommand("vid_testmode", VID_TestMode_f);

    vid_menudrawfn = VID_MenuDraw;
    vid_menukeyfn = VID_MenuKey;

    if (!VID_SetDisplay()) {
        Sys_Error("No valid display found!");
    }

    if (!VID_GetModeList()) {
        Sys_Error("No valid display modes found!");
    }

    gVidCurMode = _vid_default_mode.value;

    // revert to mode no. 0, if mode no. not available:
    if (gVidCurMode >= gVidNumModes) {
        gVidCurMode = 0;
        Cvar_SetValue("_vid_default_mode", gVidCurMode);
    }

    // switch to the desired mode:
    if (!VID_SetMode(gVidCurMode, pPalette)) {
        Sys_Error("Can\'t initialize video!");
    }

    // setup the blitter:
    VID_SetBlitMode(_vid_default_blit_mode.value, VID_NO_BUFFER_UPDATE);

    gVidIsInitialized = YES;
}

//----------------------------------------------------------------------------------------------------------------------------

void VID_SetOriginalMode(float fadeDuration)
{
    if ([FDDisplay isAnyDisplayCaptured] == YES) {
        if (gVidFadeAllDisplays == YES) {
            [FDDisplay fadeOutAllDisplays:fadeDuration];
        }
        else {
            [gVidDisplay fadeOutDisplay:fadeDuration];
        }
    }

    if (gVidWindow != nil) {
        [gVidWindow close];
        gVidWindow = nil;
    }

    if ([FDDisplay isAnyDisplayCaptured] == YES) {
        [gVidDisplay setDisplayMode:[gVidDisplay originalMode]];

        if (gVidFadeAllDisplays == YES) {
            [FDDisplay releaseAllDisplays];
            [FDDisplay fadeInAllDisplays:fadeDuration];
        }
        else {
            [gVidDisplay releaseDisplay];
            [gVidDisplay fadeInDisplay:fadeDuration];
        }
    }
}

//----------------------------------------------------------------------------------------------------------------------------

void VID_Shutdown(void)
{
    if (gVidIsInitialized == YES) {
        VID_FlushBuffers();
        VID_SetOriginalMode(VID_FADE_DURATION);

        gVidIsInitialized = NO;
    }
}

//----------------------------------------------------------------------------------------------------------------------------

void VID_SetWait(UInt32 state)
{
    const BOOL enable = (state != 0);

    [gVidWindow setVsync:enable];

    if (state == [gVidWindow vsync]) {
        if (enable == YES) {
            Con_Printf("video wait successfully enabled!\n");
        }
        else {
            Con_Printf("video wait successfully disabled!\n");
        }

        gVideoWait = vid_wait.value;
    }
    else {
        Con_Printf("Error while trying to change video wait!\n");

        vid_wait.value = gVideoWait;
    }
}

//----------------------------------------------------------------------------------------------------------------------------

int VID_SetMode(int mode, unsigned char* pPalette)
{
    const int tmp = scr_disabled_for_loading;
    BOOL isFullscreenMode = NO;

    // check if the selected video mode is valid:
    if ((mode < 0) || (mode >= gVidNumModes) || ((mode == gVidCurMode) && (gVidIsInitialized == YES))) {
        Con_Printf("Invalid video mode.\n");

        Cvar_SetValue("vid_mode", gVidCurMode);
        gVidTesting = NO;

        return 0;
    }

    Con_Printf("Switching to: %dx%d...\n", gVidModeList[mode].mWidth, gVidModeList[mode].mHeight);

    scr_disabled_for_loading = 1;
    isFullscreenMode = (gVidModeList[mode].mDisplayMode != nil);

    // free all buffers:
    VID_FlushBuffers();

    if (isFullscreenMode == YES) {
        if (gVidFadeAllDisplays) {
            [FDDisplay fadeOutAllDisplays:VID_FADE_DURATION];

            if ([gVidDisplay isCaptured] == NO) {
                [FDDisplay captureAllDisplays];
            }
        }
        else {
            [gVidDisplay fadeOutDisplay:VID_FADE_DURATION];

            if ([gVidDisplay isCaptured] == NO) {
                [gVidDisplay captureDisplay];
            }
        }

        [gVidWindow close];
        gVidWindow = nil;

        if (![gVidDisplay setDisplayMode:gVidModeList[mode].mDisplayMode]) {
            Sys_Error("Unable to switch the displaymode!");
        }

        gVidWindow = [[FDWindow alloc] initForDisplay:gVidDisplay];

        [gVidWindow setResizeHandler:&VID_ResizeHandler forContext:nil];
        [gVidWindow makeKeyAndOrderFront:nil];
        [gVidWindow flushWindow];

        if (gVidFadeAllDisplays) {
            [FDDisplay fadeInAllDisplays:VID_FADE_DURATION];
        }
        else {
            [gVidDisplay fadeInDisplay:VID_FADE_DURATION];
        }
    }
    else {
        const NSRect contentRect = NSMakeRect(0.0, 0.0, gVidModeList[mode].mWidth, gVidModeList[mode].mHeight);

        VID_SetOriginalMode(VID_FADE_DURATION);

        gVidWindow = [[FDWindow alloc] initWithContentRect:contentRect];

        [gVidWindow setTitle:[[NSRunningApplication currentApplication] localizedName]];
        [gVidWindow setResizeHandler:&VID_ResizeHandler forContext:nil];
        [gVidWindow centerForDisplay:gVidDisplay];
        [gVidWindow makeKeyAndOrderFront:nil];
        [gVidWindow makeMainWindow];
        [gVidWindow flushWindow];
    }

    gVidDisplayFullscreen = isFullscreenMode;

    // set the video refresh for the new gl context
    VID_SetWait((UInt32)vid_wait.value);

    gVidGraphMode.Width = gVidModeList[mode].mWidth;
    gVidGraphMode.Height = gVidModeList[mode].mHeight;

    // just a check [in case someone played with config.cfg]:
    if (gVidDisplayFullscreen == YES) {
        // don't allow blitmodes other than 1x1 for video modes < 640x480:
        if (gVidGraphMode.Width < 640 || gVidGraphMode.Height < 480) {
            VID_SetBlitMode(VID_BLIT_1X1, VID_NO_BUFFER_UPDATE);
        }

        if (gVidGraphMode.BlitMode == VID_BLIT_WIN) {
            VID_SetBlitMode(VID_BLIT_2X2, VID_NO_BUFFER_UPDATE);
        }
    }
    else {
        if (gVidGraphMode.BlitMode != VID_BLIT_WIN) {
            VID_SetBlitMode(VID_BLIT_WIN, VID_NO_BUFFER_UPDATE);
        }
    }

    // setup the blit-rectangle:
    switch (gVidGraphMode.BlitMode) {
    case VID_BLIT_1X2:
        gVidGraphMode.OffWidth = gVidGraphMode.Width;
        gVidGraphMode.OffHeight = gVidGraphMode.Height >> 1;
        break;
    case VID_BLIT_2X1:
        gVidGraphMode.OffWidth = gVidGraphMode.Width >> 1;
        gVidGraphMode.OffHeight = gVidGraphMode.Height;
        break;
    case VID_BLIT_2X2:
        gVidGraphMode.OffWidth = gVidGraphMode.Width >> 1;
        gVidGraphMode.OffHeight = gVidGraphMode.Height >> 1;
        break;
    case VID_BLIT_1X1:
    case VID_BLIT_WIN:
    default:
        gVidGraphMode.OffWidth = gVidGraphMode.Width;
        gVidGraphMode.OffHeight = gVidGraphMode.Height;
        break;
    }

    // allocate new buffers:
    VID_GetBuffers();

    // setup vid struct for Quake:
    vid.maxwarpwidth = WARP_WIDTH;
    vid.width = gVidGraphMode.OffWidth;
    vid.conwidth = vid.width;
    vid.maxwarpheight = WARP_HEIGHT;
    vid.height = gVidGraphMode.OffHeight;
    vid.conheight = vid.height;
    vid.aspect = ((float)gVidGraphMode.OffHeight / (float)gVidGraphMode.OffWidth) * (320.0f / 240.0f);
    vid.numpages = 1;
    vid.colormap = host_colormap;
    vid.fullbright = 256 - LittleLong(*((int*)vid.colormap + 2048));
    vid.buffer = gVidGraphMode.OffBuffer;
    vid.conbuffer = vid.buffer;
    vid.rowbytes = gVidGraphMode.OffWidth;
    vid.conrowbytes = vid.rowbytes;
    vid.direct = 0;

    // get new buffers for Quake:
    d_pzbuffer = gVidGraphMode.ZBuffer;
    D_InitCaches(gVidGraphMode.SurfCache, D_SurfaceCacheForRes(gVidGraphMode.OffWidth, gVidGraphMode.OffHeight));

    if (pPalette) {
        VID_SetPalette(pPalette);
    }

    Cvar_SetValue("vid_mode", mode);
    gVidCurMode = mode;
    vid.recalc_refdef = 1;

    scr_disabled_for_loading = tmp;

    return 1;
}

//----------------------------------------------------------------------------------------------------------------------------

#ifdef QUAKE_WORLD

void VID_SetWindowTitle(char* pTitle)
{
    if (gVidWindow != nil) {
        if (pTitle) {
            [gVidWindow setTitle:[NSString stringWithCString:pTitle encoding:NSASCIIStringEncoding]];
        }
        else {
            [gVidWindow setTitle:[[NSRunningApplication currentApplication] localizedName]];
        }
    }
}

#endif /* QUAKE_WORLD */

//----------------------------------------------------------------------------------------------------------------------------

qboolean VID_Screenshot(SInt8* pFilename, void* pBitmap, UInt32 width, UInt32 height, UInt32 rowbytes)
{
    NSString* pngName = [NSString stringWithCString:(const char*)pFilename encoding:NSASCIIStringEncoding];
    const NSSize bitmapSize = NSMakeSize((float)width, (float)height);

    return ([FDScreenshot writeToPNG:pngName fromRGB24:pBitmap withSize:bitmapSize rowbytes:rowbytes]);
}

//----------------------------------------------------------------------------------------------------------------------------

void VID_Update(vrect_t* theRects)
{
    if ((gVidIsInitialized == YES) && (gpVidBitmap != NULL)) {
        BOOL cursorIsVisible = (_windowed_mouse.value == 0.0f) && (gVidDisplayFullscreen == NO);
        BOOL blitterDidChange = NO;

        // check for the default value from config.cfg:
        if (gVidDefaultModeSet == NO) {
            if (_vid_default_mode.value != gVidDefaultMode) {
                // set the default video mode:
                if (_vid_default_mode.value < 0 || _vid_default_mode.value >= gVidNumModes) {
                    Cvar_SetValue("_vid_default_mode", 0.0);
                }

                Cvar_SetValue("vid_mode", _vid_default_mode.value);
                gVidDefaultModeSet = YES;
            }

            if (_vid_default_blit_mode.value) {
                if (_vid_default_blit_mode.value < 0 || _vid_default_blit_mode.value > 4) {
                    Cvar_SetValue("_vid_default_blit_mode", 3);
                }

                gVidDefaultModeSet = YES;
                blitterDidChange = YES;
            }
        }

        // if in test mode, check if finished:
        if (gVidTesting == YES) {
            if (realtime >= gVidEndTestTime) {
                gVidTesting = NO;
                Cvar_SetValue("vid_mode", gVidOldMode);
            }
        }

        // did the user request a new video mode?
        if (vid_mode.value != gVidCurMode) {
            S_StopAllSounds(YES);
            VID_SetMode(vid_mode.value, NULL);
        }

        if (cursorIsVisible != [gVidWindow isCursorVisible]) {
            [gVidWindow setCursorVisible:cursorIsVisible];
        }

        if (vid_wait.value != gVideoWait) {
            VID_SetWait((UInt32)vid_wait.value);
        }

        if (blitterDidChange == YES) {
            VID_SetBlitMode(_vid_default_blit_mode.value, VID_BUFFER_UPDATE);
        }

        // copy the rendered scene to the texture buffer:
        UInt32* pDst = gpVidBitmap;
        const UInt8* pSrc = gVidGraphMode.OffBuffer;
        const UInt8* pEnd = pSrc + gVidGraphMode.OffWidth * gVidGraphMode.OffHeight;

        while (pSrc < pEnd) {
            *pDst++ = gVidPalette[*pSrc++];
        }

        VID_RenderTexture();
    }
}

//----------------------------------------------------------------------------------------------------------------------------

void D_BeginDirectRect(SInt x0, SInt y0, UInt8* pSrc, SInt width, SInt height)
{
    if ((gVidIsInitialized == YES) && (gpVidBitmap != NULL)) {
        UInt32* pDst = gpVidBitmap + y0 * gVidGraphMode.OffWidth + x0;

        for (SInt y = 0; y < height; ++y) {
            for (SInt x = 0; x < width; ++x) {
                pDst[x] = gVidPalette[*pSrc++];
            }

            pDst += gVidGraphMode.OffWidth;
        }

        VID_RenderTexture();
    }
}

//----------------------------------------------------------------------------------------------------------------------------

void D_EndDirectRect(SInt x0, SInt y0, SInt width, SInt height)
{
    if ((gVidIsInitialized == YES) && (gpVidBitmap != NULL)) {
        const UInt8* pSrc = gVidGraphMode.OffBuffer + y0 * gVidGraphMode.OffWidth + x0;
        UInt32* pDst = gpVidBitmap + y0 * gVidGraphMode.OffWidth + x0;

        for (SInt y = 0; y < height; ++y) {
            for (SInt x = 0; x < width; ++x) {
                *pDst++ = gVidPalette[*pSrc++];
            }

            pSrc += gVidGraphMode.OffWidth - width;
            pDst += gVidGraphMode.OffWidth - width;
        }

        VID_RenderTexture();
    }
}

//----------------------------------------------------------------------------------------------------------------------------

void VID_DescribeCurrentMode_f(void)
{
    Con_Printf("Current videomode: %s\n", gVidModeList[gVidCurMode].mDescription);
    Con_Printf("Current blitmode: %s\n", gVidBlitModeStr[gVidGraphMode.BlitMode]);
}

//----------------------------------------------------------------------------------------------------------------------------

void VID_DescribeMode_f(void)
{
    if ((Q_atoi(Cmd_Argv(1)) >= 0) && (Q_atoi(Cmd_Argv(1)) < gVidNumModes)) {
        Con_Printf("%s\n", gVidModeList[Q_atoi(Cmd_Argv(1))].mDescription);
    }
    else {
        Con_Printf("Invalid video mode.\n");
    }
}

//----------------------------------------------------------------------------------------------------------------------------

void VID_DescribeModes_f(void)
{
    for (UInt16 i = 0; i < gVidNumModes; i++) {
        Con_Printf("%2d: %s\n", i, gVidModeList[i].mDescription);
    }
}

//----------------------------------------------------------------------------------------------------------------------------

void VID_ForceMode_f(void)
{
    VID_SetMode(Q_atoi(Cmd_Argv(1)), NULL);
}

//----------------------------------------------------------------------------------------------------------------------------

void VID_NumModes_f(void)
{
    if (gVidNumModes == 1) {
        Con_Printf("1 video mode is available\n");
    }
    else {
        Con_Printf("%d video modes are available\n", gVidNumModes);
    }
}

//----------------------------------------------------------------------------------------------------------------------------

void VID_TestMode_f(void)
{
    if (gVidTesting == NO) {
        if (Q_atoi(Cmd_Argv(1)) != gVidCurMode) {
            double testDuration = 0.0;

            // set the test mode:
            gVidOldMode = gVidCurMode;
            Cvar_SetValue("vid_mode", Q_atoi(Cmd_Argv(1)));
            gVidTesting = YES;

            // set the testtime to 5 seconds:
            testDuration = Q_atof(Cmd_Argv(2));

            if (testDuration == 0.0) {
                testDuration = 5.0;
            }

            gVidEndTestTime = realtime + testDuration;
        }
    }
    else {
        Con_Print("Please wait until the first test has finished!\n");
    }
}

//----------------------------------------------------------------------------------------------------------------------------

void VID_MenuDraw(void)
{
    qpic_t* pPicture = Draw_CachePic("gfx/vidmodes.lmp");
    int numModes = (gVidNumModes > 15) ? 15 : gVidNumModes;
    int row = 7 * VID_FONT_HEIGHT;
    int column = 0;
    char tmpStr[100];

    // draw video modes title:
    M_DrawPic((320 - pPicture->width) >> 1, 4, pPicture);
    M_Print(13 * VID_FONT_WIDTH, 5 * VID_FONT_HEIGHT, "Display Modes:");

    // draw the video modes:
    for (int i = 0; i < numModes; ++i) {
        if (strlen(gVidModeList[i].mDescription) <= 38) {
            column = VID_FONT_WIDTH + ((38 - (int)strlen(gVidModeList[i].mDescription)) << 2);

            // draw highlighted, if active:
            if (i == gVidCurMode) {
                M_PrintWhite(column, row, gVidModeList[i].mDescription);
            }
            else {
                M_Print(column, row, gVidModeList[i].mDescription);
            }
        }
        else {
            snprintf(tmpStr, 100, "%.35s...", gVidModeList[i].mDescription);

            // draw highlighted, if active:
            if (i == gVidCurMode) {
                M_PrintWhite(1 * VID_FONT_WIDTH, row, tmpStr);
            }
            else {
                M_Print(1 * VID_FONT_WIDTH, row, tmpStr);
            }
        }

        row += VID_FONT_HEIGHT;
    }

    if (gVidTesting == YES) {
        // skip keybindings, if testing a display mode:
        snprintf(tmpStr, 100, "TESTING %s", gVidModeList[gVidMenuLine].mDescription);
        if (strlen(tmpStr) > 40) {
            snprintf(tmpStr, 100, "TESTING %.29s...", gVidModeList[gVidMenuLine].mDescription);
        }
        M_Print((40 - (int)strlen(tmpStr)) << 2, 36 + 20 * VID_FONT_HEIGHT, tmpStr);
        M_Print(VID_FONT_WIDTH * 8, 36 + 22 * VID_FONT_HEIGHT, "Please wait 5 seconds...");
    }
    else {
        // print the blitmode keys:
        M_Print(16 * VID_FONT_WIDTH, 36 + 18 * VID_FONT_HEIGHT, "Blitmode:");

        // don't allow zoomed blitmodes for resolutions < 640x480 and windowed modes:
        if (gVidGraphMode.Width < 640 || gVidGraphMode.Height < 480 || gVidDisplayFullscreen == NO) {
            M_Print(1 * VID_FONT_WIDTH, 36 + 19 * VID_FONT_HEIGHT, "Resize the window to scale the display");
        }
        else {
            // blitmode keybindings:
            M_Print(1 * VID_FONT_WIDTH, 36 + 19 * VID_FONT_HEIGHT, "[1]     - [2]     - [3]     - [4]");

            // blit 1x1, draw highlighted if current:
            if (gVidGraphMode.BlitMode == VID_BLIT_1X1) {
                M_PrintWhite(5 * VID_FONT_WIDTH, 36 + 19 * VID_FONT_HEIGHT, "1x1");
            }
            else {
                M_Print(5 * VID_FONT_WIDTH, 36 + 19 * VID_FONT_HEIGHT, "1x1");
            }

            // blit 1x2, draw highlighted if current:
            if (gVidGraphMode.BlitMode == VID_BLIT_1X2) {
                M_PrintWhite(15 * VID_FONT_WIDTH, 36 + 19 * VID_FONT_HEIGHT, "1x2");
            }
            else {
                M_Print(15 * VID_FONT_WIDTH, 36 + 19 * VID_FONT_HEIGHT, "1x2");
            }

            // blit 2x1, draw highlighted if current:
            if (gVidGraphMode.BlitMode == VID_BLIT_2X1) {
                M_PrintWhite(25 * VID_FONT_WIDTH, 36 + 19 * VID_FONT_HEIGHT, "2x1");
            }
            else {
                M_Print(25 * VID_FONT_WIDTH, 36 + 19 * VID_FONT_HEIGHT, "2x1");
            }

            // blit 2x2, draw highlighted if current:
            if (gVidGraphMode.BlitMode == VID_BLIT_2X2) {
                M_PrintWhite(35 * VID_FONT_WIDTH, 36 + 19 * VID_FONT_HEIGHT, "2x2");
            }
            else {
                M_Print(35 * VID_FONT_WIDTH, 36 + 19 * VID_FONT_HEIGHT, "2x2");
            }
        }

        // show other key bindings:
        M_Print(8 * VID_FONT_WIDTH, 36 + 20 * VID_FONT_HEIGHT, "Press [Enter] to set mode");
        M_Print(5 * VID_FONT_WIDTH, 36 + 21 * VID_FONT_HEIGHT, "[T] to test mode for 5 seconds");
        M_Print(14 * VID_FONT_WIDTH, 36 + 24 * VID_FONT_HEIGHT, "[Esc] to exit");

        // set the current resolution to default:
        snprintf(tmpStr, 100, "[D] to set default: %s %s",
            gVidModeList[gVidCurMode].mDescription,
            gVidBlitModeStr[gVidGraphMode.BlitMode]);
        if (strlen(tmpStr) > 40) {
            snprintf(tmpStr, 100, "[D] to set default: %.13s... %s",
                gVidModeList[gVidCurMode].mDescription,
                gVidBlitModeStr[gVidGraphMode.BlitMode]);
        }
        M_Print((40 - (int)strlen(tmpStr)) << 2, 36 + 22 * VID_FONT_HEIGHT, tmpStr);

        // current default resolution:
        snprintf(tmpStr, 100, "Current default: %s %s",
            gVidModeList[(int)_vid_default_mode.value].mDescription,
            gVidBlitModeStr[(int)_vid_default_blit_mode.value]);
        if (strlen(tmpStr) > 40) {
            snprintf(tmpStr, 100, "Current default: %.16s... %s",
                gVidModeList[(int)_vid_default_mode.value].mDescription,
                gVidBlitModeStr[(int)_vid_default_blit_mode.value]);
        }
        M_Print((40 - (int)strlen(tmpStr)) << 2, 36 + 23 * VID_FONT_HEIGHT, tmpStr);

        // draw the cursor for the current menu line:
        row = (gVidMenuLine + 7) << 3;
        if (strlen(gVidModeList[gVidMenuLine].mDescription) < 38) {
            column = (38 - (int)strlen(gVidModeList[gVidMenuLine].mDescription)) << 2;
        }
        else {
            column = 0;
        }
        M_DrawCharacter(column, row, 12 + ((int)(realtime * 4) & 1));
    }
}

//----------------------------------------------------------------------------------------------------------------------------

void VID_MenuKey(int key)
{
    if (gVidTesting == NO) {
        switch (key) {
        case K_ESCAPE:
            S_LocalSound("misc/menu1.wav");
            M_Menu_Options_f();
            break;

        case K_UPARROW:
            S_LocalSound("misc/menu1.wav");
            gVidMenuLine -= 1;
            if (gVidMenuLine < 0) {
                if (gVidNumModes > 15) {
                    gVidMenuLine = 14;
                }
                else {
                    gVidMenuLine = gVidNumModes - 1;
                }
            }
            break;

        case K_DOWNARROW:
            S_LocalSound("misc/menu1.wav");
            gVidMenuLine += 1;
            if (gVidNumModes > 15) {
                if (gVidMenuLine >= 15) {
                    gVidMenuLine = 0;
                }
            }
            else {
                if (gVidMenuLine >= gVidNumModes) {
                    gVidMenuLine = 0;
                }
            }
            break;

        case K_ENTER:
            S_LocalSound("misc/menu1.wav");
            Cvar_SetValue("vid_mode", gVidMenuLine);
            break;

        case 'T':
        case 't':
            S_LocalSound("misc/menu1.wav");
            if (gVidMenuLine != gVidCurMode) {
                gVidTesting = YES;
                gVidEndTestTime = realtime + 5.0;
                gVidOldMode = gVidCurMode;
                Cvar_SetValue("vid_mode", gVidMenuLine);
            }
            break;

        case 'D':
        case 'd':
            S_LocalSound("misc/menu1.wav");
            gVidDefaultModeSet = YES;
            Cvar_SetValue("_vid_default_mode", (float)gVidCurMode);
            Cvar_SetValue("_vid_default_blit_mode", (float)gVidGraphMode.BlitMode);
            break;

        case '1':
            S_LocalSound("misc/menu1.wav");
            VID_SetBlitMode(VID_BLIT_1X1, VID_BUFFER_UPDATE);
            break;

        case '2':
            S_LocalSound("misc/menu1.wav");
            VID_SetBlitMode(VID_BLIT_1X2, VID_BUFFER_UPDATE);
            break;

        case '3':
            S_LocalSound("misc/menu1.wav");
            VID_SetBlitMode(VID_BLIT_2X1, VID_BUFFER_UPDATE);
            break;

        case '4':
            S_LocalSound("misc/menu1.wav");
            VID_SetBlitMode(VID_BLIT_2X2, VID_BUFFER_UPDATE);
            break;

        default:
            break;
        }
    }
}

//----------------------------------------------------------------------------------------------------------------------------
