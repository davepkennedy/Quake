//----------------------------------------------------------------------------------------------------------------------------
//
// "QSettingsWindow.m"
//
// Written by:	Axel 'awe' Wefers			[mailto:awe@fruitz-of-dojo.de].
//				©2001-2012 Fruitz Of Dojo 	[http://www.fruitz-of-dojo.de].
//
//----------------------------------------------------------------------------------------------------------------------------

#import "QSettingsWindow.h"
#import "QAboutPanel.h"
#import "QArgumentsPanel.h"
#import "QDisplaysPanel.h"
#import "QGLDisplaysPanel.h"
#import "QSoundPanel.h"

#import "FDFramework/FDFramework.h"

//----------------------------------------------------------------------------------------------------------------------------

#if defined (GLQUAKE)

#define Q_DISPLAYS_PANEL                    QGLDisplaysPanel

#else

#define Q_DISPLAYS_PANEL                    QDisplaysPanel

#endif // GLQUAKE

//----------------------------------------------------------------------------------------------------------------------------

static NSString*    sQSettingsNewGameToolbarItem = @"Quake Start ToolbarItem";

//----------------------------------------------------------------------------------------------------------------------------

@interface QSettingsWindow ()

- (NSString*) windowNibName;
- (void) awakeFromNib;
- (BOOL) validateToolbarItem: (NSToolbarItem *) theItem;
- (NSToolbarItem *) toolbar: (NSToolbar *) theToolbar
      itemForItemIdentifier: (NSString *) theIdentifier
  willBeInsertedIntoToolbar: (BOOL) theFlag;
- (NSArray *) toolbarDefaultItemIdentifiers: (NSToolbar*) theToolbar;
- (NSArray *) toolbarAllowedItemIdentifiers: (NSToolbar*) theToolbar;
- (void) showPanel: (id) panel;
- (void) newGame: (id) sender;

@end

//----------------------------------------------------------------------------------------------------------------------------

@implementation QSettingsWindow

- (NSString*) windowNibName
{
	return @"SettingsWindow";
}

//----------------------------------------------------------------------------------------------------------------------------

- (void) dealloc
{    
    [self close];
}

//----------------------------------------------------------------------------------------------------------------------------

- (void) awakeFromNib
{
    mPanels = [[NSArray alloc] initWithObjects: [[QAboutPanel alloc] init],
               [[Q_DISPLAYS_PANEL alloc] init],
               [[QSoundPanel alloc] init],
               [[QArgumentsPanel alloc] init],
               nil];
    
    mEmptyView      = [[self window] contentView];
    mToolbarItems   = [NSMutableDictionary dictionary];
  
    for (QSettingsPanel* panel in mPanels)
    {
        [panel setDelegate: self];
        [mToolbarItems setObject: [panel toolbarItem] forKey: [panel toolbarIdentifier]];
    }

    NSToolbarItem* item = [[NSToolbarItem alloc] initWithItemIdentifier: sQSettingsNewGameToolbarItem];
    
    [item setLabel: @"Play"];
    [item setPaletteLabel: @"Play"];
    [item setToolTip: @"Start the game."];
    [item setTarget: self];
    [item setImage: [NSImage imageNamed: @"Start.icns"]];
    [item setAction: @selector (newGame:)];
    
    [mToolbarItems setObject: item forKey: sQSettingsNewGameToolbarItem];
    
    NSToolbar*  toolbar = [[NSToolbar alloc] initWithIdentifier: @"Quake Toolbar"];
    
    [toolbar setDelegate: self];    
    [toolbar setAllowsUserCustomization: NO];
    [toolbar setAutosavesConfiguration: NO];
    [toolbar setDisplayMode: NSToolbarDisplayModeIconAndLabel];
    
    [[self window] setToolbar: toolbar];
    [self showPanel: [mPanels objectAtIndex: 0]];
    [[self window] center];
}

//----------------------------------------------------------------------------------------------------------------------------

- (BOOL) validateToolbarItem: (NSToolbarItem*) toolbarItem
{
    FD_UNUSED (toolbarItem);
    
    return YES;
}

//----------------------------------------------------------------------------------------------------------------------------

- (NSToolbarItem*) toolbar: (NSToolbar*) toolbar
     itemForItemIdentifier: (NSString*) identifier
 willBeInsertedIntoToolbar: (BOOL) flag
{
    FD_UNUSED (toolbar, flag);
    
    return [[mToolbarItems objectForKey: identifier] copy];
}

//----------------------------------------------------------------------------------------------------------------------------

- (NSArray*) toolbarDefaultItemIdentifiers: (NSToolbar*) toolbar
{
    FD_UNUSED (toolbar);
    
    NSMutableArray* identifiers = [[NSMutableArray alloc] initWithCapacity: [mPanels count] + 2];
    
    for (QSettingsPanel* panel in mPanels)
    {
        [identifiers addObject: [panel toolbarIdentifier]];
    }
    
    [identifiers addObject: NSToolbarFlexibleSpaceItemIdentifier];
    [identifiers addObject: sQSettingsNewGameToolbarItem];
    
    return identifiers;
}

//----------------------------------------------------------------------------------------------------------------------------

- (NSArray*) toolbarAllowedItemIdentifiers: (NSToolbar*) toolbar
{
    return [self toolbarDefaultItemIdentifiers: toolbar];
}

//----------------------------------------------------------------------------------------------------------------------------

- (void) showPanel: (id) panel
{
    NSString*   appName = [[NSRunningApplication currentApplication] localizedName];
    NSWindow*   window  = [self window];
    NSView*     view    = [panel view];
    
    if (view != nil && view != [[self window] contentView])
    {
        NSRect  windowFrame = [NSWindow contentRectForFrameRect: [window frame] styleMask:[window styleMask]];
        NSSize  newSize     = [view frame].size;
        
        if ([[window toolbar] isVisible])
        {
            const float toolbarHeight = NSHeight (windowFrame) - NSHeight ([[window contentView] frame]);
            
            newSize.height += toolbarHeight;
        }
        
        [mEmptyView setFrame: windowFrame];
        [window setContentView: mEmptyView];
        [window setTitle: [NSString stringWithFormat: @"%@ (%@)", appName, [panel title]]];
        
        windowFrame.origin.y    += NSHeight (windowFrame) - newSize.height;
        windowFrame.size         = newSize;
        
        windowFrame = [NSWindow frameRectForContentRect: windowFrame styleMask: [window styleMask]];
        
        [window setFrame: windowFrame display: YES animate: [window isVisible]];
        [window setContentView: view];
    }
}

//----------------------------------------------------------------------------------------------------------------------------

- (void) newGame: (id) sender
{
    [sender setTarget: nil];
    [sender setAction: nil];

    if ([mStartGameTarget respondsToSelector: mStartGameSelector] == YES)
    {
        [self close];
        [self synchronize];
        
        [mStartGameTarget performSelector: mStartGameSelector withObject: nil];
        
    }
}

//----------------------------------------------------------------------------------------------------------------------------

- (void) setNewGameAction: (SEL) selector target: (id) target
{
    mStartGameTarget    = target;
    mStartGameSelector  = selector;
}

//----------------------------------------------------------------------------------------------------------------------------

- (void) synchronize
{
    [mPanels makeObjectsPerformSelector: @selector (synchronize)];
    
    [[FDPreferences sharedPrefs] synchronize];
}

@end

//----------------------------------------------------------------------------------------------------------------------------
