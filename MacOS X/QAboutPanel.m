//----------------------------------------------------------------------------------------------------------------------------
//
// "QAboutPanel.m"
//
// Written by:	Axel 'awe' Wefers			[mailto:awe@fruitz-of-dojo.de].
//				©2001-2012 Fruitz Of Dojo 	[http://www.fruitz-of-dojo.de].
//
//----------------------------------------------------------------------------------------------------------------------------

#import "QAboutPanel.h"
#import "QController.h"
#import "QShared.h"

#import "FDFramework/FDFramework.h"

//----------------------------------------------------------------------------------------------------------------------------

@implementation QAboutPanel

- (NSString *) nibName
{
	return @"AboutPanel";
}

//----------------------------------------------------------------------------------------------------------------------------

- (void) awakeFromNib
{
    NSString* appName = [[NSRunningApplication currentApplication] localizedName];
    
    [mLinkView setURL: [NSURL URLWithString: FRUITZ_OF_DOJO_URL]];
    
    [mTitle setStringValue: [NSString stringWithFormat: @"%@ for MacOS X", appName]];
    [mOptionCheckBox setState: [[FDPreferences sharedPrefs] boolForKey: QUAKE_PREFS_KEY_OPTION_KEY]];
    
    [self setTitle: @"About"];
}

//----------------------------------------------------------------------------------------------------------------------------

- (NSString*) toolbarIdentifier
{
    return @"Quake About ToolbarItem";
}

//----------------------------------------------------------------------------------------------------------------------------

- (NSToolbarItem*) toolbarItem
{
    NSToolbarItem* item = [super toolbarItem];
    
    [item setLabel: @"About"];
    [item setPaletteLabel: @"About"];
    [item setToolTip: @"About Quake."];
    [item setImage: [NSImage imageNamed: @"About.icns"]];
    
    return item;
}

//----------------------------------------------------------------------------------------------------------------------------

- (IBAction) toggleOptionCheckbox: (id) sender
{
    FD_UNUSED (sender);
    
    [[FDPreferences sharedPrefs] setObject: mOptionCheckBox forKey: QUAKE_PREFS_KEY_OPTION_KEY];
}

@end

//----------------------------------------------------------------------------------------------------------------------------
