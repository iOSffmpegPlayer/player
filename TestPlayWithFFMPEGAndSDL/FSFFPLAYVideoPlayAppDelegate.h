//
//  FSVideoPlayAppDelegate.h
//  TestPlayWithFFMPEGAndSDL
//
//  Created by  on 12-12-13.
//  Copyright (c) 2012年 __MyCompanyName__. All rights reserved.
//

#import <UIKit/UIKit.h>

//@class FSVideoPlayViewController;
@class FSFFPLAYViewController;

@interface FSFFPLAYVideoPlayAppDelegate : UIResponder <UIApplicationDelegate> {
    
}

@property (retain, nonatomic) UIWindow *window;

//@property (strong, nonatomic) FSVideoPlayViewController *viewController;
@property (retain, nonatomic) FSFFPLAYViewController *fsFFPLAYViewController;

@end
