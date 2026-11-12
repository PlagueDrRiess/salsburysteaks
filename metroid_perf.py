# Title-specific fixes for SwitchRecomp by MZ Labs; Apache-2.0.
# Exact NSO build IDs and layouts guard the guest-code transformations.
MAIN_ID = bytes.fromhex('646761f643afebb379edd5e6a5151af2cef93dc1')
SDK_ID = bytes.fromhex('2d369d9999e3e10e665106f840ffff9bdaa0bb8e')
OLD_WAIT = 'L_8229E24:\n if(!((c->x[0]>>0)&1)) goto L_8229E34;\n c->x[1]=(u64)((u32)(50));\n c->x[0]=(u64)(c->x[19]);\n c->x[30]=136486452; F_82252D8(c,136467160); if(UNLIKELY(c->pc!=136486452)) return;\n'
NEW_WAIT = 'L_8229E24:\n if(!((c->x[0]>>0)&1)) goto L_8229E34;\n {\n   int release_manager=!getenv("SWR_NO_VIDEO_WAIT_UNLOCK") && RD64(c->x[29]+8)==0x82248a8ull;\n   if(release_manager){\n     c->x[0]=c->x[19]+2400;\n     c->x[30]=136486452; F_806B920(c,134658336); if(UNLIKELY(c->pc!=136486452)) return;\n   }\n   c->x[1]=50; c->x[0]=c->x[19];\n   c->x[30]=136486452; F_82252D8(c,136467160); if(UNLIKELY(c->pc!=136486452)) return;\n   if(release_manager){\n     c->x[0]=c->x[19]+2400;\n     c->x[30]=136486452; F_806B91C(c,134658332); if(UNLIKELY(c->pc!=136486452)) return;\n   }\n }\n'

SUBSDK_ID = bytes.fromhex("507f19d1240d09d00992248e8a0fd77ebd784eb4")
OLD_GLOBAL_WAIT = ' c->x[0]=(u64)(c->x[24]);\n c->x[1]=(u64)(c->x[23]);\n c->x[2]=(u64)(c->x[25]);\n c->x[30]=2058641724; F_7AD5CC30(c,2060831792); if(UNLIKELY(c->pc!=2058641724)) return;\n'
NEW_GLOBAL_WAIT = ' {\n   int release_global=0;u64 global=0,fp=c->x[29];\n   if(!getenv("SWR_NO_VIDEO_GLOBAL_WAIT_UNLOCK")){\n     for(int depth=0;depth<4&&fp;depth++){\n       if(RD64(fp+8)==0x822bcc8ull){release_global=1;break;}\n       u64 next=RD64(fp);if(next<=fp)break;fp=next;\n     }\n     global=RD64(164405248ull+3416);\n     release_global=release_global&&global&&RD32(global+8)==1&&RD64(global+16)==RD64(c->tpidrro+504);\n   }\n   if(release_global){\n     CPU before=*c;c->x[30]=2058641724;F_815EBE0(c,135654368);if(UNLIKELY(c->pc!=2058641724))return;*c=before;\n   }\n   c->x[0]=c->x[24];c->x[1]=c->x[23];c->x[2]=c->x[25];\n   c->x[30]=2058641724;F_7AD5CC30(c,2060831792);if(UNLIKELY(c->pc!=2058641724))return;\n   if(release_global){\n     CPU after=*c;\n     /* Reacquire outer before inner: preserve the original lock order. The\n        existing predicate check below handles changes while inner is unlocked. */\n     c->x[0]=after.x[23];c->x[30]=2058641724;F_7AD5AF20(c,2060824352);if(UNLIKELY(c->pc!=2058641724))return;\n     c->x[30]=2058641724;F_815EBD0(c,135654352);if(UNLIKELY(c->pc!=2058641724))return;\n     c->x[0]=after.x[23];c->x[30]=2058641724;F_7AD5AF00(c,2060824320);if(UNLIKELY(c->pc!=2058641724))return;\n     *c=after;\n   }\n }\n'

MAIN_ID_110 = bytes.fromhex('8275fbe1ec5191c279f3a943ad8524f4bab087a3')   # Metroid Prime 4 Beyond v1.1.0
# The projection builder (main+0x4A820) halves the field of view with fmov s1,#0.5 at main+0x4A858 before tan(): the
# runtime's fov_scale scales that half angle for the camera's own projections (the [Gameplay] fov setting). The callers
# it widens are the ones the community "Wider FOV" mod picked (by return address), so shadow and light projections
# keep theirs.
OLD_FOV = (' SETSB(1,1056964608);\n SETS(0,c->v[0].f[0]*c->v[1].f[0]);\n'
           ' c->x[30]=134547556; F_9393E00(c,154746368);')
NEW_FOV = (' SETSB(1,1056964608);\n if(c->rt->fov_scale) c->v[1].f[0]*=c->rt->fov_scale(c,c->x[30]);\n'
           ' SETS(0,c->v[0].f[0]*c->v[1].f[0]);\n c->x[30]=134547556; F_9393E00(c,154746368);')

def apply(text, mods, say):
    main = next((m for m in mods if m.name == 'main'), None)
    if main is not None and main.base == 0x8006000 and main.build_id.rstrip(b'\0') == MAIN_ID_110 and OLD_FOV in text:
        if text.count(OLD_FOV) != 1:
            raise ValueError('Metroid projection source changed; review the field of view hook before rebuilding')
        text = text.replace(OLD_FOV, NEW_FOV)
        say('  applied the field of view hook (v1.1.0)')
    sdk = next((m for m in mods if m.name == 'sdk'), None)
    if (main is None or sdk is None or main.base != 0x8005000 or sdk.base != 0x7b0c6000
        or main.build_id.rstrip(b'\0') != MAIN_ID or sdk.build_id.rstrip(b'\0') != SDK_ID):
        return text
    if 'L_8229E24:' in text:
        if text.count(OLD_WAIT) != 1:
            raise ValueError('Metroid video wait source changed; review the lock-scope fix before rebuilding')
        text = text.replace(OLD_WAIT, NEW_WAIT)
        say('  applied verified Metroid video completion lock scope')
    old = 'void F_7B3706D0(CPU* RESTRICT c, u64 e){\n u8* const M=c->mem;'
    if old in text:
        text = text.replace(old, 'SWR_FN(F_7B3706D0)')
        say('  applied verified Metroid inflate memory-alias optimization')
    multimedia = next((m for m in mods if m.name == 'subsdk0'), None)
    if ('L_7AB46124:' in text and multimedia is not None and multimedia.base == 0x7aa1f000
        and multimedia.build_id.rstrip(b'\0') == SUBSDK_ID):
        if text.count(OLD_GLOBAL_WAIT) != 1:
            raise ValueError('Metroid media wait source changed; review global rendering lock scope before rebuilding')
        text = text.replace(OLD_GLOBAL_WAIT, NEW_GLOBAL_WAIT)
        say('  applied verified Metroid global rendering wait lock scope')
    return text
