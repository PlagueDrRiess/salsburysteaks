/* Vulkan presentation (renderer=vulkan), included by backend_win32.c.

   The game still renders with OpenGL, on a context bound to a hidden window. Its final screen pass
   (gpu_gl screen_quad, which binds framebuffer 0) is redirected into one of two images that Vulkan
   owns and exports to GL (GL_EXT_memory_object_win32). On swap, GL signals a shared semaphore,
   Vulkan blits the image into its swapchain on the visible window and presents; a second semaphore
   tells GL when the image may be drawn into again. GPU-side waits only: the CPU never blocks on GL.

   No Vulkan SDK headers: the few types used are declared here and vulkan-1.dll (installed by the
   GPU driver) is loaded at runtime. If anything is missing the game falls back to OpenGL output. */

/* the backend doesn't include the GL headers: the few GL types and enums used here */
typedef unsigned int GLenum, GLuint; typedef int GLint, GLsizei; typedef unsigned long long GLuint64; typedef unsigned char GLubyte;
#define GL_TEXTURE_2D 0x0DE1
#define GL_RGBA8 0x8058
#define GL_FRAMEBUFFER 0x8D40
#define GL_COLOR_ATTACHMENT0 0x8CE0
#define GL_FRAMEBUFFER_COMPLETE 0x8CD5

typedef uint32_t VkFlags, VkBool32; typedef uint64_t VkDeviceSize; typedef int32_t VkResult;
typedef struct VkInstance_T* VkInstance; typedef struct VkPhysicalDevice_T* VkPhysicalDevice; typedef struct VkDevice_T* VkDevice;
typedef struct VkQueue_T* VkQueue; typedef struct VkCommandBuffer_T* VkCommandBuffer;
typedef uint64_t VkSurfaceKHR, VkSwapchainKHR, VkImage, VkDeviceMemory, VkSemaphore, VkFence, VkCommandPool;
typedef void (*PFN_vkVoid)(void);
typedef struct { uint32_t width, height; } VkExtent2D;
typedef struct { uint32_t width, height, depth; } VkExtent3D;
typedef struct { int32_t x, y, z; } VkOffset3D;
typedef struct { uint32_t sType; const void* pNext; const char* pApplicationName; uint32_t applicationVersion; const char* pEngineName; uint32_t engineVersion; uint32_t apiVersion; } VkApplicationInfo;
typedef struct { uint32_t sType; const void* pNext; VkFlags flags; const VkApplicationInfo* pApplicationInfo; uint32_t enabledLayerCount; const char* const* ppEnabledLayerNames; uint32_t enabledExtensionCount; const char* const* ppEnabledExtensionNames; } VkInstanceCreateInfo;
typedef struct { uint32_t sType; const void* pNext; VkFlags flags; HINSTANCE hinstance; HWND hwnd; } VkWin32SurfaceCreateInfoKHR;
typedef struct { uint32_t sType; void* pNext; uint8_t deviceUUID[16]; uint8_t driverUUID[16]; uint8_t deviceLUID[8]; uint32_t deviceNodeMask; VkBool32 deviceLUIDValid; } VkPhysicalDeviceIDProperties;
typedef struct { uint32_t sType; void* pNext; uint64_t props[256]; } VkPhysicalDeviceProperties2;   /* VkPhysicalDeviceProperties is < 1 KB; we only read deviceType and deviceName */
typedef struct { VkFlags queueFlags; uint32_t queueCount; uint32_t timestampValidBits; VkExtent3D minImageTransferGranularity; } VkQueueFamilyProperties;
typedef struct { uint32_t sType; const void* pNext; VkFlags flags; uint32_t queueFamilyIndex; uint32_t queueCount; const float* pQueuePriorities; } VkDeviceQueueCreateInfo;
typedef struct { uint32_t sType; const void* pNext; VkFlags flags; uint32_t queueCreateInfoCount; const VkDeviceQueueCreateInfo* pQueueCreateInfos; uint32_t enabledLayerCount; const char* const* ppEnabledLayerNames; uint32_t enabledExtensionCount; const char* const* ppEnabledExtensionNames; const void* pEnabledFeatures; } VkDeviceCreateInfo;
typedef struct { uint32_t minImageCount, maxImageCount; VkExtent2D currentExtent, minImageExtent, maxImageExtent; uint32_t maxImageArrayLayers; VkFlags supportedTransforms; uint32_t currentTransform; VkFlags supportedCompositeAlpha, supportedUsageFlags; } VkSurfaceCapabilitiesKHR;
typedef struct { uint32_t format, colorSpace; } VkSurfaceFormatKHR;
typedef struct { uint32_t sType; const void* pNext; VkFlags flags; VkSurfaceKHR surface; uint32_t minImageCount; uint32_t imageFormat; uint32_t imageColorSpace; VkExtent2D imageExtent; uint32_t imageArrayLayers; VkFlags imageUsage; uint32_t imageSharingMode; uint32_t queueFamilyIndexCount; const uint32_t* pQueueFamilyIndices; uint32_t preTransform; uint32_t compositeAlpha; uint32_t presentMode; VkBool32 clipped; VkSwapchainKHR oldSwapchain; } VkSwapchainCreateInfoKHR;
typedef struct { uint32_t sType; const void* pNext; VkFlags flags; uint32_t imageType; uint32_t format; VkExtent3D extent; uint32_t mipLevels, arrayLayers, samples, tiling; VkFlags usage; uint32_t sharingMode; uint32_t queueFamilyIndexCount; const uint32_t* pQueueFamilyIndices; uint32_t initialLayout; } VkImageCreateInfo;
typedef struct { uint32_t sType; const void* pNext; VkFlags handleTypes; } VkExternalMemoryImageCreateInfo, VkExportMemoryAllocateInfo, VkExportSemaphoreCreateInfo;
typedef struct { VkDeviceSize size, alignment; uint32_t memoryTypeBits; } VkMemoryRequirements;
typedef struct { uint32_t propertyFlags, heapIndex; } VkMemoryType;
typedef struct { VkDeviceSize size; VkFlags flags; } VkMemoryHeap;
typedef struct { uint32_t memoryTypeCount; VkMemoryType memoryTypes[32]; uint32_t memoryHeapCount; VkMemoryHeap memoryHeaps[16]; } VkPhysicalDeviceMemoryProperties;
typedef struct { uint32_t sType; const void* pNext; VkDeviceSize allocationSize; uint32_t memoryTypeIndex; } VkMemoryAllocateInfo;
typedef struct { uint32_t sType; const void* pNext; VkImage image; uint64_t buffer; } VkMemoryDedicatedAllocateInfo;
typedef struct { uint32_t sType; const void* pNext; VkDeviceMemory memory; uint32_t handleType; } VkMemoryGetWin32HandleInfoKHR;
typedef struct { uint32_t sType; const void* pNext; VkSemaphore semaphore; uint32_t handleType; } VkSemaphoreGetWin32HandleInfoKHR;
typedef struct { uint32_t sType; const void* pNext; VkFlags flags; } VkSemaphoreCreateInfo, VkFenceCreateInfo;
typedef struct { uint32_t sType; const void* pNext; VkFlags flags; uint32_t queueFamilyIndex; } VkCommandPoolCreateInfo;
typedef struct { uint32_t sType; const void* pNext; VkCommandPool commandPool; uint32_t level; uint32_t commandBufferCount; } VkCommandBufferAllocateInfo;
typedef struct { uint32_t sType; const void* pNext; VkFlags flags; const void* pInheritanceInfo; } VkCommandBufferBeginInfo;
typedef struct { VkFlags aspectMask; uint32_t baseMipLevel, levelCount, baseArrayLayer, layerCount; } VkImageSubresourceRange;
typedef struct { uint32_t sType; const void* pNext; VkFlags srcAccessMask, dstAccessMask; uint32_t oldLayout, newLayout; uint32_t srcQueueFamilyIndex, dstQueueFamilyIndex; VkImage image; VkImageSubresourceRange subresourceRange; } VkImageMemoryBarrier;
typedef struct { VkFlags aspectMask; uint32_t mipLevel, baseArrayLayer, layerCount; } VkImageSubresourceLayers;
typedef struct { VkImageSubresourceLayers srcSubresource; VkOffset3D srcOffsets[2]; VkImageSubresourceLayers dstSubresource; VkOffset3D dstOffsets[2]; } VkImageBlit;
typedef struct { uint32_t sType; const void* pNext; uint32_t waitSemaphoreCount; const VkSemaphore* pWaitSemaphores; const VkFlags* pWaitDstStageMask; uint32_t commandBufferCount; const VkCommandBuffer* pCommandBuffers; uint32_t signalSemaphoreCount; const VkSemaphore* pSignalSemaphores; } VkSubmitInfo;
typedef struct { uint32_t sType; const void* pNext; uint32_t waitSemaphoreCount; const VkSemaphore* pWaitSemaphores; uint32_t swapchainCount; const VkSwapchainKHR* pSwapchains; const uint32_t* pImageIndices; VkResult* pResults; } VkPresentInfoKHR;

enum { VKS_APP=0, VKS_INSTANCE=1, VKS_DEVQUEUE=2, VKS_DEVICE=3, VKS_SUBMIT=4, VKS_MEMALLOC=5, VKS_FENCE=8, VKS_SEMAPHORE=9, VKS_IMAGE=14,
       VKS_CMDPOOL=39, VKS_CMDALLOC=40, VKS_CMDBEGIN=42, VKS_IMGBARRIER=45, VKS_SWAPCHAIN=1000001000, VKS_PRESENT=1000001001,
       VKS_WIN32SURFACE=1000009000, VKS_PROPS2=1000059001, VKS_IDPROPS=1000071004, VKS_EXTMEMIMAGE=1000072001, VKS_EXPORTMEM=1000072002,
       VKS_MEMWIN32HANDLE=1000073003, VKS_EXPORTSEM=1000077000, VKS_SEMWIN32HANDLE=1000078003, VKS_DEDICATED=1000127001 };
#define VKL_UNDEFINED 0
#define VKL_GENERAL 1
#define VKL_TRANSFER_DST 7
#define VKL_PRESENT 1000001002
#define VK_OPAQUE_WIN32 (g_vkp_kmt? 0x4u : 0x2u)
#define VK_SUBOPTIMAL 1000001003
#define VK_OUT_OF_DATE (-1000001004)

typedef PFN_vkVoid (__stdcall *PFN_GIPA)(VkInstance, const char*);
typedef PFN_vkVoid (__stdcall *PFN_GDPA)(VkDevice, const char*);
#define VKFN(X) \
  X(VkResult,vkCreateInstance,(const VkInstanceCreateInfo*,const void*,VkInstance*)) \
  X(VkResult,vkEnumeratePhysicalDevices,(VkInstance,uint32_t*,VkPhysicalDevice*)) \
  X(void,vkGetPhysicalDeviceProperties2,(VkPhysicalDevice,VkPhysicalDeviceProperties2*)) \
  X(void,vkGetPhysicalDeviceQueueFamilyProperties,(VkPhysicalDevice,uint32_t*,VkQueueFamilyProperties*)) \
  X(void,vkGetPhysicalDeviceMemoryProperties,(VkPhysicalDevice,VkPhysicalDeviceMemoryProperties*)) \
  X(VkResult,vkCreateWin32SurfaceKHR,(VkInstance,const VkWin32SurfaceCreateInfoKHR*,const void*,VkSurfaceKHR*)) \
  X(VkResult,vkGetPhysicalDeviceSurfaceSupportKHR,(VkPhysicalDevice,uint32_t,VkSurfaceKHR,VkBool32*)) \
  X(VkResult,vkGetPhysicalDeviceSurfaceCapabilitiesKHR,(VkPhysicalDevice,VkSurfaceKHR,VkSurfaceCapabilitiesKHR*)) \
  X(VkResult,vkGetPhysicalDeviceSurfaceFormatsKHR,(VkPhysicalDevice,VkSurfaceKHR,uint32_t*,VkSurfaceFormatKHR*)) \
  X(VkResult,vkGetPhysicalDeviceSurfacePresentModesKHR,(VkPhysicalDevice,VkSurfaceKHR,uint32_t*,uint32_t*)) \
  X(VkResult,vkCreateDevice,(VkPhysicalDevice,const VkDeviceCreateInfo*,const void*,VkDevice*))
#define VKDFN(X) \
  X(void,vkGetDeviceQueue,(VkDevice,uint32_t,uint32_t,VkQueue*)) \
  X(VkResult,vkDeviceWaitIdle,(VkDevice)) \
  X(VkResult,vkCreateSwapchainKHR,(VkDevice,const VkSwapchainCreateInfoKHR*,const void*,VkSwapchainKHR*)) \
  X(void,vkDestroySwapchainKHR,(VkDevice,VkSwapchainKHR,const void*)) \
  X(VkResult,vkGetSwapchainImagesKHR,(VkDevice,VkSwapchainKHR,uint32_t*,VkImage*)) \
  X(VkResult,vkAcquireNextImageKHR,(VkDevice,VkSwapchainKHR,uint64_t,VkSemaphore,VkFence,uint32_t*)) \
  X(VkResult,vkQueuePresentKHR,(VkQueue,const VkPresentInfoKHR*)) \
  X(VkResult,vkCreateImage,(VkDevice,const VkImageCreateInfo*,const void*,VkImage*)) \
  X(void,vkDestroyImage,(VkDevice,VkImage,const void*)) \
  X(void,vkGetImageMemoryRequirements,(VkDevice,VkImage,VkMemoryRequirements*)) \
  X(VkResult,vkAllocateMemory,(VkDevice,const VkMemoryAllocateInfo*,const void*,VkDeviceMemory*)) \
  X(void,vkFreeMemory,(VkDevice,VkDeviceMemory,const void*)) \
  X(VkResult,vkBindImageMemory,(VkDevice,VkImage,VkDeviceMemory,VkDeviceSize)) \
  X(VkResult,vkGetMemoryWin32HandleKHR,(VkDevice,const VkMemoryGetWin32HandleInfoKHR*,HANDLE*)) \
  X(VkResult,vkCreateSemaphore,(VkDevice,const VkSemaphoreCreateInfo*,const void*,VkSemaphore*)) \
  X(void,vkDestroySemaphore,(VkDevice,VkSemaphore,const void*)) \
  X(VkResult,vkGetSemaphoreWin32HandleKHR,(VkDevice,const VkSemaphoreGetWin32HandleInfoKHR*,HANDLE*)) \
  X(VkResult,vkCreateFence,(VkDevice,const VkFenceCreateInfo*,const void*,VkFence*)) \
  X(VkResult,vkWaitForFences,(VkDevice,uint32_t,const VkFence*,VkBool32,uint64_t)) \
  X(VkResult,vkResetFences,(VkDevice,uint32_t,const VkFence*)) \
  X(VkResult,vkCreateCommandPool,(VkDevice,const VkCommandPoolCreateInfo*,const void*,VkCommandPool*)) \
  X(VkResult,vkAllocateCommandBuffers,(VkDevice,const VkCommandBufferAllocateInfo*,VkCommandBuffer*)) \
  X(VkResult,vkBeginCommandBuffer,(VkCommandBuffer,const VkCommandBufferBeginInfo*)) \
  X(VkResult,vkEndCommandBuffer,(VkCommandBuffer)) \
  X(VkResult,vkResetCommandBuffer,(VkCommandBuffer,VkFlags)) \
  X(void,vkCmdPipelineBarrier,(VkCommandBuffer,VkFlags,VkFlags,VkFlags,uint32_t,const void*,uint32_t,const void*,uint32_t,const VkImageMemoryBarrier*)) \
  X(void,vkCmdBlitImage,(VkCommandBuffer,VkImage,uint32_t,VkImage,uint32_t,uint32_t,const VkImageBlit*,uint32_t)) \
  X(VkResult,vkQueueSubmit,(VkQueue,uint32_t,const VkSubmitInfo*,VkFence))
#define VKDECL(R,n,a) static R (__stdcall *n)a;
VKFN(VKDECL) VKDFN(VKDECL)
#undef VKDECL

/* GL side: EXT_memory_object(_win32), EXT_semaphore(_win32) */
typedef void (APIENTRY *PFN_GLCMO)(GLsizei,GLuint*); typedef void (APIENTRY *PFN_GLMOPI)(GLuint,GLenum,const GLint*);
typedef void (APIENTRY *PFN_GLIMW)(GLuint,GLuint64,GLenum,void*); typedef void (APIENTRY *PFN_GLTSM2)(GLenum,GLsizei,GLenum,GLsizei,GLsizei,GLuint,GLuint64);
typedef void (APIENTRY *PFN_GLGS)(GLsizei,GLuint*); typedef void (APIENTRY *PFN_GLISW)(GLuint,GLenum,void*);
typedef void (APIENTRY *PFN_GLSS)(GLuint,GLuint,const GLuint*,GLuint,const GLuint*,const GLenum*);
typedef void (APIENTRY *PFN_GLDMO)(GLsizei,const GLuint*); typedef void (APIENTRY *PFN_GLDS)(GLsizei,const GLuint*);
typedef void (APIENTRY *PFN_GLGUB)(GLenum,GLuint,GLubyte*);
typedef void (APIENTRY *PFN_GLGEN)(GLsizei,GLuint*); typedef void (APIENTRY *PFN_GLDEL)(GLsizei,const GLuint*);
typedef void (APIENTRY *PFN_GLBIND)(GLenum,GLuint); typedef void (APIENTRY *PFN_GLFBT)(GLenum,GLenum,GLenum,GLuint,GLint);
typedef GLenum (APIENTRY *PFN_GLCFS)(GLenum); typedef GLenum (APIENTRY *PFN_GLERR)(void); typedef void (APIENTRY *PFN_GLV)(void); typedef void (APIENTRY *PFN_GLGI)(GLenum,GLint*);
static struct { PFN_GLCMO cmo; PFN_GLMOPI mopi; PFN_GLIMW imw; PFN_GLTSM2 tsm2; PFN_GLGS gs; PFN_GLISW isw; PFN_GLSS sig, wait; PFN_GLDMO dmo; PFN_GLDS ds; PFN_GLGUB gub;
  PFN_GLGEN gentex, genfb; PFN_GLDEL deltex, delfb; PFN_GLBIND bindtex, bindfb; PFN_GLFBT fbtex; PFN_GLCFS cfs; PFN_GLV flush, finish; PFN_GLGI geti; PFN_GLERR err; } gx;
#define GL_HANDLE_OPAQUE_WIN32 (g_vkp_kmt? 0x9588 : 0x9587)
#define GL_DEDICATED_MEMORY 0x9581
#define GL_LAYOUT_GENERAL_ 0x958D
#define GL_DEVICE_UUID_ 0x9597

#define VKP_IMAGES 2
static struct {
  int on;                                /* Vulkan presentation active */
  VkInstance inst; VkPhysicalDevice pd; VkDevice dev; VkQueue q; uint32_t qf; VkSurfaceKHR surf;
  VkSwapchainKHR sc; VkImage scimg[8]; uint32_t nsc; VkExtent2D ext; uint32_t scfmt, mode;
  VkSemaphore acq[2], rdone[8]; VkFence fence[2]; VkCommandPool pool; VkCommandBuffer cb[2]; uint32_t frame;
  VkImage img[VKP_IMAGES]; VkDeviceMemory mem[VKP_IMAGES]; VkSemaphore ready[VKP_IMAGES], done[VKP_IMAGES];   /* shared with GL */
  GLuint gmem[VKP_IMAGES], gtex[VKP_IMAGES], gfbo[VKP_IMAGES], gready[VKP_IMAGES], gdone[VKP_IMAGES]; int pending[VKP_IMAGES]; int cur;
  int fallback_window;                   /* Vulkan failed after the hidden GL window was made: GL renders to the real window */
  u64 presents, recreates;
} vkp;
static int g_vkp_quiet;                /* probing sharing modes: failures are expected */
static int g_vkp_kmt=0;                /* 1 = OPAQUE_WIN32_KMT handles (no CloseHandle) */
static int g_vkp_dedicated=1;          /* VkMemoryDedicatedAllocateInfo + GL_DEDICATED_MEMORY_OBJECT_EXT; plain allocation if GL refuses it */
static volatile GLuint g_vkp_fbo;        /* what "framebuffer 0" means to the renderer while Vulkan presents */
static PFN_GLBIND g_real_bindfb;
static void APIENTRY vkp_bind_framebuffer(GLenum target, GLuint fb){ g_real_bindfb(target, fb? fb : g_vkp_fbo); }

static int vkp_fail(const char* what){ LOG("vulkan: %s - using OpenGL output\n",what); return 0; }

static int vkp_load_gl(void){
  /* AMD's OpenGL driver has every extension Vulkan output needs and then crashes in the first glSignalSemaphoreEXT, the
     call that hands a frame to Vulkan (atio6axx.dll+714047, an access to 0x1001b: RX 9070 XT and RX 7800 XT, drivers
     26.10.1 and 26.10.2, four reports). The game never got past its first frame. On AMD the picture goes out through
     OpenGL instead, as it does where the driver lacks the extensions. SWR_VKP_AMD=1 tries Vulkan output anyway */
  { const unsigned char* (APIENTRY *gstr)(GLenum)=backend_gl_proc_raw("glGetString"); const char* ven= gstr? (const char*)gstr(0x1F00 /* GL_VENDOR */) : NULL;
    if(ven && (strstr(ven,"ATI") || strstr(ven,"AMD")) && !getenv("SWR_VKP_AMD")) return vkp_fail("AMD's OpenGL driver crashes when it hands a frame to Vulkan"); }
  #define G(f,n) *(void**)&gx.f=backend_gl_proc_raw(n); if(!gx.f) return vkp_fail("the OpenGL driver lacks " n);
  G(cmo,"glCreateMemoryObjectsEXT") G(mopi,"glMemoryObjectParameterivEXT") G(imw,"glImportMemoryWin32HandleEXT") G(tsm2,"glTexStorageMem2DEXT")
  G(gs,"glGenSemaphoresEXT") G(isw,"glImportSemaphoreWin32HandleEXT") G(sig,"glSignalSemaphoreEXT") G(wait,"glWaitSemaphoreEXT")
  G(dmo,"glDeleteMemoryObjectsEXT") G(ds,"glDeleteSemaphoresEXT") G(gub,"glGetUnsignedBytei_vEXT")
  G(gentex,"glGenTextures") G(genfb,"glGenFramebuffers") G(deltex,"glDeleteTextures") G(delfb,"glDeleteFramebuffers") G(bindtex,"glBindTexture")
  G(bindfb,"glBindFramebuffer") G(fbtex,"glFramebufferTexture2D") G(cfs,"glCheckFramebufferStatus") G(flush,"glFlush") G(finish,"glFinish") G(geti,"glGetIntegerv") G(err,"glGetError")
  #undef G
  g_real_bindfb=gx.bindfb; return 1; }

static uint32_t vkp_memtype(uint32_t bits, VkFlags want){
  VkPhysicalDeviceMemoryProperties mp; vkGetPhysicalDeviceMemoryProperties(vkp.pd,&mp);
  for(uint32_t i=0;i<mp.memoryTypeCount;i++) if((bits&(1u<<i)) && (mp.memoryTypes[i].propertyFlags&want)==want) return i;
  for(uint32_t i=0;i<mp.memoryTypeCount;i++) if(bits&(1u<<i)) return i;
  return 0; }

/* swapchain + the two shared images at the current window size */
static void vkp_destroy_targets(void){
  if(!vkp.dev) return;
  vkDeviceWaitIdle(vkp.dev); gx.finish();
  for(int i=0;i<VKP_IMAGES;i++){
    if(vkp.gfbo[i]){ gx.delfb(1,&vkp.gfbo[i]); vkp.gfbo[i]=0; } if(vkp.gtex[i]){ gx.deltex(1,&vkp.gtex[i]); vkp.gtex[i]=0; }
    if(vkp.gmem[i]){ gx.dmo(1,&vkp.gmem[i]); vkp.gmem[i]=0; }
    if(vkp.gready[i]){ gx.ds(1,&vkp.gready[i]); vkp.gready[i]=0; } if(vkp.gdone[i]){ gx.ds(1,&vkp.gdone[i]); vkp.gdone[i]=0; }
    if(vkp.ready[i]){ vkDestroySemaphore(vkp.dev,vkp.ready[i],NULL); vkp.ready[i]=0; } if(vkp.done[i]){ vkDestroySemaphore(vkp.dev,vkp.done[i],NULL); vkp.done[i]=0; }
    if(vkp.img[i]){ vkDestroyImage(vkp.dev,vkp.img[i],NULL); vkp.img[i]=0; } if(vkp.mem[i]){ vkFreeMemory(vkp.dev,vkp.mem[i],NULL); vkp.mem[i]=0; }
    vkp.pending[i]=0; }
  for(uint32_t i=0;i<8;i++) if(vkp.rdone[i]){ vkDestroySemaphore(vkp.dev,vkp.rdone[i],NULL); vkp.rdone[i]=0; } }

static VkSemaphore vkp_shared_sem(GLuint* gsem){
  VkExportSemaphoreCreateInfo ex={VKS_EXPORTSEM,NULL,VK_OPAQUE_WIN32}; VkSemaphoreCreateInfo ci={VKS_SEMAPHORE,&ex,0}; VkSemaphore s=0;
  if(vkCreateSemaphore(vkp.dev,&ci,NULL,&s)) return 0;
  VkSemaphoreGetWin32HandleInfoKHR hi={VKS_SEMWIN32HANDLE,NULL,s,VK_OPAQUE_WIN32}; HANDLE h=NULL;
  if(vkGetSemaphoreWin32HandleKHR(vkp.dev,&hi,&h) || !h){ vkDestroySemaphore(vkp.dev,s,NULL); return 0; }
  gx.gs(1,gsem); gx.isw(*gsem,GL_HANDLE_OPAQUE_WIN32,h); if(!g_vkp_kmt) CloseHandle(h); return s; }

static int vkp_make_shared(int i, uint32_t w, uint32_t hgt); static int vkp_targets_ready(void);
static int vkp_create_targets(void){
  VkSurfaceCapabilitiesKHR caps; if(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(vkp.pd,vkp.surf,&caps)) return 0;
  VkExtent2D e=caps.currentExtent; if(e.width==0xFFFFFFFFu){ e.width=(uint32_t)g_ww; e.height=(uint32_t)g_wh; }
  if(!e.width || !e.height) return 0;   /* minimized: try again later */
  uint32_t n=caps.minImageCount+1; if(n<2) n=2; if(caps.maxImageCount && n>caps.maxImageCount) n=caps.maxImageCount; if(n>8) n=8;
  VkSwapchainKHR old=vkp.sc;
  VkSwapchainCreateInfoKHR sci={VKS_SWAPCHAIN,NULL,0,vkp.surf,n,vkp.scfmt,0,e,1,0x2 /*TRANSFER_DST*/,0,0,NULL,
    (caps.supportedTransforms&1)? 1 : caps.currentTransform,(caps.supportedCompositeAlpha&1)? 1 : caps.supportedCompositeAlpha&-caps.supportedCompositeAlpha,vkp.mode,1,old};
  if(vkCreateSwapchainKHR(vkp.dev,&sci,NULL,&vkp.sc)) { vkp.sc=old; return 0; }
  if(old) vkDestroySwapchainKHR(vkp.dev,old,NULL);
  vkp.nsc=8; vkGetSwapchainImagesKHR(vkp.dev,vkp.sc,&vkp.nsc,vkp.scimg); vkp.ext=e;
  VkSemaphoreCreateInfo si={VKS_SEMAPHORE,NULL,0}; for(uint32_t i=0;i<vkp.nsc;i++) if(vkCreateSemaphore(vkp.dev,&si,NULL,&vkp.rdone[i])) return 0;
  GLint prev_tex=0, prev_fb=0; gx.geti(0x8069 /*TEXTURE_BINDING_2D*/,&prev_tex); gx.geti(0x8CA6 /*DRAW_FRAMEBUFFER_BINDING*/,&prev_fb);
  for(int i=0;i<VKP_IMAGES;i++) if(!vkp_make_shared(i,e.width,e.height)) return 0;
  gx.bindtex(GL_TEXTURE_2D,(GLuint)prev_tex); gx.bindfb(GL_FRAMEBUFFER,(GLuint)prev_fb);
  return vkp_targets_ready(); }

/* one image Vulkan allocates and GL renders into, with its two semaphores */
static int vkp_make_shared(int i, uint32_t w, uint32_t hgt){
    VkExternalMemoryImageCreateInfo emi={VKS_EXTMEMIMAGE,NULL,VK_OPAQUE_WIN32};
    VkImageCreateInfo ci={VKS_IMAGE,&emi,0,1 /*2D*/,37 /*R8G8B8A8_UNORM*/,{w,hgt,1},1,1,1,0 /*OPTIMAL*/,0x1|0x4|0x10 /*TRANSFER_SRC|SAMPLED|COLOR_ATTACHMENT*/,0,0,NULL,VKL_UNDEFINED};
    if(vkCreateImage(vkp.dev,&ci,NULL,&vkp.img[i])) return 0;
    VkMemoryRequirements mr; vkGetImageMemoryRequirements(vkp.dev,vkp.img[i],&mr);
    VkMemoryDedicatedAllocateInfo ded={VKS_DEDICATED,NULL,vkp.img[i],0}; VkExportMemoryAllocateInfo exm={VKS_EXPORTMEM,g_vkp_dedicated? &ded : NULL,VK_OPAQUE_WIN32};
    VkMemoryAllocateInfo ai={VKS_MEMALLOC,&exm,mr.size,vkp_memtype(mr.memoryTypeBits,1 /*DEVICE_LOCAL*/)};
    if(vkAllocateMemory(vkp.dev,&ai,NULL,&vkp.mem[i]) || vkBindImageMemory(vkp.dev,vkp.img[i],vkp.mem[i],0)) return 0;
    VkMemoryGetWin32HandleInfoKHR hi={VKS_MEMWIN32HANDLE,NULL,vkp.mem[i],VK_OPAQUE_WIN32}; HANDLE h=NULL;
    if(vkGetMemoryWin32HandleKHR(vkp.dev,&hi,&h) || !h) return 0;
    while(gx.err()){}
    GLint one=1; gx.cmo(1,&vkp.gmem[i]); if(g_vkp_dedicated) gx.mopi(vkp.gmem[i],GL_DEDICATED_MEMORY,&one);
    gx.imw(vkp.gmem[i],(GLuint64)mr.size,GL_HANDLE_OPAQUE_WIN32,h); if(!g_vkp_kmt) CloseHandle(h);
    { GLenum e1=gx.err(); if(e1){ if(!g_vkp_quiet) LOG("vulkan: GL memory import failed (GL error %x, size %llu, type %u, dedicated %d, kmt %d, handle %p)\n",e1,(unsigned long long)mr.size,ai.memoryTypeIndex,g_vkp_dedicated,g_vkp_kmt,h); return 0; } }
    gx.gentex(1,&vkp.gtex[i]); gx.bindtex(GL_TEXTURE_2D,vkp.gtex[i]); gx.tsm2(GL_TEXTURE_2D,1,GL_RGBA8,(GLsizei)w,(GLsizei)hgt,vkp.gmem[i],0);
    { GLenum e2=gx.err(); if(e2){ LOG("vulkan: GL texture on shared memory failed (GL error %x)\n",e2); return 0; } }
    gx.genfb(1,&vkp.gfbo[i]); gx.bindfb(GL_FRAMEBUFFER,vkp.gfbo[i]); gx.fbtex(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,vkp.gtex[i],0);
    { GLenum st=gx.cfs(GL_FRAMEBUFFER); if(st!=GL_FRAMEBUFFER_COMPLETE){ LOG("vulkan: shared image %d is not a complete GL framebuffer (status %x, GL error %x)\n",i,st,gx.err()); return 0; } }
    if(!(vkp.ready[i]=vkp_shared_sem(&vkp.gready[i])) || !(vkp.done[i]=vkp_shared_sem(&vkp.gdone[i]))) return 0;
    return 1; }

static int vkp_targets_ready(void){
  /* new images start UNDEFINED: move them to GENERAL once, the layout both APIs use from now on */
  vkWaitForFences(vkp.dev,1,&vkp.fence[0],1,~0ull); vkResetFences(vkp.dev,1,&vkp.fence[0]);
  VkCommandBuffer cb=vkp.cb[0]; vkResetCommandBuffer(cb,0); VkCommandBufferBeginInfo bi={VKS_CMDBEGIN,NULL,1,NULL}; vkBeginCommandBuffer(cb,&bi);
  VkImageMemoryBarrier b[VKP_IMAGES]; for(int i=0;i<VKP_IMAGES;i++) b[i]=(VkImageMemoryBarrier){VKS_IMGBARRIER,NULL,0,0,VKL_UNDEFINED,VKL_GENERAL,~0u,~0u,vkp.img[i],{1,0,1,0,1}};
  vkCmdPipelineBarrier(cb,0x1,0x2000,0,0,NULL,0,NULL,VKP_IMAGES,b); vkEndCommandBuffer(cb);
  VkSubmitInfo s={VKS_SUBMIT,NULL,0,NULL,NULL,1,&cb,0,NULL}; vkQueueSubmit(vkp.q,1,&s,vkp.fence[0]); vkWaitForFences(vkp.dev,1,&vkp.fence[0],1,~0ull);
  vkp.cur=0; g_vkp_fbo=vkp.gfbo[0];
  return 1; }

static int vkp_recreate(void){
  vkp_destroy_targets(); vkp.recreates++;
  if(!vkp_create_targets()){ g_vkp_fbo=0; return 0; }
  return 1; }

/* called on the GL thread right after the GL context exists (hidden window) */
static int vkp_init(void){
  if(!vkp_load_gl()) return 0;
  HMODULE lib=LoadLibraryA("vulkan-1.dll"); if(!lib) return vkp_fail("vulkan-1.dll not found (update the GPU driver)");
  PFN_GIPA gipa=(PFN_GIPA)(void*)GetProcAddress(lib,"vkGetInstanceProcAddr"); if(!gipa) return vkp_fail("vulkan-1.dll is incomplete");
  *(void**)&vkCreateInstance=(void*)gipa(NULL,"vkCreateInstance");
  VkApplicationInfo app={VKS_APP,NULL,"SwitchRecomp",1,"SwitchRecomp",1,(1u<<22)|(1u<<12)};
  const char* iext[]={"VK_KHR_surface","VK_KHR_win32_surface"};
  VkInstanceCreateInfo ici={VKS_INSTANCE,NULL,0,&app,0,NULL,2,iext};
  if(!vkCreateInstance || vkCreateInstance(&ici,NULL,&vkp.inst)) return vkp_fail("no Vulkan 1.1 driver");
  #define VKI(R,n,a) *(void**)&n=(void*)gipa(vkp.inst,#n); if(!n) return vkp_fail("driver lacks " #n);
  VKFN(VKI)
  #undef VKI
  PFN_GDPA gdpa=(PFN_GDPA)(void*)gipa(vkp.inst,"vkGetDeviceProcAddr"); if(!gdpa) return vkp_fail("driver lacks vkGetDeviceProcAddr");
  /* the Vulkan device must be the GPU the GL context runs on: match the device UUID */
  GLubyte glu[16]={0}; gx.gub(GL_DEVICE_UUID_,0,glu);
  VkPhysicalDevice pds[16]; uint32_t npd=16; vkEnumeratePhysicalDevices(vkp.inst,&npd,pds);
  for(uint32_t i=0;i<npd && !vkp.pd;i++){
    VkPhysicalDeviceIDProperties idp={VKS_IDPROPS,NULL}; VkPhysicalDeviceProperties2 p2={VKS_PROPS2,&idp}; vkGetPhysicalDeviceProperties2(pds[i],&p2);
    if(memcmp(idp.deviceUUID,glu,16)) continue;
    VkQueueFamilyProperties qp[16]; uint32_t nq=16; vkGetPhysicalDeviceQueueFamilyProperties(pds[i],&nq,qp);
    for(uint32_t f=0;f<nq;f++) if(qp[f].queueFlags&1){ vkp.pd=pds[i]; vkp.qf=f; LOG("vulkan: device %s\n",(const char*)p2.props+20); break; } }
  if(!vkp.pd) return vkp_fail("no Vulkan device matches the OpenGL GPU");
  float prio=1.0f; VkDeviceQueueCreateInfo qci={VKS_DEVQUEUE,NULL,0,vkp.qf,1,&prio};
  const char* dext[]={"VK_KHR_swapchain","VK_KHR_external_memory_win32","VK_KHR_external_semaphore_win32"};
  VkDeviceCreateInfo dci={VKS_DEVICE,NULL,0,1,&qci,0,NULL,3,dext,NULL};
  if(vkCreateDevice(vkp.pd,&dci,NULL,&vkp.dev)) return vkp_fail("cannot create the Vulkan device (external memory not supported?)");
  #define VKD(R,n,a) *(void**)&n=(void*)gdpa(vkp.dev,#n); if(!n) return vkp_fail("driver lacks " #n);
  VKDFN(VKD)
  #undef VKD
  vkGetDeviceQueue(vkp.dev,vkp.qf,0,&vkp.q);
  VkCommandPoolCreateInfo cpi={VKS_CMDPOOL,NULL,0x2 /*RESET_COMMAND_BUFFER*/,vkp.qf}; if(vkCreateCommandPool(vkp.dev,&cpi,NULL,&vkp.pool)) return vkp_fail("command pool");
  VkCommandBufferAllocateInfo cai={VKS_CMDALLOC,NULL,vkp.pool,0,2}; if(vkAllocateCommandBuffers(vkp.dev,&cai,vkp.cb)) return vkp_fail("command buffers");
  VkSemaphoreCreateInfo si={VKS_SEMAPHORE,NULL,0}; VkFenceCreateInfo fi={VKS_FENCE,NULL,1 /*SIGNALED*/};
  for(int i=0;i<2;i++) if(vkCreateSemaphore(vkp.dev,&si,NULL,&vkp.acq[i]) || vkCreateFence(vkp.dev,&fi,NULL,&vkp.fence[i])) return vkp_fail("sync objects");
  /* prove GL can render into Vulkan memory on a small offscreen image before touching the window:
     once a Vulkan swapchain owns the window, GL could no longer take it back as a fallback */
  { int ok=0; g_vkp_quiet=1;
    for(int k=0;k<4 && !ok;k++){ g_vkp_kmt=!(k>>1); g_vkp_dedicated=!(k&1); ok=vkp_make_shared(0,64,64); vkp_destroy_targets(); }
    g_vkp_quiet=0;
    if(!ok) return vkp_fail("the driver cannot share images between OpenGL and Vulkan"); }
  VkWin32SurfaceCreateInfoKHR wsi={VKS_WIN32SURFACE,NULL,0,GetModuleHandle(NULL),g_hwnd};
  if(vkCreateWin32SurfaceKHR(vkp.inst,&wsi,NULL,&vkp.surf)) return vkp_fail("cannot create a Vulkan surface for the window");
  { VkBool32 sup=0; vkGetPhysicalDeviceSurfaceSupportKHR(vkp.pd,vkp.qf,vkp.surf,&sup); if(!sup) return vkp_fail("the GPU queue cannot present to this window"); }
  /* B8G8R8A8_UNORM if offered: the GL image holds display-ready (already sRGB-encoded) bytes */
  VkSurfaceFormatKHR fm[64]; uint32_t nf=64; vkGetPhysicalDeviceSurfaceFormatsKHR(vkp.pd,vkp.surf,&nf,fm); vkp.scfmt=nf? fm[0].format : 44;
  for(uint32_t i=0;i<nf;i++) if(fm[i].format==44 || fm[i].format==37){ vkp.scfmt=fm[i].format; if(fm[i].format==44) break; }
  uint32_t pm[16], np=16; vkGetPhysicalDeviceSurfacePresentModesKHR(vkp.pd,vkp.surf,&np,pm); extern int g_opt_vsync;
  vkp.mode=2;   /* FIFO: always available, waits for vsync */
  if(!g_opt_vsync){ for(uint32_t i=0;i<np;i++) if(pm[i]==1) vkp.mode=1; if(vkp.mode!=1) for(uint32_t i=0;i<np;i++) if(pm[i]==0) vkp.mode=0; }   /* MAILBOX, else IMMEDIATE */
  if(!vkp_create_targets()) return vkp_fail("cannot share images between OpenGL and Vulkan");
  vkp.on=1;
  LOG("vulkan: presentation active, %ux%u, %s, %s handles%s\n",vkp.ext.width,vkp.ext.height,vkp.mode==2? "vsync (FIFO)" : vkp.mode==1? "mailbox" : "immediate",g_vkp_kmt? "KMT" : "NT",g_vkp_dedicated? ", dedicated" : "");
  return 1; }

/* replaces SwapBuffers. GL has drawn this frame into gtex[cur]. */
static void vkp_swap(void){
  if(!vkp.sc || !g_vkp_fbo){ if(g_ww>0 && g_wh>0) vkp_recreate(); return; }   /* minimized earlier: retry */
  if((uint32_t)g_ww!=vkp.ext.width || (uint32_t)g_wh!=vkp.ext.height){ if(g_ww>0 && g_wh>0) vkp_recreate(); return; }   /* resized: drop this frame */
  int c=vkp.cur; GLenum lay=GL_LAYOUT_GENERAL_;
  gx.sig(vkp.gready[c],0,NULL,1,&vkp.gtex[c],&lay); gx.flush();   /* the flush submits the signal before Vulkan waits on it */
  uint32_t f=vkp.frame&1; vkp.frame++;
  vkWaitForFences(vkp.dev,1,&vkp.fence[f],1,~0ull); vkResetFences(vkp.dev,1,&vkp.fence[f]);
  uint32_t idx=0; VkResult ar=vkAcquireNextImageKHR(vkp.dev,vkp.sc,~0ull,vkp.acq[f],0,&idx); int have= ar==0 || ar==VK_SUBOPTIMAL;
  VkCommandBuffer cb=vkp.cb[f]; vkResetCommandBuffer(cb,0); VkCommandBufferBeginInfo bi={VKS_CMDBEGIN,NULL,1,NULL}; vkBeginCommandBuffer(cb,&bi);
  if(have){
    VkImageMemoryBarrier b0[2]={{VKS_IMGBARRIER,NULL,0,0x1000 /*TRANSFER_WRITE*/,VKL_UNDEFINED,VKL_TRANSFER_DST,~0u,~0u,vkp.scimg[idx],{1,0,1,0,1}},
                                {VKS_IMGBARRIER,NULL,0x10000 /*MEMORY_WRITE*/,0x800 /*TRANSFER_READ*/,VKL_GENERAL,VKL_GENERAL,~0u,~0u,vkp.img[c],{1,0,1,0,1}}};
    vkCmdPipelineBarrier(cb,0x1000,0x1000,0,0,NULL,0,NULL,2,b0);
    int w=(int)vkp.ext.width, h=(int)vkp.ext.height;
    VkImageBlit r={{1,0,0,1},{{0,h,0},{w,0,1}},{1,0,0,1},{{0,0,0},{w,h,1}}};   /* GL rows are bottom-up: flip */
    vkCmdBlitImage(cb,vkp.img[c],VKL_GENERAL,vkp.scimg[idx],VKL_TRANSFER_DST,1,&r,0 /*NEAREST*/);
    VkImageMemoryBarrier b1={VKS_IMGBARRIER,NULL,0x1000,0,VKL_TRANSFER_DST,VKL_PRESENT,~0u,~0u,vkp.scimg[idx],{1,0,1,0,1}};
    vkCmdPipelineBarrier(cb,0x1000,0x2000,0,0,NULL,0,NULL,1,&b1); }
  vkEndCommandBuffer(cb);
  VkSemaphore ws[2]={vkp.ready[c],vkp.acq[f]}; VkFlags wst[2]={0x1000,0x1000}; VkSemaphore ss[2]={vkp.done[c],vkp.rdone[idx]};
  VkSubmitInfo s={VKS_SUBMIT,NULL,have? 2u : 1u,ws,wst,1,&cb,have? 2u : 1u,ss};   /* without an image we still consume ready / signal done */
  vkQueueSubmit(vkp.q,1,&s,vkp.fence[f]);
  vkp.pending[c]=1;
  if(have){ VkPresentInfoKHR pi={VKS_PRESENT,NULL,1,&vkp.rdone[idx],1,&vkp.sc,&idx,NULL}; VkResult pr=vkQueuePresentKHR(vkp.q,&pi); vkp.presents++;
    if(pr==VK_OUT_OF_DATE || pr==VK_SUBOPTIMAL) ar=VK_OUT_OF_DATE; }
  /* next frame draws into the other image, once Vulkan has finished reading it */
  vkp.cur=c^1; c=vkp.cur;
  if(vkp.pending[c]){ gx.wait(vkp.gdone[c],0,NULL,1,&vkp.gtex[c],&lay); vkp.pending[c]=0; }
  g_vkp_fbo=vkp.gfbo[c];
  if(!have || ar==VK_OUT_OF_DATE) vkp_recreate(); }
