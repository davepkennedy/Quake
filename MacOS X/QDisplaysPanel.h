//----------------------------------------------------------------------------------------------------------------------------
//
// "QDisplaysPanel.h"
//
// Written by:	Axel 'awe' Wefers			[mailto:awe@fruitz-of-dojo.de].
//				©2001-2012 Fruitz Of Dojo 	[http://www.fruitz-of-dojo.de].
//
//----------------------------------------------------------------------------------------------------------------------------

#import "QSettingsPanel.h"
#import <Cocoa/Cocoa.h>

//----------------------------------------------------------------------------------------------------------------------------

@interface QDisplaysPanel : QSettingsPanel {
@private
    IBOutlet NSPopUpButton* mDisplayPopUp;
    IBOutlet NSButton* mFadeAllCheckBox;
}

- (NSString*)nibName;
- (void)awakeFromNib;

- (NSString*)toolbarIdentifier;
- (NSToolbarItem*)toolbarItem;

- (void)buildDisplayList;
- (void)selectDisplayFromDescription:(NSString*)description;

- (IBAction)displayChanged:(id)sender;
- (IBAction)fadeChanged:(id)sender;

@end

//----------------------------------------------------------------------------------------------------------------------------
