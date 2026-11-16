/* SwitchRecomp - Copyright 2026 MZ Labs
   Licensed under the Apache License 2.0. See LICENSE.txt and NOTICE.txt;
   any copy or derived work must keep the credit to SwitchRecomp by MZ Labs. */
/* X11 + GLX 4.3 backend, the Linux counterpart of backend_win32.c (Wayland desktops run it through
   XWayland). Controllers come from SDL2, loaded at run time like XInput on Windows.
   F1 help  F3 screenshot  F4 fps limit  F5 trace  F8 full-res screenshot  F11/Alt+Enter fullscreen  Esc quit
   Also what the Xvfb tests run on (screenshots, scripted input). */
#include "rt.h"
#include "pad_remap.h"
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>
#include <X11/XKBlib.h>
#include <X11/keysym.h>
#include <GL/glx.h>
#include <ctype.h>
#include <dlfcn.h>
#include <math.h>
#include <strings.h>
#include <time.h>

static Display* dpy; static Window win; static Atom wm_delete, a_state, a_fs; static Cursor g_blank;
static volatile int g_ww=1280, g_wh=720; static int g_fullscreen=0, g_want_fs=1;
static int g_seconds=0, g_maxframes=-1; static char g_shot[1024]; static u64 g_start; static u64 g_press=0, g_press_at=0, g_press_len=300;
static char g_script[1024]; static char g_title[256]; static volatile int g_shot_req;
static int g_focused=1, g_grabbed;
extern int g_opt_fps;
static void bind_ini(const char* k, const char* v);

void backend_ini(const char* k, const char* v){
  if(!strncmp(k,"bind_",5)) bind_ini(k,v);
  else if(!strcmp(k,"mouse_buttons")) bind_mouse_buttons_apply(v);
  else if(!strcmp(k,"fullscreen")) g_want_fs=atoi(v);
  else if(!strcmp(k,"seconds")) g_seconds=atoi(v);
  else if(!strcmp(k,"frames")) g_maxframes=atoi(v);
  else if(!strcmp(k,"shot")) strncpy(g_shot,v,1023);
  else if(!strcmp(k,"press")) g_press=strtoull(v,NULL,16);
  else if(!strcmp(k,"press_at")) g_press_at=strtoull(v,NULL,10);
  else if(!strcmp(k,"press_len")) g_press_len=strtoull(v,NULL,10);
  else if(!strcmp(k,"script")) strncpy(g_script,v,1023);
}

int video_has_gl(void){ return 1; }

/* ---------------------------------------------------------------- SDL2 game controllers, loaded dynamically */
typedef void SDLGC;
static struct {
  int (*Init)(u32); int (*SetHint)(const char*, const char*); int (*NumJoysticks)(void); int (*IsGameController)(int);
  SDLGC* (*Open)(int); int (*Attached)(SDLGC*); void (*Close)(SDLGC*); void (*Update)(void);
  u8 (*Button)(SDLGC*, int); i16 (*Axis)(SDLGC*, int); const char* (*Name)(SDLGC*);
} sdl;
static SDLGC* g_pad_gc; static u64 g_pad_scan;
static int (*sdl_rumble)(SDLGC*, u16, u16, u32);

static int (*sdl_gctype)(int);   /* SDL_GameControllerTypeForIndex, SDL 2.0.12+ (optional) */
static void sdl_load(void){
  static int done; if(done) return; done=1;   /* also called early by backend_pad_family */
  void* h=dlopen("libSDL2-2.0.so.0",RTLD_NOW|RTLD_LOCAL); if(!h) h=dlopen("libSDL2.so",RTLD_NOW|RTLD_LOCAL);
  if(!h){ LOG("controllers: SDL2 not found (install libsdl2), keyboard only\n"); return; }
  void** f[]={(void**)&sdl.Init,(void**)&sdl.SetHint,(void**)&sdl.NumJoysticks,(void**)&sdl.IsGameController,(void**)&sdl.Open,(void**)&sdl.Attached,
              (void**)&sdl.Close,(void**)&sdl.Update,(void**)&sdl.Button,(void**)&sdl.Axis,(void**)&sdl.Name};
  const char* n[]={"SDL_Init","SDL_SetHint","SDL_NumJoysticks","SDL_IsGameController","SDL_GameControllerOpen","SDL_GameControllerGetAttached",
                   "SDL_GameControllerClose","SDL_GameControllerUpdate","SDL_GameControllerGetButton","SDL_GameControllerGetAxis","SDL_GameControllerName"};
  for(int i=0;i<11;i++){ *f[i]=dlsym(h,n[i]); if(!*f[i]){ memset(&sdl,0,sizeof sdl); return; } }
  *(void**)&sdl_rumble=dlsym(h,"SDL_GameControllerRumble");   /* SDL 2.0.9+; optional */
  *(void**)&sdl_gctype=dlsym(h,"SDL_GameControllerTypeForIndex");
  /* buttons by position like on Windows, also for Nintendo controllers that SDL would map by label */
  sdl.SetHint("SDL_GAMECONTROLLER_USE_BUTTON_LABELS","0");
  sdl.SetHint("SDL_JOYSTICK_ALLOW_BACKGROUND_EVENTS","1");
  if(sdl.Init(0x2000 /* SDL_INIT_GAMECONTROLLER */)){ memset(&sdl,0,sizeof sdl); return; }
}

/* the kind of controller connected, for the button icons (svc_fs.c), asked before the window exists: 1 Xbox or any
   other (the Steam Deck's controls and Steam Input pads among them), 2 DualShock 4, 3 DualSense, 4 Switch, 0 none */
int backend_pad_family(void){
  sdl_load(); if(!sdl.Init) return 0;
  for(int i=0;i<sdl.NumJoysticks();i++) if(sdl.IsGameController(i)){
    int t= sdl_gctype? sdl_gctype(i) : 0;
    return t==4||t==3? 2 : t==7? 3 : t==5||t==11||t==12||t==13? 4 : 1; }
  return 0; }
static i32 dz(i32 v){ return (v>-7000 && v<7000)? 0 : v; }

static void pad_poll(u64* b, i32* lx, i32* ly, i32* rx, i32* ry){
  if(!sdl.Init) return;
  sdl.Update();
  if(g_pad_gc && !sdl.Attached(g_pad_gc)){ sdl.Close(g_pad_gc); g_pad_gc=NULL; LOG("controllers: disconnected\n"); }
  if(!g_pad_gc){ u64 now=plat_ns(); if(now-g_pad_scan<1000000000ull) return; g_pad_scan=now;
    for(int i=0;i<sdl.NumJoysticks() && !g_pad_gc;i++) if(sdl.IsGameController(i)) g_pad_gc=sdl.Open(i);
    if(!g_pad_gc) return;
    LOG("controllers: %s\n",sdl.Name(g_pad_gc)? sdl.Name(g_pad_gc) : "connected"); }
  SDLGC* g=g_pad_gc;
  /* SDL names buttons by Xbox position: A bottom, B right, X left, Y top */
  static const struct { int sdl, bit; } m[]={{1,0},{0,1},{3,2},{2,3},{7,4},{8,5},{9,6},{10,7},{6,10},{4,11},{13,12},{11,13},{14,14},{12,15}};
  for(unsigned i=0;i<sizeof m/sizeof m[0];i++) if(sdl.Button(g,m[i].sdl)) *b|=1ull<<m[i].bit;
  if(sdl.Button(g,5)) *b|=1ull<<30;   /* Guide / Home: the in-game menu only, never the game (video_poll) */
  { i32 lt=sdl.Axis(g,4), rt=sdl.Axis(g,5); if(lt>7700) *b|=1ull<<8; if(rt>7700) *b|=1ull<<9;
    /* log trigger presses (first 40) and the strongest pull seen, for "ZL does nothing" reports */
    static int was, said; static i32 maxlt; if(lt>maxlt){ maxlt=lt; if(said<40 && (maxlt>=32000 || maxlt%4096<64)) { said++; LOG("controller: left trigger reached %d of 32767 (ZL at 7700)\n",maxlt); } }
    int now= lt>7700; if(now!=was && said<400){ said++; LOG("controller: ZL (left trigger) %s, value %d\n",now? "pressed" : "released",lt); } was=now; }
  /* SDL's Y axes point down, the Switch's up */
  i32 x=sdl.Axis(g,0), y=-(i32)sdl.Axis(g,1), x2=sdl.Axis(g,2), y2=-(i32)sdl.Axis(g,3);
  if(y>32767) y=32767; if(y2>32767) y2=32767;
  if(!*lx && !*ly){ *lx=dz(x); *ly=dz(y); } if(!*rx && !*ry){ *rx=dz(x2); *ry=dz(y2); }
  if(sdl_rumble){ /* rumble (svc_hid.c hid_rumble_get): on a change, else refreshed every 100 ms; on a Steam Deck
                     Steam Input passes it to the haptics */
    extern void hid_rumble_get(float*,float*); float lo, hi; hid_rumble_get(&lo,&hi);
    static u16 plo=1, phi=1; static u64 last; u64 now=plat_ns(); u16 ulo=(u16)(lo*65535.0f), uhi=(u16)(hi*65535.0f);
    if(ulo!=plo || uhi!=phi || ((ulo||uhi) && now-last>100000000ull)){ plo=ulo; phi=uhi; last=now; sdl_rumble(g,ulo,uhi,ulo||uhi? 250 : 0); } }
}

/* ---------------------------------------------------------------- window */
static void set_fullscreen(int on){
  if(on==g_fullscreen) return;
  XEvent e; memset(&e,0,sizeof e); e.xclient.type=ClientMessage; e.xclient.window=win; e.xclient.message_type=a_state; e.xclient.format=32;
  e.xclient.data.l[0]=on? 1 : 0; e.xclient.data.l[1]=(long)a_fs; e.xclient.data.l[3]=1;
  XSendEvent(dpy,DefaultRootWindow(dpy),False,SubstructureRedirectMask|SubstructureNotifyMask,&e);
  XFlush(dpy); g_fullscreen=on; }   /* the cursor follows in cursor_sync */
/* the cursor is hidden in fullscreen, but shown while the in-game menu is open (its mouse / touch), as backend_win32.c
   does with WM_SETCURSOR; checked on every poll */
extern int ov_is_open(void);
static void cursor_sync(void){
  static int hidden=-1; int want= g_fullscreen && !ov_is_open();
  if(want==hidden) return;
  hidden=want; if(want) XDefineCursor(dpy,win,g_blank); else XUndefineCursor(dpy,win); XFlush(dpy); }

static void set_title(const char* t){ XStoreName(dpy,win,t); XFlush(dpy); }
/* fast forward on/off in the title, as backend_win32.c does it (main.c ff_*) */
void backend_ff_status(int on, int speed){ if(!dpy || !win) return; char t[320];
  if(on) snprintf(t,sizeof t,"%s  |  FAST FORWARD x%d (Tab or L3+R3)",g_title,speed); else snprintf(t,sizeof t,"%s",g_title); set_title(t); }

static const char* HELP=
  "SwitchRecomp - controls\n\n"
  "Controller: same as the Switch controller (buttons by position).\n\n"
  "Keyboard:\n"
  "  Arrows = D-Pad     W A S D = Left stick    I J K L = Right stick\n"
  "  X / Space = A    Z = B    C = X    V = Y\n"
  "  Q = L   E = R   1 = ZL   3 = ZR   Enter = +   Backspace = -\n\n"
  "F1 = this help   F3 = screenshot   F4 = FPS limit 60 / unlimited   F5 = diagnostic dump (trace)\n"
  "F8 = instant full-resolution screenshot (PNG in the captures folder)\n"
  "F11 / Alt+Enter = fullscreen   Esc = quit";

void video_init(const char* title){
  snprintf(g_title,sizeof g_title,"%s - SwitchRecomp  (F1 help)",title);
  XInitThreads();
  /* let Mesa run its own glthread so driver work isn't on our GPU thread (Deck, AMD/Intel on Linux) */
  if(!getenv("SWR_NO_GLTHREAD")) setenv("mesa_glthread","true",0);
  /* the drivers' own shader caches sit under our program binaries: a translated shader the game changes slightly still
     reuses the driver's compiled pieces. Their defaults are small for a game this size (Mesa 1 GB, NVIDIA 128 MB and
     cleaned up), and a full cache throws away what it compiled. A value already in the environment wins */
  setenv("MESA_SHADER_CACHE_MAX_SIZE","8G",0); setenv("MESA_DISK_CACHE_MULTI_FILE","1",0);
  setenv("__GL_SHADER_DISK_CACHE","1",0); setenv("__GL_SHADER_DISK_CACHE_SIZE","8589934592",0); setenv("__GL_SHADER_DISK_CACHE_SKIP_CLEANUP","1",0);
  dpy=XOpenDisplay(NULL); if(!dpy) fatal("No X11 display (DISPLAY is not set). Start the game from a desktop session.");
  int scr=DefaultScreen(dpy);
  static int att[]={GLX_X_RENDERABLE,True,GLX_DRAWABLE_TYPE,GLX_WINDOW_BIT,GLX_RENDER_TYPE,GLX_RGBA_BIT,GLX_RED_SIZE,8,GLX_GREEN_SIZE,8,GLX_BLUE_SIZE,8,GLX_ALPHA_SIZE,8,GLX_DEPTH_SIZE,24,GLX_DOUBLEBUFFER,True,None};
  int n=0; GLXFBConfig* fc=glXChooseFBConfig(dpy,scr,att,&n); if(!fc||!n) fatal("Your graphics driver has no OpenGL support for this display (GLX).");
  XVisualInfo* vi=glXGetVisualFromFBConfig(dpy,fc[0]);
  XSetWindowAttributes swa; memset(&swa,0,sizeof swa); swa.colormap=XCreateColormap(dpy,RootWindow(dpy,vi->screen),vi->visual,AllocNone);
  swa.background_pixel=BlackPixel(dpy,scr);
  swa.event_mask=KeyPressMask|KeyReleaseMask|StructureNotifyMask|FocusChangeMask|ButtonPressMask|ButtonReleaseMask|PointerMotionMask;   /* the mouse / touch: the in-game menu */
  win=XCreateWindow(dpy,RootWindow(dpy,vi->screen),0,0,g_ww,g_wh,0,vi->depth,InputOutput,vi->visual,CWColormap|CWEventMask|CWBackPixel,&swa);
  XStoreName(dpy,win,g_title);
  { XClassHint ch={(char*)"switchrecomp",(char*)"SwitchRecomp"}; XSetClassHint(dpy,win,&ch); }
  wm_delete=XInternAtom(dpy,"WM_DELETE_WINDOW",False); XSetWMProtocols(dpy,win,&wm_delete,1);
  a_state=XInternAtom(dpy,"_NET_WM_STATE",False); a_fs=XInternAtom(dpy,"_NET_WM_STATE_FULLSCREEN",False);
  { static char none[8]; XColor black; memset(&black,0,sizeof black);
    Pixmap p=XCreateBitmapFromData(dpy,win,none,8,8); g_blank=XCreatePixmapCursor(dpy,p,p,&black,&black,0,0); XFreePixmap(dpy,p); }
  /* without this X11 sends a release before every auto-repeated press */
  XkbSetDetectableAutoRepeat(dpy,True,NULL);
  /* start fullscreen: the window manager reads the state when the window is mapped */
  if(g_want_fs){ XChangeProperty(dpy,win,a_state,XA_ATOM,32,PropModeReplace,(unsigned char*)&a_fs,1); g_fullscreen=1; }
  XMapWindow(dpy,win); XFlush(dpy);
  sdl_load();
  g_start=plat_ns();
  extern void gpu_gl_start(void); gpu_gl_start();
}
/* GPU thread */
static GLXContext ctx, wctx[8]; static GLXFBConfig wfc; static int g_nwctx; static volatile int g_wnext;
/* shader compile threads (gpu_gl.c worker_start): Mesa and NVIDIA compile GLSL on the thread that asks, so each thread
   gets its own context in the game's share group and several shaders build at once. [Performance] shader_threads */
int g_opt_shader_threads;

static int xerr_ignore(Display* d, XErrorEvent* e){ (void)d;(void)e; return 0; }

int backend_gl_create(void){
  int scr=DefaultScreen(dpy);
  static int att[]={GLX_X_RENDERABLE,True,GLX_DRAWABLE_TYPE,GLX_WINDOW_BIT,GLX_RENDER_TYPE,GLX_RGBA_BIT,GLX_RED_SIZE,8,GLX_GREEN_SIZE,8,GLX_BLUE_SIZE,8,GLX_ALPHA_SIZE,8,GLX_DOUBLEBUFFER,True,None};
  int n=0; GLXFBConfig* fc=glXChooseFBConfig(dpy,scr,att,&n); if(!fc||!n) return 0;
  typedef GLXContext (*CCA)(Display*,GLXFBConfig,GLXContext,Bool,const int*);
  CCA cca=(CCA)glXGetProcAddressARB((const GLubyte*)"glXCreateContextAttribsARB");
  static const int vers[4][2]={{4,6},{4,5},{4,4},{4,3}};
  int (*old)(Display*,XErrorEvent*)=XSetErrorHandler(xerr_ignore);
  for(int i=0;i<4 && !ctx && cca;i++){ int ca[]={0x2091,vers[i][0],0x2092,vers[i][1],0x9126,0x00000001,None}; ctx=cca(dpy,fc[0],NULL,True,ca); XSync(dpy,False);
    if(ctx){ long nc=sysconf(_SC_NPROCESSORS_ONLN); int want=g_opt_shader_threads>0? g_opt_shader_threads : (int)(nc/4);
      if(want<2 && g_opt_shader_threads<=0) want=2; if(want>8) want=8;
      for(g_nwctx=0;g_nwctx<want;g_nwctx++){ wctx[g_nwctx]=cca(dpy,fc[0],ctx,True,ca); XSync(dpy,False); if(!wctx[g_nwctx]) break; }
      { extern int g_gl_workers; g_gl_workers= g_nwctx? g_nwctx : 1; } } }
  wfc=fc[0];
  XSetErrorHandler(old);
  if(!ctx){ fatal("Your graphics driver does not support OpenGL 4.3.\nPlease update Mesa or your GPU driver."); return 0; }
  glXMakeCurrent(dpy,win,ctx);
  extern int g_opt_vsync;
  typedef void (*SIE)(Display*,GLXDrawable,int); typedef int (*SIM)(unsigned);
  SIE sie=(SIE)glXGetProcAddressARB((const GLubyte*)"glXSwapIntervalEXT"); SIM sim=(SIM)glXGetProcAddressARB((const GLubyte*)"glXSwapIntervalMESA");
  if(sie) sie(dpy,win,g_opt_vsync? 1 : 0); else if(sim) sim(g_opt_vsync? 1 : 0);
  return 1; }

void* backend_gl_proc(const char* n){ return (void*)glXGetProcAddressARB((const GLubyte*)n); }

void backend_gl_swap(void){ glXSwapBuffers(dpy,win);
  /* SWR_VIDEO_TIMING=1: what reaches the monitor. The display's refresh counter (GLX_OML_sync_control MSC) after each
     swap: +1 = a clean new frame, +0 = two swaps within one refresh (a torn or never-seen frame), +2 or more = the
     previous frame shown again (a stutter). Every 120 swaps */
  static int timing=-1; static Bool (*gsv)(Display*,GLXDrawable,int64_t*,int64_t*,int64_t*); static int64_t last_msc; static u32 n, h[4];
  if(timing<0){ timing=!getenv("SWR_NO_VIDEO_TIMING"); if(timing) gsv=(Bool(*)(Display*,GLXDrawable,int64_t*,int64_t*,int64_t*))glXGetProcAddressARB((const GLubyte*)"glXGetSyncValuesOML");
    if(timing && !gsv) LOG("display timing: GLX_OML_sync_control not available\n"); }
  if(timing && gsv){ int64_t ust=0,msc=0,sbc=0; if(gsv(dpy,win,&ust,&msc,&sbc)){ if(last_msc){ int64_t d=msc-last_msc; h[d<=0? 0 : d==1? 1 : d==2? 2 : 3]++;
        if(++n==120){ LOG("display timing: of 120 swaps %u clean, %u in the same refresh (torn/unseen), %u repeated once, %u repeated more\n",h[1],h[0],h[2],h[3]); n=0; h[0]=h[1]=h[2]=h[3]=0; } }
      last_msc=msc; } } }
/* forced_vsync applies live (glq.c, the GL thread) */
void backend_gl_vsync(int on){
  typedef void (*SIE)(Display*,GLXDrawable,int); typedef int (*SIM)(unsigned);
  static SIE sie; static SIM sim; static int got;
  if(!got){ got=1; sie=(SIE)glXGetProcAddressARB((const GLubyte*)"glXSwapIntervalEXT"); sim=(SIM)glXGetProcAddressARB((const GLubyte*)"glXSwapIntervalMESA"); }
  if(sie) sie(dpy,win,on? 1 : 0); else if(sim) sim(on? 1 : 0); }
/* fullscreen from another thread (settings): done by the event loop, Xlib being single-threaded here */
static volatile int g_fs_req=-1;
void backend_set_fullscreen(int on){ g_fs_req= on!=0; }
int backend_is_fullscreen(void){ return g_fullscreen; }
static volatile u64 keys_down[4]; static volatile u32 mouse_down;
void backend_release_keys(void){ for(int i=0;i<4;i++) keys_down[i]=0; mouse_down=0; }

int backend_gl_worker_current(void){
  int k=__atomic_fetch_add(&g_wnext,1,__ATOMIC_ACQ_REL); if(k>=g_nwctx) return 0; int pa[]={GLX_PBUFFER_WIDTH,4,GLX_PBUFFER_HEIGHT,4,None};
  int (*old)(Display*,XErrorEvent*)=XSetErrorHandler(xerr_ignore); GLXPbuffer pb=glXCreatePbuffer(dpy,wfc,pa); XSync(dpy,False); XSetErrorHandler(old);
  if(pb && glXMakeContextCurrent(dpy,pb,pb,wctx[k])) return 1;
  return glXMakeContextCurrent(dpy,None,None,wctx[k])? 1 : 0; }

void backend_window_size(int* w, int* h){ *w=g_ww; *h=g_wh; }

void video_present_rgba(const u32* px, int w, int h, int stride){ extern void gpu_gl_present_cpu(const u32*,int,int,int); gpu_gl_present_cpu(px,w,h,stride); }

/* ---------------------------------------------------------------- keyboard and mouse bindings (keybind.h) */
enum {
  VK_BACK=0x08, VK_TAB=0x09, VK_RETURN=0x0D, VK_SHIFT=0x10, VK_CONTROL=0x11, VK_MENU=0x12, VK_PAUSE=0x13,
  VK_CAPITAL=0x14, VK_ESCAPE=0x1B, VK_SPACE=0x20, VK_PRIOR=0x21, VK_NEXT=0x22, VK_END=0x23, VK_HOME=0x24,
  VK_LEFT=0x25, VK_UP=0x26, VK_RIGHT=0x27, VK_DOWN=0x28, VK_INSERT=0x2D, VK_DELETE=0x2E,
  VK_LWIN=0x5B, VK_RWIN=0x5C, VK_APPS=0x5D,
  VK_NUMPAD0=0x60, VK_NUMPAD9=0x69, VK_MULTIPLY=0x6A, VK_ADD=0x6B, VK_SUBTRACT=0x6D, VK_DECIMAL=0x6E, VK_DIVIDE=0x6F,
  VK_F1=0x70, VK_F12=0x7B, VK_F24=0x87, VK_NUMLOCK=0x90, VK_SCROLL=0x91,
  VK_OEM_1=0xBA, VK_OEM_PLUS=0xBB, VK_OEM_COMMA=0xBC, VK_OEM_MINUS=0xBD, VK_OEM_PERIOD=0xBE,
  VK_OEM_2=0xBF, VK_OEM_3=0xC0, VK_OEM_4=0xDB, VK_OEM_5=0xDC, VK_OEM_6=0xDD, VK_OEM_7=0xDE, VK_OEM_102=0xE2
};
typedef struct { const char* key; const char* label; int bit; int def[BIND_SLOTS]; } BindInput;
static const BindInput BIND_IN[]={
  {"bind_a","A",0,{'E',VK_RETURN}}, {"bind_b","B",1,{VK_SPACE,VK_BACK}}, {"bind_x","X",2,{'Q',0}}, {"bind_y","Y",3,{VK_CONTROL,'C'}},
  {"bind_l","L",6,{VK_SHIFT,0}}, {"bind_r","R",7,{'F',BIND_MOUSE+2}}, {"bind_zl","ZL",8,{BIND_MOUSE+1,0}}, {"bind_zr","ZR",9,{BIND_MOUSE+0,0}},
  {"bind_plus","Plus",10,{'V',0}}, {"bind_minus","Minus",11,{VK_TAB,0}},
  {"bind_lstick","L-stick click",4,{'R',0}}, {"bind_rstick","R-stick click",5,{'T',0}},
  {"bind_dpad_up","D-pad up",13,{VK_UP,'1'}}, {"bind_dpad_down","D-pad down",15,{VK_DOWN,'4'}},
  {"bind_dpad_left","D-pad left",12,{VK_LEFT,'2'}}, {"bind_dpad_right","D-pad right",14,{VK_RIGHT,'3'}},
  {"bind_lstick_up","Left stick up",41,{'W',0}}, {"bind_lstick_down","Left stick down",43,{'S',0}},
  {"bind_lstick_left","Left stick left",40,{'A',0}}, {"bind_lstick_right","Left stick right",42,{'D',0}},
  {"bind_rstick_up","Right stick up",45,{'I',0}}, {"bind_rstick_down","Right stick down",47,{'K',0}},
  {"bind_rstick_left","Right stick left",44,{'J',0}}, {"bind_rstick_right","Right stick right",46,{'L',0}},
  {"bind_lasso","Lasso (L-stick click, hold: pull up)",48,{'G',0}}};
#define BIND_N ((int)(sizeof BIND_IN/sizeof *BIND_IN))
static const struct { int vk; const char* ini; const char* show; } BIND_KEYS[]={
  {VK_BACK,"Backspace","Backspace"},{VK_TAB,"Tab","Tab"},{VK_RETURN,"Enter","Enter"},{VK_SHIFT,"Shift","Shift"},{VK_CONTROL,"Ctrl","Ctrl"},
  {VK_MENU,"Alt","Alt"},{VK_PAUSE,"Pause","Pause"},{VK_CAPITAL,"CapsLock","Caps Lock"},{VK_ESCAPE,"Escape","Esc"},{VK_SPACE,"Space","Space"},
  {VK_PRIOR,"PageUp","Page Up"},{VK_NEXT,"PageDown","Page Down"},{VK_END,"End","End"},{VK_HOME,"Home","Home"},{VK_LEFT,"Left","Left"},
  {VK_UP,"Up","Up"},{VK_RIGHT,"Right","Right"},{VK_DOWN,"Down","Down"},{VK_INSERT,"Insert","Insert"},{VK_DELETE,"Delete","Delete"},
  {VK_LWIN,"LWin","Win"},{VK_RWIN,"RWin","Win"},{VK_APPS,"Apps","Menu key"},{VK_MULTIPLY,"Num*","Num *"},{VK_ADD,"Num+","Num +"},
  {VK_SUBTRACT,"Num-","Num -"},{VK_DECIMAL,"Num.","Num ."},{VK_DIVIDE,"Num/","Num /"},{VK_NUMLOCK,"NumLock","Num Lock"},
  {VK_SCROLL,"ScrollLock","Scroll Lock"},{VK_OEM_1,"Semicolon",";"},{VK_OEM_PLUS,"Equals","="},{VK_OEM_COMMA,"Comma",","},
  {VK_OEM_MINUS,"Minus","-"},{VK_OEM_PERIOD,"Period","."},{VK_OEM_2,"Slash","/"},{VK_OEM_3,"Backquote","`"},{VK_OEM_4,"LBracket","["},
  {VK_OEM_5,"Backslash","\\"},{VK_OEM_6,"RBracket","]"},{VK_OEM_7,"Quote","'"},{VK_OEM_102,"OEM102","<"}};
static int g_bind[BIND_N][BIND_SLOTS];
static volatile u64 g_bind_key[256], g_bind_mouse[5];
static char g_bind_ini[BIND_MOUSE+5][16], g_bind_show[BIND_MOUSE+5][16], g_bind_def[BIND_N][40];
static volatile int g_bind_ready;

const char* bind_refused(int code, int fast_forward){
  if(code>=BIND_MOUSE && code<BIND_MOUSE+5) return NULL;
  if(code<=0 || code>=255) return "That key can't be bound";
  if(code>=VK_F1 && code<=VK_F12) return "F1 to F12 are hotkeys";
  if(code==VK_ESCAPE) return "Esc is the menu key";
  if(code==VK_MENU) return "Alt is for Alt+Enter";
  if(code==VK_LWIN || code==VK_RWIN) return "The Super key can't be bound";
  if(code==VK_TAB && fast_forward) return "Tab is fast forward while that is on";
  return NULL; }
static int bind_code_of(const char* t){
  static const struct { const char* n; int vk; } AKA[]={{"Esc",VK_ESCAPE},{"Control",VK_CONTROL},{"Return",VK_RETURN}};
  for(int c=1;c<BIND_MOUSE+5;c++) if(g_bind_ini[c][0] && !strcasecmp(t,g_bind_ini[c])) return c;
  for(size_t i=0;i<sizeof AKA/sizeof *AKA;i++) if(!strcasecmp(t,AKA[i].n)) return AKA[i].vk;
  return 0; }
static int bind_parse_(const char* v, int c[BIND_SLOTS], const char* key){
  int n=0; for(int s=0;s<BIND_SLOTS;s++) c[s]=0;
  while(v && *v){ char t[32]; int k=0;
    while(*v && *v!=','){ if(*v!=' ' && *v!='\t' && k<31) t[k++]=*v; v++; }
    t[k]=0; if(*v==',') v++;
    if(!t[0] || !strcmp(t,"-") || !strcasecmp(t,"none")) continue;
    int code=bind_code_of(t); const char* why= code? bind_refused(code,0) : "not a key name";
    if(why){ if(key) LOG("input: %s: %s left out (%s)\n",key,t,why); continue; }
    if(n && c[0]==code) continue;
    if(n==BIND_SLOTS){ if(key) LOG("input: %s: %s left out (two keys at most)\n",key,t); continue; }
    c[n++]=code; }
  return n; }
static void bind_fmt_(const int c[BIND_SLOTS], char* out, int cap){
  int n=0; if(cap>0) out[0]=0;
  for(int s=0;s<BIND_SLOTS;s++) if(c[s]>0 && c[s]<BIND_MOUSE+5 && n<cap) n+=snprintf(out+n,(size_t)(cap-n),"%s%s",n? "," : "",g_bind_ini[c[s]]); }
static void bind_rebuild(void){
  u64 key[256]={0}, mouse[5]={0};
  for(int i=0;i<BIND_N;i++) for(int s=0;s<BIND_SLOTS;s++){ int c=g_bind[i][s]; u64 m=1ull<<BIND_IN[i].bit;
    if(c>0 && c<256) key[c]|=m; else if(c>=BIND_MOUSE && c<BIND_MOUSE+5) mouse[c-BIND_MOUSE]|=m; }
  for(int i=0;i<256;i++) g_bind_key[i]=key[i];
  for(int i=0;i<5;i++) g_bind_mouse[i]=mouse[i]; }
static void bind_init(void){
  if(g_bind_ready) return;
  for(int c=1;c<BIND_MOUSE+5;c++){ char* n=g_bind_ini[c]; char* s=g_bind_show[c];
    if(c>=BIND_MOUSE){ snprintf(n,16,"Mouse%d",c-BIND_MOUSE+1); snprintf(s,16,"Mouse %d",c-BIND_MOUSE+1); }
    else if((c>='A' && c<='Z') || (c>='0' && c<='9')){ snprintf(n,16,"%c",c); snprintf(s,16,"%c",c); }
    else if(c>=VK_NUMPAD0 && c<=VK_NUMPAD9){ snprintf(n,16,"Num%d",c-VK_NUMPAD0); snprintf(s,16,"Num %d",c-VK_NUMPAD0); }
    else if(c>=VK_F1 && c<=VK_F24){ snprintf(n,16,"F%d",c-VK_F1+1); snprintf(s,16,"F%d",c-VK_F1+1); }
    else if(c<256){ snprintf(n,16,"Key%d",c); snprintf(s,16,"Key %d",c); } }
  for(size_t j=0;j<sizeof BIND_KEYS/sizeof *BIND_KEYS;j++){ int c=BIND_KEYS[j].vk;
    snprintf(g_bind_ini[c],16,"%s",BIND_KEYS[j].ini); snprintf(g_bind_show[c],16,"%s",BIND_KEYS[j].show); }
  for(int i=0;i<BIND_N;i++){ for(int s=0;s<BIND_SLOTS;s++) g_bind[i][s]=BIND_IN[i].def[s]; bind_fmt_(BIND_IN[i].def,g_bind_def[i],sizeof g_bind_def[i]); }
  bind_rebuild(); g_bind_ready=1; }

int bind_count(void){ return BIND_N; }
const char* bind_input_key(int i){ return i>=0 && i<BIND_N? BIND_IN[i].key : NULL; }
const char* bind_input_label(int i){ return i>=0 && i<BIND_N? BIND_IN[i].label : NULL; }
int bind_input_bit(int i){ return i>=0 && i<BIND_N? BIND_IN[i].bit : -1; }
const char* bind_default(int i){ bind_init(); return i>=0 && i<BIND_N? g_bind_def[i] : ""; }
int bind_parse(const char* v, int c[BIND_SLOTS]){ bind_init(); return bind_parse_(v,c,NULL); }
void bind_format(const int c[BIND_SLOTS], char* out, int cap){ bind_init(); bind_fmt_(c,out,cap); }
const char* bind_code_name(int code){ bind_init(); return code>0 && code<BIND_MOUSE+5 && g_bind_show[code][0]? g_bind_show[code] : NULL; }
const char* bind_key_name(int bit, int slot){ bind_init();
  if(slot<0 || slot>=BIND_SLOTS) return NULL;
  for(int i=0;i<BIND_N;i++) if(BIND_IN[i].bit==bit) return bind_code_name(g_bind[i][slot]);
  return NULL; }
int bind_code_from_msg(unsigned m, unsigned long long w, long long l){ (void)m;(void)w;(void)l; return 0; }
static void bind_ini(const char* k, const char* v){ bind_init();
  for(int i=0;i<BIND_N;i++) if(!strcmp(k,BIND_IN[i].key)){ bind_parse_(v,g_bind[i],k); bind_rebuild(); } }
int bind_mouse_buttons(int i, const char* mb, const char* value, char* out, int cap){
  static const struct { const char* n; int bit; } N[]={{"a",0},{"b",1},{"x",2},{"y",3},{"lstick",4},{"rstick",5},{"l",6},{"r",7},
    {"zl",8},{"zr",9},{"plus",10},{"minus",11}};
  bind_init();
  if(i<0 || i>=BIND_N || !mb || !*mb || !strcmp(mb,"zr,zl,r,-,-") || !strcmp(mb,"y,l,r,-,-")) return 0;
  int c[BIND_SLOTS], k[BIND_SLOTS]={0}, n=0; bind_parse_(value,c,NULL);
  for(int s=0;s<BIND_SLOTS;s++) if(c[s] && c[s]<BIND_MOUSE) k[n++]=c[s];
  const char* p=mb;
  for(int j=0;j<5;j++){ char t[16]; int q=0; while(*p==' ') p++; while(*p && *p!=',' && q<15) t[q++]=(char)tolower((unsigned char)*p++); t[q]=0; if(*p==',') p++;
    for(size_t m=0;m<sizeof N/sizeof *N;m++) if(!strcmp(t,N[m].n) && N[m].bit==BIND_IN[i].bit){ if(n<BIND_SLOTS) k[n++]=BIND_MOUSE+j; else k[BIND_SLOTS-1]=BIND_MOUSE+j; } }
  bind_fmt_(k,out,cap); return 1; }
void bind_mouse_buttons_apply(const char* mb){
  extern void ini_set(const char*, const char*); int moved=0; bind_init();
  for(int i=0;i<BIND_N;i++){ char cur[40], nv[40]; bind_fmt_(g_bind[i],cur,sizeof cur);
    if(bind_mouse_buttons(i,mb,cur,nv,sizeof nv) && strcmp(cur,nv)){ bind_parse_(nv,g_bind[i],NULL); ini_set(BIND_IN[i].key,nv); moved=1; } }
  if(moved){ bind_rebuild(); LOG("input: mouse_buttons=%s moved into the bind_ lines of the .ini\n",mb); } }

static int xkey_to_vk(KeySym k, unsigned code){
  static const struct { unsigned char code; int vk; } BY_PLACE[] = {
    {49, VK_OEM_3}, {20, VK_OEM_MINUS}, {21, VK_OEM_PLUS}, {34, VK_OEM_4}, {35, VK_OEM_6},
    {51, VK_OEM_5}, {47, VK_OEM_1}, {48, VK_OEM_7}, {59, VK_OEM_COMMA}, {60, VK_OEM_PERIOD},
    {61, VK_OEM_2}, {94, VK_OEM_102},
    {90, VK_NUMPAD0}, {87, VK_NUMPAD0+1}, {88, VK_NUMPAD0+2}, {89, VK_NUMPAD0+3},
    {83, VK_NUMPAD0+4}, {84, VK_NUMPAD0+5}, {85, VK_NUMPAD0+6}, {79, VK_NUMPAD0+7}, {80, VK_NUMPAD0+8},
    {81, VK_NUMPAD9}, {86, VK_ADD}, {82, VK_SUBTRACT}, {63, VK_MULTIPLY}, {106, VK_DIVIDE}, {91, VK_DECIMAL}
  };
  if(k>=XK_a && k<=XK_z) return 'A'+(int)(k-XK_a);
  if(k>=XK_A && k<=XK_Z) return 'A'+(int)(k-XK_A);
  if(code>=10 && code<=19) return code==19? '0' : '1'+(int)(code-10);
  if(k>=XK_0 && k<=XK_9) return '0'+(int)(k-XK_0);
  if(k>=XK_F1 && k<=XK_F12) return VK_F1+(int)(k-XK_F1);
  for(size_t i=0;i<sizeof BY_PLACE/sizeof *BY_PLACE;i++) if(BY_PLACE[i].code==code) return BY_PLACE[i].vk;
  switch(k){
    case XK_space: return VK_SPACE;
    case XK_Return: case XK_KP_Enter: return VK_RETURN;
    case XK_BackSpace: return VK_BACK;
    case XK_Escape: return VK_ESCAPE;
    case XK_Left: return VK_LEFT; case XK_Up: return VK_UP; case XK_Right: return VK_RIGHT; case XK_Down: return VK_DOWN;
    case XK_Tab: case XK_ISO_Left_Tab: return VK_TAB;
    case XK_Shift_L: case XK_Shift_R: return VK_SHIFT;
    case XK_Control_L: case XK_Control_R: return VK_CONTROL;
    case XK_Alt_L: case XK_Alt_R: case XK_Meta_L: case XK_Meta_R: return VK_MENU;
    case XK_Pause: return VK_PAUSE; case XK_Caps_Lock: return VK_CAPITAL;
    case XK_Insert: return VK_INSERT; case XK_Delete: return VK_DELETE;
    case XK_Home: return VK_HOME; case XK_End: return VK_END;
    case XK_Prior: return VK_PRIOR; case XK_Next: return VK_NEXT;
    case XK_Super_L: return VK_LWIN; case XK_Super_R: return VK_RWIN; case XK_Menu: return VK_APPS;
    case XK_Num_Lock: return VK_NUMLOCK; case XK_Scroll_Lock: return VK_SCROLL;
  }
  return 0; }
static int xbtn_to_bind(unsigned bt){
  switch(bt){ case 1: return BIND_MOUSE; case 3: return BIND_MOUSE+1; case 2: return BIND_MOUSE+2; case 8: return BIND_MOUSE+3; case 9: return BIND_MOUSE+4; }
  return 0; }
static void key_state(int vk, int down){
  if(vk<=0 || vk>=256) return;
  if(down) keys_down[vk>>6]|=1ull<<(vk&63); else keys_down[vk>>6]&=~(1ull<<(vk&63)); }
static u64 keys_bits(void){ u64 k=0; bind_init();
  for(int w=0;w<4;w++){ u64 m=keys_down[w]; while(m){ k|=g_bind_key[w*64+__builtin_ctzll(m)]; m&=m-1; } }
  return k; }
static u64 mouse_bits(void){ u64 k=0; bind_init(); for(int i=0;i<5;i++) if(mouse_down&(1u<<i)) k|=g_bind_mouse[i]; return k; }

/* Mouse look: grab pointer while focused and menu closed, accumulate relative motion */
static volatile int g_mdx, g_mdy;
static int g_last_x, g_last_y, g_warping;
static void mouse_warp_centre(void){ g_warping=1; XWarpPointer(dpy,None,win,0,0,0,0,g_ww/2,g_wh/2); XFlush(dpy); }
static void mouse_grab_sync(void){
  extern int g_opt_mouse_aim; static u64 retry;
  int want= g_opt_mouse_aim && g_focused && !ov_is_open();
  if(want && !g_grabbed){
    u64 now=plat_ns(); if(now-retry<250000000ull) return; retry=now;
    if(XGrabPointer(dpy,win,True,ButtonPressMask|ButtonReleaseMask|PointerMotionMask,GrabModeAsync,GrabModeAsync,win,g_blank,CurrentTime)!=GrabSuccess) return;
    g_grabbed=1;
    Window root, child; int rx, ry, x, y; unsigned mask;
    if(!XQueryPointer(dpy,win,&root,&child,&rx,&ry,&x,&y,&mask)){ x=g_ww/2; y=g_wh/2; }
    g_last_x=x; g_last_y=y; g_warping=0;
    if(x!=g_ww/2 || y!=g_wh/2) mouse_warp_centre();
  } else if(!want && g_grabbed){
    XUngrabPointer(dpy,CurrentTime); XFlush(dpy); g_grabbed=0; } }
extern void ov_input_mouse(float x, float y);
static void mouse_motion(int x, int y){
  if(!g_grabbed){ ov_input_mouse((float)x,(float)y); return; }
  int cx=g_ww/2, cy=g_wh/2;
  if(g_warping && x==cx && y==cy){ g_warping=0; g_last_x=x; g_last_y=y; return; }
  __atomic_fetch_add(&g_mdx,x-g_last_x,__ATOMIC_RELAXED);
  __atomic_fetch_add(&g_mdy,y-g_last_y,__ATOMIC_RELAXED);
  g_last_x=x; g_last_y=y;
  if(!g_warping && (abs(x-cx)>g_ww/4 || abs(y-cy)>g_wh/4)) mouse_warp_centre(); }
static void mouse_apply(i32* rx, i32* ry){
  extern int g_opt_mouse_aim, g_opt_mouse_sens;
  int dx=__atomic_exchange_n(&g_mdx,0,__ATOMIC_RELAXED), dy=__atomic_exchange_n(&g_mdy,0,__ATOMIC_RELAXED);
  static float vx, vy; static u64 last;
  if(!g_opt_mouse_aim){ vx=vy=0; last=0; return; }
  u64 now=plat_ns(); float dt= last? (float)((now-last)/1e9) : 0.016f; last=now; if(dt<0.001f) dt=0.001f; if(dt>0.1f) dt=0.1f;
  { extern int g_opt_mouse_mode; extern volatile float g_motion_acc[3]; extern volatile long long g_motion_pend_u[3]; extern volatile unsigned long long g_motion_mouse_ns;
    if(g_opt_mouse_mode==1){ double k=(double)g_opt_mouse_sens/100.0/40000.0*1e6; static int said; if(!said){ said=1; LOG("input: mouse drives the motion sensor (Pointer mode)\n"); }
      if(dx||dy){ __atomic_fetch_add(&g_motion_pend_u[0],(long long)((double)dy*k),__ATOMIC_RELAXED); __atomic_fetch_add(&g_motion_pend_u[2],(long long)((double)dx*k),__ATOMIC_RELAXED); }
      if(g_motion_acc[2]==0) g_motion_acc[2]=-1.0f; g_motion_mouse_ns=now; vx=vy=0; (void)dt; return; } }
  float s=(float)g_opt_mouse_sens/100.0f/3000.0f;
  static float owed_x, owed_y;
  { float mx=(float)dx*s, my=-(float)dy*s;
    if((mx>0 && owed_x<0) || (mx<0 && owed_x>0)) owed_x=0;
    if((my>0 && owed_y<0) || (my<0 && owed_y>0)) owed_y=0;
    owed_x+=mx; owed_y+=my;
    float cap= dx||dy? 0.15f : 0.03f, om=sqrtf(owed_x*owed_x+owed_y*owed_y);
    if(om>cap){ owed_x*=cap/om; owed_y*=cap/om; } }
  float tx=owed_x/dt, ty=owed_y/dt;
  { float tm=sqrtf(tx*tx+ty*ty); if(tm>1.0f){ tx/=tm; ty/=tm; } }
  owed_x-=tx*dt; owed_y-=ty*dt;
  float a=dt/(dt+0.012f); vx+=(tx-vx)*a; vy+=(ty-vy)*a;
  float m=sqrtf(vx*vx+vy*vy);
  if(m<0.01f){ vx=vy=0; return; }
  if(m>1.0f){ vx/=m; vy/=m; m=1.0f; }
  { float out=0.16f+0.84f*m; *rx=(i32)(vx/m*out*32767.0f); *ry=(i32)(vy/m*out*32767.0f); }
  { static int said; if(!said){ said=1; LOG("input: mouse look in use\n"); } } }

/* one-shot keys act on the first press only, not on auto-repeat */
static int key_once(KeySym k, int down){
  static KeySym held[8]; int i;
  for(i=0;i<8;i++) if(held[i]==k) break;
  if(!down){ if(i<8) held[i]=0; return 0; }
  if(i<8) return 0;
  for(i=0;i<8;i++) if(!held[i]){ held[i]=k; break; }
  return 1; }

/* the in-game menu (beyond_ui/ov.cpp): F1 toggles it; while it is open it takes the keyboard and the mouse */
extern void ov_input_toggle(void); extern void ov_input_key(int ovk, int down); extern void ov_input_char(unsigned c);
extern void ov_input_mouse(float x, float y); extern void ov_input_button(int b, int down); extern void ov_input_wheel(float dy);
extern int ov_is_open(void); extern int ov_pad(unsigned long long phys, int lx, int ly);
extern int ov_is_capturing(void); extern void ov_input_bind_code(int code);
static int ovk_from_keysym(KeySym k){
  switch(k){ case XK_Up: return 1; case XK_Down: return 2; case XK_Left: return 3; case XK_Right: return 4; case XK_Return: case XK_KP_Enter: return 5;
    case XK_Escape: return 6; case XK_Tab: return 7; case XK_Prior: return 8; case XK_Next: return 9; case XK_space: return 10; case XK_Home: return 11; case XK_End: return 12; }
  return 0; }
void backend_request_quit(void){ g_quit=1; }
static void key_down(XKeyEvent* e){
  KeySym k=XLookupKeysym(e,0);
  int once=key_once(k,1);
  if(k==XK_F1){ if(once) ov_input_toggle(); return; }
  if(ov_is_open() && k!=XK_F11){
    if(once && ov_is_capturing()){ int vk=xkey_to_vk(k,e->keycode); if(vk>0) ov_input_bind_code(vk); return; }
    ov_input_key(ovk_from_keysym(k),1);
    { char c[8]; KeySym ks; int n=XLookupString(e,c,sizeof c,&ks,NULL); if(n==1 && (unsigned char)c[0]>=32) ov_input_char((unsigned char)c[0]); }
    return; }
  if(k==XK_Return && (e->state&Mod1Mask)){ if(once) set_fullscreen(!g_fullscreen); return; }
  switch(k){
    case XK_Escape: g_quit=1; return;
    case XK_F11: if(once) set_fullscreen(!g_fullscreen); return;
    case XK_F3: if(once) g_shot_req=1; return;
    case XK_F7: if(once) LOG("F7 video recording is only available on Windows\n"); return;
    case XK_F8: if(once){ extern volatile int g_shot8_req; g_shot8_req=1; } return;
    case XK_F5: if(once){ extern void gpu_gl_dump_rts(void); extern void kernel_dump_threads(void); extern __thread FILE* g_log_redirect;
        plat_mkdir("captures"); FILE* f=fopen("captures/threads.txt","w");   /* written next to the trace */
        if(f){ g_log_redirect=f; kernel_dump_threads(); g_log_redirect=NULL; fclose(f); }
        kernel_dump_threads(); gpu_gl_dump_rts(); } return;
    case XK_F4: if(once){ g_opt_fps= g_opt_fps==0? 1 : 0; char t[300]; snprintf(t,sizeof t,"%s  |  FPS limit: %s",g_title,g_opt_fps==0? "unlimited" : "60"); set_title(t);
        extern void ini_set(const char*, const char*); char v[8]; snprintf(v,sizeof v,"%d",g_opt_fps); ini_set("fps_limit",v); } return;
  }
  key_state(xkey_to_vk(k,e->keycode),1); }

static i32 axis(u64 b, int neg, int pos){ return ((b>>pos)&1? 32767:0) - ((b>>neg)&1? 32767:0); }

/* Test-only frame-indexed replay, as backend_win32.c has it (the same route files run on both): SWR_TEST_SCRIPT CSV
   guest_frame,hex_buttons,lx,ly,rx,ry, held until the next row; SWR_TEST_SETENV=FRAME:NAME=VALUE; captures at
   SWR_TEST_CAPTURE_FRAMES (SWR_TEST_CAPTURE_PREFIX-fN); SWR_TEST_EXIT_FRAME. Inert when unset */
typedef struct { u32 frame; u64 buttons; i32 lx, ly, rx, ry; } TestInput;
static TestInput* test_inputs; static u32 test_n, test_i; static int test_loaded, test_replay;
static void test_input_apply(u64* b, i32* lx, i32* ly, i32* rx, i32* ry){
  if(!test_loaded){ test_loaded=1; const char* path=getenv("SWR_TEST_SCRIPT");
    if(path && *path){ FILE* f=fopen(path,"r"); char line[256]; u32 cap=8192;
      if(!f){ WARN("test input: cannot open replay %s\n",path); _exit(2); }
      test_inputs=calloc(cap,sizeof *test_inputs);
      while(fgets(line,sizeof line,f)){ if(line[0]=='#'||line[0]=='\n'||line[0]=='\r') continue; TestInput v={0}; unsigned long long m;
        if(sscanf(line,"%u,%llx,%d,%d,%d,%d",&v.frame,&m,&v.lx,&v.ly,&v.rx,&v.ry)!=6 || test_n==cap){ fclose(f); WARN("test input: invalid replay row\n"); _exit(2); }
        v.buttons=(u64)m; test_inputs[test_n++]=v; }
      fclose(f); test_replay= test_n>0; LOG("test input: frame replay active, %u states\n",test_n); } }
  u32 frame=(u32)g_frames;
  { static int done; const char* se; if(!done && (se=getenv("SWR_TEST_SETENV")) && *se){ u32 at=(u32)strtoul(se,NULL,10); const char* kv=strchr(se,':');
      if(!kv) done=1; else if(frame>=at){ char k[64]; const char* eq=strchr(kv+1,'=');
        if(eq && eq-kv-1<64){ memcpy(k,kv+1,eq-kv-1); k[eq-kv-1]=0; setenv(k,eq+1,1); LOG("test: frame %u set %s=%s\n",frame,k,eq+1); } done=1; } } }
  if(test_replay){ while(test_i+1<test_n && test_inputs[test_i+1].frame<=frame) test_i++; TestInput* v=&test_inputs[test_i];
    *b=v->buttons; *lx=v->lx; *ly=v->ly; *rx=v->rx; *ry=v->ry; }
}
static void test_frame_hooks(void){
  extern u64 gpu_gl_screenshot(const char* path);
  if(!getenv("SWR_TEST_SCRIPT")) return;
  static int configured; static const char* times; static const char* prefix; static u64 next;
  if(!configured){ configured=1; times=getenv("SWR_TEST_CAPTURE_FRAMES"); prefix=getenv("SWR_TEST_CAPTURE_PREFIX"); if(times&&prefix) next=strtoull(times,(char**)&times,10); else times=NULL; }
  if(times && next && (u64)g_frames>=next){ char path[1024]; snprintf(path,sizeof path,"%s-f%llu",prefix,(unsigned long long)next);
    LOG("test capture: requested_frame=%llu actual_frame=%d ns=%llu\n",(unsigned long long)next,g_frames,(unsigned long long)plat_ns()); gpu_gl_screenshot(path);
    if(*times==','){ times++; next=strtoull(times,(char**)&times,10); } else { times=NULL; next=0; } }
  static u64 heartbeat; u64 now=plat_ns(); if(!heartbeat || now-heartbeat>=1000000000ull){ heartbeat=now; LOG("test heartbeat: frame=%d fps_limit=%d ns=%llu\n",g_frames,g_opt_fps,(unsigned long long)now); }
  const char* stop=getenv("SWR_TEST_EXIT_FRAME"); if(stop && (u64)g_frames>=strtoull(stop,NULL,10)){ LOG("test complete: frame=%d\n",g_frames); extern void save_hints(void); save_hints(); log_flush(); fflush(NULL); _exit(0); }
}

void video_poll(void){
  { int r=g_fs_req; if(r>=0){ g_fs_req=-1; set_fullscreen(r); } }
  cursor_sync();
  mouse_grab_sync();
  while(XPending(dpy)){ XEvent e; XNextEvent(dpy,&e);
    if(e.type==KeyPress) key_down(&e.xkey);
    else if(e.type==KeyRelease){ KeySym k=XLookupKeysym(&e.xkey,0); key_once(k,0); key_state(xkey_to_vk(k,e.xkey.keycode),0); if(ov_is_open()) ov_input_key(ovk_from_keysym(k),0); }
    else if(e.type==MotionNotify){ mouse_motion(e.xmotion.x,e.xmotion.y); }
    else if(e.type==ButtonPress || e.type==ButtonRelease){ unsigned bt=e.xbutton.button; int down=e.type==ButtonPress;
      if(ov_is_open()){
        if(down && ov_is_capturing()){ int mb=xbtn_to_bind(bt); if(mb>0){ ov_input_bind_code(mb); continue; } }
        ov_input_mouse((float)e.xbutton.x,(float)e.xbutton.y);
        if(bt==1 || bt==3) ov_input_button(bt==1? 0 : 1, down); else if(down && (bt==4 || bt==5)) ov_input_wheel(bt==4? 1.0f : -1.0f);
      } else {
        int mb=xbtn_to_bind(bt);
        if(mb>=BIND_MOUSE && mb<BIND_MOUSE+5){ int idx=mb-BIND_MOUSE; if(down) mouse_down|=1u<<idx; else mouse_down&=~(1u<<idx); }
      } }
    else if(e.type==FocusIn) g_focused=1;
    else if(e.type==FocusOut){ g_focused=0; backend_release_keys(); }
    else if(e.type==ConfigureNotify){ g_ww=e.xconfigure.width>0? e.xconfigure.width : 1; g_wh=e.xconfigure.height>0? e.xconfigure.height : 1; }
    else if(e.type==ClientMessage && (Atom)e.xclient.data.l[0]==wm_delete){ LOG("window: closed by the user\n"); g_quit=1; } }
  u64 k=keys_bits()|mouse_bits(), b=0;
  { extern int g_opt_mouse_mode; if(g_opt_mouse_mode==1 && (k>>9&1)) k=(k&~(1ull<<9))|1ull; }
  i32 lx=axis(k,40,42), ly=axis(k,43,41), rx=axis(k,44,46), ry=axis(k,47,45);
  pad_poll(&b,&lx,&ly,&rx,&ry);
  u64 phys=b; i32 plx=lx, ply=ly; b&=~(1ull<<30);
  b=swr_pad_remap(b)|(k&0xFFFFull);
  /* stick-as-button bits 16..23 */
  if(lx<-16000) b|=1ull<<16; if(ly>16000) b|=1ull<<17; if(lx>16000) b|=1ull<<18; if(ly<-16000) b|=1ull<<19;
  if(rx<-16000) b|=1ull<<20; if(ry>16000) b|=1ull<<21; if(rx>16000) b|=1ull<<22; if(ry<-16000) b|=1ull<<23;
  /* test input: --press=hex --press_at=ms, or --script=ms:hex,ms:hex,... */
  u64 el=(plat_ns()-g_start)/1000000ull;
  if(g_press && el>=g_press_at && el<g_press_at+g_press_len) b|=g_press;
  if(g_script[0]){ const char* p=g_script; while(*p){ u64 t=strtoull(p,(char**)&p,10); if(*p!=':') break; p++; u64 m=strtoull(p,(char**)&p,16); if(el>=t && el<t+g_press_len) b|=m; if(*p==',') p++; } }
  test_input_apply(&b,&lx,&ly,&rx,&ry);
  /* every session's input goes to input_record.csv (the SWR_TEST_SCRIPT replay format, a row per change), so a
     problem spot can be reached again by replaying the route there. SWR_NO_INPUT_RECORD=1: off */
  if(!test_replay){ static FILE* rf; static int rinit; static u64 pb=~0ull; static i32 plx=1<<30, ply, prx, pry;
    if(!rinit){ rinit=1; if(!getenv("SWR_NO_INPUT_RECORD")){ extern char g_exedir[]; char p[1200]; snprintf(p,sizeof p,"%sinput_record.csv",g_exedir); rf=fopen(p,"w");
        if(rf) fprintf(rf,"# guest_frame,hex_buttons,lx,ly,rx,ry (replay: SWR_TEST_SCRIPT=this file)\n"); } }
    if(rf && (b!=pb || lx!=plx || ly!=ply || rx!=prx || ry!=pry)){ fprintf(rf,"%d,%llx,%d,%d,%d,%d\n",g_frames,(unsigned long long)b,lx,ly,rx,ry); pb=b; plx=lx; ply=ly; prx=rx; pry=ry;
      static int nfl; if(++nfl%64==0) fflush(rf); } }
  if(test_replay){ phys=b; plx=lx; ply=ly; }   /* tests drive the menu with the replayed buttons */
  if(ov_pad(phys,plx,ply)){ b=0; lx=ly=rx=ry=0; }   /* the menu has the controller (and the game neutral input) */
  { int menu=ov_is_open(); if(!menu) mouse_apply(&rx,&ry);
    static u64 g0; int g=!menu && ((k>>48)&1); u64 now=plat_ns();
    if(!g) g0=0; else { if(!g0) g0=now; u64 held=now-g0;
      if(held<120000000ull) b|=1ull<<4;
      else if(held>300000000ull){ extern volatile float g_motion_gyro[3]; extern volatile unsigned long long g_motion_ns; ly=32767; ry=32767; g_motion_gyro[0]=1.0f; g_motion_gyro[1]=0; g_motion_gyro[2]=0; g_motion_ns=now; }
      static int said; if(!said){ said=1; LOG("input: lasso key used\n"); } } }
  g_pad.buttons=b; g_pad.lx=lx; g_pad.ly=ly; g_pad.rx=rx; g_pad.ry=ry;
}

void frontend_frame(void){
  extern u64 gpu_gl_screenshot(const char* path);
  test_frame_hooks();
  if(g_shot_req){ g_shot_req=0; char p[300]; time_t now=time(NULL); struct tm t; localtime_r(&now,&t); plat_mkdir("captures");
    snprintf(p,sizeof p,"captures/screenshot_%04d%02d%02d_%02d%02d%02d",t.tm_year+1900,t.tm_mon+1,t.tm_mday,t.tm_hour,t.tm_min,t.tm_sec); gpu_gl_screenshot(p); }
  u64 el=plat_ns()-g_start;
  if((g_maxframes>=0 && g_frames>=g_maxframes) || (g_seconds && el>=(u64)g_seconds*1000000000ull)){
    u64 h=gpu_gl_screenshot(g_shot[0]? g_shot : NULL);
    fprintf(stderr,"\nframes=%d hash=%016llx\n",g_frames,(unsigned long long)h);
    extern void save_hints(void); save_hints(); log_flush(); fflush(NULL); _exit(0); }
}

int backend_glq_ok(void){ return 1; }   /* GL calls go through glq */

/* Play screen / first-run setup (beyond_ui/fe.cpp, in its own window) before the game starts (main.c). 0 = go on with the
   game, 1 = quit. Skipped for test runs and --no-launcher, as backend_win32.c does; builds without it link stub_fe.c */
extern int fe_should_show(int argc, char** argv, const char* exedir, const char* exename); extern int fe_run(void* hwnd);
int backend_frontend(int argc, char** argv, const char* exedir, const char* exename){
  if(!getenv("SWR_FE_TEST") && (getenv("SWR_TEST_SCRIPT") || getenv("SWR_TEST_EXIT_FRAME") || getenv("SWR_TEST_RECORD"))) return 0;
  for(int i=1;i<argc;i++) if(!strcmp(argv[i],"--no-launcher") || !strncmp(argv[i],"--seconds=",10) || !strncmp(argv[i],"--frames=",9)) return 0;   /* timed runs are tests */
  if(!fe_should_show(argc,argv,exedir,exename)) return 0;
  return fe_run(NULL); }
