//----------------------------------------------------------------------------------------------------------------------------
//
// "QAboutPanel.h"
//
// Written by:	Axel 'awe' Wefers			[mailto:awe@fruitz-of-dojo.de].
//				©2001-2012 Fruitz Of Dojo 	[http://www.fruitz-of-dojo.de].
//
//----------------------------------------------------------------------------------------------------------------------------

#import "FDFramework/FDLinkView.h"
#import "QSettingsPanel.h"
#import <Cocoa/Cocoa.h>

//----------------------------------------------------------------------------------------------------------------------------

@interface QAboutPanel : QSettingsPanel {
@private
    IBOutlet NSTextField* mTitle;
    IBOutlet FDLinkView* mLinkView;
    IBOutlet NSButton* mOptionCheckBox;
}

- (NSString*)nibName;
- (void)awakeFromNib;

- (NSString*)toolbarIdentifier;
- (NSToolbarItem*)toolbarItem;

- (IBAction)toggleOptionCheckbox:(id)sender;

@end

//----------------------------------------------------------------------------------------------------------------------------
