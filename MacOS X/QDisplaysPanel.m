//----------------------------------------------------------------------------------------------------------------------------
//
// "QDisplaysPanel.m"
//
// Written by:	Axel 'awe' Wefers			[mailto:awe@fruitz-of-dojo.de].
//				©2001-2012 Fruitz Of Dojo 	[http://www.fruitz-of-dojo.de].
//
//----------------------------------------------------------------------------------------------------------------------------

#import "QDisplaysPanel.h"
#import "QShared.h"

#import "FDFramework/FDFramework.h"

//----------------------------------------------------------------------------------------------------------------------------

@implementation QDisplaysPanel

- (NSString*)nibName
{
    return @"DisplaysPanel";
}

//----------------------------------------------------------------------------------------------------------------------------

- (void)awakeFromNib
{
    [self buildDisplayList];

    [self selectDisplayFromDescription:[[FDPreferences sharedPrefs] stringForKey:QUAKE_PREFS_KEY_SW_DISPLAY]];

    [mFadeAllCheckBox setState:[[FDPreferences sharedPrefs] boolForKey:QUAKE_PREFS_KEY_SW_FADE_ALL]];

    if ([mDisplayPopUp numberOfItems] <= 1) {
        [mDisplayPopUp setEnabled:NO];
        [mFadeAllCheckBox setEnabled:NO];
    }
    else {
        [mDisplayPopUp setEnabled:YES];
        [mFadeAllCheckBox setEnabled:YES];
    }

    [self setTitle:@"Displays"];
}

//----------------------------------------------------------------------------------------------------------------------------

- (NSString*)toolbarIdentifier
{
    return @"Quake Displays ToolbarItem";
}

//----------------------------------------------------------------------------------------------------------------------------

- (NSToolbarItem*)toolbarItem
{
    NSToolbarItem* item = [super toolbarItem];

    [item setLabel:@"Displays"];
    [item setPaletteLabel:@"Displays"];
    [item setToolTip:@"Change display settings."];
    [item setImage:[NSImage imageNamed:@"Displays.icns"]];

    return item;
}

//----------------------------------------------------------------------------------------------------------------------------

- (void)buildDisplayList
{
    NSString* key = [[NSString alloc] init];

    [mDisplayPopUp removeAllItems];

    for (FDDisplay* display in [FDDisplay displays]) {
        NSMenuItem* item = [[NSMenuItem alloc] initWithTitle:[display description] action:nil keyEquivalent:key];

        [item setRepresentedObject:display];
        [[mDisplayPopUp menu] addItem:[item autorelease]];
    }

    [mDisplayPopUp selectItemAtIndex:0];
    [key release];
}

//----------------------------------------------------------------------------------------------------------------------------

- (void)selectDisplayFromDescription:(NSString*)description
{
    const NSInteger numItems = [mDisplayPopUp numberOfItems];

    for (NSInteger i = 0; i < numItems; ++i) {
        if ([[[[mDisplayPopUp itemAtIndex:i] representedObject] description] isEqualToString:description] == YES) {
            [mDisplayPopUp selectItemAtIndex:i];

            break;
        }
    }
}

//----------------------------------------------------------------------------------------------------------------------------

- (IBAction)displayChanged:(id)sender
{
    [[FDPreferences sharedPrefs] setObject:mDisplayPopUp forKey:QUAKE_PREFS_KEY_SW_DISPLAY];
}

//----------------------------------------------------------------------------------------------------------------------------

- (IBAction)fadeChanged:(id)sender
{
    [[FDPreferences sharedPrefs] setObject:mFadeAllCheckBox forKey:QUAKE_PREFS_KEY_SW_FADE_ALL];
}

@end

//----------------------------------------------------------------------------------------------------------------------------
