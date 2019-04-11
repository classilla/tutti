/*   SDLMain.m - main entry point for our Cocoa-ized SDL app
       Initial Version: Darrell Walisser <dwaliss1@purdue.edu>
       Non-NIB-Code & other changes: Max Horn <max@quendi.de>

     Adjusted for Tutti II by Cameron Kaiser
*/

#import <SDL/SDL.h>
#import "SDLMain.h"
#import <sys/param.h> /* for MAXPATHLEN */
#import <unistd.h>

/* For some reason, Apple removed setAppleMenu from the headers in 10.4,
 but the method still is there and works. To avoid warnings, we declare
 it ourselves here. */
@interface NSApplication(SDL_Missing_Methods)
- (void)setAppleMenu:(NSMenu *)menu;
@end

/* Use this flag to determine whether we use CPS (docking) or not */
#ifdef SDL_USE_CPS
/* Portions of CPS.h */
typedef struct CPSProcessSerNum
{
	UInt32		lo;
	UInt32		hi;
} CPSProcessSerNum;

extern OSErr	CPSGetCurrentProcess( CPSProcessSerNum *psn);
extern OSErr 	CPSEnableForegroundOperation( CPSProcessSerNum *psn, UInt32 _arg2, UInt32 _arg3, UInt32 _arg4, UInt32 _arg5);
extern OSErr	CPSSetFrontProcess( CPSProcessSerNum *psn);

#endif /* SDL_USE_CPS */

static int    gArgc;
static char  **gArgv;
static BOOL   gFinderLaunch;
static BOOL   gCalledAppMainline = FALSE;

id warpToggleItem;
id frameToggleItem;
id tapeToggleItem;

static NSString *getApplicationName(void)
{
#if(0)
    NSDictionary *dict;
    NSString *appName = 0;

    /* Determine the application name */
    dict = (NSDictionary *)CFBundleGetInfoDictionary(CFBundleGetMainBundle());
    if (dict)
        appName = [dict objectForKey: @"CFBundleName"];
    
    if (![appName length])
        appName = [[NSProcessInfo processInfo] processName];

    return appName;
#else
    return @"Tutti II";
#endif
}

@interface SDLApplication : NSApplication
@end

/* External routines the emulation core expects to call.
   We segregate the Cocoa stuff here. */

void toggleWarpSpeed() {
    if (!warpToggleItem) return;
    [warpToggleItem setState:(([warpToggleItem state] == NSOffState) ?
	NSOnState : NSOffState)];
}
void toggleFrameLock() {
    if (!frameToggleItem) return;
    [frameToggleItem setState:(([frameToggleItem state] == NSOffState) ?
	NSOnState : NSOffState)];
}
void setWarpSpeed(int value) {
    if (!warpToggleItem) return;
    [warpToggleItem setState:((value) ? NSOnState : NSOffState)];
}
void setFrameLock(int value) {
    if (!frameToggleItem) return;
    [frameToggleItem setState:((value) ?  NSOnState : NSOffState)];
}

/* isEnabled was removed in Yosemite, so no toggle. Up yours, Apple. */
void enableStopTape() {
    if (!tapeToggleItem) return;
    [tapeToggleItem setEnabled:YES];
}
void disableStopTape() {
    if (!tapeToggleItem) return;
    [tapeToggleItem setEnabled:NO];
}

char *pathToTutor1() {
    NSBundle *appBundle = [NSBundle mainBundle];
    NSString *s = NULL;
    char *w = NULL;

    if (!appBundle) return "tutor1.bin";
    s = [appBundle pathForResource:@"tutor1" ofType:@"bin"];
    if (!s) return "tutor1.bin";
    w = (char *)[s UTF8String];
    if (!w) return "tutor1.bin";
    return w;
}
char *pathToTutor2() {
    NSBundle *appBundle = [NSBundle mainBundle];
    NSString *s = NULL;
    char *w = NULL;

    if (!appBundle) return "tutor2.bin";
    s = [appBundle pathForResource:@"tutor2" ofType:@"bin"];
    if (!s) return "tutor2.bin";
    w = (char *)[s UTF8String];
    if (!w) return "tutor2.bin";
    return w;
}

char *loadFilename() {
    int result, i;
    NSArray *results;
    NSURL *url;
    char *f = NULL;
    NSOpenPanel *thePanel = [NSOpenPanel openPanel];
    [thePanel retain];

    [thePanel setAllowsMultipleSelection:NO];
    [thePanel setCanSelectHiddenExtension:YES];
    [thePanel setCanChooseDirectories:NO];
    [thePanel setCanChooseFiles:YES];
    [thePanel setResolvesAliases:YES];
    [thePanel setTreatsFilePackagesAsDirectories:YES];
    result = [thePanel runModalForDirectory:@"/" file:nil types:nil];
    results = [thePanel URLs];
    if (results) {
	for (i=0; i<[results count]; i++) {
		f = (char *)[[[[thePanel URLs] objectAtIndex:i] path] UTF8String];
	}
    }
    [thePanel release];
    return f;
}

char *saveFilename(char *name) {
    int result, i;
    NSURL *url;
    NSSavePanel *thePanel = [NSSavePanel savePanel];
    NSString *basefile = (name) ? [NSString stringWithUTF8String:name]
	: @"tutor.out";
    [thePanel retain];

    result = [thePanel
runModalForDirectory:[NSString stringWithUTF8String:getenv("HOME")]
file:basefile];
    url = [thePanel URL];
    [thePanel release];
    return (result) ? (char *)[[url path] UTF8String] : (char *)NULL;
}

char *getPastedText(int *cblen)
{
    NSPasteboard *pasteboard = [NSPasteboard generalPasteboard];
    NSString *type = [pasteboard availableTypeFromArray:[NSArray arrayWithObject:NSStringPboardType]];
    if(type != nil) 
    {
        NSString *contents = [pasteboard stringForType:type];
        if(contents != nil)
        {
            int len = (int)[contents lengthOfBytesUsingEncoding:NSUTF8StringEncoding] + 1; // 10.4+
            if(len > 1)
            {
                char *buf = (char *)malloc(len);
                if(buf)
                {
                    if([contents getCString:buf maxLength:len encoding:NSUTF8StringEncoding]) // 10.4+
                    {
                        *cblen = len;
                        return buf;
                    }
                    free(buf);
                }
            }
        }
    }
    return NULL;
}

void minimizeApp() { [[NSApp mainWindow] performMiniaturize:nil]; }

@implementation SDLApplication
/* Invoked from the Quit menu item */
- (void)terminate:(id)sender
{
    /* Post a SDL_QUIT event */
    SDL_Event event;
    event.type = SDL_QUIT;
    SDL_PushEvent(&event);
}

- (void)resetTutor:(id)sender
{
    /* Post a Cmd-R keydown event that tutti.c will pick up */
    SDL_Event event;
    event.type = SDL_KEYDOWN;
    event.key.keysym.sym = SDLK_r;
    event.key.keysym.mod = KMOD_LMETA;
    SDL_PushEvent(&event);
}

- (void)warpSpeed:(id)sender
{
    /* Post Cmd-B keydown event */
    SDL_Event event;
    event.type = SDL_KEYDOWN;
    event.key.keysym.sym = SDLK_b;
    event.key.keysym.mod = KMOD_LMETA;
    SDL_PushEvent(&event);
}

- (void)frameLock:(id)sender
{
    /* Post Cmd-F */
    SDL_Event event;
    event.type = SDL_KEYDOWN;
    event.key.keysym.sym = SDLK_f;
    event.key.keysym.mod = KMOD_LMETA;
    SDL_PushEvent(&event);
}

- (void)stopTape:(id)sender
{
    /* Post Cmd-S */
    SDL_Event event;
    event.type = SDL_KEYDOWN;
    event.key.keysym.sym = SDLK_s;
    event.key.keysym.mod = KMOD_LMETA;
    SDL_PushEvent(&event);
}

- (void)pasteAsKeystrokes:(id)sender
{
    /* Post Cmd-V */
    SDL_Event event;
    event.type = SDL_KEYDOWN;
    event.key.keysym.sym = SDLK_v;
    event.key.keysym.mod = KMOD_LMETA;
    SDL_PushEvent(&event);
}

- (void)saveSnapshot:(id)sender
{
    /* Post F5 */
    SDL_Event event;
    event.type = SDL_KEYDOWN;
    event.key.keysym.sym = SDLK_F5;
    event.key.keysym.mod = 0;
    SDL_PushEvent(&event);
}

- (void)loadSnapshot:(id)sender
{
    /* Post F6 */
    SDL_Event event;
    event.type = SDL_KEYDOWN;
    event.key.keysym.sym = SDLK_F6;
    event.key.keysym.mod = 0;
    SDL_PushEvent(&event);
}

- (void)debuggerHit:(id)sender
{
    /* Post F1 */
    SDL_Event event;
    event.type = SDL_KEYDOWN;
    event.key.keysym.sym = SDLK_F1;
    event.key.keysym.mod = 0;
    SDL_PushEvent(&event);
}
- (void)aboutBox:(id)sender
{
    /* Populate NSDictionary */
    NSString* versionString = @""; //[NSString stringWithFormat:@"v%.1f", 0.3];
    NSAttributedString* creditString = [ [ [ NSAttributedString alloc ]
	initWithString:versionString ] autorelease ];
    NSString* copyRightString = [ NSString stringWithCString:
	"Copyright \xc2\xa9 2003 Ian Gledhill\n"
	"Copyright \xc2\xa9 2019 Cameron Kaiser"
	encoding: NSUTF8StringEncoding ];
    NSDictionary *dict = [ NSDictionary dictionaryWithObjectsAndKeys:
	creditString, @"Credits", copyRightString, @"Copyright", nil ];
    [ NSApp orderFrontStandardAboutPanelWithOptions: dict ];
}
@end

/* The main class of the application, the application's delegate */
@implementation SDLMain

/* Set the working directory */
/* (SDLMain previously set it to the parent. We don't.) */
- (void) setupWorkingDirectory:(BOOL)shouldChdir
{
    if (shouldChdir)
    {
        char parentdir[MAXPATHLEN];
		CFURLRef url = CFBundleCopyBundleURL(CFBundleGetMainBundle());
		//CFURLRef url2 = CFURLCreateCopyDeletingLastPathComponent(0, url);
		//if (CFURLGetFileSystemRepresentation(url2, true, (UInt8 *)parentdir, MAXPATHLEN)) {
		if (CFURLGetFileSystemRepresentation(url, true, (UInt8 *)parentdir, MAXPATHLEN)) {
	        assert ( chdir (parentdir) == 0 );   /* chdir to the binary app's parent */
		}
		CFRelease(url);
		//CFRelease(url2);
	}
}

static void setApplicationMenu(void)
{
    /* warning: this code is very odd */
    NSMenu *menu;
    NSMenu *appleMenu;
    NSMenuItem *menuItem;
    NSString *title;
    NSString *appName;
    
    appName = getApplicationName();
    appleMenu = [[NSMenu alloc] initWithTitle:@""];
    
    /* Add menu items */
    title = [@"About " stringByAppendingString:appName];
    [appleMenu addItemWithTitle:title action:@selector(aboutBox:) keyEquivalent:@""];

    [appleMenu addItem:[NSMenuItem separatorItem]];

    title = [@"Hide " stringByAppendingString:appName];
    [appleMenu addItemWithTitle:title action:@selector(hide:) keyEquivalent:@"h"];

    menuItem = (NSMenuItem *)[appleMenu addItemWithTitle:@"Hide Others" action:@selector(hideOtherApplications:) keyEquivalent:@"h"];
    [menuItem setKeyEquivalentModifierMask:(NSAlternateKeyMask|NSCommandKeyMask)];

    [appleMenu addItemWithTitle:@"Show All" action:@selector(unhideAllApplications:) keyEquivalent:@""];

    [appleMenu addItem:[NSMenuItem separatorItem]];

    title = [@"Quit " stringByAppendingString:appName];
    [appleMenu addItemWithTitle:title action:@selector(terminate:) keyEquivalent:@"q"];

    
    /* Put menu into the menubar */
    menuItem = [[NSMenuItem alloc] initWithTitle:@"" action:nil keyEquivalent:@""];
    [menuItem setSubmenu:appleMenu];
    [[NSApp mainMenu] addItem:menuItem];

    /* Tell the application object that this is now the application menu */
    [NSApp setAppleMenu:appleMenu];

    /* Finally give up our references to the objects */
    [appleMenu release];
    [menuItem release];
}

static void setupCustomMenus(void)
{
    NSMenu *menu;
    NSMenuItem *menuItem;
    unichar c1 = NSF1FunctionKey;
    unichar c5 = NSF5FunctionKey;
    unichar c6 = NSF6FunctionKey;
    NSString *f1 = [NSString stringWithCharacters:&c1 length:1];
    NSString *f5 = [NSString stringWithCharacters:&c5 length:1];
    NSString *f6 = [NSString stringWithCharacters:&c6 length:1];

    menu = [[NSMenu alloc] initWithTitle:@"File"];
    [menu setAutoenablesItems:NO];

    tapeToggleItem = [[NSMenuItem alloc] initWithTitle:@"Stop Tape" action:@selector(stopTape:) keyEquivalent:@"s"];
    [tapeToggleItem setEnabled:NO];
    [tapeToggleItem retain];
    [menu addItem:tapeToggleItem];
    [menu addItem:[NSMenuItem separatorItem]];
    menuItem = [[NSMenuItem alloc] initWithTitle:@"Save Snapshot..." action:@selector(saveSnapshot:) keyEquivalent:f5];
    [menuItem setKeyEquivalentModifierMask:nil];
    [menu addItem:menuItem];
    [menuItem release];
    menuItem = [[NSMenuItem alloc] initWithTitle:@"Load Snapshot..." action:@selector(loadSnapshot:) keyEquivalent:f6];
    [menuItem setKeyEquivalentModifierMask:nil];
    [menu addItem:menuItem];
    [menuItem release];

    menuItem = [[[NSMenuItem alloc] initWithTitle:@"File" action:nil keyEquivalent:@""] autorelease];
    [menuItem setSubmenu:menu];
    [[NSApp mainMenu] addItem:menuItem];

    menu = [[NSMenu alloc] initWithTitle:@"Edit"];

    [menu addItem:[[[NSMenuItem alloc] initWithTitle:@"Paste as Keystrokes" action:@selector(pasteAsKeystrokes:) keyEquivalent:@"v"] autorelease]];
    
    menuItem = [[[NSMenuItem alloc] initWithTitle:@"Edit" action:nil keyEquivalent:@""] autorelease];
    [menuItem setSubmenu:menu];
    [[NSApp mainMenu] addItem:menuItem];

    menu = [[NSMenu alloc] initWithTitle:@"Emulation"];

    [menu addItem:[[[NSMenuItem alloc] initWithTitle:@"Reset" action:@selector(resetTutor:) keyEquivalent:@"r"] autorelease]];
    menuItem = [[NSMenuItem alloc] initWithTitle:@"Step in Debugger" action:@selector(debuggerHit:) keyEquivalent:f1];
    [menuItem setKeyEquivalentModifierMask:nil];
    [menu addItem:menuItem];
    [menuItem release];
    [menu addItem:[NSMenuItem separatorItem]];
    warpToggleItem = [[NSMenuItem alloc] initWithTitle:@"Turbo" action:@selector(warpSpeed:) keyEquivalent:@"b"];
    [warpToggleItem retain];
    [menu addItem:warpToggleItem];
    frameToggleItem = [[NSMenuItem alloc] initWithTitle:@"Sync IRQ3 to System Clock" action:@selector(frameLock:) keyEquivalent:@"f"];
    [frameToggleItem retain];
    [menu addItem:frameToggleItem];

    menuItem = [[[NSMenuItem alloc] initWithTitle:@"Emulation" action:nil keyEquivalent:@""] autorelease];
    [menuItem setSubmenu:menu];
    [[NSApp mainMenu] addItem:menuItem];

    [menuItem release];
    [menu release];
}

/* Create a window menu */
static void setupWindowMenu(void)
{
    NSMenu      *windowMenu;
    NSMenuItem  *windowMenuItem;
    NSMenuItem  *menuItem;

    windowMenu = [[NSMenu alloc] initWithTitle:@"Window"];
    
    /* "Minimize" item */
    menuItem = [[NSMenuItem alloc] initWithTitle:@"Minimize" action:@selector(performMiniaturize:) keyEquivalent:@"m"];
    [windowMenu addItem:menuItem];
    [menuItem release];
    
    /* Put menu into the menubar */
    windowMenuItem = [[NSMenuItem alloc] initWithTitle:@"Window" action:nil keyEquivalent:@""];
    [windowMenuItem setSubmenu:windowMenu];
    [[NSApp mainMenu] addItem:windowMenuItem];
    
    /* Tell the application object that this is now the window menu */
    [NSApp setWindowsMenu:windowMenu];

    /* Finally give up our references to the objects */
    [windowMenu release];
    [windowMenuItem release];
}

/* Replacement for NSApplicationMain */
static void CustomApplicationMain (int argc, char **argv)
{
    NSAutoreleasePool	*pool = [[NSAutoreleasePool alloc] init];
    SDLMain				*sdlMain;

    /* Ensure the application object is initialised */
    [SDLApplication sharedApplication];
    
#ifdef SDL_USE_CPS
    {
        CPSProcessSerNum PSN;
        /* Tell the dock about us */
        if (!CPSGetCurrentProcess(&PSN))
            if (!CPSEnableForegroundOperation(&PSN,0x03,0x3C,0x2C,0x1103))
                if (!CPSSetFrontProcess(&PSN))
                    [SDLApplication sharedApplication];
    }
#endif /* SDL_USE_CPS */

    /* Set up the menubar */
    [NSApp setMainMenu:[[NSMenu alloc] init]];
    setApplicationMenu();
    setupCustomMenus();
    setupWindowMenu();

    /* Create SDLMain and make it the app delegate */
    sdlMain = [[SDLMain alloc] init];
    [NSApp setDelegate:sdlMain];
    
    /* Start the main event loop */
    [NSApp run];
    
    [sdlMain release];
    [pool release];
}

/*
 * Catch document open requests...this lets us notice files when the app
 *  was launched by double-clicking a document, or when a document was
 *  dragged/dropped on the app's icon. You need to have a
 *  CFBundleDocumentsType section in your Info.plist to get this message,
 *  apparently.
 *
 * Files are added to gArgv, so to the app, they'll look like command line
 *  arguments. Previously, apps launched from the finder had nothing but
 *  an argv[0].
 *
 * This message may be received multiple times to open several docs on launch.
 *
 * This message is ignored once the app's mainline has been called.
 */
- (BOOL)application:(NSApplication *)theApplication openFile:(NSString *)filename
{
    const char *temparg;
    size_t arglen;
    char *arg;
    char **newargv;

    if (!gFinderLaunch)  /* MacOS is passing command line args. */
        return FALSE;

    if (gCalledAppMainline)  /* app has started, ignore this document. */
        return FALSE;

    temparg = [filename UTF8String];
    arglen = SDL_strlen(temparg) + 1;
    arg = (char *) SDL_malloc(arglen);
    if (arg == NULL)
        return FALSE;

    newargv = (char **) realloc(gArgv, sizeof (char *) * (gArgc + 2));
    if (newargv == NULL)
    {
        SDL_free(arg);
        return FALSE;
    }
    gArgv = newargv;

    SDL_strlcpy(arg, temparg, arglen);
    gArgv[gArgc++] = arg;
    gArgv[gArgc] = NULL;
    return TRUE;
}


/* Called when the internal event loop has just started running */
- (void) applicationDidFinishLaunching: (NSNotification *) note
{
    int status;

    /* Set the working directory to the .app's parent directory */
    [self setupWorkingDirectory:gFinderLaunch];

#if SDL_USE_NIB_FILE
    /* Set the main menu to contain the real app name instead of "SDL App" */
    [self fixMenu:[NSApp mainMenu] withAppName:getApplicationName()];
#endif

    /* Hand off to main application code */
    gCalledAppMainline = TRUE;
    status = SDL_main (gArgc, gArgv);

    /* We're done, thank you for playing */
    exit(status);
}
@end


@implementation NSString (ReplaceSubString)

- (NSString *)stringByReplacingRange:(NSRange)aRange with:(NSString *)aString
{
    unsigned int bufferSize;
    unsigned int selfLen = [self length];
    unsigned int aStringLen = [aString length];
    unichar *buffer;
    NSRange localRange;
    NSString *result;

    bufferSize = selfLen + aStringLen - aRange.length;
    buffer = NSAllocateMemoryPages(bufferSize*sizeof(unichar));
    
    /* Get first part into buffer */
    localRange.location = 0;
    localRange.length = aRange.location;
    [self getCharacters:buffer range:localRange];
    
    /* Get middle part into buffer */
    localRange.location = 0;
    localRange.length = aStringLen;
    [aString getCharacters:(buffer+aRange.location) range:localRange];
     
    /* Get last part into buffer */
    localRange.location = aRange.location + aRange.length;
    localRange.length = selfLen - localRange.location;
    [self getCharacters:(buffer+aRange.location+aStringLen) range:localRange];
    
    /* Build output string */
    result = [NSString stringWithCharacters:buffer length:bufferSize];
    
    NSDeallocateMemoryPages(buffer, bufferSize);
    
    return result;
}

@end



#ifdef main
#  undef main
#endif


/* Main entry point to executable - should *not* be SDL_main! */
int main (int argc, char **argv)
{
    /* Copy the arguments into a global variable */
    /* This is passed if we are launched by double-clicking */
    if ( argc >= 2 && strncmp (argv[1], "-psn", 4) == 0 ) {
        gArgv = (char **) SDL_malloc(sizeof (char *) * 2);
        gArgv[0] = argv[0];
        gArgv[1] = NULL;
        gArgc = 1;
        gFinderLaunch = YES;
    } else {
        int i;
        gArgc = argc;
        gArgv = (char **) SDL_malloc(sizeof (char *) * (argc+1));
        for (i = 0; i <= argc; i++)
            gArgv[i] = argv[i];
        gFinderLaunch = NO;
    }

#if SDL_USE_NIB_FILE
    [SDLApplication poseAsClass:[NSApplication class]];
    NSApplicationMain (argc, argv);
#else
    CustomApplicationMain (argc, argv);
#endif
    return 0;
}
