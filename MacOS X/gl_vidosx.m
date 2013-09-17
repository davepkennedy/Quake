//----------------------------------------------------------------------------------------------------------------------------
//
// "gl_vidosx.m" - MacOS X OpenGL Video driver
//
// Written by:	Axel 'awe' Wefers			[mailto:awe@fruitz-of-dojo.de].
//				©2001-2012 Fruitz Of Dojo 	[http://www.fruitz-of-dojo.de].
//
// Quakeª is copyrighted by id software		[http://www.idsoftware.com].
//
// Version History:
// v1.2.0: Replaced calls to CGDirectDisplay functions that are deprecated under MacOS X 10.7.
// v1.0.9: Moved functions for capturing and fading displays to "vid_shared_osx.m".
// v1.0.8: Fixed an issue with the console aspect, if apspect ratio was not 4:3.
//	       Introduces FSAA via the gl_ARB_multisample extension.
//	       Radeon users may switch FSAA on the fly.
//	       Introduces multitexture option for MacOS X v10.2 [see "video options"].
// v1.0.7: Brings back the video options menu.
//	       "vid_wait" now availavle via video options.
//	       ATI Radeon only:
//	       Added support for FSAA, via variable "gl_fsaa" or video options.
//	       Added support for Truform, via variable "gl_truform" or video options.
//	       Added support for anisotropic texture filtering, via variable "gl_anisotropic" or options.
// v1.0.5: Added "minimized in Dock" mode.
//         Displays are now catured manually due to a bug with CGReleaseAllDisplays().
//	       Reduced the fade duration to 1.0s.
// v1.0.4: Fixed continuous console output, if gamma setting fails.
//	       Fixed a multi-monitor issue.
// v1.0.3: Enables setting the gamma via the brightness slider at the options dialog.
//	       Enable/Disable VBL syncing via "vid_wait".
// v1.0.2: GLQuake/GLQuakeWorld:
//	       Fixed a performance issue [see "gl_rsurf.c"].
//         Default value of "gl_keeptjunctions" is now "1" [see "gl_rmain.c"].
//	       Added "DrawSprocket" style gamma fading at game start/end.
//         Some internal changes.
//         GLQuakeWorld:
//         Fixed console width/height bug with resolutions other than 640x480 [was always 640x480].
// v1.0.1: Initial release.
//
//----------------------------------------------------------------------------------------------------------------------------

#import	<AppKit/AppKit.h>
#import <IOKit/graphics/IOGraphicsTypes.h>
#import <OpenGL/OpenGL.h>
#import <OpenGL/gl.h>
#import <OpenGL/glu.h>
#import <OpenGL/glext.h>

#import	"quakedef.h"
#import "in_osx.h"
#import "vid_osx.h"
#import	"sys_osx.h"

#import "QShared.h"

#import "FDFramework/FDFramework.h"

//----------------------------------------------------------------------------------------------------------------------------

#ifndef GL_ATI_pn_triangles

#define GL_PN_TRIANGLES_ATI                                  0x6090
#define GL_MAX_PN_TRIANGLES_TESSELATION_LEVEL_ATI            0x6091
#define GL_PN_TRIANGLES_POINT_MODE_ATI                       0x6092
#define GL_PN_TRIANGLES_NORMAL_MODE_ATI                      0x6093
#define GL_PN_TRIANGLES_TESSELATION_LEVEL_ATI                0x6094
#define GL_PN_TRIANGLES_POINT_MODE_LINEAR_ATI                0x6095
#define GL_PN_TRIANGLES_POINT_MODE_CUBIC_ATI                 0x6096
#define GL_PN_TRIANGLES_NORMAL_MODE_LINEAR_ATI               0x6097
#define GL_PN_TRIANGLES_NORMAL_MODE_QUADRATIC_ATI            0x6098

#endif // GL_ATI_pn_triangles

//----------------------------------------------------------------------------------------------------------------------------

#define VID_WARP_WIDTH			320
#define VID_WARP_HEIGHT			200
#define VID_CONSOLE_MIN_WIDTH	320
#define VID_CONSOLE_MIN_HEIGHT	200
#define VID_FIRST_MENU_LINE		40
#define VID_ATI_FSAA_LEVEL		510		// required for CGLSetParamter () and ATI "instant" FSAA.

//----------------------------------------------------------------------------------------------------------------------------

#define	FD_SQUARE(A)            ((A) * (A))

//----------------------------------------------------------------------------------------------------------------------------

typedef	enum                {
                                VID_MENUITEM_WAIT,
                                VID_MENUITEM_FSAA,
                                VID_MENUITEM_ANISOTROPIC,
                                VID_MENUITEM_MULTITEXTURE,
                                VID_MENUITEM_TRUFORM
                            }	vid_menuitem_t;

//----------------------------------------------------------------------------------------------------------------------------

const char*                     gl_renderer = NULL;
const char*                     gl_vendor = NULL;
const char*                     gl_version = NULL;
const char*                     gl_extensions = NULL;

cvar_t							vid_mode = { "vid_mode", "0", 0 };
cvar_t                          vid_redrawfull = { "vid_redrawfull", "0", 0 };
cvar_t							vid_wait = { "vid_wait", "1", 1 };
cvar_t							_vid_default_mode = { "_vid_default_mode", "0", 1 };
cvar_t							_vid_default_blit_mode = { "_vid_default_blit_mode", "0", 1 };
cvar_t							_windowed_mouse = { "_windowed_mouse","0", 0 };
cvar_t							gl_anisotropic = { "gl_anisotropic", "0", 1 };
cvar_t							gl_fsaa = { "gl_fsaa", "0", 0 };
cvar_t							gl_truform = { "gl_truform", "-1", 1 };
cvar_t							gl_ztrick = { "gl_ztrick", "1" };
cvar_t                          gl_multitexture = { "gl_multitexture", "0", 1 };

unsigned						d_8to24table[256] = { 0 };
unsigned char					d_15to8table[65536] = { 0 };

int								texture_extension_number = 1;

GLfloat							gl_texureanisotropylevel = 1.0f;
qboolean						gl_fsaaavailable = NO;
qboolean                        gl_mtexable = NO;
qboolean                        gl_texturefilteranisotropic = NO;
qboolean                        gl_luminace_lightmaps = NO;
qboolean                        gl_palettedtex = NO;
qboolean                        isPermedia = NO;

FDDisplay*                      gVidDisplay = nil;
FDDisplayMode*                  gVidDisplayMode = nil;
FDWindow*						gVidWindow = nil;

qboolean						gVidDisplayFullscreen = NO;
BOOL							gVidFadeAllDisplays = NO;

static const float				gGLTruformAmbient[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
static BOOL						gGLDisplayIs8Bit = NO;
static BOOL						gGLAnisotropic = NO;
static BOOL						gGLMultiTextureAvailable = NO;
static BOOL						gGLMultiTexture = NO;
NSUInteger						gGLDisplayWidth = 0;
NSUInteger						gGLDisplayHeight = 0;
static float					gGLVideoWait = 0.0f;
static float					gGLFSAALevel = 1.0f;
static float					gGLPNTriangleLevel = -1.0f;
static void                     (*gpGLPNTrianglesiATI)(GLenum pname, GLint param) = NULL;
static SInt8					gGLMenuMaxLine = 0;
static SInt8					gGLMenuLine = VID_FIRST_MENU_LINE;
static vid_menuitem_t           gGLMenuItem = VID_MENUITEM_WAIT;

//----------------------------------------------------------------------------------------------------------------------------

qboolean            VID_Is8bit (void);

static  void        VID_CheckGamma (unsigned char*);
static	void		VID_SetWait (UInt32);
static  float       VID_SetGamma (float, BOOL);
static  BOOL        VID_SetDisplay (void);
static	BOOL		VID_SetDisplayMode (void);
static  void        VID_ResizeHandler (id, void*);
static	void 		VID_MenuDraw (void);
static	void		VID_MenuKey (int theKey);

qboolean            GL_SaveScreenshot (const char *theFilename);

static	void		GL_Init (void);
static	BOOL		GL_ExtensionSupported (const char *theExtension);
static	void		GL_CheckMultiTextureExtensions (void);
static	void		GL_CheckPalettedTexture (void);
static  void        GL_CheckPNTrianglesExtension (const char* pExtensionName, const char* pFunctionName);
static	void		GL_CheckPNTrianglesExtensions (void);
static	void		GL_CheckSwitchFSAAOnTheFly (void);
static	void		GL_CheckTextureFilterAnisotropic (void);
static	void		GL_CheckLuminanceLightmaps (void);
static	void		GL_SetFSAA (UInt32 theFSAALevel);
static	void		GL_SetTextureFilterAnisotropic (UInt32 theState);
static	void		GL_SetPNTriangles (SInt32 thePNTriangleLevel);
static	void		GL_SetMultiTexture (UInt32 theState);

//----------------------------------------------------------------------------------------------------------------------------

#ifdef QUAKE_WORLD

void 	VID_LockBuffer (void)
{
}

#endif /* QUAKE_WORLD */

//----------------------------------------------------------------------------------------------------------------------------

#ifdef QUAKE_WORLD

void	VID_UnlockBuffer (void)
{
}

#endif /* QUAKE_WORLD */

//----------------------------------------------------------------------------------------------------------------------------

qboolean VID_Is8bit (void)
{
    return gGLDisplayIs8Bit;
}

//----------------------------------------------------------------------------------------------------------------------------

void	VID_CheckGamma (unsigned char* pPalette)
{
    float			gamma;
    unsigned char	palette[768];
    SInt			index;
    
    if ((index = COM_CheckParm ("-gamma")) == 0)
    {
        if ((gl_renderer && strstr (gl_renderer, "Voodoo")) || (gl_vendor && strstr (gl_vendor, "3Dfx")))
        {
            gamma = 1.0f;
        }
		else
        {
            gamma = 0.7f;
        }
    }
    else
    {
		gamma = Q_atof (com_argv[index + 1]);
    }
    
    for (SInt i = 0 ; i < sizeof (palette); ++i)
    {
        float newValue = pow ((pPalette[i] + 1) / 256.0f, gamma) * 255 + 0.5f;
        
		if (newValue < 0.0f)
        {
            newValue = 0.0f;
        }
        
        if (newValue > 255.0f)
        {
            newValue = 255.0f;
        }
        
		palette[i] = (unsigned char) newValue;
    }
    
    FD_MEMCPY (pPalette, palette, sizeof (palette));
}

//----------------------------------------------------------------------------------------------------------------------------

void	VID_SetPalette (UInt8* pPalette)
{
    for (UInt16 i = 0; i < 256; ++i)
    {
        const UInt  red     = pPalette[i * 3 + 0];
        const UInt  green   = pPalette[i * 3 + 1];
        const UInt  blue    = pPalette[i * 3 + 2];
        const UInt  alpha   = 0xFF;
        
#ifdef __LITTLE_ENDIAN__
		const UInt  color   = (red <<  0) + (green <<  8) + (blue << 16) + (alpha << 24);
#else
		const UInt  color   = (red << 24) + (green << 16) + (blue <<  8) + (alpha <<  0);
#endif // __LITTLE_ENDIAN__

		d_8to24table[i]     = color;
    }
    
#ifdef __LITTLE_ENDIAN__
    d_8to24table[255] &= 0x00ffffff;
#else
    d_8to24table[255] &= 0xffffff00;
#endif // __LITTLE_ENDIAN__
    
    for (UInt16 i = 0; i < (1 << 15); i++)
    {
        const SInt  red             = ((i & 0x001F) << 3) + 4;
		const SInt  green           = ((i & 0x03E0) >> 2) + 4;
		const SInt  blue            = ((i & 0x7C00) >> 7) + 4;
        SInt        bestDistance    = FD_SQUARE (10000);
        UInt        bestValue       = 0;
        
		pPalette = (UInt8*) d_8to24table;
        
		for (UInt16 j = 0; j < 256; ++j)
        {
            const SInt  redNew      = red   - (SInt) pPalette[j * 4 + 0];
            const SInt  greenNew    = green - (SInt) pPalette[j * 4 + 1];
            const SInt  blueNew     = blue  - (SInt) pPalette[j * 4 + 2];
            const SInt  distance    = FD_SQUARE (redNew) + FD_SQUARE (greenNew) + FD_SQUARE (blueNew);
            
            if (distance < bestDistance)
            {
                bestValue     = j;
				bestDistance  = distance;
            }
		}
        
		d_15to8table[i] = bestValue;
    }
}

//----------------------------------------------------------------------------------------------------------------------------

void	VID_ShiftPalette (UInt8* pPalette)
{
    FD_UNUSED (pPalette);
}

//----------------------------------------------------------------------------------------------------------------------------

SInt 	VID_SetMode (SInt modeNum, UInt8* pPalette)
{
    FD_UNUSED (modeNum, pPalette);
    
    return 1;
}

//----------------------------------------------------------------------------------------------------------------------------

#ifdef QUAKE_WORLD

void	VID_SetWindowTitle (char* pTitle)
{
    if (gVidWindow != nil)
    {
        if (pTitle)
        {
            [gVidWindow setTitle: [NSString stringWithCString: pTitle encoding: NSASCIIStringEncoding]];
        }
        else
        {
            [gVidWindow setTitle: [[NSRunningApplication currentApplication] localizedName]];
        }
    }
}

#endif /* QUAKE_WORLD */

//----------------------------------------------------------------------------------------------------------------------------

void VID_SetWait (UInt32 state)
{
    const BOOL  enable  = (state != 0);
    
    [gVidWindow setVsync: enable];
    
    if (state == [gVidWindow vsync])
    {
        if (enable == YES)
        {
            Con_Printf ("video wait successfully enabled!\n");
        }
        else
        {
            Con_Printf ("video wait successfully disabled!\n");
        }
        
        gGLVideoWait = vid_wait.value;
    }
    else
    {
        Con_Printf ("Error while trying to change video wait!\n");
        
        vid_wait.value = gGLVideoWait;
    }   
}

//----------------------------------------------------------------------------------------------------------------------------

float    VID_SetGamma (float value, BOOL update)
{
    if (value < 0.5f)
    {
        value = 0.5f;
    }
    else if (value > 1.0f)
    {
        value = 1.0f;
    }
    
    if (gVidDisplayFullscreen == YES)
    {        
        [gVidDisplay setGamma: ((1.4f - value) * 2.5f) update: update];
    }
    
    return value;
}

//----------------------------------------------------------------------------------------------------------------------------

BOOL	VID_SetDisplay (void)
{
    NSString*       displayName = [[FDPreferences sharedPrefs] stringForKey: QUAKE_PREFS_KEY_GL_DISPLAY];
    FDDisplay*      display     = nil;
    
    for (display in [FDDisplay displays])
    {
        if ([[display description] isEqualToString: displayName] == YES)
        {
            break;
        }
    }
    
    if (display == nil)
    {
        display = [FDDisplay mainDisplay];
    }
    
    gVidDisplay             = display;
    gVidFadeAllDisplays     = [[FDPreferences sharedPrefs] boolForKey: QUAKE_PREFS_KEY_GL_FADE_ALL];
    gVidDisplayFullscreen   = [[FDPreferences sharedPrefs] boolForKey: QUAKE_PREFS_KEY_GL_FULLSCREEN];
    
    return gVidDisplay != nil;
}

//----------------------------------------------------------------------------------------------------------------------------

FDDisplayMode*    VID_FindDisplayMode (NSString* modeStr, NSUInteger bitsPerPixel)
{
    FDDisplayMode* foundMode = nil;
    
    if (modeStr != nil)
    {
        for (FDDisplayMode* displayMode in [gVidDisplay displayModes])
        {
            if (([[displayMode description] isEqualToString: modeStr] == YES) && ([displayMode bitsPerPixel] == bitsPerPixel))
            {
                foundMode = displayMode;
                break;
            }
        }
    }
    else
    {
        for (FDDisplayMode* displayMode in [gVidDisplay displayModes])
        {
            if ([displayMode bitsPerPixel] == bitsPerPixel)
            {
                foundMode = displayMode;
                break;
            }
        }
    }
    
    return foundMode;
}

//----------------------------------------------------------------------------------------------------------------------------

BOOL	VID_SetDisplayMode (void)
{    
    const NSInteger bitsPerPixel    = [[FDPreferences sharedPrefs] integerForKey: QUAKE_PREFS_KEY_GL_COLORS];
    NSInteger       numSamples      = [[FDPreferences sharedPrefs] integerForKey: QUAKE_PREFS_KEY_GL_SAMPLES];
    NSString*       modeStr         = [[FDPreferences sharedPrefs] stringForKey: QUAKE_PREFS_KEY_GL_DISPLAY_MODE];
    
    gVidDisplayMode = VID_FindDisplayMode (modeStr, bitsPerPixel);

    if (gVidDisplayMode == nil)
    {
        gVidDisplayMode = VID_FindDisplayMode (modeStr, 32);
    }
    
    if (gVidDisplayMode == nil)
    {
        gVidDisplayMode = VID_FindDisplayMode (nil, 32);
    }

    if (gVidDisplayMode == nil)
    {
        gVidDisplayMode = VID_FindDisplayMode (nil, 16);
    }
    
    if (gVidDisplayMode == nil)
    {
        Sys_Error ("Failed to find valid display mode!");
    }
    
    if ([gVidDisplay hasFSAA] == NO)
    {
        numSamples = 0;
    }
    
    gGLDisplayWidth     = [gVidDisplayMode width];
    gGLDisplayHeight    = [gVidDisplayMode height];
    
    if (gVidDisplayFullscreen == YES)
    {
        if (gVidFadeAllDisplays)
        {
            [FDDisplay fadeOutAllDisplays: VID_FADE_DURATION];
            [FDDisplay captureAllDisplays];
        }
        else
        {
            [gVidDisplay fadeOutDisplay: VID_FADE_DURATION];
            [gVidDisplay captureDisplay];
        }
        
        if (![gVidDisplay setDisplayMode: gVidDisplayMode])
        {
            Sys_Error ("Unable to switch the displaymode!");
        }

        gVidWindow = [[FDWindow alloc] initForDisplay: gVidDisplay samples: numSamples];
        
        [gVidWindow setResizeHandler: &VID_ResizeHandler forContext: nil];
        [gVidWindow makeKeyAndOrderFront: nil];
        [gVidWindow flushWindow];
            
        VID_SetGamma (v_gamma.value, NO);
        
        if (gVidFadeAllDisplays)
        {
            [FDDisplay fadeInAllDisplays: VID_FADE_DURATION];
        }
        else
        {
            
            [gVidDisplay fadeInDisplay: VID_FADE_DURATION];
        }
    }
    else
    {
        int     index;
        NSRect  contentRect;
        
        if ((index = COM_CheckParm("-width")))
        {
            gGLDisplayWidth = atoi (com_argv[index + 1]);
        }
        
        if (gGLDisplayWidth < 320)
        {
            gGLDisplayWidth = 320;
        }
        
        if ((index = COM_CheckParm("-height")))
        {
            gGLDisplayHeight = atoi (com_argv[index + 1]);
        }

        if (gGLDisplayHeight < 240)
        {
            gGLDisplayHeight = 240;
        }
        
		contentRect = NSMakeRect (0.0, 0.0, gGLDisplayWidth, gGLDisplayHeight);

        gVidWindow = [[FDWindow alloc] initWithContentRect: contentRect samples: numSamples];
        
        [gVidWindow setTitle: [[NSRunningApplication currentApplication] localizedName]];
        [gVidWindow setResizeHandler: &VID_ResizeHandler forContext: nil];
        [gVidWindow centerForDisplay: gVidDisplay];
        [gVidWindow makeKeyAndOrderFront: nil];
        [gVidWindow makeMainWindow];
        [gVidWindow flushWindow];
    }
    
    gGLDisplayIs8Bit = ([gVidDisplayMode bitsPerPixel] == 8);
    
    VID_SetWait ((UInt32) vid_wait.value);
    
    return YES;
}

//----------------------------------------------------------------------------------------------------------------------------

void	VID_Init (unsigned char* pPalette)
{
    char    myGLDir[MAX_OSPATH];
    UInt    i;

    // register miscelanous vars:
    Cvar_RegisterVariable (&vid_mode);
    Cvar_RegisterVariable (&_vid_default_mode);
    Cvar_RegisterVariable (&_vid_default_blit_mode);
    Cvar_RegisterVariable (&vid_wait);
    Cvar_RegisterVariable (&vid_redrawfull);
    Cvar_RegisterVariable (&_windowed_mouse);
    Cvar_RegisterVariable (&gl_anisotropic);
    Cvar_RegisterVariable (&gl_fsaa);
    Cvar_RegisterVariable (&gl_truform);
    Cvar_RegisterVariable (&gl_multitexture);
    Cvar_RegisterVariable (&gl_ztrick);
        
    // setup basic width/height:
    vid.maxwarpwidth = VID_WARP_WIDTH;
    vid.maxwarpheight = VID_WARP_HEIGHT;
    vid.colormap = host_colormap;
    vid.fullbright = 256 - LittleLong (*((SInt *)vid.colormap + 2048));

    if (VID_SetDisplay() == NO)
    {
        Sys_Error ("No valid display found!");
    }
    
    if (VID_SetDisplayMode() == NO)
    {
        Sys_Error ("Failed to switch to display mode!");
    }
    
    vid.width       = (unsigned) gGLDisplayWidth;
    vid.height      = (unsigned) gGLDisplayHeight;
    vid.aspect      = ((float) vid.height / (float) vid.width) * (320.0f / 240.0f);
    vid.numpages    = 2;

    // setup console width according to display width:
    if ((i = COM_CheckParm("-conwidth")))
    {
        vid.conwidth = Q_atoi (com_argv[i+1]);
    }
    else
    {
        vid.conwidth = vid.width;
    }
    
    vid.conwidth &= 0xfff8;

    // setup console height according to display height:
    if ((i = COM_CheckParm ("-conheight")))
    {
        vid.conheight = Q_atoi (com_argv[i+1]);
    }
    else
    {
        vid.conheight = vid.height;
    }
    
    // check the console size:
    if (vid.conwidth > ((unsigned) gGLDisplayWidth))
    {
        vid.conwidth = (unsigned) gGLDisplayWidth;
    }
    
    if (vid.conheight > ((unsigned) gGLDisplayHeight))
    {
        vid.conheight = (unsigned) gGLDisplayHeight;
    }
    
    if (vid.conwidth < VID_CONSOLE_MIN_WIDTH)
    {
        vid.conwidth = VID_CONSOLE_MIN_WIDTH;
    }
    
    if (vid.conheight < VID_CONSOLE_MIN_HEIGHT)
    {
        vid.conheight = VID_CONSOLE_MIN_HEIGHT;
    }
    
    // setup OpenGL:
    GL_Init ();

    // setup the "glquake" folder within the "id1" folder:
    snprintf (myGLDir, MAX_OSPATH, "%s/glquake", com_gamedir);
    Sys_mkdir (myGLDir);

    // enable the video options menu:
    vid_menudrawfn = VID_MenuDraw;
    vid_menukeyfn = VID_MenuKey;
    
    // finish up initialization:
    VID_CheckGamma (pPalette);
    VID_SetPalette (pPalette);
    Con_SafePrintf ("Video mode %dx%d initialized.\n", gGLDisplayWidth, gGLDisplayHeight);
    
    vid.recalc_refdef = 1;
}

//----------------------------------------------------------------------------------------------------------------------------

void	VID_Shutdown (void)
{
    if ([FDDisplay isAnyDisplayCaptured] == YES)
    {
        if (gVidFadeAllDisplays == YES)
        {
            [FDDisplay fadeOutAllDisplays: VID_FADE_DURATION];
        }
        else
        {
            [gVidDisplay fadeOutDisplay: VID_FADE_DURATION];
        }
    }
    
    if (gVidWindow != nil)
    {
        [gVidWindow close];
        gVidWindow = nil;
    }

    if ([FDDisplay isAnyDisplayCaptured] == YES)
    {
        [gVidDisplay setDisplayMode: [gVidDisplay originalMode]];
        
        VID_SetGamma (1.0f, NO);
        
        if (gVidFadeAllDisplays == YES)
        {
            [FDDisplay releaseAllDisplays];
            [FDDisplay fadeInAllDisplays: VID_FADE_DURATION];
        }
        else
        {
            [gVidDisplay releaseDisplay];
            [gVidDisplay fadeInDisplay: VID_FADE_DURATION];
        }
    }
}

//----------------------------------------------------------------------------------------------------------------------------

BOOL	VID_HideFullscreen (BOOL hide)
{
    static BOOL		isHidden = NO;
    
    if (isHidden == hide || gVidDisplayFullscreen == NO)
    {
        return YES;
    }
    
    if (hide == YES)
    {
        if (gVidFadeAllDisplays == YES)
        {
            [FDDisplay fadeOutAllDisplays: VID_FADE_DURATION];
        }
        else
        {
            [gVidDisplay fadeOutDisplay: VID_FADE_DURATION];
        }
        
        [gVidWindow orderOut: nil];
        [gVidDisplay setDisplayMode: [gVidDisplay originalMode]];
        
        VID_SetGamma (1.0f, NO);
        
        if (gVidFadeAllDisplays == YES)
        {
            [FDDisplay releaseAllDisplays];
            [FDDisplay fadeInAllDisplays: VID_FADE_DURATION];
        }
        else
        {
            [gVidDisplay releaseDisplay];
            [gVidDisplay fadeInDisplay: VID_FADE_DURATION];
        }
    }
    else
    {
        if (gVidFadeAllDisplays == YES)
        {
            [FDDisplay fadeOutAllDisplays: VID_FADE_DURATION];
            [FDDisplay captureAllDisplays];
        }
        else
        {
            [gVidDisplay fadeOutDisplay: VID_FADE_DURATION];
            [gVidDisplay captureDisplay];
        }
        
        [gVidDisplay setDisplayMode: gVidDisplayMode];
        [gVidWindow makeKeyAndOrderFront: nil];
                
        VID_SetGamma (v_gamma.value, NO);
        
        if (gVidFadeAllDisplays == YES)
        {
            [FDDisplay fadeInAllDisplays: VID_FADE_DURATION];
        }
        else
        {
            [gVidDisplay fadeInDisplay: VID_FADE_DURATION];
        }
    }
    
    isHidden = hide;
    
    return YES;
}

//----------------------------------------------------------------------------------------------------------------------------

void    VID_ResizeHandler (id fdView, void* pContext)
{
    FD_UNUSED (pContext);
    
    const NSRect    frame   = [fdView frame];
    const float     width   = NSWidth (frame);
    const float     height  = NSHeight (frame);
    
    gGLDisplayWidth     = width;
    glwidth             = width;
    vid.width           = width;
    vid.conwidth        = width;
    
    gGLDisplayHeight    = height;
    glheight            = height;
    vid.height          = height;
    vid.conheight       = height;

    vid.recalc_refdef   = 1;

    Host_Frame (0.02f);
    Host_Frame (0.02f);
}

//----------------------------------------------------------------------------------------------------------------------------

void	VID_MenuDraw (void)
{
    qpic_t* picture = Draw_CachePic ("gfx/vidmodes.lmp");
    UInt8	row     = VID_FIRST_MENU_LINE;
	char	buffer[16];

    M_DrawPic ((320 - picture->width) / 2, 4, picture);
    
    // draw vid_wait option:
    M_Print (VID_FONT_WIDTH, row, "Video Sync:");
    if (vid_wait.value)
    {
        M_Print ((39 - 2) * VID_FONT_WIDTH, row, "On");
    }
    else
    {
        M_Print ((39 - 3) * VID_FONT_WIDTH, row, "Off");
    }
    
    if (gGLMenuLine == row)
    {
        gGLMenuItem = VID_MENUITEM_WAIT;
    }

    // draw FSAA option:
    if (gl_fsaaavailable == YES)
    {
        row += VID_FONT_HEIGHT;
        M_Print (VID_FONT_WIDTH, row, "FSAA:");
        if (gl_fsaa.value == 0.0f)
        {
             M_Print ((39 - 3) * VID_FONT_WIDTH, row, "Off");
        }
        else
        {
            snprintf (buffer, 16, "%dx", (int) gl_fsaa.value);
            M_Print ((int)(39 - strlen (buffer)) * VID_FONT_WIDTH, row, buffer);
        }
		
        if (gGLMenuLine == row)
		{
			gGLMenuItem = VID_MENUITEM_FSAA;
		}
    }
    
    // draw anisotropic option:
    if (gl_texturefilteranisotropic == YES)
    {
        row += VID_FONT_HEIGHT;
        M_Print (VID_FONT_WIDTH, row, "Anisotropic Texture Filtering:");
		
        if (gl_anisotropic.value)
		{
			M_Print ((39 - 2) * VID_FONT_WIDTH, row, "On");
		}
		else
		{
            M_Print ((39 - 3) * VID_FONT_WIDTH, row, "Off");
		}
		
        if (gGLMenuLine == row)
		{
			gGLMenuItem = VID_MENUITEM_ANISOTROPIC;
		}
    }
    
    // draw multitexture option:
    if (gGLMultiTextureAvailable == YES)
    {
        row += VID_FONT_HEIGHT;
        M_Print (VID_FONT_WIDTH, row, "Multitexturing:");
		
        if (gl_multitexture.value)
		{
			M_Print ((39 - 2) * VID_FONT_WIDTH, row, "On");
		}
		else
		{
			M_Print ((39 - 3) * VID_FONT_WIDTH, row, "Off");
		}
		
        if (gGLMenuLine == row)
		{
			gGLMenuItem = VID_MENUITEM_MULTITEXTURE;
		}
    }
    
    // draw truform option:
    if (gpGLPNTrianglesiATI != NULL)
    {
        row += VID_FONT_HEIGHT;
        M_Print (VID_FONT_WIDTH, row, "ATI Truform Tesselation Level:");
		
        if (gl_truform.value < 0)
        {
             M_Print ((39 - 3) * VID_FONT_WIDTH, row, "Off");
        }
        else
        {
            snprintf (buffer, 16, "%dx", (int) gl_truform.value);
            M_Print ((int)(39 - strlen (buffer)) * VID_FONT_WIDTH, row, buffer);
        }
		
        if (gGLMenuLine == row)
		{
			gGLMenuItem = VID_MENUITEM_TRUFORM;
		}
    }

    M_Print (4 * VID_FONT_WIDTH + 4, 36 + 23 * VID_FONT_HEIGHT, "Video modes must be set at the");
    M_Print (11 * VID_FONT_WIDTH + 4, 36 + 24 * VID_FONT_HEIGHT, "startup dialog!");
    
    M_DrawCharacter (0, gGLMenuLine, 12 + ((int)(realtime * 4) & 1));
    gGLMenuMaxLine = row;
}

//----------------------------------------------------------------------------------------------------------------------------

void	VID_MenuKey (int key)
{
    switch (key)
    {
        case K_ESCAPE:
            S_LocalSound ("misc/menu1.wav");
            M_Menu_Options_f ();
            break;
            
		case K_UPARROW:
            S_LocalSound ("misc/menu1.wav");
            gGLMenuLine -= VID_FONT_HEIGHT;
            if (gGLMenuLine < VID_FIRST_MENU_LINE)
            {
                gGLMenuLine = gGLMenuMaxLine;
            }
            break;
            
		case K_DOWNARROW:
            S_LocalSound ("misc/menu1.wav");
            gGLMenuLine += VID_FONT_HEIGHT;
            if (gGLMenuLine > gGLMenuMaxLine)
            {
                gGLMenuLine = VID_FIRST_MENU_LINE;
            }
            break;
            
        case K_LEFTARROW:
            S_LocalSound ("misc/menu1.wav");
			
            switch (gGLMenuItem)
            {
                case VID_MENUITEM_WAIT:
                    Cvar_SetValue (vid_wait.name, (vid_wait.value == 0.0f) ? 1.0f : 0.0f);
                    break;
                case VID_MENUITEM_FSAA:
                    Cvar_SetValue (gl_fsaa.name, (gl_fsaa.value <= 0.0f) ? 8.0f : gl_fsaa.value - 4.0f);
                    break;
                case VID_MENUITEM_ANISOTROPIC:
                    Cvar_SetValue (gl_anisotropic.name, (gl_anisotropic.value == 0.0f) ? 1.0f : 0.0f);
                    break;
                case VID_MENUITEM_MULTITEXTURE:
                    Cvar_SetValue (gl_multitexture.name, (gl_multitexture.value == 0.0f) ? 1.0f : 0.0f);
                    break;
                case VID_MENUITEM_TRUFORM:
                    Cvar_SetValue (gl_truform.name, (gl_truform.value <= -1.0f) ? 7.0f : gl_truform.value-1.0f);
                    break;
            }
            break;
            
        case K_RIGHTARROW:
		case K_ENTER:
            S_LocalSound ("misc/menu1.wav");
			
            switch (gGLMenuItem)
            {
                case VID_MENUITEM_WAIT:
                    Cvar_SetValue (vid_wait.name, (vid_wait.value == 0.0f) ? 1.0f : 0.0f);
                    break;
                case VID_MENUITEM_FSAA:
                    Cvar_SetValue (gl_fsaa.name, (gl_fsaa.value >= 8.0f) ? 0.0f : gl_fsaa.value + 4.0f);
                    break;
                case VID_MENUITEM_ANISOTROPIC:
                    Cvar_SetValue (gl_anisotropic.name, (gl_anisotropic.value == 0.0f) ? 1.0f : 0.0f);
                    break;
                case VID_MENUITEM_MULTITEXTURE:
                    Cvar_SetValue (gl_multitexture.name, (gl_multitexture.value == 0.0f) ? 1.0f : 0.0f);
                    break;
                case VID_MENUITEM_TRUFORM:
                    Cvar_SetValue (gl_truform.name, (gl_truform.value >= 7.0f) ? -1.0f : gl_truform.value+1.0f);
                    break;
            }
            break;
            
        default:
            break;
    }
}

//----------------------------------------------------------------------------------------------------------------------------

BOOL	GL_ExtensionSupported (const char* pExtension)
{
    const char* pExtensions = gl_extensions;
    size_t		len         = 0;

    if (pExtension == NULL || pExtensions == NULL || strchr (pExtension, ' ') != NULL	|| *pExtension == '\0')
    {
        return NO;
    }

    len = strlen (pExtension);
    
    while (1)
    {
        const char*   pStart  = strstr (pExtensions, pExtension);
		const char*   pEnd    = pStart + len;
        
        if (pStart == NULL)
        {
            break;
        }
		
        if ((pStart == pExtensions || *(pStart - 1) == ' ') && (*pEnd == ' ' || *pEnd == '\0'))
        {
            return YES;
        }

        pExtensions = pEnd;
    }

    return NO;
}

//----------------------------------------------------------------------------------------------------------------------------

void	GL_CheckMultiTextureExtensions (void) 
{
    if (GL_ExtensionSupported ("GL_ARB_multitexture") == YES && gl_luminace_lightmaps == YES && !COM_CheckParm ("-nomtex"))
    {
        GLint	myMaxTextureUnits = 0;

        glGetIntegerv (GL_MAX_TEXTURE_UNITS_ARB, &myMaxTextureUnits); 
        
        gGLMultiTextureAvailable = (myMaxTextureUnits >= 2);

        if (gGLMultiTextureAvailable)
        {
            qglSelectTextureSGIS    = &glActiveTextureARB;
            qglMTexCoord2fSGIS      = &glMultiTexCoord2fARB;
            
            Con_Printf ("Found GL_ARB_multitexture...\n(%d texture units)\n", myMaxTextureUnits);
        }
        else
        {
            Con_Printf ("Not enough texture units (%d).\nGL_ARB_multitexture disabled.\n", myMaxTextureUnits);
        }
    }
    else
    {
        gGLMultiTextureAvailable = NO;
    }
    
    gl_mtexable = NO;
}

//----------------------------------------------------------------------------------------------------------------------------

void	GL_CheckPalettedTexture (void)
{
    gl_palettedtex = GL_ExtensionSupported ("GL_EXT_paletted_texture");
    
    if (gl_palettedtex)
    {
        Con_Printf ("Found GL_EXT_paletted_texture...\n");
    }
}

//----------------------------------------------------------------------------------------------------------------------------

void	GL_CheckPNTrianglesExtension (const char* pExtensionName, const char* pFunctionName)
{
    if (GL_ExtensionSupported (pExtensionName))
    {
        gpGLPNTrianglesiATI = Sys_GetProcAddress (pFunctionName, false);
        
        if (gpGLPNTrianglesiATI)
        {
            Con_Printf ("Found %s...\n", pExtensionName);
        }
    }
}

//----------------------------------------------------------------------------------------------------------------------------

void	GL_CheckPNTrianglesExtensions (void)
{
    GL_CheckPNTrianglesExtension ("GL_ATIX_pn_triangles", "glPNTrianglesiATIX");

    if (!gpGLPNTrianglesiATI)
    {
        GL_CheckPNTrianglesExtension ("GL_ATI_pn_triangles", "glPNTrianglesiATI");
    }
}

//----------------------------------------------------------------------------------------------------------------------------

void	GL_CheckSwitchFSAAOnTheFly (void)
{
    // Changing the FSAA samples is only available for Radeon boards
    // [a sample buffer is not required at the pixelformat].
    //
    // We don't want to support FSAA under 10.1.x because the current driver will crash the WindowServer.
    // Thus we check for the Radeon string AND the GL_ARB_multisample extension, which is only available for
    // Radeon boards under 10.2 or later.
    
    gl_fsaaavailable    = (strstr (gl_renderer, "ATI Radeon") && GL_ExtensionSupported ("GL_ARB_multisample") == YES);
    gGLFSAALevel        = (float) [[FDPreferences sharedPrefs] integerForKey: QUAKE_PREFS_KEY_GL_SAMPLES];
    
    if (gl_fsaaavailable)
    {
        Con_Printf ("Found ATI FSAA...\n");
    }

    Cvar_SetValue (gl_fsaa.name, gGLFSAALevel);
}

//----------------------------------------------------------------------------------------------------------------------------

void	GL_CheckTextureFilterAnisotropic (void)
{
    gl_texturefilteranisotropic = GL_ExtensionSupported ("GL_EXT_texture_filter_anisotropic");
    
    if (gl_texturefilteranisotropic)
    {
        Con_Printf ("Found GL_EXT_texture_filter_anisotropic...\n");
    }
}

//----------------------------------------------------------------------------------------------------------------------------

void	GL_CheckLuminanceLightmaps (void)
{
    // We allow luminance lightmaps only on MacOS X v10.2 or later because of a driver related
    // performance issue.
    //
    // NSAppKitVersionNumber10_1 is defined as 620.0. 10.2 has 663.0. So test against 663.0:
    
    gl_luminace_lightmaps = (NSAppKitVersionNumber >= 663.0);
    
    if (gl_luminace_lightmaps)
    {
        Con_Printf ("Found MacOS X v10.2 or later. Using luminance lightmaps...\n");
    }
    else
    {
        Con_Printf ("Found MacOS X v10.1 or earlier.  Using RGBA lightmaps...\n");
    }
}

//----------------------------------------------------------------------------------------------------------------------------

void	GL_CheckTextureRAM (GLenum target, GLint level, GLint internalFormat, GLsizei width, GLsizei height, GLsizei depth,
                            GLint border, GLenum format, GLenum type)
{
    GLint	actualWidth = -1;
    GLenum	error       = 0;
    
    // flush existing errors:
    glGetError ();

    // check our target texture type:
    switch (target)
    {
        case GL_TEXTURE_1D:
        case GL_PROXY_TEXTURE_1D:
            target = GL_PROXY_TEXTURE_1D;
            glTexImage1D (target, level, internalFormat, width, border, format, type, NULL); 
            break;
            
        case GL_TEXTURE_2D:
        case GL_PROXY_TEXTURE_2D:
            target = GL_PROXY_TEXTURE_2D;
            glTexImage2D (target, level, internalFormat, width, height, border, format, type, NULL); 
            break;
        
        case GL_TEXTURE_3D:
        case GL_PROXY_TEXTURE_3D:
            target = GL_PROXY_TEXTURE_3D;
            glTexImage3D (target, level, internalFormat, width, height, depth, border, format, type, NULL); 
            break;
            
        default:
            return;
    }
    
    error = glGetError ();

    // get the width of the texture [should be zero on failure]:
    glGetTexLevelParameteriv (target, level, GL_TEXTURE_WIDTH, &actualWidth);
    
    // now let's see if the width is equal to our requested value:
    if ((error != GL_NO_ERROR) || (width != actualWidth))
    {
        Sys_Error ("Out of texture RAM. Please try a lower resolution and/or depth!");
    }
}

//----------------------------------------------------------------------------------------------------------------------------

void	GL_Init (void)
{
    // show OpenGL stats at the console:
    gl_vendor = (const char*) glGetString (GL_VENDOR);
    Con_Printf ("GL_VENDOR: %s\n", gl_vendor);
    
    gl_renderer = (const char*) glGetString (GL_RENDERER);
    Con_Printf ("GL_RENDERER: %s\n", gl_renderer);
    
    gl_version = (const char*) glGetString (GL_VERSION);
    Con_Printf ("GL_VERSION: %s\n", gl_version);
    
    gl_extensions = (const char*) glGetString (GL_EXTENSIONS);
    Con_Printf ("GL_EXTENSIONS: %s\n", gl_extensions);
    
    // not required for MacOS X, but nevertheless:
    isPermedia = !strncasecmp ((char*) gl_renderer, "Permedia", 8);

    // check if we have fast luminance lightmaps:
    GL_CheckLuminanceLightmaps ();

    // check for multitexture extensions:
    GL_CheckMultiTextureExtensions ();

    // check for pn_triangles extension:
    GL_CheckPNTrianglesExtensions ();

    // check for texture filter anisotropic extension:
    GL_CheckTextureFilterAnisotropic ();

    // check if FSAA is available:
    GL_CheckSwitchFSAAOnTheFly ();

    // setup OpenGL:    
    glClearColor (1,0,0,0);
    glEnable (GL_TEXTURE_2D);
    glAlphaFunc (GL_GREATER, 0.666f);
    glEnable (GL_ALPHA_TEST);
    glPolygonMode (GL_FRONT_AND_BACK, GL_FILL);
    glCullFace (GL_FRONT);
    glShadeModel (GL_FLAT);

    if ([[FDPreferences sharedPrefs] integerForKey: QUAKE_PREFS_KEY_GL_SAMPLES] > 0)
    {
        glEnable (GL_MULTISAMPLE_ARB);
    }
    
    glTexParameterf (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameterf (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameterf (GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameterf (GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    
    glBlendFunc (GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    
    glTexEnvf (GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
}

//----------------------------------------------------------------------------------------------------------------------------

qboolean GL_SaveScreenshot (const char* filename)
{
    NSString* path = [NSString stringWithCString: filename encoding: NSASCIIStringEncoding];
    
    return ([FDGLScreenshot writeToPNG: path] == YES ? true : false);
}

//----------------------------------------------------------------------------------------------------------------------------

void	GL_SetFSAA (UInt32 fsaaLevel)
{
	GLint	actualFsaaLevel = 0;
    
    // check the level value:
    if (fsaaLevel != 0 && fsaaLevel != 4 && fsaaLevel != 8)
    {
        Cvar_SetValue (gl_fsaa.name, gGLFSAALevel);
        
        Con_Printf ("Invalid FSAA level, accepted values are 0, 4 or 8!\n");
        
        return;
    }
    
    // check if FSAA is available:
    if (gl_fsaaavailable == NO)
    {
        gGLFSAALevel = gl_fsaa.value;
        
        if (fsaaLevel != 0)
        {
            Cvar_SetValue (gl_fsaa.name, gGLFSAALevel);
            
            Con_Printf ("FSAA not supported with the current graphics board!\n");
        }
        
        return;
    }

    // convert the ARB_multisample value for the ATI hack:
    if (fsaaLevel == 0)
    {
        actualFsaaLevel = 1;
    }
    else
    {
        actualFsaaLevel = fsaaLevel >> 1;
    }
    
    // set the level:
    [[gVidWindow openGLContext] makeCurrentContext];
    
    if (CGLSetParameter (CGLGetCurrentContext (), VID_ATI_FSAA_LEVEL, &actualFsaaLevel) == CGDisplayNoErr)
    {
        gGLFSAALevel = fsaaLevel;

        Con_Printf ("FSAA level set to: %d!\n", fsaaLevel);
    }
    else
    {
        Con_Printf ("Error while trying to set the new FSAA Level!\n");
    }
    
    Cvar_SetValue (gl_fsaa.name, gGLFSAALevel);
}

//----------------------------------------------------------------------------------------------------------------------------

void	GL_SetPNTriangles (SInt32 triangleLevel)
{
    if (gpGLPNTrianglesiATI != NULL)
    {
        if (triangleLevel >= 0)
        {
            if (triangleLevel > 7)
            {
                triangleLevel = 7;
                
                Con_Printf ("Clamping to max. pntriangle level 7!\n");
            }
            
            // enable pn_triangles. lightning required due to a bug of OpenGL!
            glEnable (GL_PN_TRIANGLES_ATI);
            glEnable (GL_LIGHTING);
            glLightModelfv (GL_LIGHT_MODEL_AMBIENT, gGLTruformAmbient);
            glEnable (GL_COLOR_MATERIAL);

            // point mode:
            gpGLPNTrianglesiATI (GL_PN_TRIANGLES_POINT_MODE_ATI, GL_PN_TRIANGLES_POINT_MODE_CUBIC_ATI);
                        
            // normal mode (no normals used at all by Quake):
            gpGLPNTrianglesiATI (GL_PN_TRIANGLES_NORMAL_MODE_ATI, GL_PN_TRIANGLES_NORMAL_MODE_QUADRATIC_ATI);

            // tesselation level:
            gpGLPNTrianglesiATI (GL_PN_TRIANGLES_TESSELATION_LEVEL_ATI, triangleLevel);

            Con_Printf ("Truform enabled, current tesselation level: %d!\n", triangleLevel);
        }
        else
        {
            triangleLevel = -1;
            
            glDisable (GL_PN_TRIANGLES_ATI);
            glDisable (GL_LIGHTING);
            
            Con_Printf ("Truform disabled!\n");
        }
        
        gGLPNTriangleLevel = triangleLevel;
        Cvar_SetValue (gl_truform.name, gGLPNTriangleLevel);
    }
    else
    {
        if (triangleLevel != -1)
        {
            Con_Printf ("pntriangles not supported with the current graphics board!\n");
        }
        
		gGLPNTriangleLevel = (gl_truform.value > 7.0f) ? 7.0f : gl_truform.value;
        
		if (gGLPNTriangleLevel < 0.0f)
		{
			gGLPNTriangleLevel = -1.0f;
		}
		
        Cvar_SetValue (gl_truform.name, gGLPNTriangleLevel);
    }
}

//----------------------------------------------------------------------------------------------------------------------------

void	GL_SetTextureFilterAnisotropic (UInt32 enable)
{
    // clamp the value to 1 [= enabled]:
    gGLAnisotropic = (enable != 0) ? YES : NO;
    Cvar_SetValue (gl_anisotropic.name, gGLAnisotropic);
    
    // check if anisotropic filtering is available:
    if (gl_texturefilteranisotropic == NO)
    {
        gl_texureanisotropylevel = 1.0f;
        
        if (enable != 0)
        {
            Con_Printf ("Anisotropic tetxure filtering not supported with the current graphics card!\n");
        }
    }
    else
    {
        // enable/disable anisotropic filtering:
        if (enable == 0)
        {
            gl_texureanisotropylevel = 1.0f;
        }
        else
        {
            glGetFloatv (GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &gl_texureanisotropylevel);
        }
    }
}

//----------------------------------------------------------------------------------------------------------------------------

void	GL_SetMultiTexture (UInt32 enable)
{
    // clamp the value to 1 [= enabled]:
    gGLMultiTexture = (enable != 0) ? YES : NO;
    Cvar_SetValue (gl_multitexture.name, gGLMultiTexture);
    
    // check if multitexturing is available:
    if (gGLMultiTextureAvailable == YES)
    {
        gl_mtexable = gGLMultiTexture;
        
        if (gl_mtexable == YES)
        {
            Con_Printf ("Multitexturing enabled!\n");
        }
        else
        {
            Con_Printf ("Multitexturing disabled!\n");
        }
    }
    else
    {
        Con_Printf ("Multitexturing not available!\n");
    }
}

//----------------------------------------------------------------------------------------------------------------------------

void	GL_BeginRendering (int *x, int *y, int *width, int *height)
{
    *x      = 0;
    *y      = 0;
    *width  = (int) gGLDisplayWidth;
    *height = (int) gGLDisplayHeight;
}

//----------------------------------------------------------------------------------------------------------------------------

void	GL_EndRendering (void)
{
    const BOOL cursorIsVisible = (_windowed_mouse.value == 0.0f)  && (gVidDisplayFullscreen == NO);
    
    [gVidWindow endFrame];

    if (cursorIsVisible != [gVidWindow isCursorVisible])
    {
        [gVidWindow setCursorVisible: cursorIsVisible];
    }

    v_gamma.value = VID_SetGamma (v_gamma.value, YES);
    
    // check if video_wait changed:
    if(vid_wait.value != gGLVideoWait)
    {
        VID_SetWait ((UInt32) vid_wait.value);
    }

    // check if anisotropic texture filtering changed:
    if (gl_anisotropic.value != gGLAnisotropic)
    {
        GL_SetTextureFilterAnisotropic ((UInt32) gl_anisotropic.value);
    }

    // check if vid_fsaa changed:
    if (gl_fsaa.value != gGLFSAALevel)
    {
        GL_SetFSAA ((UInt32) gl_fsaa.value);
    }

    // check if truform changed:
    if (gl_truform.value != gGLPNTriangleLevel)
    {
        GL_SetPNTriangles ((SInt32) gl_truform.value);
    }

    // check if multitexture changed:
    if (gl_multitexture.value != gGLMultiTexture)
    {
        GL_SetMultiTexture ((UInt32) gl_multitexture.value);
    }
}

//----------------------------------------------------------------------------------------------------------------------------
