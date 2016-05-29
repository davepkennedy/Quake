//
//  FDAlertPanel.m
//  Quake
//
//  Created by Dave Kennedy on 24/05/2016.
//
//

#import "FDAlertPanel.h"

static NSInteger FDRunAlertPanel(NSAlertStyle alertStyle, NSString* title, NSString* message, NSArray<NSString*>* buttons)
{
    NSAlert* alert = [[NSAlert alloc] init];
    alert.alertStyle = alertStyle;
    alert.messageText = title;
    [alert setInformativeText:message];
    for (NSString* btn in buttons) {
        [alert addButtonWithTitle:btn];
    }
    return [alert runModal];
}

NSInteger FDRunCriticalAlertPanel(NSString* title, NSArray<NSString*>* buttons, NSString* format, ...)
{
    va_list args;
    va_start(args, format);
    NSString* message = [[NSString alloc] initWithFormat:format arguments:args];
    return FDRunAlertPanel(NSCriticalAlertStyle, title, message, buttons);
}