/* SwitchRecomp - Copyright 2026 MZ Labs
   Licensed under the Apache License 2.0. See LICENSE.txt and NOTICE.txt;
   any copy or derived work must keep the credit to SwitchRecomp by MZ Labs. */
/* hid. Games read the shared memory directly, so we fill it in the real layout (17 entry LIFOs). */
#include <math.h>
#include "rt.h"
#include "hid_layout.h"

KShmem* kshmem_new(u64 size, const char* name);
u8* kshmem_ptr(KShmem* s);
static KShmem* g_hid_shm;
PadState g_pad;
/* the controller's motion, set by the input backend (backend_win32.c sdl_poll): Switch units and axes */
volatile float g_motion_gyro[3], g_motion_acc[3]; volatile unsigned long long g_motion_ns;
volatile long long g_motion_pend_u[3]; volatile unsigned long long g_motion_mouse_ns;   /* mouse Pointer mode: angle moved since the last update, in millionths of a turn */
static u64 g_samp;
static u32 g_style_supported=0x7F;
static KEvent* g_style_ev[10];

static void lifo_push(u8* lifo, u32 esize, const void* state, u32 ssize, u64 samp){
  /* lifo header: unused, buffer_count, tail, count. entries: {u64 sampling_number; state} */
  u64 cnt=17, tail, count; memcpy(&tail,lifo+0x10,8); memcpy(&count,lifo+0x18,8);
  tail=(tail+1)%cnt; u8* e=lifo+0x20+tail*esize;
  memcpy(e,&samp,8); memcpy(e+8,state,ssize);
  __atomic_thread_fence(__ATOMIC_SEQ_CST);
  memcpy(lifo+8,&cnt,8); memcpy(lifo+0x10,&tail,8); if(count<16) count++; memcpy(lifo+0x18,&count,8);
}

/* npad index: 0..7 = No1..No8, 8 = Handheld (0x20), 9 = Other (0x10) */
static void npad_setup(u8* base, int idx, u32 style, u32 devtype){
  u8* e=base+HO_HidSharedMemory_npad+idx*HS_HidNpadSharedMemoryEntry;
  memcpy(e+HO_HidNpadInternalState_style_set,&style,4);
  u32 jam=0; memcpy(e+HO_HidNpadInternalState_joy_assignment_mode,&jam,4);
  u32 col[3]={1,0xFF323232u,0xFF0A0A0A}; memcpy(e+HO_HidNpadInternalState_full_key_color,col,12);
  u32 jc[5]={1,0xFF3C3C3Cu,0xFF0A0A0A,0xFF3C3C3Cu,0xFF0A0A0A}; memcpy(e+HO_HidNpadInternalState_joy_color,jc,20);
  memcpy(e+HO_HidNpadInternalState_device_type,&devtype,4);
  u64 props=(1ull<<0)|(1ull<<1)|(1ull<<2)|(1ull<<3)|(1ull<<11)|(1ull<<12); /* charging, powered, dual joy... + abxy + plus/minus */
  memcpy(e+HO_HidNpadInternalState_system_properties,&props,8);
  u32 bat[3]={4,4,4}; memcpy(e+HO_HidNpadInternalState_battery_level,bat,12);
  u32 fattr=0; memcpy(e+HO_HidNpadInternalState_applet_footer_ui_attribute,&fattr,4);
  u8 ftype= idx==8? 4 : 3; e[HO_HidNpadInternalState_applet_footer_ui_type]=ftype;   /* handheld / pro controller */
}

u64 g_pad_buttons_now;   /* cheats.c: keypress codes */
#ifdef SWR_DEVTOOLS
#include "local/aim_dev.inc"   /* developer tools: the aim scan and the camera hooks' tests (not in the repository) */
#else
/* public builds: the game code has no hooks for svc 0xFFE1-0xFFEA (kernel.c), and the raw mouse counters
   (backend_win32.c) are never used, so mouse_mode=raw falls back to the stick */
volatile long long g_raw_mx, g_raw_my; volatile unsigned long long g_raw_used_ns; char g_test_shot[64];
void aim_axis_hook(CPU* c, int at_exit){ (void)c; (void)at_exit; }
void aim_look_hook(CPU* c){ (void)c; }
void aim_ctl_hook(CPU* c){ (void)c; }
void aim_val_hook(CPU* c, int at_exit){ (void)c; (void)at_exit; }
void aim_src_hook(CPU* c, int at_exit){ (void)c; (void)at_exit; }
void aim_cam_entry(CPU* c){ (void)c; }
void aim_cam_hook(CPU* c){ (void)c; }
static void aim_scan_tick(void){}
#endif

void hid_update(void){
  if(!g_hid_shm) return;
  aim_scan_tick();
  u8* b=kshmem_ptr(g_hid_shm);
  g_samp++;
  u8 st[0x28]; memset(st,0,sizeof st);
  u64 btn=g_pad.buttons;
  /* sticks also set the stick-direction button bits, like hw */
  if(g_pad.lx<-16000) btn|=1ull<<16; if(g_pad.ly>16000) btn|=1ull<<17; if(g_pad.lx>16000) btn|=1ull<<18; if(g_pad.ly<-16000) btn|=1ull<<19;
  if(g_pad.rx<-16000) btn|=1ull<<20; if(g_pad.ry>16000) btn|=1ull<<21; if(g_pad.rx>16000) btn|=1ull<<22; if(g_pad.ry<-16000) btn|=1ull<<23;
  memcpy(st+HO_HidNpadCommonState_sampling_number,&g_samp,8);
  memcpy(st+HO_HidNpadCommonState_buttons,&btn,8); g_pad_buttons_now=btn;
  i32 s4[4]={g_pad.lx,g_pad.ly,g_pad.rx,g_pad.ry}; memcpy(st+HO_HidNpadCommonState_analog_stick_l,s4,16);
  u32 attr=1; memcpy(st+HO_HidNpadCommonState_attributes,&attr,4);   /* IsConnected */
  /* No1 = pro / dual joycon, Handheld = attached */
  u8* e0=b+HO_HidSharedMemory_npad;
  lifo_push(e0+HO_HidNpadInternalState_full_key_lifo,HS_HidNpadCommonStateAtomicStorage,st,0x28,g_samp);
  lifo_push(e0+HO_HidNpadInternalState_joy_dual_lifo,HS_HidNpadCommonStateAtomicStorage,st,0x28,g_samp);
  lifo_push(e0+HO_HidNpadInternalState_handheld_lifo,HS_HidNpadCommonStateAtomicStorage,st,0x28,g_samp);
  u8* eh=b+HO_HidSharedMemory_npad+8*HS_HidNpadSharedMemoryEntry;
  lifo_push(eh+HO_HidNpadInternalState_handheld_lifo,HS_HidNpadCommonStateAtomicStorage,st,0x28,g_samp);
  lifo_push(eh+HO_HidNpadInternalState_full_key_lifo,HS_HidNpadCommonStateAtomicStorage,st,0x28,g_samp);
  /* unused npads still need a sampling number, the SDK looks at it */
  u8 z[0x28]; memset(z,0,sizeof z); memcpy(z,&g_samp,8);
  for(int i=1;i<10;i++){ if(i==8) continue; u8* e=b+HO_HidSharedMemory_npad+i*HS_HidNpadSharedMemoryEntry;
    lifo_push(e+HO_HidNpadInternalState_full_key_lifo,HS_HidNpadCommonStateAtomicStorage,z,0x28,g_samp); }
  /* six-axis: the controller's gyro and accelerometer when the backend has them (fresh within 200 ms), else at rest,
     gravity down. Units: G, rotations per second, rotations (angle), and the orientation as a 3x3 matrix, integrated
     here from the angular velocity */
  u8 six[HS_HidSixAxisSensorState]; memset(six,0,sizeof six);
  memcpy(six,&g_samp,8); memcpy(six+8,&g_samp,8);
  float acc[3]={0,0,-1}, gyr[3]={0,0,0};
  static float ang[3], q[4]={1,0,0,0}; static u64 m_last;
  { u64 now=plat_ns(); float dt= m_last? (float)((now-m_last)/1e9) : 0.f; m_last=now; if(dt>0.1f) dt=0.1f;
    int mouse= g_motion_mouse_ns && now-g_motion_mouse_ns<200000000ull;
    if(mouse || (g_motion_ns && now-g_motion_ns<200000000ull)){
      if(mouse){ float hd= dt>0.002f? dt : 0.002f;   /* the angle moved this update, as a speed over it */
        for(int k=0;k<3;k++){ float turned=(float)__atomic_exchange_n(&g_motion_pend_u[k],0,__ATOMIC_RELAXED)/1e6f; gyr[k]=turned/hd; acc[k]=g_motion_acc[k]; ang[k]+=turned; } }
      else for(int k=0;k<3;k++){ gyr[k]=g_motion_gyro[k]; acc[k]=g_motion_acc[k]; ang[k]+=gyr[k]*dt; }
      /* q += 0.5 * q * (0, w) dt, w in rad/s */
      float wx=gyr[0]*6.2831853f, wy=gyr[1]*6.2831853f, wz=gyr[2]*6.2831853f;
      float a=q[0], b=q[1], c=q[2], d=q[3], h=0.5f*dt;
      q[0]+=h*(-b*wx-c*wy-d*wz); q[1]+=h*(a*wx+c*wz-d*wy); q[2]+=h*(a*wy-b*wz+d*wx); q[3]+=h*(a*wz+b*wy-c*wx);
      float n=sqrtf(q[0]*q[0]+q[1]*q[1]+q[2]*q[2]+q[3]*q[3]); if(n>0) for(int k=0;k<4;k++) q[k]/=n;
      /* the mouse: gravity turns with the aim, as a real controller's accelerometer would. Held level while the "gyro"
         pitched, it disagreed, and the game's drift correction pulled the aim back toward the centre (2026-11-14) */
      if(mouse){ float w=q[0], x=q[1], y=q[2], z=q[3]; acc[0]=-2*(x*z-w*y); acc[1]=-2*(y*z+w*x); acc[2]=-(1-2*(x*x+y*y)); } } }
  memcpy(six+HO_HidSixAxisSensorState_acceleration,acc,12);
  memcpy(six+HO_HidSixAxisSensorState_acceleration+12,gyr,12);   /* angular_velocity follows acceleration */
  memcpy(six+HO_HidSixAxisSensorState_acceleration+24,ang,12);   /* then angle */
  { float w=q[0], x=q[1], y=q[2], z=q[3];
    float dir[9]={1-2*(y*y+z*z), 2*(x*y-w*z), 2*(x*z+w*y),
                  2*(x*y+w*z), 1-2*(x*x+z*z), 2*(y*z-w*x),
                  2*(x*z-w*y), 2*(y*z+w*x), 1-2*(x*x+y*y)};
    memcpy(six+HO_HidSixAxisSensorState_direction,dir,36); }
  u32 sattr=1; memcpy(six+HO_HidSixAxisSensorState_attributes,&sattr,4);
  lifo_push(e0+HO_HidNpadInternalState_full_key_six_axis_sensor_lifo,HS_HidSixAxisSensorStateAtomicStorage,six,sizeof six,g_samp);
  lifo_push(e0+HO_HidNpadInternalState_joy_dual_left_six_axis_sensor_lifo,HS_HidSixAxisSensorStateAtomicStorage,six,sizeof six,g_samp);
  lifo_push(e0+HO_HidNpadInternalState_joy_dual_right_six_axis_sensor_lifo,HS_HidSixAxisSensorStateAtomicStorage,six,sizeof six,g_samp);
  lifo_push(eh+HO_HidNpadInternalState_handheld_six_axis_sensor_lifo,HS_HidSixAxisSensorStateAtomicStorage,six,sizeof six,g_samp);
  /* touch */
  u8 ts[HS_HidTouchScreenState]; memset(ts,0,sizeof ts); memcpy(ts,&g_samp,8);
  if(g_pad.touch){ u32 cnt=1; memcpy(ts+HO_HidTouchScreenState_count,&cnt,4);
    u8* t=ts+HO_HidTouchScreenState_touches; u64 dt=0; memcpy(t,&dt,8); u32 attr2=0, fid=0; memcpy(t+8,&attr2,4); memcpy(t+12,&fid,4);
    u32 xy[4]={(u32)g_pad.tx,(u32)g_pad.ty,20,20}; memcpy(t+16,xy,16); }
  lifo_push(b+HO_HidSharedMemory_touchscreen+HO_HidTouchScreenSharedMemoryFormat_lifo,HS_HidTouchScreenStateAtomicStorage,ts,sizeof ts,g_samp);
  /* keyboard/mouse/debug pad: only bump the counters */
  u8 kb[HS_HidKeyboardState]; memset(kb,0,sizeof kb); memcpy(kb,&g_samp,8);
  lifo_push(b+HO_HidSharedMemory_keyboard,8+HS_HidKeyboardState,kb,sizeof kb,g_samp);
  u8 ms[HS_HidMouseState]; memset(ms,0,sizeof ms); memcpy(ms,&g_samp,8);
  lifo_push(b+HO_HidSharedMemory_mouse,8+HS_HidMouseState,ms,sizeof ms,g_samp);
  u8 dp[HS_HidDebugPadState]; memset(dp,0,sizeof dp); memcpy(dp,&g_samp,8);
  lifo_push(b+HO_HidSharedMemory_debug_pad,8+HS_HidDebugPadState,dp,sizeof dp,g_samp);
  { extern void cheats_tick(void); cheats_tick(); }
  { extern void dk_tick(void); dk_tick(); }   /* devkit toggles held at their value (devkit.c) */   /* cheat codes and the FOV hook, once per update like dmnt */
}

static int hid_applet_res(SvcObj* o, IpcCtx* c){
  if(c->cmd==0){ out_copy_h(c,h_new(&g_hid_shm->o)); return 0; }
  return stub_ok(o,c); }

static int hid_vibration_list(SvcObj* o, IpcCtx* c){ (void)o; (void)c; return 0; }   /* IActiveVibrationDeviceList: 0 ActivateVibrationDevice */
static u64 g_hold_type=0, g_comm_mode=0, g_handheld_mode=0;

/* controller rumble: the newest HD rumble value the game sent for each Joy-Con side (VibrationDeviceHandle device
   index: 0 left, 1 right) as {low amplitude, low frequency, high amplitude, high frequency}. The backends read it as
   a standard pad's two motors (hid_rumble_get) */
int g_opt_rumble=100;   /* ini [Input] rumble: strength in percent, 0 = off */
/* kept per whole handle: the game sends values for every pad style it set up (handheld, full key, both Joy-Con sides)
   in one go, and with only two slots the zeros for the styles not in use overwrote the real rumble at once, so a
   DualSense never rumbled (2026-11-20). The pad takes the strongest of all handles refreshed lately */
#define VIB_N 16
static volatile float g_vib[VIB_N][4]; static volatile u64 g_vib_ns[VIB_N]; static u32 g_vib_h[VIB_N]; static int g_vib_hn; static u64 g_vib_n;
static void vib_set(u32 handle, const float* v){
  int dev=-1; for(int i=0;i<g_vib_hn;i++) if(g_vib_h[i]==handle){ dev=i; break; }
  if(dev<0){ if(g_vib_hn<VIB_N){ dev=g_vib_hn; g_vib_h[g_vib_hn++]=handle; } else dev=VIB_N-1; }
  for(int i=0;i<4;i++) g_vib[dev][i]= v[i]==v[i] && v[i]>0? v[i] : 0;   /* NaN or negative: silent */
  g_vib_ns[dev]=plat_ns();
  if(!g_vib_n++) LOG("input: the game sent its first rumble (amplitudes %.2f low / %.2f high)\n",v[0],v[2]);
  { static int said; if(!said && (v[0]>0.001f || v[2]>0.001f)){ said=1; LOG("input: the game's first non-zero rumble (amplitudes %.2f low / %.2f high)\n",v[0],v[2]); } } }
/* the pad's motors, 0..1: the heavy (low-frequency) one takes the stronger low band of the two sides, the light one
   the stronger high band. A side not refreshed for 200 ms counts as stopped (the game stops sending when it ends) */
void hid_rumble_get(float* lo, float* hi){
  u64 now=plat_ns(); float l=0, h=0;
  for(int d=0;d<VIB_N;d++){ if(!g_vib_ns[d] || now-g_vib_ns[d]>200000000ull) continue; if(g_vib[d][0]>l) l=g_vib[d][0]; if(g_vib[d][2]>h) h=g_vib[d][2]; }
  float s= g_opt_rumble>0? g_opt_rumble/100.0f : 0; l*=s; h*=s;
  *lo= l>1? 1 : l; *hi= h>1? 1 : h; }

static int hid_disp(SvcObj* o, IpcCtx* c){
  switch(c->cmd){
    case 0: out_obj(c,svcobj_new("IAppletResource",hid_applet_res,0)); return 0;
    case 40: out_copy_h(c,h_new(&kevent_new("xpad id",0)->o)); return 0;         /* AcquireXpadIdEventHandle */
    case 55: case 59: out64(c,0); return 0;                                        /* GetXpadIds: none */
    case 58: case 62: out_copy_h(c,h_new(&kshmem_new(0x1000,"hid lifo")->o)); return 0;
    case 68: out8(c,1); return 0;                                                  /* IsSixAxisSensorFusionEnabled */
    case 82: { u64 now=plat_ns(); int moving= g_motion_ns && now-g_motion_ns<200000000ull &&
                 fabsf(g_motion_gyro[0])+fabsf(g_motion_gyro[1])+fabsf(g_motion_gyro[2])>0.01f;   /* > ~3.6 deg/s */
               out8(c,moving? 0 : 1); return 0; }                                    /* IsSixAxisSensorAtRest */
    case 69: case 70: case 71: case 72: case 73: case 74: case 75: case 76: case 77: case 78: case 79: case 80: case 81: case 83: case 84: case 85: case 86: case 87: case 88: case 89:
      if(c->cmd==71||c->cmd==73||c->cmd==79||c->cmd==84||c->cmd==86||c->cmd==88){ out32(c,0); out32(c,0); } return 0;
    case 100: { g_style_supported=in32(c,0);                                         /* SetSupportedNpadStyleSet */
      /* player 1 reports one style, like a real controller: FullKey (Pro Controller) when the game takes it, else JoyDual.
         Reporting both made Metroid Prime 4 read player 1 as JoyDual, a style it had no six-axis handles for (an all-zero
         handle, then an abort in nn::hid::detail::GetSixAxisSensorStates) */
      u32 st= (g_style_supported&1u)? 1u : (g_style_supported&4u)? 4u : (1u<<0)|(1u<<2);
      u32 dev= st==1u? 1u<<0 : (1u<<0)|(1u<<5);
      if(g_hid_shm){ npad_setup(kshmem_ptr(g_hid_shm),0,st,dev); if(g_style_ev[0]) kevent_signal(g_style_ev[0]); }
      return 0; }
    case 101: out32(c,g_style_supported); return 0;
    case 106: { u32 id=in32(c,0); int i= id==0x20? 8 : id==0x10? 9 : (int)(id&7);
      if(!g_style_ev[i]){ g_style_ev[i]=kevent_new("npad style",0); kobj_ref(&g_style_ev[i]->o); kevent_signal(g_style_ev[i]); }
      out_copy_h(c,h_new(&g_style_ev[i]->o)); return 0; }
    case 108: { u32 id=in32(c,0); u64 led= id<8? (1ull<<id) : 0; out64(c,led); return 0; }   /* GetPlayerLedPattern */
    case 120: g_hold_type=in64(c,8); return 0;
    case 121: out64(c,g_hold_type); return 0;                                      /* GetNpadJoyHoldType */
    case 127: g_handheld_mode=in64(c,8); return 0;
    case 128: out64(c,g_handheld_mode); return 0;                                  /* GetNpadHandheldActivationMode */
    case 129: out32(c,0); return 0;                                                /* GetNpadFullKeyGripColor... */
    case 133: case 134: case 135: case 136: case 137: case 138: out8(c,0); return 0;
    case 200: { u32 h=in32(c,0); u32 dev=(h>>16)&0xFF; out32(c,1); out32(c,dev==0? 1 : 2); return 0; }  /* GetVibrationDeviceInfo: LRA, left/right */
    case 201: { float v[4]; for(int i=0;i<4;i++){ u32 w=in32(c,4+4*i); memcpy(&v[i],&w,4); } vib_set(in32(c,0),v); return 0; }   /* SendVibrationValue(handle, value, aruid) */
    case 206: if(c->nx>=2){ u64 nh=c->x[0].size/4, nv=c->x[1].size/16, n= nh<nv? nh : nv;   /* SendVibrationValues(aruid, handles[], values[]) */
        for(u64 i=0;i<n && i<8;i++){ float v[4]; memcpy(v,GP(c->x[1].addr+i*16),16); vib_set(gr32(c->x[0].addr+i*4),v); } } return 0;
    case 202: { u32 h=in32(c,0); int dev=-1; for(int i=0;i<g_vib_hn;i++) if(g_vib_h[i]==h){ dev=i; break; }   /* GetActualVibrationValue: what was last sent */
      float v[4]={0,160.f,0,320.f}; if(dev>=0){ v[0]=g_vib[dev][0]; v[1]=g_vib[dev][1]>0? g_vib[dev][1] : 160.f; v[2]=g_vib[dev][2]; v[3]=g_vib[dev][3]>0? g_vib[dev][3] : 320.f; }
      outbytes(c,v,16); return 0; }
    case 203: out_obj(c,svcobj_new("IActiveVibrationDeviceList",hid_vibration_list,0)); return 0;
    case 205: case 211: out8(c,1); return 0;                                       /* IsVibrationPermitted / Mounted */
    case 208: out32(c,0); return 0;
    case 400: out8(c,0); return 0;                                                 /* IsUsbFullKeyControllerEnabled */
    case 1000: g_comm_mode=in64(c,8); return 0;
    case 1001: out64(c,g_comm_mode); return 0;
    case 1003: out8(c,0); return 0;
    case 102: case 103: case 104: case 107: case 109: case 122: case 123: case 124: case 125: case 126: case 130: case 131: case 132:
    case 204: case 207: case 209: case 210: case 212:
    case 1002: case 1004:
    case 300: case 301: case 302: case 303: case 304: case 305: case 306: case 307: case 308: case 309: case 310: case 311: case 312: case 313: case 314: case 315: case 316: case 317:
    case 60: case 61: case 63: case 64: case 65: case 66: case 67: case 91:
    case 1: case 11: case 21: case 31: case 32: case 51: case 56: return 0;
  }
  return stub_ok(o,c); }

static SvcObj* new_hid(void){ return svcobj_new("hid",hid_disp,0); }

static int hidsys_disp(SvcObj* o, IpcCtx* c){ (void)o; (void)c; return 0; }

static SvcObj* new_hidsys(void){ return svcobj_new("hid:sys",hidsys_disp,0); }

void svc_hid_init(void){
  g_hid_shm=kshmem_new(0x40000,"hid"); kobj_ref(&g_hid_shm->o);
  u8* b=kshmem_ptr(g_hid_shm);
  npad_setup(b,0,(1u<<0)|(1u<<2),(1u<<0)|(1u<<5));    /* No1: FullKey + JoyDual */
  npad_setup(b,8,1u<<1,(1u<<2)|(1u<<3));              /* Handheld */
  hid_update(); hid_update();
  svc_register("hid",new_hid); svc_register("hid:sys",new_hidsys);
}
