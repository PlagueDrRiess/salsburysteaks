/* SwitchRecomp - Copyright 2026 MZ Labs
   Licensed under the Apache License 2.0. See LICENSE.txt and NOTICE.txt;
   any copy or derived work must keep the credit to SwitchRecomp by MZ Labs. */
/* DLAA for the OpenGL renderer. OpenGL has no NGX: a small Vulkan device on the same GPU runs it. Each frame, on the
   GL thread (a custom entry in the GL queue, so it runs in order with the frame's GL commands):
     GL copies the scene colour and depth into images Vulkan allocated and GL imported (same formats: plain copies),
     GL signals a shared semaphore, Vulkan runs NGX DLSS (DLAA: render size = output size) waiting on it and signals
     another, GL waits on that and blits the result back into the scene target.
   No NVIDIA SDK code or header is used: NGX is the driver's NGX core (_nvngx.dll, found through the registry), its
   few types are declared here, and the DLSS model (nvngx_dlss.dll) is supplied by the player next to the game. The
   NGX calls follow the ones in the native Vulkan renderer's dlss.c, which was written for this project too.
   Motion vectors and jitter: see gpu_gl.c (dl_* helpers); this file takes them as inputs. */
#if defined(_WIN32)
#include <windows.h>
#include "rt.h"
#define VK_NO_PROTOTYPES
#define VK_USE_PLATFORM_WIN32_KHR
#include <vulkan/vulkan.h>
#include <GL/glcorearb.h>
#include <wchar.h>

extern void* backend_gl_proc(const char*);
extern char g_exedir[1024];

/* ---------------------------------------------------------------- NGX (as in snxvk dlss.c) */
typedef int NgxResult;
#define NGX_OK(r) (((r) & 0xFFF00000) != 0xBAD00000)
typedef struct NgxParam NgxParam; typedef struct NgxHandle NgxHandle;
typedef struct NgxParamVtbl { void* slot[17]; } NgxParamVtbl;
struct NgxParam { const NgxParamVtbl* vt; };
typedef struct { VkImageView ImageView; VkImage Image; VkImageSubresourceRange SubresourceRange; VkFormat Format; unsigned int Width, Height; } NgxImageView;
typedef struct { union { NgxImageView ImageViewInfo; struct { VkBuffer Buffer; unsigned int SizeInBytes; } BufferInfo; } Resource; int Type; _Bool ReadWrite; } NgxResourceVK;
typedef NgxResult (*PFN_Init_Ext2)(unsigned long long, const wchar_t*, VkInstance, VkPhysicalDevice, VkDevice, PFN_vkGetInstanceProcAddr, PFN_vkGetDeviceProcAddr, int, const NgxParam*);
typedef NgxResult (*PFN_ReqExt)(unsigned*, const char***, unsigned*, const char***);
typedef NgxResult (*PFN_GetParams)(NgxParam**);
typedef NgxResult (*PFN_Create)(VkCommandBuffer, int, NgxParam*, NgxHandle**);
typedef NgxResult (*PFN_Evaluate)(VkCommandBuffer, const NgxHandle*, const NgxParam*, void*);
typedef NgxResult (*PFN_Release)(NgxHandle*);
enum { S_P = 0, S_D12, S_D11, S_I, S_U32, S_D, S_F, S_U64, G_P, G_D12, G_D11, G_I, G_U32, G_D, G_F, G_U64 };
static void p_seti(NgxParam* p, const char* n, int v){ ((void(*)(NgxParam*,const char*,int))p->vt->slot[S_I])(p,n,v); }
static void p_setu(NgxParam* p, const char* n, unsigned v){ ((void(*)(NgxParam*,const char*,unsigned))p->vt->slot[S_U32])(p,n,v); }
static void p_setf(NgxParam* p, const char* n, float v){ ((void(*)(NgxParam*,const char*,float))p->vt->slot[S_F])(p,n,v); }
static void p_setp(NgxParam* p, const char* n, void* v){ ((void(*)(NgxParam*,const char*,void*))p->vt->slot[S_P])(p,n,v); }
static int p_geti(const NgxParam* p, const char* n, int* v){ return NGX_OK(((NgxResult(*)(const NgxParam*,const char*,int*))p->vt->slot[G_I])(p,n,v)); }
static int p_getu(const NgxParam* p, const char* n, unsigned* v){ return NGX_OK(((NgxResult(*)(const NgxParam*,const char*,unsigned*))p->vt->slot[G_U32])(p,n,v)); }
static int p_getf(const NgxParam* p, const char* n, float* v){ return NGX_OK(((NgxResult(*)(const NgxParam*,const char*,float*))p->vt->slot[G_F])(p,n,v)); }
static int p_getp(const NgxParam* p, const char* n, void** v){ return NGX_OK(((NgxResult(*)(const NgxParam*,const char*,void**))p->vt->slot[G_P])(p,n,v)); }
static struct { HMODULE core; PFN_Init_Ext2 init; PFN_ReqExt req; PFN_GetParams caps, alloc; PFN_Create create; PFN_Evaluate eval; PFN_Release release; } ngx;

/* ---------------------------------------------------------------- Vulkan, loaded at run time */
#define VKI_FN(X) X(vkEnumeratePhysicalDevices) X(vkGetPhysicalDeviceProperties2) X(vkGetPhysicalDeviceQueueFamilyProperties) \
  X(vkGetPhysicalDeviceMemoryProperties) X(vkEnumerateDeviceExtensionProperties) X(vkGetPhysicalDeviceFeatures2) X(vkCreateDevice) \
  X(vkGetDeviceProcAddr)
#define VKD_FN(X) X(vkGetDeviceQueue) X(vkCreateCommandPool) X(vkAllocateCommandBuffers) X(vkCreateFence) X(vkWaitForFences) \
  X(vkResetFences) X(vkResetCommandBuffer) X(vkBeginCommandBuffer) X(vkEndCommandBuffer) X(vkQueueSubmit) X(vkCreateImage) \
  X(vkGetImageMemoryRequirements) X(vkAllocateMemory) X(vkBindImageMemory) X(vkGetMemoryWin32HandleKHR) X(vkCreateImageView) \
  X(vkCreateSemaphore) X(vkGetSemaphoreWin32HandleKHR) X(vkCmdPipelineBarrier) X(vkCmdClearColorImage) X(vkDestroyImage) \
  X(vkDestroyImageView) X(vkFreeMemory) X(vkDeviceWaitIdle) X(vkCreateQueryPool) X(vkCmdResetQueryPool) X(vkCmdWriteTimestamp) \
  X(vkGetQueryPoolResults)
#define VKDECL(n) static PFN_##n n;
static PFN_vkGetInstanceProcAddr vkGetInstanceProcAddr_; static PFN_vkCreateInstance vkCreateInstance_;
VKI_FN(VKDECL) VKD_FN(VKDECL)

/* GL interop (EXT_memory_object_win32, EXT_semaphore_win32) and the few GL calls this file makes, real driver
   functions: this file only runs on the GL thread */
typedef void (APIENTRY* PFN_CMO)(GLsizei, GLuint*); typedef void (APIENTRY* PFN_MOPI)(GLuint, GLenum, const GLint*);
typedef void (APIENTRY* PFN_IMW)(GLuint, GLuint64, GLenum, void*); typedef void (APIENTRY* PFN_TSM2)(GLenum, GLsizei, GLenum, GLsizei, GLsizei, GLuint, GLuint64);
typedef void (APIENTRY* PFN_GS)(GLsizei, GLuint*); typedef void (APIENTRY* PFN_ISW)(GLuint, GLenum, void*);
typedef void (APIENTRY* PFN_SIG)(GLuint, GLuint, const GLuint*, GLuint, const GLuint*, const GLenum*);
typedef void (APIENTRY* PFN_GUB)(GLenum, GLuint, GLubyte*);
static struct { PFN_CMO cmo; PFN_MOPI mopi; PFN_IMW imw; PFN_TSM2 tsm2; PFN_GS gs; PFN_ISW isw; PFN_SIG sig, wait; PFN_GUB gub;
  PFNGLGENTEXTURESPROC gentex; PFNGLBINDTEXTUREPROC bindtex; PFNGLGETINTEGERVPROC geti; PFNGLGETERRORPROC err;
  PFNGLCOPYIMAGESUBDATAPROC copy; PFNGLBLITFRAMEBUFFERPROC blit; PFNGLGENFRAMEBUFFERSPROC genfb; PFNGLBINDFRAMEBUFFERPROC bindfb;
  PFNGLFRAMEBUFFERTEXTURE2DPROC fbtex; PFNGLISENABLEDPROC isen; PFNGLENABLEPROC en; PFNGLDISABLEPROC dis;
  PFNGLDELETETEXTURESPROC deltex; } gl;
#define GL_DEDICATED_MEMORY_OBJECT_EXT_ 0x9581
#define GL_HANDLE_TYPE_OPAQUE_WIN32_EXT_ 0x9587
#define GL_HANDLE_TYPE_OPAQUE_WIN32_KMT_EXT_ 0x9588
#define GL_LAYOUT_GENERAL_EXT_ 0x958D
#define GL_DEVICE_UUID_EXT_ 0x9597

/* the shared images: 0 colour, 1 depth, 2 motion vectors, 3 output */
enum { IM_COLOR, IM_DEPTH, IM_MV, IM_OUT, IM_N };
static struct {
  int tried, ok, failed;
  /* DL_RING command buffers in turn: the CPU waits only on the one from DL_RING frames back. With 2 it waited on the
     frame before last every frame, so the CPU stood still until the GPU caught up: 10-50 ms a frame, while DLSS itself
     took 0.5-0.7 ms on the GPU (2026-11-14). The shared images need no CPU wait: GL and Vulkan order them on the GPU
     through s_ready and s_done */
  #define DL_RING 4
  VkInstance inst; VkPhysicalDevice pd; VkDevice dev; VkQueue q; u32 qf; VkCommandPool pool; VkCommandBuffer cb[DL_RING]; VkFence fence[DL_RING]; u32 cur;
  VkQueryPool qp; float tsp; int qp_used[DL_RING]; u64 gpu_sum, gpu_max; u32 gpu_n;   /* DLSS's own GPU time (timestamps around the evaluation) */
  int kmt, dedicated;
  VkSemaphore s_ready, s_done; GLuint g_ready, g_done;
  u32 w, h; GLenum zfmt; int fresh;
  VkImage img[IM_N]; VkDeviceMemory mem[IM_N]; VkImageView view[IM_N]; VkFormat vf[IM_N]; GLuint gmem[IM_N], gtex[IM_N];
  GLuint fbo_out, fbo_dst;
  NgxParam* params; NgxHandle* feat; u32 fw, fh; int evals;
} D;

static int fail(const char* what){ LOG("dlaa (OpenGL): %s - DLAA off\n",what); D.failed=1; return 0; }

static int ngx_load(void){
  char dir[MAX_PATH]={0}, path[MAX_PATH+32]; DWORD n=sizeof dir;
  if(RegGetValueA(HKEY_LOCAL_MACHINE,"SOFTWARE\\NVIDIA Corporation\\Global\\NGXCore","FullPath",RRF_RT_REG_SZ,NULL,dir,&n)!=ERROR_SUCCESS)
    return fail("no NVIDIA NGX in the graphics driver (an NVIDIA RTX card and a recent driver are needed)");
  snprintf(path,sizeof path,"%s\\_nvngx.dll",dir);
  if(!(ngx.core=LoadLibraryA(path))) return fail("the driver's NGX core could not be loaded");
  #define NG(f,s) *(void**)&ngx.f=(void*)GetProcAddress(ngx.core,s)
  NG(init,"NVSDK_NGX_VULKAN_Init_Ext2"); NG(req,"NVSDK_NGX_VULKAN_RequiredExtensions"); NG(caps,"NVSDK_NGX_VULKAN_GetCapabilityParameters");
  NG(alloc,"NVSDK_NGX_VULKAN_AllocateParameters"); NG(create,"NVSDK_NGX_VULKAN_CreateFeature"); NG(eval,"NVSDK_NGX_VULKAN_EvaluateFeature");
  NG(release,"NVSDK_NGX_VULKAN_ReleaseFeature");
  #undef NG
  if(!ngx.init || !ngx.req || !ngx.caps || !ngx.alloc || !ngx.create || !ngx.eval || !ngx.release) return fail("the driver's NGX core lacks Vulkan entry points");
  return 1; }

static int gl_load(void){
  #define G(f,s) *(void**)&gl.f=backend_gl_proc(s)
  G(cmo,"glCreateMemoryObjectsEXT"); G(mopi,"glMemoryObjectParameterivEXT"); G(imw,"glImportMemoryWin32HandleEXT"); G(tsm2,"glTexStorageMem2DEXT");
  G(gs,"glGenSemaphoresEXT"); G(isw,"glImportSemaphoreWin32HandleEXT"); G(sig,"glSignalSemaphoreEXT"); G(wait,"glWaitSemaphoreEXT");
  G(gub,"glGetUnsignedBytei_vEXT"); G(gentex,"glGenTextures"); G(bindtex,"glBindTexture"); G(geti,"glGetIntegerv"); G(err,"glGetError");
  G(copy,"glCopyImageSubData"); G(blit,"glBlitFramebuffer"); G(genfb,"glGenFramebuffers"); G(bindfb,"glBindFramebuffer");
  G(fbtex,"glFramebufferTexture2D"); G(isen,"glIsEnabled"); G(en,"glEnable"); G(dis,"glDisable"); G(deltex,"glDeleteTextures");
  #undef G
  return gl.cmo && gl.mopi && gl.imw && gl.tsm2 && gl.gs && gl.isw && gl.sig && gl.wait && gl.gub && gl.copy && gl.blit; }

static int has_ext(const VkExtensionProperties* have, u32 n, const char* name){ for(u32 i=0;i<n;i++) if(!strcmp(have[i].extensionName,name)) return 1; return 0; }

static GLubyte g_dl_uuid[16];
static int vk_up(void){
  HMODULE lib=LoadLibraryA("vulkan-1.dll"); if(!lib) return fail("vulkan-1.dll not found");
  vkGetInstanceProcAddr_=(PFN_vkGetInstanceProcAddr)(void*)GetProcAddress(lib,"vkGetInstanceProcAddr"); if(!vkGetInstanceProcAddr_) return fail("vulkan-1.dll is incomplete");
  vkCreateInstance_=(PFN_vkCreateInstance)vkGetInstanceProcAddr_(NULL,"vkCreateInstance");
  unsigned ni=0, nd=0; const char **ie=NULL, **de=NULL;
  if(!NGX_OK(ngx.req(&ni,&ie,&nd,&de))) return fail("NGX gave no Vulkan extension list");
  const char* iext[32]; u32 nie=0; for(unsigned i=0;i<ni && nie<32;i++) iext[nie++]=ie[i];
  VkApplicationInfo app={VK_STRUCTURE_TYPE_APPLICATION_INFO,NULL,"SwitchRecomp DLAA",1,"SwitchRecomp",1,VK_API_VERSION_1_2};
  VkInstanceCreateInfo ici={VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,NULL,0,&app,0,NULL,nie,iext};
  if(!vkCreateInstance_ || vkCreateInstance_(&ici,NULL,&D.inst)!=VK_SUCCESS) return fail("no Vulkan 1.2 driver");
  #define VKI(n) n=(PFN_##n)vkGetInstanceProcAddr_(D.inst,#n); if(!n) return fail("the driver lacks " #n);
  VKI_FN(VKI)
  #undef VKI
  /* the GPU the GL context runs on */
  GLubyte uuid[16]; memcpy(uuid,g_dl_uuid,16);   /* read on the GL thread before this thread started (gl.* needs the GL context) */
  VkPhysicalDevice pds[16]; u32 npd=16; vkEnumeratePhysicalDevices(D.inst,&npd,pds);
  for(u32 i=0;i<npd && !D.pd;i++){
    VkPhysicalDeviceIDProperties idp={VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES}; VkPhysicalDeviceProperties2 p2={VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2,&idp};
    vkGetPhysicalDeviceProperties2(pds[i],&p2); if(memcmp(idp.deviceUUID,uuid,16)) continue;
    VkQueueFamilyProperties qp[16]; u32 nq=16; vkGetPhysicalDeviceQueueFamilyProperties(pds[i],&nq,qp);
    for(u32 f=0;f<nq;f++) if((qp[f].queueFlags&(VK_QUEUE_GRAPHICS_BIT|VK_QUEUE_COMPUTE_BIT))==(VK_QUEUE_GRAPHICS_BIT|VK_QUEUE_COMPUTE_BIT)){ D.pd=pds[i]; D.qf=f; break; } }
  if(!D.pd) return fail("no Vulkan device matches the OpenGL GPU");
  u32 nh=0; vkEnumerateDeviceExtensionProperties(D.pd,NULL,&nh,NULL); VkExtensionProperties* have=calloc(nh? nh : 1,sizeof *have); if(!have) return fail("out of memory");
  vkEnumerateDeviceExtensionProperties(D.pd,NULL,&nh,have);
  const char* dext[48]; u32 nde=0;
  dext[nde++]=VK_KHR_EXTERNAL_MEMORY_WIN32_EXTENSION_NAME; dext[nde++]=VK_KHR_EXTERNAL_SEMAPHORE_WIN32_EXTENSION_NAME;
  for(unsigned i=0;i<nd && nde<48;i++){ int dup=0; for(u32 k=0;k<nde;k++) dup|=!strcmp(dext[k],de[i]); if(!dup && has_ext(have,nh,de[i])) dext[nde++]=de[i]; }
  free(have);
  VkPhysicalDeviceVulkan12Features f12={VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};
  VkPhysicalDeviceFeatures2 f2={VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,&f12}; vkGetPhysicalDeviceFeatures2(D.pd,&f2);
  VkPhysicalDeviceVulkan12Features on12={VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES}; on12.bufferDeviceAddress=f12.bufferDeviceAddress; on12.timelineSemaphore=f12.timelineSemaphore;
  float prio=1.0f; VkDeviceQueueCreateInfo qci={VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,NULL,0,D.qf,1,&prio};
  VkDeviceCreateInfo dci={VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,&on12,0,1,&qci,0,NULL,nde,dext,NULL};
  if(vkCreateDevice(D.pd,&dci,NULL,&D.dev)!=VK_SUCCESS) return fail("could not create the Vulkan device");
  #define VKD(n) n=(PFN_##n)vkGetDeviceProcAddr(D.dev,#n); if(!n) return fail("the driver lacks " #n);
  VKD_FN(VKD)
  #undef VKD
  vkGetDeviceQueue(D.dev,D.qf,0,&D.q);
  VkCommandPoolCreateInfo cpi={VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,NULL,VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,D.qf};
  if(vkCreateCommandPool(D.dev,&cpi,NULL,&D.pool)!=VK_SUCCESS) return fail("command pool");
  VkCommandBufferAllocateInfo cai={VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,NULL,D.pool,VK_COMMAND_BUFFER_LEVEL_PRIMARY,DL_RING};
  if(vkAllocateCommandBuffers(D.dev,&cai,D.cb)!=VK_SUCCESS) return fail("command buffers");
  VkFenceCreateInfo fci={VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,NULL,VK_FENCE_CREATE_SIGNALED_BIT};
  for(int i=0;i<DL_RING;i++) if(vkCreateFence(D.dev,&fci,NULL,&D.fence[i])!=VK_SUCCESS) return fail("fences");
  { VkQueryPoolCreateInfo qpi={VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO,NULL,0,VK_QUERY_TYPE_TIMESTAMP,2*DL_RING,0};
    VkPhysicalDeviceProperties2 pp={VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2,NULL}; vkGetPhysicalDeviceProperties2(D.pd,&pp);
    D.tsp=pp.properties.limits.timestampPeriod; if(vkCreateQueryPool(D.dev,&qpi,NULL,&D.qp)!=VK_SUCCESS) D.qp=VK_NULL_HANDLE; }   /* optional: timing only */
  /* NGX on this device */
  wchar_t wdir[1024]; MultiByteToWideChar(CP_UTF8,0,g_exedir,-1,wdir,1024);
  NgxResult r=ngx.init(231313132ull,wdir,D.inst,D.pd,D.dev,vkGetInstanceProcAddr_,vkGetDeviceProcAddr,0x15,NULL);
  if(!NGX_OK(r)){ LOG("dlaa (OpenGL): NGX init failed (%08x)\n",(unsigned)r); return fail("NGX could not start"); }
  { NgxParam* t=NULL; int i=0; unsigned u=0; float fv=0; void* v=NULL;   /* the parameter object's vtable layout */
    if(!NGX_OK(ngx.alloc(&t)) || !t) return fail("NGX parameters");
    p_seti(t,"SWR.Test.I",-12345); p_setu(t,"SWR.Test.U",4000000000u); p_setf(t,"SWR.Test.F",1.25f); p_setp(t,"SWR.Test.P",(void*)t);
    if(!(p_geti(t,"SWR.Test.I",&i) && i==-12345 && p_getu(t,"SWR.Test.U",&u) && u==4000000000u && p_getf(t,"SWR.Test.F",&fv) && fv==1.25f && p_getp(t,"SWR.Test.P",&v) && v==(void*)t))
      return fail("NGX parameter layout check");
    D.params=t; }
  { NgxParam* caps=NULL; int avail=0, upd=0;
    if(NGX_OK(ngx.caps(&caps)) && caps){ p_geti(caps,"SuperSampling.Available",&avail); p_geti(caps,"SuperSampling.NeedsUpdatedDriver",&upd); }
    if(!avail) return fail(upd? "DLSS needs a newer NVIDIA driver" : "DLSS is not available (an RTX card and nvngx_dlss.dll next to the game are needed)"); }
  LOG("dlaa (OpenGL): NGX up on the OpenGL GPU, DLSS available\n");
  return 1; }

static u32 memtype(u32 bits){
  VkPhysicalDeviceMemoryProperties mp; vkGetPhysicalDeviceMemoryProperties(D.pd,&mp);
  for(u32 i=0;i<mp.memoryTypeCount;i++) if((bits>>i)&1 && (mp.memoryTypes[i].propertyFlags&VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)) return i;
  for(u32 i=0;i<mp.memoryTypeCount;i++) if((bits>>i)&1) return i;
  return 0; }

static void free_images(void){
  if(!D.dev) return; vkDeviceWaitIdle(D.dev);
  for(int i=0;i<IM_N;i++){ if(D.view[i]) vkDestroyImageView(D.dev,D.view[i],NULL); if(D.img[i]) vkDestroyImage(D.dev,D.img[i],NULL); if(D.mem[i]) vkFreeMemory(D.dev,D.mem[i],NULL);
    if(D.gtex[i]) gl.deltex(1,&D.gtex[i]); D.view[i]=0; D.img[i]=0; D.mem[i]=0; D.gtex[i]=0; } }

/* one image Vulkan allocates and GL imports */
static int shared_image(int i, VkFormat vf, GLenum gf, VkImageUsageFlags use, VkImageAspectFlags aspect){
  VkExternalMemoryHandleTypeFlagBits ht= D.kmt? VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_KMT_BIT : VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_BIT;
  VkExternalMemoryImageCreateInfo emi={VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO,NULL,ht};
  VkImageCreateInfo ci={VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,&emi,0,VK_IMAGE_TYPE_2D,vf,{D.w,D.h,1},1,1,VK_SAMPLE_COUNT_1_BIT,VK_IMAGE_TILING_OPTIMAL,use,VK_SHARING_MODE_EXCLUSIVE,0,NULL,VK_IMAGE_LAYOUT_UNDEFINED};
  if(vkCreateImage(D.dev,&ci,NULL,&D.img[i])!=VK_SUCCESS) return 0;
  VkMemoryRequirements mr; vkGetImageMemoryRequirements(D.dev,D.img[i],&mr);
  VkMemoryDedicatedAllocateInfo ded={VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO,NULL,D.img[i],VK_NULL_HANDLE};
  VkExportMemoryAllocateInfo exm={VK_STRUCTURE_TYPE_EXPORT_MEMORY_ALLOCATE_INFO,D.dedicated? &ded : NULL,ht};
  VkMemoryAllocateInfo ai={VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,&exm,mr.size,memtype(mr.memoryTypeBits)};
  if(vkAllocateMemory(D.dev,&ai,NULL,&D.mem[i])!=VK_SUCCESS || vkBindImageMemory(D.dev,D.img[i],D.mem[i],0)!=VK_SUCCESS) return 0;
  VkMemoryGetWin32HandleInfoKHR hi={VK_STRUCTURE_TYPE_MEMORY_GET_WIN32_HANDLE_INFO_KHR,NULL,D.mem[i],ht}; HANDLE h=NULL;
  if(vkGetMemoryWin32HandleKHR(D.dev,&hi,&h)!=VK_SUCCESS || !h) return 0;
  while(gl.err()){}
  GLint one=1, prev=0; gl.cmo(1,&D.gmem[i]); if(D.dedicated) gl.mopi(D.gmem[i],GL_DEDICATED_MEMORY_OBJECT_EXT_,&one);
  gl.imw(D.gmem[i],(GLuint64)mr.size,D.kmt? GL_HANDLE_TYPE_OPAQUE_WIN32_KMT_EXT_ : GL_HANDLE_TYPE_OPAQUE_WIN32_EXT_,h); if(!D.kmt) CloseHandle(h);
  gl.geti(GL_TEXTURE_BINDING_2D,&prev);
  gl.gentex(1,&D.gtex[i]); gl.bindtex(GL_TEXTURE_2D,D.gtex[i]); gl.tsm2(GL_TEXTURE_2D,1,gf,(GLsizei)D.w,(GLsizei)D.h,D.gmem[i],0);
  gl.bindtex(GL_TEXTURE_2D,(GLuint)prev);
  if(gl.err()) return 0;
  VkImageViewCreateInfo vi={VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,NULL,0,D.img[i],VK_IMAGE_VIEW_TYPE_2D,vf,{0},{aspect,0,1,0,1}};
  if(vkCreateImageView(D.dev,&vi,NULL,&D.view[i])!=VK_SUCCESS) return 0;
  D.vf[i]=vf; return 1; }

static VkSemaphore shared_sem(GLuint* g){
  VkExportSemaphoreCreateInfo ex={VK_STRUCTURE_TYPE_EXPORT_SEMAPHORE_CREATE_INFO,NULL,VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_WIN32_BIT};
  VkSemaphoreCreateInfo ci={VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,&ex,0}; VkSemaphore s=VK_NULL_HANDLE;
  if(vkCreateSemaphore(D.dev,&ci,NULL,&s)!=VK_SUCCESS) return VK_NULL_HANDLE;
  VkSemaphoreGetWin32HandleInfoKHR hi={VK_STRUCTURE_TYPE_SEMAPHORE_GET_WIN32_HANDLE_INFO_KHR,NULL,s,VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_WIN32_BIT}; HANDLE h=NULL;
  if(vkGetSemaphoreWin32HandleKHR(D.dev,&hi,&h)!=VK_SUCCESS || !h) return VK_NULL_HANDLE;
  gl.gs(1,g); gl.isw(*g,GL_HANDLE_TYPE_OPAQUE_WIN32_EXT_,h); CloseHandle(h);
  return s; }

static void barrier_all(VkCommandBuffer cb, VkImageLayout from){
  VkImageMemoryBarrier b[IM_N];
  for(int i=0;i<IM_N;i++){ VkImageAspectFlags a= i==IM_DEPTH? VK_IMAGE_ASPECT_DEPTH_BIT|((D.vf[i]==VK_FORMAT_D32_SFLOAT || D.vf[i]==VK_FORMAT_D16_UNORM)? 0 : VK_IMAGE_ASPECT_STENCIL_BIT) : VK_IMAGE_ASPECT_COLOR_BIT;
    b[i]=(VkImageMemoryBarrier){VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,NULL,VK_ACCESS_MEMORY_WRITE_BIT,VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT,from,VK_IMAGE_LAYOUT_GENERAL,
      VK_QUEUE_FAMILY_IGNORED,VK_QUEUE_FAMILY_IGNORED,D.img[i],{a,0,1,0,1}}; }
  vkCmdPipelineBarrier(cb,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,0,0,NULL,0,NULL,IM_N,b); }

static VkFormat depth_vk(GLenum f){ return f==GL_DEPTH32F_STENCIL8? VK_FORMAT_D32_SFLOAT_S8_UINT : f==GL_DEPTH24_STENCIL8? VK_FORMAT_D24_UNORM_S8_UINT :
  f==GL_DEPTH_COMPONENT32F? VK_FORMAT_D32_SFLOAT : f==GL_DEPTH_COMPONENT16? VK_FORMAT_D16_UNORM : VK_FORMAT_UNDEFINED; }

static int images(u32 w, u32 h, GLenum zfmt){
  if(D.w==w && D.h==h && D.zfmt==zfmt && D.img[IM_OUT]) return 1;
  free_images(); D.w=w; D.h=h; D.zfmt=zfmt;
  VkFormat zf=depth_vk(zfmt); if(zf==VK_FORMAT_UNDEFINED) return fail("unexpected depth format");
  int stencil= zf==VK_FORMAT_D32_SFLOAT_S8_UINT || zf==VK_FORMAT_D24_UNORM_S8_UINT;
  static const int modes[4][2]={{1,1},{1,0},{0,1},{0,0}};   /* KMT/NT handles, dedicated or not: what this driver takes */
  int ok=0;
  for(int m=0; m<4 && !ok; m++){
    if(D.ok>1 && m) break;                       /* the mode is known: only that one */
    if(D.ok<2){ D.kmt=modes[m][0]; D.dedicated=modes[m][1]; }
    ok= shared_image(IM_COLOR,VK_FORMAT_B10G11R11_UFLOAT_PACK32,GL_R11F_G11F_B10F,VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT|VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,VK_IMAGE_ASPECT_COLOR_BIT)
     && shared_image(IM_DEPTH,zf,zfmt,VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT|VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,VK_IMAGE_ASPECT_DEPTH_BIT)
     && shared_image(IM_MV,VK_FORMAT_R16G16_SFLOAT,GL_RG16F,VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT|VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT|VK_IMAGE_USAGE_STORAGE_BIT,VK_IMAGE_ASPECT_COLOR_BIT)
     && shared_image(IM_OUT,VK_FORMAT_R16G16B16A16_SFLOAT,GL_RGBA16F,VK_IMAGE_USAGE_STORAGE_BIT|VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT|VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,VK_IMAGE_ASPECT_COLOR_BIT);
    if(!ok){ free_images(); D.w=w; D.h=h; D.zfmt=zfmt; } }
  if(!ok) return fail("the driver cannot share images between OpenGL and Vulkan");
  if(D.ok<2) LOG("dlaa (OpenGL): sharing images with Vulkan (%s handles%s), %ux%u, depth %s\n",D.kmt? "KMT" : "NT",D.dedicated? ", dedicated" : "",w,h,stencil? "with stencil" : "only");
  D.ok=2;
  { /* new images: UNDEFINED -> GENERAL once, before GL writes into them (a later transition would drop GL's data) */
    u32 c=D.cur; VkCommandBuffer cb=D.cb[c];
    vkWaitForFences(D.dev,1,&D.fence[c],VK_TRUE,~0ull); vkResetFences(D.dev,1,&D.fence[c]); vkResetCommandBuffer(cb,0);
    VkCommandBufferBeginInfo bi={VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,NULL,VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,NULL}; vkBeginCommandBuffer(cb,&bi);
    barrier_all(cb,VK_IMAGE_LAYOUT_UNDEFINED); vkEndCommandBuffer(cb);
    VkSubmitInfo si={VK_STRUCTURE_TYPE_SUBMIT_INFO,NULL,0,NULL,NULL,1,&cb,0,NULL};
    if(vkQueueSubmit(D.q,1,&si,D.fence[c])!=VK_SUCCESS) return fail("Vulkan submit");
    vkWaitForFences(D.dev,1,&D.fence[c],VK_TRUE,~0ull); }
  if(!D.s_ready && (!(D.s_ready=shared_sem(&D.g_ready)) || !(D.s_done=shared_sem(&D.g_done)))) return fail("shared semaphores");
  if(!D.fbo_out){ gl.genfb(1,&D.fbo_out); gl.genfb(1,&D.fbo_dst); }
  D.fresh=1; return 1; }

static NgxResourceVK res(int i, VkImageAspectFlags aspect, int rw){
  NgxResourceVK r; memset(&r,0,sizeof r);
  r.Resource.ImageViewInfo.ImageView=D.view[i]; r.Resource.ImageViewInfo.Image=D.img[i];
  r.Resource.ImageViewInfo.SubresourceRange=(VkImageSubresourceRange){aspect,0,1,0,1};
  r.Resource.ImageViewInfo.Format=D.vf[i]; r.Resource.ImageViewInfo.Width=D.w; r.Resource.ImageViewInfo.Height=D.h;
  r.Type=0; r.ReadWrite=rw; return r; }

/* ---------------------------------------------------------------- the per-frame step, on the GL thread */
typedef struct { GLuint color, depth, mv; GLenum zfmt; u32 w, h; float jx, jy; int reset, have_mv; } DlJob;
/* NGX + its Vulkan device start on a thread of their own: on the GL thread the first call froze the whole pipeline for
   1.4 s right as the intro's audio started (audible hitching). Until they are up, frames go out without DLAA */
static volatile LONG g_dl_bg;   /* 0 not started, 1 starting, 2 up, 3 failed */
static void dl_bg(void* a){ (void)a; u64 t0=plat_ns(); int ok= ngx_load() && vk_up(); InterlockedExchange(&g_dl_bg,ok? 2 : 3);
  LOG("dlaa (OpenGL): NGX and Vulkan %s on a background thread in %.0f ms\n",ok? "started" : "failed to start",(plat_ns()-t0)/1e6); }
/* where a frame's DLAA step spends its time (the map waited in here) */
static u64 g_dlt[6], g_dltmax[6], g_dltn;
static void dl_run(const DlJob* j){
  if(D.failed) return;
  if(!D.tried){
    if(g_dl_bg==0){   /* here on the GL thread: its functions and the GPU's id first, then the slow part on the side */
      if(!gl_load()){ D.tried=1; fail("the OpenGL driver lacks memory/semaphore sharing"); return; }
      gl.gub(GL_DEVICE_UUID_EXT_,0,g_dl_uuid);
      g_dl_bg=1; if(plat_thread(dl_bg,NULL,1u<<20)) dl_bg(NULL); }
    if(g_dl_bg==1) return;   /* still starting */
    D.tried=1; if(g_dl_bg!=2) return; D.ok=1; }
  u64 tp0=plat_ns();
  if(!j->w) return;   /* start-up only: NGX is up, the images follow with the first frame */
  if(D.ok<1 || !images(j->w,j->h,j->zfmt)) return;
  /* inputs into the shared images */
  gl.copy(j->color,GL_TEXTURE_2D,0,0,0,0,D.gtex[IM_COLOR],GL_TEXTURE_2D,0,0,0,0,(GLsizei)j->w,(GLsizei)j->h,1);
  gl.copy(j->depth,GL_TEXTURE_2D,0,0,0,0,D.gtex[IM_DEPTH],GL_TEXTURE_2D,0,0,0,0,(GLsizei)j->w,(GLsizei)j->h,1);
  if(j->have_mv) gl.copy(j->mv,GL_TEXTURE_2D,0,0,0,0,D.gtex[IM_MV],GL_TEXTURE_2D,0,0,0,0,(GLsizei)j->w,(GLsizei)j->h,1);
  GLenum lay[IM_N]={GL_LAYOUT_GENERAL_EXT_,GL_LAYOUT_GENERAL_EXT_,GL_LAYOUT_GENERAL_EXT_,GL_LAYOUT_GENERAL_EXT_};
  gl.sig(D.g_ready,0,NULL,IM_N,D.gtex,lay);
  /* DLSS on Vulkan */
  u64 tp1=plat_ns();
  u32 c=D.cur; D.cur=(D.cur+1)%DL_RING; VkCommandBuffer cb=D.cb[c];
  vkWaitForFences(D.dev,1,&D.fence[c],VK_TRUE,~0ull); u64 tp2=plat_ns(); vkResetFences(D.dev,1,&D.fence[c]); vkResetCommandBuffer(cb,0);
  if(D.qp && D.qp_used[c]){ u64 ts[2]={0,0};   /* this command buffer's last DLSS evaluation, now finished */
    if(vkGetQueryPoolResults(D.dev,D.qp,c*2,2,sizeof ts,ts,8,VK_QUERY_RESULT_64_BIT)==VK_SUCCESS && ts[1]>=ts[0]){
      u64 g=(u64)((double)(ts[1]-ts[0])*D.tsp); D.gpu_sum+=g; if(g>D.gpu_max) D.gpu_max=g; D.gpu_n++; } }
  VkCommandBufferBeginInfo bi={VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,NULL,VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,NULL}; vkBeginCommandBuffer(cb,&bi);
  if(D.qp) vkCmdResetQueryPool(cb,D.qp,c*2,2);
  barrier_all(cb,VK_IMAGE_LAYOUT_GENERAL);
  if(!j->have_mv){ VkClearColorValue z={{0,0,0,0}}; VkImageSubresourceRange all={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
    vkCmdClearColorImage(cb,D.img[IM_MV],VK_IMAGE_LAYOUT_GENERAL,&z,1,&all); barrier_all(cb,VK_IMAGE_LAYOUT_GENERAL); }
  NgxParam* p=D.params; int reset=j->reset;
  if(!D.feat || D.fw!=j->w || D.fh!=j->h){
    if(D.feat){ vkDeviceWaitIdle(D.dev); ngx.release(D.feat); D.feat=NULL; }
    p_setu(p,"CreationNodeMask",1); p_setu(p,"VisibilityNodeMask",1); p_setu(p,"Width",j->w); p_setu(p,"Height",j->h);
    p_setu(p,"OutWidth",j->w); p_setu(p,"OutHeight",j->h); p_seti(p,"PerfQualityValue",5 /* DLAA */);
    p_seti(p,"DLSS.Feature.Create.Flags",(1<<0)|(1<<1)|(1<<6));   /* IsHDR | MVLowRes | AutoExposure */
    p_seti(p,"DLSS.Enable.Output.Subrects",0);
    NgxResult r=ngx.create(cb,1,p,&D.feat);
    if(!NGX_OK(r) || !D.feat){ LOG("dlaa (OpenGL): DLSS feature creation failed (%08x)\n",(unsigned)r); vkEndCommandBuffer(cb); D.failed=1; return; }
    D.fw=j->w; D.fh=j->h; reset=1; LOG("dlaa (OpenGL): DLSS feature %ux%u created\n",j->w,j->h); }
  NgxResourceVK rc=res(IM_COLOR,VK_IMAGE_ASPECT_COLOR_BIT,0), rz=res(IM_DEPTH,VK_IMAGE_ASPECT_DEPTH_BIT,0), rm=res(IM_MV,VK_IMAGE_ASPECT_COLOR_BIT,0), ro=res(IM_OUT,VK_IMAGE_ASPECT_COLOR_BIT,1);
  p_setp(p,"Color",&rc); p_setp(p,"Depth",&rz); p_setp(p,"MotionVectors",&rm); p_setp(p,"Output",&ro);
  p_setf(p,"Jitter.Offset.X",j->jx); p_setf(p,"Jitter.Offset.Y",j->jy); p_setf(p,"Sharpness",0.0f); p_seti(p,"Reset",reset);
  p_setf(p,"MV.Scale.X",1.0f); p_setf(p,"MV.Scale.Y",1.0f);
  p_setu(p,"DLSS.Render.Subrect.Dimensions.Width",j->w); p_setu(p,"DLSS.Render.Subrect.Dimensions.Height",j->h);
  p_setf(p,"DLSS.Pre.Exposure",1.0f); p_setf(p,"DLSS.Exposure.Scale",1.0f); p_setf(p,"FrameTimeDeltaInMsec",16.67f);
  if(D.qp) vkCmdWriteTimestamp(cb,VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,D.qp,c*2);
  u64 tp3=plat_ns(); NgxResult r=ngx.eval(cb,D.feat,p,NULL); u64 tp4=plat_ns();
  if(D.qp){ vkCmdWriteTimestamp(cb,VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,D.qp,c*2+1); D.qp_used[c]=1; }
  barrier_all(cb,VK_IMAGE_LAYOUT_GENERAL);
  vkEndCommandBuffer(cb);
  VkPipelineStageFlags ws=VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
  VkSubmitInfo si={VK_STRUCTURE_TYPE_SUBMIT_INFO,NULL,1,&D.s_ready,&ws,1,&cb,1,&D.s_done};
  if(vkQueueSubmit(D.q,1,&si,D.fence[c])!=VK_SUCCESS){ fail("Vulkan submit"); return; }
  D.fresh=0;
  /* GL waits for the result, then it goes back into the scene target */
  u64 tp5=plat_ns(); gl.wait(D.g_done,0,NULL,IM_N,D.gtex,lay); u64 tp6=plat_ns();
  if(!NGX_OK(r)){ LOG("dlaa (OpenGL): DLSS evaluation failed (%08x)\n",(unsigned)r); D.failed=1; return; }
  GLint rfb=0, dfb=0; gl.geti(GL_READ_FRAMEBUFFER_BINDING,&rfb); gl.geti(GL_DRAW_FRAMEBUFFER_BINDING,&dfb);
  GLboolean sc=gl.isen(GL_SCISSOR_TEST); if(sc) gl.dis(GL_SCISSOR_TEST);
  gl.bindfb(GL_READ_FRAMEBUFFER,D.fbo_out); gl.fbtex(GL_READ_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,D.gtex[IM_OUT],0);
  gl.bindfb(GL_DRAW_FRAMEBUFFER,D.fbo_dst); gl.fbtex(GL_DRAW_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,j->color,0);
  gl.blit(0,0,(GLint)j->w,(GLint)j->h,0,0,(GLint)j->w,(GLint)j->h,GL_COLOR_BUFFER_BIT,GL_NEAREST);
  gl.bindfb(GL_READ_FRAMEBUFFER,(GLuint)rfb); gl.bindfb(GL_DRAW_FRAMEBUFFER,(GLuint)dfb); if(sc) gl.en(GL_SCISSOR_TEST);
  { u64 tp7=plat_ns(); u64 ph[6]={tp1-tp0,tp2-tp1,tp4-tp3,tp5-tp4,tp6-tp5,tp7-tp6};   /* copies in, fence wait, NGX eval, submit, GL wait, blit back */
    for(int i=0;i<6;i++){ g_dlt[i]+=ph[i]; if(ph[i]>g_dltmax[i]) g_dltmax[i]=ph[i]; } g_dltn++;
    /* watchdog: half a second of steps over 20 ms and DLAA goes off for this session. It should cost a few ms; on the map
       the copies waited 50-60 ms a frame for the previous evaluation (2026-11-08: 17 FPS, and the audio ran dry with it) */
    { static int slow; slow= tp7-tp0>20000000ull? slow+1 : 0;
      if(slow>=30 && !getenv("SWR_DLSS_NO_WATCHDOG")){ fail("each frame's step keeps taking over 20 ms, which costs more than it gives"); return; } }
    if(tp7-tp0>12000000ull){ static int said; if(said<40){ said++; LOG("dlaa (OpenGL): a %.1f ms step: copies %.1f, fence wait %.1f, eval %.1f, submit %.1f, GL wait %.1f, blit %.1f ms\n",(tp7-tp0)/1e6,ph[0]/1e6,ph[1]/1e6,ph[2]/1e6,ph[3]/1e6,ph[4]/1e6,ph[5]/1e6); } }
    if(g_dltn>=600){ LOG("dlaa (OpenGL): step averages over %llu frames (worst): copies %.2f (%.1f), fence wait %.2f (%.1f), eval %.2f (%.1f), submit %.2f (%.1f), GL wait %.2f (%.1f), blit %.2f (%.1f) ms\n",(unsigned long long)g_dltn,
        g_dlt[0]/1e6/g_dltn,g_dltmax[0]/1e6,g_dlt[1]/1e6/g_dltn,g_dltmax[1]/1e6,g_dlt[2]/1e6/g_dltn,g_dltmax[2]/1e6,g_dlt[3]/1e6/g_dltn,g_dltmax[3]/1e6,g_dlt[4]/1e6/g_dltn,g_dltmax[4]/1e6,g_dlt[5]/1e6/g_dltn,g_dltmax[5]/1e6);
      if(D.gpu_n) LOG("dlaa (OpenGL): DLSS's own GPU time %.2f ms average, %.2f worst (%u frames)\n",D.gpu_sum/1e6/D.gpu_n,D.gpu_max/1e6,D.gpu_n);
      D.gpu_sum=D.gpu_max=0; D.gpu_n=0;
      memset(g_dlt,0,sizeof g_dlt); memset(g_dltmax,0,sizeof g_dltmax); g_dltn=0; } }
  if(++D.evals==1 || D.evals%600==0) LOG("dlaa (OpenGL): %d frames, %ux%u, jitter %.3f %.3f, motion vectors %s\n",D.evals,j->w,j->h,j->jx,j->jy,j->have_mv? "yes" : "none");
}

/* queued from the render thread (gpu_gl.c): the step runs on the GL thread in order with the frame */
extern void* glq_alloc(size_t sz, size_t extra, void (*ex)(void*)); extern void glq_commit(void); extern int glq_active(void);
static void e_dl(void* p){ dl_run((const DlJob*)p); }
int dlgl_failed(void){ return D.failed; }
void dlgl_queue(GLuint color, GLuint depth, GLuint mv, GLenum zfmt, u32 w, u32 h, float jx, float jy, int reset){
  DlJob j={color,depth,mv,zfmt,w,h,jx,jy,reset,mv!=0};
  if(!glq_active()){ dl_run(&j); return; }
  DlJob* q=(DlJob*)glq_alloc(sizeof *q,0,e_dl); *q=j; glq_commit(); }
#else
int dlgl_failed(void){ return 1; }
void dlgl_queue(unsigned color, unsigned depth, unsigned mv, unsigned zfmt, unsigned w, unsigned h, float jx, float jy, int reset){ (void)color; (void)depth; (void)mv; (void)zfmt; (void)w; (void)h; (void)jx; (void)jy; (void)reset; }
#endif
