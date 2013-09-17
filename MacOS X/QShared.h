//----------------------------------------------------------------------------------------------------------------------------
//
// "QShared.h"
//
// Written by:	Axel 'awe' Wefers			[mailto:awe@fruitz-of-dojo.de].
//				©2001-2012 Fruitz Of Dojo 	[http://www.fruitz-of-dojo.de].
//
//----------------------------------------------------------------------------------------------------------------------------

#define FRUITZ_OF_DOJO_URL                  @"http://www.fruitz-of-dojo.de/"

//----------------------------------------------------------------------------------------------------------------------------

#define	QUAKE_PREFS_KEY_BASE_PATH           @"Quake ID1 Path"
#define QUAKE_PREFS_KEY_ARGUMENTS           @"Quake Command-Line Arguments"
#define QUAKE_PREFS_KEY_AUDIO_PATH          @"Quake Audio Files Path"
#define QUAKE_PREFS_KEY_SW_DISPLAY          @"Quake Display"
#define QUAKE_PREFS_KEY_SW_FADE_ALL         @"Quake Fade All Displays"
#define	QUAKE_PREFS_KEY_SW_OPTION_KEY		@"Quake Dialog Requires Option Key"
#define QUAKE_PREFS_KEY_GL_DISPLAY			@"GLQuake Display"
#define	QUAKE_PREFS_KEY_GL_DISPLAY_MODE		@"GLQuake Display Mode"
#define QUAKE_PREFS_KEY_GL_COLORS			@"GLQuake Display Depth"
#define	QUAKE_PREFS_KEY_GL_SAMPLES			@"GLQuake Samples"
#define QUAKE_PREFS_KEY_GL_FADE_ALL			@"GLQuake Fade All Displays"
#define QUAKE_PREFS_KEY_GL_FULLSCREEN		@"GLQuake Fullscreen"
#define QUAKE_PREFS_KEY_GL_OPTION_KEY		@"GLQuake Dialog Requires Option Key"

//----------------------------------------------------------------------------------------------------------------------------

#if defined (GLQUAKE)

#define	QUAKE_PREFS_KEY_OPTION_KEY          QUAKE_PREFS_KEY_GL_OPTION_KEY

#else

#define	QUAKE_PREFS_KEY_OPTION_KEY          QUAKE_PREFS_KEY_SW_OPTION_KEY

#endif // GLQUAKE

//----------------------------------------------------------------------------------------------------------------------------

#define QUAKE_PREFS_VALUE_BASE_PATH         @"id1"
#define	QUAKE_PREFS_VALUE_OPTION_KEY        [NSNumber numberWithBool: NO]
#define QUAKE_PREFS_VALUE_AUDIO_PATH        @""
#define QUAKE_PREFS_VALUE_ARGUMENTS         [NSArray array]
#define QUAKE_PREFS_VALUE_DISPLAY           @"0"
#define QUAKE_PREFS_VALUE_FADE_ALL          [NSNumber numberWithBool: YES]
#define QUAKE_PREFS_VALUE_GL_DISPLAY        @"0"
#define	QUAKE_PREFS_VALUE_GL_DISPLAY_MODE   @"640x480 0Hz"
#define QUAKE_PREFS_VALUE_GL_COLORS         [NSNumber numberWithInt: 32]
#define	QUAKE_PREFS_VALUE_GL_SAMPLES        [NSNumber numberWithInt: 0]
#define QUAKE_PREFS_VALUE_GL_FADE_ALL       [NSNumber numberWithBool: YES]
#define	QUAKE_PREFS_VALUE_GL_FULLSCREEN     [NSNumber numberWithBool: YES]
#define	QUAKE_PREFS_VALUE_GL_OPTION_KEY     [NSNumber numberWithBool: NO]

//----------------------------------------------------------------------------------------------------------------------------
