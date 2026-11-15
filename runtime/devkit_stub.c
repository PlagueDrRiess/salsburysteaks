/* Developer tools are not part of this build: the diagnostic hotkeys stay off and the menu has no tools page */
#include "rt.h"

volatile u64 g_dk_camera;
int dk_devmode(void){ return 0; }
void dk_tick(void){}
void dk_page_draw(void){}
