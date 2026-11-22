/* ---------------------------------------------------------------- shader helper process (NVIDIA, 2026-11-08)
   Included by backend_win32.c. While NVIDIA's driver compiles a shader it blocks the game's GL thread on unrelated
   calls: fences, "is it done?" checks, glCreateShader, 100-800 ms at a time (about 15 s in an hour of play, the door and
   map hitches). So new shaders are compiled in a second copy of this EXE with its own hidden GL context: the game writes
   the GLSL to shader_cache/jobs/<key>.job, the helper builds it and saves the program binary under the game's cache
   name, and the game loads that (gpu_gl.c sh_sep_ready, sps 6). Same GPU, driver and context type, so the binary
   loads; anything that goes wrong falls back to compiling in the game. SWR_NO_SHADER_HELPER=1: off */
typedef unsigned (APIENTRY *HP_CS)(unsigned); typedef void (APIENTRY *HP_SS)(unsigned,int,const char* const*,const int*);
typedef void (APIENTRY *HP_U)(unsigned); typedef unsigned (APIENTRY *HP_CP)(void); typedef void (APIENTRY *HP_AS)(unsigned,unsigned);
typedef void (APIENTRY *HP_PPI)(unsigned,unsigned,int); typedef void (APIENTRY *HP_GPIV)(unsigned,unsigned,int*);
typedef void (APIENTRY *HP_GPB)(unsigned,int,int*,unsigned*,void*); typedef void (APIENTRY *HP_GPIL)(unsigned,int,int*,char*);
typedef const unsigned char* (APIENTRY *HP_GS)(unsigned);
#define HELPER_COUNT 3
static HANDLE g_helper_proc2[HELPER_COUNT], g_helper_job; static char g_hp_tag[32];

static int hp_read(const char* path, unsigned char** out, size_t* n){
  FILE* f=fopen(path,"rb"); if(!f) return 0; fseek(f,0,SEEK_END); long sz=ftell(f); fseek(f,0,SEEK_SET);
  if(sz<=0 || sz>(16<<20)){ fclose(f); return 0; } unsigned char* b=malloc((size_t)sz+1); if(!b){ fclose(f); return 0; }
  size_t got=fread(b,1,(size_t)sz,f); fclose(f); if(got!=(size_t)sz){ free(b); return 0; } b[sz]=0; *out=b; *n=(size_t)sz; return 1; }

/* ---- pipeline warm-up (the last helper, when it has nothing to build). The game records each combination of its
   separable programs with the state NVIDIA's driver builds a program variant for (gpu_gl.c pipe_record: target and
   depth formats, sRGB, primitive, colour masks, blending). NVIDIA built that variant at the first draw on the game's GL
   thread, 300-880 ms each while the game streams an area, and the game's present thread waited for it (main-thread
   freezes up to 2.7 s, 2026-11-20). Here each recorded combination is drawn once, one pixel, with the same programs and
   state, so the driver builds the variant now and keeps it in its disk cache for the game. Records warmed are listed in
   shader_cache/pipes_warm_<tag>.bin. SWR_NO_PIPE_WARM=1: off */
typedef struct { unsigned magic, mode; unsigned long long key[3]; unsigned rt[8], z, flags, cmask, benable, blend[7]; } HpPipe;
typedef void (APIENTRY *HP_GEN)(int,unsigned*); typedef void (APIENTRY *HP_BIND)(unsigned,unsigned);
typedef void (APIENTRY *HP_TS2)(unsigned,int,unsigned,int,int); typedef void (APIENTRY *HP_FT2)(unsigned,unsigned,unsigned,unsigned,int);
typedef void (APIENTRY *HP_DB)(int,const unsigned*); typedef void (APIENTRY *HP_UPS)(unsigned,unsigned,unsigned);
typedef void (APIENTRY *HP_PB)(unsigned,unsigned,const void*,int); typedef void (APIENTRY *HP_EI)(unsigned,unsigned);
typedef void (APIENTRY *HP_BES)(unsigned,unsigned,unsigned); typedef void (APIENTRY *HP_BFS)(unsigned,unsigned,unsigned,unsigned,unsigned);
typedef void (APIENTRY *HP_CMI)(unsigned,unsigned char,unsigned char,unsigned char,unsigned char);
typedef void (APIENTRY *HP_VP)(int,int,int,int); typedef void (APIENTRY *HP_DA)(unsigned,int,int); typedef void (APIENTRY *HP_V)(void);
typedef void (APIENTRY *HP_E)(unsigned); typedef void (APIENTRY *HP_DEL)(int,const unsigned*); typedef unsigned (APIENTRY *HP_CFS)(unsigned);
static struct { HP_GEN gen_tex, gen_fb, gen_pp, gen_va, del_tex, del_fb, del_pp, del_va; HP_BIND bind_tex, bind_fb; HP_U bind_pp, bind_va;
  HP_TS2 ts2; HP_FT2 ft2; HP_DB db; HP_UPS ups; HP_PB pb; HP_EI enai, disi; HP_BES bes; HP_BFS bfs; HP_CMI cmi; HP_VP vp; HP_DA da; HP_V fin;
  HP_E ena, dis; HP_CFS cfs; HP_CP cp; HP_PPI ppi; HP_GPIV gpiv; HP_U dpr; int ok; } hw;
static unsigned hp_bf(unsigned v){ if(v&0x4000) return v&~0x4000u;
  switch(v){ case 1: return 0; case 2: return 1; case 3: return 0x0300; case 4: return 0x0301; case 5: return 0x0302; case 6: return 0x0303; case 7: return 0x0304; case 8: return 0x0305;
    case 9: return 0x0306; case 10: return 0x0307; case 11: return 0x0308; case 14: return 0x8001; case 15: return 0x8002; case 16: return 0x88F9; case 17: return 0x88FA; case 18: return 0x8589; case 19: return 0x88FB; }
  return 1; }
static unsigned hp_beq(unsigned v){ if(v>=0x8000) return v; switch(v){ case 2: return 0x800A; case 3: return 0x800B; case 4: return 0x8007; case 5: return 0x8008; } return 0x8006; }
static void hp_warm_init(HMODULE gl, HP_CP cp, HP_PPI ppi, HP_GPIV gpiv, HP_U dpr){
#define W(f,n) *(PROC*)&hw.f=wglGetProcAddress(n); if(!hw.f) *(FARPROC*)&hw.f=GetProcAddress(gl,n); if(!hw.f) return;
  W(gen_tex,"glGenTextures") W(gen_fb,"glGenFramebuffers") W(gen_pp,"glGenProgramPipelines") W(gen_va,"glGenVertexArrays")
  W(del_tex,"glDeleteTextures") W(del_fb,"glDeleteFramebuffers") W(del_pp,"glDeleteProgramPipelines") W(del_va,"glDeleteVertexArrays")
  W(bind_tex,"glBindTexture") W(bind_fb,"glBindFramebuffer") W(bind_pp,"glBindProgramPipeline") W(bind_va,"glBindVertexArray")
  W(ts2,"glTexStorage2D") W(ft2,"glFramebufferTexture2D") W(db,"glDrawBuffers") W(ups,"glUseProgramStages") W(pb,"glProgramBinary")
  W(enai,"glEnablei") W(disi,"glDisablei") W(bes,"glBlendEquationSeparatei") W(bfs,"glBlendFuncSeparatei") W(cmi,"glColorMaski")
  W(vp,"glViewport") W(da,"glDrawArrays") W(fin,"glFinish") W(ena,"glEnable") W(dis,"glDisable") W(cfs,"glCheckFramebufferStatus")
#undef W
  hw.cp=cp; hw.ppi=ppi; hw.gpiv=gpiv; hw.dpr=dpr; hw.ok=1; }
static unsigned hp_load_prog(const char* tag, unsigned long long key){   /* a separable program the helpers built, from its cache file */
  char fp[160]; snprintf(fp,sizeof fp,"shader_cache\\%s_s%016llx.bin",tag,key); unsigned char* b; size_t n;
  if(!hp_read(fp,&b,&n)) return 0; if(n<9){ free(b); return 0; }
  unsigned fmt, len; memcpy(&fmt,b,4); memcpy(&len,b+4,4); if((size_t)len+8>n){ free(b); return 0; }
  unsigned p=hw.cp(); hw.ppi(p,0x8258 /* SEPARABLE */,1); hw.pb(p,fmt,b+8,(int)len); free(b);
  int ok=0; hw.gpiv(p,0x8B82,&ok); if(!ok){ hw.dpr(p); return 0; } return p; }
static unsigned long long* g_hw_seen; static FILE* g_hw_out; static long g_hw_off; static unsigned g_hw_done, g_hw_skip;
/* one record a call; returns 1 when it drew one (0: nothing to do now) */
static int hp_warm_one(const char* tag, FILE* lg){
  if(!hw.ok) return 0;
  static DWORD rest_until; if(rest_until && (int)(GetTickCount()-rest_until)<0) return 0;   /* went through the whole file: look again a little later */
  if(!g_hw_seen){ g_hw_seen=calloc(1u<<18,8); if(!g_hw_seen){ hw.ok=0; return 0; }
    char wp[160]; snprintf(wp,sizeof wp,"shader_cache\\pipes_warm_%s.bin",tag); FILE* f=fopen(wp,"rb");
    if(f){ unsigned long long h; while(fread(&h,8,1,f)==1){ unsigned i=(unsigned)(h^(h>>29))&0x3FFFF; while(g_hw_seen[i] && g_hw_seen[i]!=h) i=(i+1)&0x3FFFF; g_hw_seen[i]=h; } fclose(f); }
    g_hw_out=fopen(wp,"ab"); }
  char pp[160]; snprintf(pp,sizeof pp,"shader_cache\\pipes_%s.bin",tag); FILE* f=fopen(pp,"rb"); if(!f) return 0;
  if(fseek(f,g_hw_off,SEEK_SET)){ fclose(f); return 0; }
  HpPipe r; int drew=0;
  while(!drew && fread(&r,sizeof r,1,f)==1){ g_hw_off+=(long)sizeof r; if(r.magic!=0x31504950u) break;
    unsigned long long h=1469598103934665603ull; const unsigned char* q=(const unsigned char*)&r; for(size_t k=0;k<sizeof r;k++){ h^=q[k]; h*=1099511628211ull; } h|=1;
    unsigned i=(unsigned)(h^(h>>29))&0x3FFFF; while(g_hw_seen[i] && g_hw_seen[i]!=h) i=(i+1)&0x3FFFF; if(g_hw_seen[i]) continue;
    if(r.mode>6 || !r.key[0] || !r.key[1]){ g_hw_seen[i]=h; continue; }   /* quads, patches, adjacency, a missing stage: not drawn here */
    unsigned pv=hp_load_prog(tag,r.key[0]), pf= pv? hp_load_prog(tag,r.key[1]) : 0, pg= (pf && r.key[2])? hp_load_prog(tag,r.key[2]) : 0;
    if(!pv || !pf || (r.key[2] && !pg)){ if(pv) hw.dpr(pv); if(pf) hw.dpr(pf); g_hw_skip++; continue; }   /* not built yet: tried again on a later pass */
    unsigned tex[9]={0}, nt=0, fb=0, pl=0, va=0, bufs[8]; int nb=0;
    hw.gen_fb(1,&fb); hw.bind_fb(0x8D40,fb);
    for(int k=0;k<8;k++){ bufs[k]=0; if(!r.rt[k]) continue; hw.gen_tex(1,&tex[nt]); hw.bind_tex(0x0DE1,tex[nt]); hw.ts2(0x0DE1,1,r.rt[k],1,1); hw.ft2(0x8D40,0x8CE0+k,0x0DE1,tex[nt],0); nt++; bufs[k]=0x8CE0+k; nb=k+1; }
    if(r.z){ int st= r.z==0x88F0 || r.z==0x8CAD; hw.gen_tex(1,&tex[nt]); hw.bind_tex(0x0DE1,tex[nt]); hw.ts2(0x0DE1,1,r.z,1,1); hw.ft2(0x8D40,st? 0x821A : 0x8D00,0x0DE1,tex[nt],0); nt++; }
    hw.db(nb,bufs);
    if(hw.cfs(0x8D40)==0x8CD5){   /* complete */
      hw.gen_pp(1,&pl); hw.ups(pl,1,pv); hw.ups(pl,2,pf); if(pg) hw.ups(pl,4,pg); hw.bind_pp(pl); hw.gen_va(1,&va); hw.bind_va(va);
      if(r.flags&1) hw.ena(0x8DB9); else hw.dis(0x8DB9);
      for(int k=0;k<nb;k++){ unsigned cm=(r.cmask>>(k*4))&15; hw.cmi(k,cm&1,(cm>>1)&1,(cm>>2)&1,(cm>>3)&1);
        if(r.benable&(1u<<k)){ hw.enai(0x0BE2,k); hw.bes(k,hp_beq(r.blend[0]),hp_beq(r.blend[3]? r.blend[3] : r.blend[0]));
          hw.bfs(k,hp_bf(r.blend[1]),hp_bf(r.blend[2]),hp_bf(r.blend[4]? r.blend[4] : r.blend[1]),hp_bf(r.blend[5]? r.blend[5] : r.blend[2])); }
        else hw.disi(0x0BE2,k); }
      if(r.z) hw.ena(0x0B71); else hw.dis(0x0B71);
      hw.vp(0,0,1,1); DWORD t0=GetTickCount(); hw.da(r.mode,0,r.mode==0? 1 : r.mode<=3? 2 : 3); hw.fin();
      g_hw_done++; if(lg && (GetTickCount()-t0>50 || !(g_hw_done%200))){ fprintf(lg,"warm %u done (%u waiting for their programs): one took %lu ms\n",g_hw_done,g_hw_skip,(unsigned long)(GetTickCount()-t0)); fflush(lg); }
      hw.bind_va(0); hw.del_va(1,&va); hw.bind_pp(0); hw.del_pp(1,&pl); }
    hw.bind_fb(0x8D40,0); hw.del_fb(1,&fb); hw.del_tex((int)nt,tex);
    hw.dpr(pv); hw.dpr(pf); if(pg) hw.dpr(pg);
    g_hw_seen[i]=h; if(g_hw_out){ fwrite(&h,8,1,g_hw_out); fflush(g_hw_out); } drew=1; }
  if(!drew){ g_hw_off=0; g_hw_skip=0; rest_until=GetTickCount()+5000; if(!rest_until) rest_until=1; }   /* at the end: start over in 5 s for new records and the ones still waiting for their programs */
  fclose(f); return drew; }

int shader_helper_main(int argc, char** argv){
  if(argc<4) return 2;
  const char* tag=argv[2]; DWORD ppid=(DWORD)strtoul(argv[3],NULL,10);
  HANDLE parent=OpenProcess(SYNCHRONIZE,FALSE,ppid); if(!parent) return 3;
  { char ed[MAX_PATH]; GetModuleFileNameA(NULL,ed,MAX_PATH); char* sl=strrchr(ed,'\\'); if(sl){ *sl=0; SetCurrentDirectoryA(ed); } }
  SetPriorityClass(GetCurrentProcess(),(argc>=6 && atoi(argv[4])==0)? NORMAL_PRIORITY_CLASS : BELOW_NORMAL_PRIORITY_CLASS);   /* helper 0 builds what a draw is waiting for */
  FILE* lg; { char lp[64]; snprintf(lp,sizeof lp,"shader_cache\\jobs\\helper%s.log",argc>=6? argv[4] : ""); lg=fopen(lp,"w"); }
  WNDCLASSA wc; memset(&wc,0,sizeof wc); wc.lpfnWndProc=DefWindowProcA; wc.hInstance=GetModuleHandleA(NULL); wc.lpszClassName="swr_shader_helper"; RegisterClassA(&wc);
  HWND w=CreateWindowA(wc.lpszClassName,"",WS_OVERLAPPEDWINDOW,0,0,16,16,NULL,NULL,wc.hInstance,NULL); HDC dc=GetDC(w);
  PIXELFORMATDESCRIPTOR pfd; memset(&pfd,0,sizeof pfd); pfd.nSize=sizeof pfd; pfd.nVersion=1; pfd.dwFlags=PFD_DRAW_TO_WINDOW|PFD_SUPPORT_OPENGL|PFD_DOUBLEBUFFER;
  pfd.iPixelType=PFD_TYPE_RGBA; pfd.cColorBits=32; pfd.cDepthBits=24; pfd.cStencilBits=8; pfd.iLayerType=PFD_MAIN_PLANE;
  SetPixelFormat(dc,ChoosePixelFormat(dc,&pfd),&pfd);
  HGLRC tmp=wglCreateContext(dc); if(!tmp || !wglMakeCurrent(dc,tmp)){ if(lg){ fprintf(lg,"no GL context\n"); fclose(lg); } return 4; }
  CCA cca=(CCA)wglGetProcAddress("wglCreateContextAttribsARB"); HGLRC ctx=NULL;
  static const int vers[4][2]={{4,6},{4,5},{4,4},{4,3}};
  for(int i=0;i<4 && !ctx && cca;i++){ int ca[]={0x2091,vers[i][0],0x2092,vers[i][1],0x9126,0x00000001,0}; ctx=cca(dc,NULL,ca); }
  if(!ctx || !wglMakeCurrent(dc,ctx)){ if(lg){ fprintf(lg,"no core context\n"); fclose(lg); } return 4; }
  wglDeleteContext(tmp);
  HMODULE gl=GetModuleHandleA("opengl32.dll");
  HP_GS gs=(HP_GS)GetProcAddress(gl,"glGetString");
#define HPF(t,v,n) t v=(t)wglGetProcAddress(n);
  HPF(HP_CS,cs,"glCreateShader") HPF(HP_SS,ss,"glShaderSource") HPF(HP_U,comp,"glCompileShader") HPF(HP_CP,cp,"glCreateProgram")
  HPF(HP_AS,as,"glAttachShader") HPF(HP_PPI,ppi,"glProgramParameteri") HPF(HP_U,link,"glLinkProgram") HPF(HP_GPIV,gpiv,"glGetProgramiv")
  HPF(HP_GPB,gpb,"glGetProgramBinary") HPF(HP_GPIL,gpil,"glGetProgramInfoLog") HPF(HP_U,dsh,"glDeleteShader") HPF(HP_U,dpr,"glDeleteProgram")
#undef HPF
  if(!gs||!cs||!ss||!comp||!cp||!as||!ppi||!link||!gpiv||!gpb||!gpil||!dsh||!dpr){ if(lg){ fprintf(lg,"missing GL functions\n"); fclose(lg); } return 5; }
  { /* same fingerprint as the game's cache names (gpu_gl.c pcache_init): otherwise its binaries would be for another driver */
    const char* rn=(const char*)gs(0x1F01), *vv=(const char*)gs(0x1F02); unsigned long long h=1469598103934665603ull;
    for(const char* q=rn;q&&*q;q++){ h^=(unsigned char)*q; h*=1099511628211ull; } for(const char* q=vv;q&&*q;q++){ h^=(unsigned char)*q; h*=1099511628211ull; }
    char mine[16]; snprintf(mine,sizeof mine,"%08x",(unsigned)(h^(h>>32)));
    if(strcmp(mine,tag)){ if(lg){ fprintf(lg,"driver fingerprint %s, the game has %s: not building\n",mine,tag); fclose(lg); } return 6; }
    if(lg){ fprintf(lg,"ready: %s / %s\n",rn? rn : "?",vv? vv : "?"); fflush(lg); } }
  /* not glMaxShaderCompilerThreads: tested here too (driver 617.42), no compile ever completes after it. More throughput
     comes from more helpers (argv[4] is this one's index) */
  int hp_i= argc>=6? atoi(argv[4]) : 0;
  /* an extra helper (index HELPER_COUNT and up) is started only for the game's "Preparing shaders" hold (main.c), one
     per spare CPU thread, and leaves when the hold ends (prepare.on gone) and it has finished its own program */
  int extra= hp_i>=HELPER_COUNT; DWORD extra_check=0;
  if(hp_i==HELPER_COUNT-1 && !getenv("SWR_NO_PIPE_WARM")) hp_warm_init(gl,cp,ppi,gpiv,dpr);
  typedef struct { unsigned p; char name[64]; unsigned long long key; DWORD t0; } Pend;
  Pend pend[16]; int np=0; unsigned built=0, failed=0;
  while(WaitForSingleObject(parent,0)==WAIT_TIMEOUT){
    if(extra && !np && GetTickCount()-extra_check>500){ extra_check=GetTickCount();
      if(GetFileAttributesA("shader_cache\\jobs\\prepare.on")==INVALID_FILE_ATTRIBUTES) break; }
    for(int i=0;i<np;){ int done=0; gpiv(pend[i].p,0x91B1 /* GL_COMPLETION_STATUS_KHR */,&done);
      if(!done){ i++; continue; }
      int ok=0; gpiv(pend[i].p,0x8B82 /* GL_LINK_STATUS */,&ok);
      char jp[128]; snprintf(jp,sizeof jp,"shader_cache\\jobs\\%s",pend[i].name);
      if(ok){ int len=0; gpiv(pend[i].p,0x8741 /* GL_PROGRAM_BINARY_LENGTH */,&len);
        unsigned char* b= len>0? malloc((size_t)len+8) : NULL; int got=0; unsigned fmt=0;
        if(b) gpb(pend[i].p,len,&got,&fmt,b+8);
        if(b && got>0){ unsigned n=(unsigned)got; memcpy(b,&fmt,4); memcpy(b+4,&n,4);
          char tp[160], fp[160]; snprintf(tp,sizeof tp,"shader_cache\\%s_s%016llx.tmp",tag,pend[i].key); snprintf(fp,sizeof fp,"shader_cache\\%s_s%016llx.bin",tag,pend[i].key);
          FILE* o=fopen(tp,"wb"); if(o){ fwrite(b,1,(size_t)got+8,o); fclose(o); MoveFileExA(tp,fp,MOVEFILE_REPLACE_EXISTING); }
          built++; if(lg){ fprintf(lg,"built %016llx in %lu ms\n",pend[i].key,(unsigned long)(GetTickCount()-pend[i].t0)); fflush(lg); } }
        free(b); }
      else { char e[2048]; e[0]=0; gpil(pend[i].p,sizeof e,NULL,e); failed++;
        char ep[128]; snprintf(ep,sizeof ep,"shader_cache\\jobs\\%016llx.err",pend[i].key); FILE* o=fopen(ep,"w"); if(o){ fputs(e,o); fclose(o); }
        if(lg){ fprintf(lg,"failed %016llx\n",pend[i].key); fflush(lg); } }
      dpr(pend[i].p); DeleteFileA(jp); pend[i]=pend[--np]; }
    /* .job: a draw is waiting for it, always first. .pjob: found ahead of its first draw; not on helper 0, which stays free
       for draws. One at a time: the driver builds a process's shaders strictly in order, so more in flight only makes the
       next urgent one wait (measured 2026-11-08: finished 2.3 s apart in submission order, one slow shader held up the rest).
       A job is claimed by renaming it, so the helpers share one queue and a shader that takes minutes ties up only one */
    /* helper 0 joins the ahead-of-time jobs while the game holds for its shaders (prepare.on): no draw waits then */
    static int prep_on; static DWORD prep_check; if(hp_i==0 && GetTickCount()-prep_check>1000){ prep_check=GetTickCount(); prep_on= GetFileAttributesA("shader_cache\\jobs\\prepare.on")!=INVALID_FILE_ATTRIBUTES; }
    for(int pass=0;pass<((hp_i==0 && !prep_on)? 1 : 2) && np<1;pass++){
      WIN32_FIND_DATAA fd; HANDLE fh=FindFirstFileA(pass? "shader_cache\\jobs\\*.pjob" : "shader_cache\\jobs\\*.job",&fd);
      if(fh!=INVALID_HANDLE_VALUE){ do {
          unsigned long long jk=strtoull(fd.cFileName,NULL,16);
          char jp[128]; snprintf(jp,sizeof jp,"shader_cache\\jobs\\%s",fd.cFileName); unsigned char* b; size_t n;
          { char fp[160]; snprintf(fp,sizeof fp,"shader_cache\\%s_s%016llx.bin",tag,jk); if(GetFileAttributesA(fp)!=INVALID_FILE_ATTRIBUTES){ DeleteFileA(jp); continue; } }   /* already built */
          if(!hp_read(jp,&b,&n)) continue;
          /* "SWRJ", GL stage enum, GLSL length, GLSL, "SWRJOBOK": a job still being written has no trailer yet */
          unsigned st=0, sl=0; if(n<20 || memcmp(b,"SWRJ",4)){ free(b); DeleteFileA(jp); continue; }
          memcpy(&st,b+4,4); memcpy(&sl,b+8,4); if((size_t)sl+20!=n || memcmp(b+12+sl,"SWRJOBOK",8)){ free(b); continue; }
          char cn[64]; snprintf(cn,sizeof cn,"%016llx.c%d",jk,hp_i); char cp_[128]; snprintf(cp_,sizeof cp_,"shader_cache\\jobs\\%s",cn);
          if(!MoveFileA(jp,cp_)){ free(b); continue; }   /* another helper took it */
          b[12+sl]=0; const char* src=(const char*)b+12;
          unsigned sh=cs(st); ss(sh,1,&src,NULL); comp(sh); unsigned p=cp(); ppi(p,0x8258 /* SEPARABLE */,1); ppi(p,0x8257 /* BINARY_RETRIEVABLE */,1);
          as(p,sh); link(p); dsh(sh); free(b);
          Pend* q=&pend[np++]; q->p=p; snprintf(q->name,sizeof q->name,"%s",cn); q->key=jk; q->t0=GetTickCount(); break;
        } while(FindNextFileA(fh,&fd)); FindClose(fh); } }
    if(!np && hw.ok && hp_warm_one(tag,lg)) continue;   /* nothing to build: warm a recorded draw state */
    Sleep(np? 2 : 10); }
  if(lg){ fprintf(lg,"game closed: %u built, %u failed\n",built,failed); fclose(lg); }
  return 0; }

/* the game's side: start the helper (killed with the game through a job object) */
int backend_shader_helper_start(const char* tag){
  CreateDirectoryA("shader_cache",NULL); CreateDirectoryA("shader_cache\\jobs",NULL);
  { WIN32_FIND_DATAA fd; HANDLE fh=FindFirstFileA("shader_cache\\jobs\\*.*",&fd);   /* jobs and errors from an earlier run */
    if(fh!=INVALID_HANDLE_VALUE){ do { if(fd.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY) continue; char p[200]; snprintf(p,sizeof p,"shader_cache\\jobs\\%s",fd.cFileName); DeleteFileA(p); } while(FindNextFileA(fh,&fd)); FindClose(fh); } }
  char exe[MAX_PATH]; GetModuleFileNameA(NULL,exe,MAX_PATH); snprintf(g_hp_tag,sizeof g_hp_tag,"%s",tag);
  g_helper_job=CreateJobObjectA(NULL,NULL);
  if(g_helper_job){ JOBOBJECT_EXTENDED_LIMIT_INFORMATION li; memset(&li,0,sizeof li); li.BasicLimitInformation.LimitFlags=JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    SetInformationJobObject(g_helper_job,JobObjectExtendedLimitInformation,&li,sizeof li); }
  for(int i=0;i<HELPER_COUNT;i++){ char cmd[MAX_PATH+128]; snprintf(cmd,sizeof cmd,"\"%s\" --shader-helper %s %lu %d %d",exe,tag,(unsigned long)GetCurrentProcessId(),i,HELPER_COUNT);
    STARTUPINFOA si; memset(&si,0,sizeof si); si.cb=sizeof si; PROCESS_INFORMATION pi; memset(&pi,0,sizeof pi);
    if(!CreateProcessA(NULL,cmd,NULL,NULL,FALSE,CREATE_NO_WINDOW|CREATE_SUSPENDED,NULL,NULL,&si,&pi)){
      LOG("shaders: the shader helper could not start (error %lu)\n",(unsigned long)GetLastError()); for(int k=0;k<i;k++){ TerminateProcess(g_helper_proc2[k],0); CloseHandle(g_helper_proc2[k]); g_helper_proc2[k]=NULL; } return 0; }
    if(g_helper_job) AssignProcessToJobObject(g_helper_job,pi.hProcess);
    ResumeThread(pi.hThread); CloseHandle(pi.hThread); g_helper_proc2[i]=pi.hProcess; }
  LOG("shaders: new shaders are built by %d helper processes: the game's GL thread doesn't wait on the driver's compiler\n",HELPER_COUNT);
  return 1; }
/* the "Preparing shaders" hold (main.c): extra helpers on the spare CPU threads, so a first start builds its tens of
   thousands of shaders in minutes rather than the better part of an hour (3 helpers: about 10 a second on a 12-thread
   CPU, 2026-11-22). They leave by themselves when backend_shader_helper_extra_stop removes prepare.on */
static HANDLE g_helper_extra[32]; static int g_helper_extra_n;
int backend_shader_helper_extra(void){
  if(!g_helper_job) return -1;   /* the helpers aren't started yet: ask again */
  if(g_helper_extra_n || getenv("SWR_NO_EXTRA_HELPERS")) return 0;
  /* the game itself does little while it holds: all threads but one (measured 2026-11-22: 12 threads at 83% with 9 building) */
  int threads=(int)GetActiveProcessorCount(ALL_PROCESSOR_GROUPS), n=threads-1-HELPER_COUNT; if(n>32) n=32; if(n<=0) return 0;
  { FILE* f=fopen("shader_cache\\jobs\\prepare.on","wb"); if(!f) return 0; fclose(f); }
  char exe[MAX_PATH]; GetModuleFileNameA(NULL,exe,MAX_PATH);
  for(int i=0;i<n;i++){ char cmd[MAX_PATH+128]; snprintf(cmd,sizeof cmd,"\"%s\" --shader-helper %s %lu %d %d",exe,g_hp_tag,(unsigned long)GetCurrentProcessId(),HELPER_COUNT+i,HELPER_COUNT+n);
    STARTUPINFOA si; memset(&si,0,sizeof si); si.cb=sizeof si; PROCESS_INFORMATION pi; memset(&pi,0,sizeof pi);
    if(!CreateProcessA(NULL,cmd,NULL,NULL,FALSE,CREATE_NO_WINDOW|CREATE_SUSPENDED,NULL,NULL,&si,&pi)) break;
    AssignProcessToJobObject(g_helper_job,pi.hProcess); ResumeThread(pi.hThread); CloseHandle(pi.hThread); g_helper_extra[g_helper_extra_n++]=pi.hProcess; }
  LOG("shaders: %d more helper processes while the game prepares its shaders (%d CPU threads)\n",g_helper_extra_n,threads);
  return g_helper_extra_n; }
void backend_shader_helper_extra_stop(void){
  if(!g_helper_extra_n) return;
  DeleteFileA("shader_cache\\jobs\\prepare.on");
  for(int i=0;i<g_helper_extra_n;i++){ CloseHandle(g_helper_extra[i]); g_helper_extra[i]=NULL; }
  LOG("shaders: the %d extra helper processes leave after their current shader\n",g_helper_extra_n); g_helper_extra_n=0; }
int backend_shader_helper_alive(void){ for(int i=0;i<HELPER_COUNT;i++) if(g_helper_proc2[i] && WaitForSingleObject(g_helper_proc2[i],0)==WAIT_TIMEOUT) return 1; return 0; }
int backend_file_exists(const char* path){ DWORD a=GetFileAttributesA(path); return a!=INVALID_FILE_ATTRIBUTES && !(a&FILE_ATTRIBUTE_DIRECTORY); }
