/* ASTC decoded on the GPU (OpenGL compute), included by gpu_gl.c.
   Desktop GPUs have no ASTC, and Prime 4 streams ASTC textures in as you move and turn. Decoded on the CPU, a
   texture's sharp levels came in the background, from a queue that a burst of new textures backed up for up to
   1.4 s: the textures visibly loading in after they were on screen. Here every 16-byte block is decoded by one GPU
   thread straight into a pixel buffer that the texture is filled from, in the same frame, with no CPU pass over
   the pixels. A line-for-line port of astc.c (LDR, 2D blocks, from the Khronos spec). SWR_ASTC_CPU=1: the CPU path */
static const char* ASTC_CS =
"layout(local_size_x=64) in;\n"
"layout(std430,binding=BI) readonly buffer AB { uint blk[]; };\n"
"layout(std430,binding=BO) writeonly buffer AO { uint outp[]; };\n"
"uniform ivec4 u_dimi;   /* level w, h, block w, h */\n"
"uniform ivec4 u_gridi;  /* blocks across, down, slices, row of groups */\n"
"#define u_dim uvec4(u_dimi)\n"
"#define u_grid uvec4(u_gridi)\n"
"uint B4[4];\n"
"uint gb(int pos,int n){ uint v=0u; for(int i=0;i<n;i++){ int p=pos+i; v|=((B4[p>>5]>>uint(p&31))&1u)<<uint(i); } return v; }\n"
"uint gbr(int pos,int n){ uint v=0u; for(int i=0;i<n;i++){ int p=127-(pos+i); v|=((B4[p>>5]>>uint(p&31))&1u)<<uint(i); } return v; }\n"
"const ivec3 QT[21]=ivec3[21](ivec3(1,0,0),ivec3(0,1,0),ivec3(2,0,0),ivec3(0,0,1),ivec3(1,1,0),ivec3(3,0,0),ivec3(1,0,1),ivec3(2,1,0),ivec3(4,0,0),ivec3(2,0,1),ivec3(3,1,0),ivec3(5,0,0),ivec3(3,0,1),ivec3(4,1,0),ivec3(6,0,0),ivec3(4,0,1),ivec3(5,1,0),ivec3(7,0,0),ivec3(5,0,1),ivec3(6,1,0),ivec3(8,0,0));\n"
"int ise_bits(int count,int q){ ivec3 t=QT[q]; return t.x*count+(t.y!=0?(8*count+4)/5:0)+(t.z!=0?(7*count+2)/3:0); }\n"
"int TR[5]; int QQ[3]; int IV[64];\n"
"void dtrits(uint T){ int C;\n"
"  if(((T>>2)&7u)==7u){ C=int((((T>>5)&7u)<<2)|(T&3u)); TR[4]=2; TR[3]=2; }\n"
"  else { C=int(T&0x1Fu); if(((T>>5)&3u)==3u){ TR[4]=2; TR[3]=int((T>>7)&1u); } else { TR[4]=int((T>>7)&1u); TR[3]=int((T>>5)&3u); } }\n"
"  int c0=C&1, c1=(C>>1)&1, c2=(C>>2)&1, c3=(C>>3)&1, c4=(C>>4)&1;\n"
"  if((C&3)==3){ TR[2]=2; TR[1]=c4; TR[0]=(c3<<1)|(c2&~c3&1); }\n"
"  else if(((C>>2)&3)==3){ TR[2]=2; TR[1]=2; TR[0]=C&3; }\n"
"  else { TR[2]=c4; TR[1]=(C>>2)&3; TR[0]=(c1<<1)|(c0&~c1&1); } }\n"
"void dquints(uint Q){\n"
"  if(((Q>>1)&3u)==3u && ((Q>>5)&3u)==0u){ QQ[2]=int(((Q&1u)<<2)|((((Q>>4)&1u)&~(Q&1u))<<1)|(((Q>>3)&1u)&~(Q&1u))); QQ[1]=4; QQ[0]=4; }\n"
"  else { int C;\n"
"    if(((Q>>1)&3u)==3u){ QQ[2]=4; C=int((((Q>>3)&3u)<<3)|((~(Q>>5)&3u)<<1)|(Q&1u)); }\n"
"    else { QQ[2]=int((Q>>5)&3u); C=int(Q&0x1Fu); }\n"
"    if((C&7)==5){ QQ[1]=4; QQ[0]=(C>>3)&3; } else { QQ[1]=(C>>3)&3; QQ[0]=C&7; } } }\n"
"uint GBX(int p,int n,bool rev){ return rev? gbr(p,n) : gb(p,n); }\n"
"void ise(int pos,int count,int q,bool rev){ ivec3 t=QT[q]; int nb=t.x;\n"
"  if(t.y!=0){ const ivec2 tb[5]=ivec2[5](ivec2(0,2),ivec2(2,2),ivec2(4,1),ivec2(5,2),ivec2(7,1));\n"
"    for(int b=0;b<count;b+=5){ uint m[5]; uint T=0u; int n=min(count-b,5);\n"
"      for(int i=0;i<5;i++){ m[i]=0u; if(i<n){ m[i]=GBX(pos,nb,rev); pos+=nb; uint tv=GBX(pos,tb[i].y,rev); pos+=tb[i].y; T|=tv<<uint(tb[i].x); } }\n"
"      dtrits(T); for(int i=0;i<n;i++) IV[b+i]=int(m[i]|(uint(TR[i])<<uint(nb))); } }\n"
"  else if(t.z!=0){ const ivec2 qb[3]=ivec2[3](ivec2(0,3),ivec2(3,2),ivec2(5,2));\n"
"    for(int b=0;b<count;b+=3){ uint m[3]; uint Q=0u; int n=min(count-b,3);\n"
"      for(int i=0;i<3;i++){ m[i]=0u; if(i<n){ m[i]=GBX(pos,nb,rev); pos+=nb; uint qv=GBX(pos,qb[i].y,rev); pos+=qb[i].y; Q|=qv<<uint(qb[i].x); } }\n"
"      dquints(Q); for(int i=0;i<n;i++) IV[b+i]=int(m[i]|(uint(QQ[i])<<uint(nb))); } }\n"
"  else { for(int i=0;i<count;i++){ IV[i]=int(GBX(pos,nb,rev)); pos+=nb; } } }\n"
"int unq_color(int v,int q){ ivec3 t=QT[q]; int nb=t.x;\n"
"  if(t.y==0 && t.z==0){ int x=v; int r=x<<(8-nb); int sh=8-nb; while(sh>0){ sh-=nb; r|= sh>=0? x<<sh : x>>(-sh); } return r&255; }\n"
"  int m=v&((1<<nb)-1), tq=v>>nb; int A=(m&1)!=0?0x1FF:0, B=0, C=0, D=tq;\n"
"  int b=(m>>1)&1, c=(m>>2)&1, dd=(m>>3)&1, e=(m>>4)&1, f=(m>>5)&1;\n"
"  if(t.y!=0){ if(nb==1){C=204;B=0;} else if(nb==2){C=93;B=(b<<8)|(b<<4)|(b<<2)|(b<<1);} else if(nb==3){C=44;B=(c<<8)|(b<<7)|(c<<3)|(b<<2)|(c<<1)|b;}\n"
"    else if(nb==4){C=22;B=(dd<<8)|(c<<7)|(b<<6)|(dd<<2)|(c<<1)|b;} else if(nb==5){C=11;B=(e<<8)|(dd<<7)|(c<<6)|(b<<5)|(e<<1)|dd;} else if(nb==6){C=5;B=(f<<8)|(e<<7)|(dd<<6)|(c<<5)|(b<<4)|f;} }\n"
"  else { if(nb==1){C=113;B=0;} else if(nb==2){C=54;B=(b<<8)|(b<<3)|(b<<2);} else if(nb==3){C=26;B=(c<<8)|(b<<7)|(c<<2)|(b<<1)|c;}\n"
"    else if(nb==4){C=13;B=(dd<<8)|(c<<7)|(b<<6)|(dd<<1)|c;} else if(nb==5){C=6;B=(e<<8)|(dd<<7)|(c<<6)|(b<<5)|e;} }\n"
"  int T2=D*C+B; T2^=A; T2=(A&0x80)|(T2>>2); return T2&255; }\n"
"int unq_weight(int v,int q){ ivec3 t=QT[q]; int nb=t.x; int r;\n"
"  if(t.y==0 && t.z==0){ if(nb==1) r=v!=0?63:0; else if(nb==2) r=(v<<4)|(v<<2)|v; else if(nb==3) r=(v<<3)|v; else if(nb==4) r=(v<<2)|(v>>2); else if(nb==5) r=(v<<1)|(v>>4); else r=v; }\n"
"  else { int m=v&((1<<nb)-1), tq=v>>nb; int A=(m&1)!=0?0x7F:0, B=0, C=0, D=tq; int b=(m>>1)&1, c=(m>>2)&1; bool done=false;\n"
"    if(t.y!=0){ if(nb==0){ r= tq==0?0: tq==1?32:63; done=true; } else if(nb==1){C=50;B=0;} else if(nb==2){C=23;B=(b<<6)|(b<<2)|b;} else if(nb==3){C=11;B=(c<<6)|(b<<5)|(c<<1)|b;} }\n"
"    else { if(nb==0){ r= tq==0?0: tq==1?16: tq==2?32: tq==3?47:63; done=true; } else if(nb==1){C=28;B=0;} else if(nb==2){C=13;B=(b<<6)|(b<<1);} }\n"
"    if(!done){ int T2=D*C+B; T2^=A; T2=(A&0x20)|(T2>>2); r=T2; } }\n"
"  if(r>32) r++; return r; }\n"
"int EP[32];   /* [part][end][rgba] */\n"
"int cl(int v){ return clamp(v,0,255); }\n"
"void bts(inout int a,inout int b){ b>>=1; b|=a&0x80; a>>=1; a&=0x3F; if((a&0x20)!=0) a-=0x40; }\n"
"void put(int p,int e0r,int e0g,int e0b,int e0a,int e1r,int e1g,int e1b,int e1a){ int o=p*8; EP[o]=e0r; EP[o+1]=e0g; EP[o+2]=e0b; EP[o+3]=e0a; EP[o+4]=e1r; EP[o+5]=e1g; EP[o+6]=e1b; EP[o+7]=e1a; }\n"
"void endpoints(int p,int mode,int vi){ int v0=IV[vi],v1=IV[vi+1],v2=IV[vi+2],v3=IV[vi+3],v4=IV[vi+4],v5=IV[vi+5],v6=IV[vi+6],v7=IV[vi+7];\n"
"  if(mode==0) put(p,v0,v0,v0,255,v1,v1,v1,255);\n"
"  else if(mode==1){ int L0=(v0>>2)|(v1&0xC0), L1=min(L0+(v1&0x3F),255); put(p,L0,L0,L0,255,L1,L1,L1,255); }\n"
"  else if(mode==4) put(p,v0,v0,v0,v2,v1,v1,v1,v3);\n"
"  else if(mode==5){ bts(v1,v0); bts(v3,v2); put(p,v0,v0,v0,v2,cl(v0+v1),cl(v0+v1),cl(v0+v1),cl(v2+v3)); }\n"
"  else if(mode==6) put(p,(v0*v3)>>8,(v1*v3)>>8,(v2*v3)>>8,255,v0,v1,v2,255);\n"
"  else if(mode==8){ if(v1+v3+v5>=v0+v2+v4) put(p,v0,v2,v4,255,v1,v3,v5,255); else put(p,(v1+v5)>>1,(v3+v5)>>1,v5,255,(v0+v4)>>1,(v2+v4)>>1,v4,255); }\n"
"  else if(mode==9){ bts(v1,v0); bts(v3,v2); bts(v5,v4);\n"
"    if(v1+v3+v5>=0) put(p,cl(v0),cl(v2),cl(v4),255,cl(v0+v1),cl(v2+v3),cl(v4+v5),255);\n"
"    else { int r0=cl(v0+v1),g0=cl(v2+v3),b0=cl(v4+v5); put(p,cl((r0+b0)>>1),cl((g0+b0)>>1),cl(b0),255,cl((v0+v4)>>1),cl((v2+v4)>>1),cl(v4),255); } }\n"
"  else if(mode==10) put(p,(v0*v3)>>8,(v1*v3)>>8,(v2*v3)>>8,v4,v0,v1,v2,v5);\n"
"  else if(mode==12){ if(v1+v3+v5>=v0+v2+v4) put(p,v0,v2,v4,v6,v1,v3,v5,v7); else put(p,(v1+v5)>>1,(v3+v5)>>1,v5,v7,(v0+v4)>>1,(v2+v4)>>1,v4,v6); }\n"
"  else if(mode==13){ bts(v1,v0); bts(v3,v2); bts(v5,v4); bts(v7,v6);\n"
"    if(v1+v3+v5>=0) put(p,cl(v0),cl(v2),cl(v4),cl(v6),cl(v0+v1),cl(v2+v3),cl(v4+v5),cl(v6+v7));\n"
"    else { int r0=v0+v1,g0=v2+v3,b0=v4+v5; put(p,cl((r0+b0)>>1),cl((g0+b0)>>1),cl(b0),cl(v6+v7),cl((v0+v4)>>1),cl((v2+v4)>>1),cl(v4),cl(v6)); } }\n"
"  else put(p,255,0,255,255,255,0,255,255); }\n"
"uint hash52(uint p){ p^=p>>15; p-=p<<17; p+=p<<7; p+=p<<4; p^=p>>5; p+=p<<16; p^=p>>7; p^=p>>3; p^=p<<6; p^=p>>17; return p; }\n"
"int sel_part(int seed,int x,int y,int count,bool small){ if(small){ x<<=1; y<<=1; } seed+=(count-1)*1024; uint rnum=hash52(uint(seed));\n"
"  uint s1=rnum&15u,s2=(rnum>>4)&15u,s3=(rnum>>8)&15u,s4=(rnum>>12)&15u,s5=(rnum>>16)&15u,s6=(rnum>>20)&15u,s7=(rnum>>24)&15u,s8=(rnum>>28)&15u;\n"
"  s1*=s1; s2*=s2; s3*=s3; s4*=s4; s5*=s5; s6*=s6; s7*=s7; s8*=s8;\n"
"  int sh1,sh2; if((seed&1)!=0){ sh1=(seed&2)!=0?4:5; sh2=count==3?6:5; } else { sh1=count==3?6:5; sh2=(seed&2)!=0?4:5; }\n"
"  s1>>=uint(sh1); s2>>=uint(sh2); s3>>=uint(sh1); s4>>=uint(sh2); s5>>=uint(sh1); s6>>=uint(sh2); s7>>=uint(sh1); s8>>=uint(sh2);\n"
"  uint X=uint(x), Y=uint(y);\n"
"  uint a=(s1*X+s2*Y+(rnum>>14))&63u, b=(s3*X+s4*Y+(rnum>>10))&63u, c=(s5*X+s6*Y+(rnum>>6))&63u, d=(s7*X+s8*Y+(rnum>>2))&63u;\n"
"  if(count<4) d=0u; if(count<3) c=0u;\n"
"  if(a>=b && a>=c && a>=d) return 0; if(b>=c && b>=d) return 1; if(c>=d) return 2; return 3; }\n"
"bool bmode(uint bm,out int gw,out int gh,out int dual,out int wq){ int R; int H=int((bm>>9)&1u), D=int((bm>>10)&1u); int A,B; gw=0; gh=0; dual=0; wq=0;\n"
"  if((bm&3u)==0u){ R=int(((bm>>4)&1u)|((bm>>1)&6u)); A=int((bm>>5)&3u); B=int((bm>>9)&3u); uint k=(bm>>7)&3u;\n"
"    if(k==0u){ gw=12; gh=A+2; } else if(k==1u){ gw=A+2; gh=12; } else if(k==2u){ gw=A+6; gh=B+6; D=0; H=0; }\n"
"    else { uint k2=(bm>>5)&3u; if(k2==0u){ gw=6; gh=10; } else if(k2==1u){ gw=10; gh=6; } else return false; } }\n"
"  else { R=int(((bm>>4)&1u)|((bm&3u)<<1)); A=int((bm>>5)&3u); B=int((bm>>7)&3u); uint k=(bm>>2)&3u;\n"
"    if(k==0u){ gw=B+4; gh=A+2; } else if(k==1u){ gw=B+8; gh=A+2; } else if(k==2u){ gw=A+2; gh=B+8; }\n"
"    else { if(((bm>>8)&1u)!=0u){ gw=int((bm>>7)&1u)+2; gh=A+2; } else { gw=A+2; gh=int((bm>>7)&1u)+6; } } }\n"
"  if(R<2) return false; wq=(H!=0? 6 : 0)+R-2; dual=D; return true; }\n"
"void main(){\n"
"  uint id=gl_GlobalInvocationID.x+gl_GlobalInvocationID.y*u_grid.w*64u; uint per=u_grid.x*u_grid.y; if(id>=per*u_grid.z) return;\n"
"  uint sl=id/per, r=id%per, bxi=r%u_grid.x, byi=r/u_grid.x; int bw=int(u_dim.z), bh=int(u_dim.w);\n"
"  for(int i=0;i<4;i++) B4[i]=blk[id*4u+uint(i)];\n"
"  uint base=sl*u_dim.x*u_dim.y; uint px0=bxi*uint(bw), py0=byi*uint(bh);\n"
"  #define STORE(s,t,col) { uint X_=px0+uint(s), Y_=py0+uint(t); if(X_<u_dim.x && Y_<u_dim.y) outp[base+Y_*u_dim.x+X_]=(col); }\n"
"  uint bm=gb(0,11);\n"
"  if((bm&0x1FFu)==0x1FCu){ uint col=(gb(64,16)>>8)|((gb(80,16)>>8)<<8)|((gb(96,16)>>8)<<16)|((gb(112,16)>>8)<<24);\n"
"    for(int t=0;t<bh;t++) for(int s=0;s<bw;s++) STORE(s,t,col); return; }\n"
"  int gw,gh,dual,wq; bool ok=bmode(bm,gw,gh,dual,wq) && gw<=bw && gh<=bh;\n"
"  int nparts=int(gb(11,2))+1; int nw=gw*gh*(dual!=0?2:1); int wbits=ok? ise_bits(nw,wq) : 0;\n"
"  if(!ok || nw>64 || wbits<24 || wbits>96 || (nparts==4 && dual!=0)){ for(int t=0;t<bh;t++) for(int s=0;s<bw;s++) STORE(s,t,0xFFFF00FFu); return; }\n"
"  int cem[4]; int pos; int seed=0; int xcb=0;\n"
"  if(nparts==1){ cem[0]=int(gb(13,4)); pos=17; }\n"
"  else { seed=int(gb(13,10)); uint cv=gb(23,6);\n"
"    if((cv&3u)==0u){ for(int i=0;i<4;i++) cem[i]=int(cv>>2); pos=29; }\n"
"    else { int base2=int((cv&3u)-1u)*4; xcb=3*nparts-4; uint ex=gb(128-wbits-xcb,xcb); uint cm=(cv>>2)|(ex<<4); int c[4]; int m[4];\n"
"      for(int i=0;i<nparts;i++){ c[i]=int(cm&1u); cm>>=1; } for(int i=0;i<nparts;i++){ m[i]=int(cm&3u); cm>>=2; }\n"
"      for(int i=0;i<nparts;i++) cem[i]=base2+c[i]*4+m[i]; pos=29; } }\n"
"  int nv=0; for(int i=0;i<nparts;i++) nv+=((cem[i]>>2)+1)*2;\n"
"  if(nv>18){ for(int t=0;t<bh;t++) for(int s=0;s<bw;s++) STORE(s,t,0xFFFF00FFu); return; }\n"
"  int avail=128-wbits-pos-xcb-(dual!=0?2:0); int cq=-1; for(int q=20;q>=0;q--){ if(ise_bits(nv,q)<=avail){ cq=q; break; } }\n"
"  if(cq<0){ for(int t=0;t<bh;t++) for(int s=0;s<bw;s++) STORE(s,t,0xFFFF00FFu); return; }\n"
"  ise(pos,nv,cq,false); for(int i=0;i<nv;i++) IV[i]=unq_color(IV[i],cq);\n"
"  int p2=-1; if(dual!=0) p2=int(gb(128-wbits-xcb-2,2));\n"
"  { int vi=0; for(int i=0;i<nparts;i++){ endpoints(i,cem[i],vi); vi+=((cem[i]>>2)+1)*2; } }\n"
"  ise(0,nw,wq,true); int WV[64]; for(int i=0;i<nw;i++) WV[i]=unq_weight(IV[i],wq);\n"
"  int Ds=(1024+bw/2)/(bw-1), Dt=(1024+bh/2)/(bh-1); int np=dual!=0?2:1; bool small=bw*bh<31;\n"
"  for(int t=0;t<bh;t++) for(int s=0;s<bw;s++){\n"
"    int cs=Ds*s, ct=Dt*t; int gs=(cs*(gw-1)+32)>>6, gt=(ct*(gh-1)+32)>>6; int js=gs>>4, fs=gs&15, jt=gt>>4, ft=gt&15;\n"
"    int w11=(fs*ft+8)>>4, w10=ft-w11, w01=fs-w11, w00=16-fs-ft+w11; int pw[2]; pw[1]=0;\n"
"    for(int pl=0;pl<np;pl++){ int v0=js+jt*gw; int p00=WV[v0*np+pl]; int p01=js+1<gw? WV[(v0+1)*np+pl] : 0; int p10=jt+1<gh? WV[(v0+gw)*np+pl] : 0;\n"
"      int p11=(js+1<gw && jt+1<gh)? WV[(v0+gw+1)*np+pl] : 0; pw[pl]=(p00*w00+p01*w01+p10*w10+p11*w11+8)>>4; }\n"
"    int part= nparts>1? sel_part(seed,s,t,nparts,small) : 0; uint col=0u;\n"
"    for(int c=0;c<4;c++){ int w=(dual!=0 && c==p2)? pw[1] : pw[0]; int e0=EP[part*8+c], e1=EP[part*8+4+c];\n"
"      int C=((e0*257)*(64-w)+(e1*257)*w+32)>>6; col|=uint((C>>8)&255)<<uint(c*8); }\n"
"    STORE(s,t,col); } }\n";

static GLuint g_astc_prog, g_astc_buf[2]; static GLint g_astc_bi, g_astc_bo; static GLint g_astc_ldim=-1, g_astc_lgrid=-1; static GLsizeiptr g_astc_cap[2]; static int g_astc_gpu=-1;
static int astc_gpu_on(void){
  if(g_astc_gpu>=0) return g_astc_gpu; g_astc_gpu=0;
  if(g_gles || getenv("SWR_ASTC_CPU")) return 0;
  GLint mb=0; glGetIntegerv(0x90DD /* GL_MAX_SHADER_STORAGE_BUFFER_BINDINGS */,&mb);
  if(mb<8){ LOG("textures: %d storage buffer bindings: ASTC decoded on the CPU\n",mb); return 0; }
  g_astc_bi=mb-2; g_astc_bo=mb-1;   /* the last two: the draws' own buffers use the low ones */
  size_t sl=strlen(ASTC_CS)+96; char* full=malloc(sl); if(!full) return 0;
  snprintf(full,sl,"#version 430\n#define BI %d\n#define BO %d\n%s",g_astc_bi,g_astc_bo,ASTC_CS);
  char err[4096]; GLuint sh=compile(GL_COMPUTE_SHADER,full,err,sizeof err); free(full);
  if(!sh){ LOG("textures: the GPU ASTC decoder did not compile (%.400s): decoded on the CPU\n",err); return 0; }
  GLuint pr=glCreateProgram(); glAttachShader(pr,sh); glLinkProgram(pr); glDeleteShader(sh); GLint ok=0; glGetProgramiv(pr,GL_LINK_STATUS,&ok);
  if(!ok){ glGetProgramInfoLog(pr,sizeof err,NULL,err); LOG("textures: the GPU ASTC decoder did not link (%.400s): decoded on the CPU\n",err); glDeleteProgram(pr); return 0; }
  g_astc_prog=pr; g_astc_ldim=glGetUniformLocation(pr,"u_dimi"); g_astc_lgrid=glGetUniformLocation(pr,"u_gridi"); glGenBuffers(2,g_astc_buf);
  LOG("textures: ASTC decoded on the GPU (compute), as each texture loads\n"); g_astc_gpu=1; return 1; }
/* decodes ns slices of lw x lh (linear blocks at lin) into a pixel buffer and leaves that buffer bound as the unpack
   buffer: the caller's glTexSubImage with a NULL pointer fills the texture from it */
static void astc_gpu_decode(const u8* lin, u32 lw, u32 lh, u32 ns, u32 bw, u32 bh){
  u32 bx=(lw+bw-1)/bw, by=(lh+bh-1)/bh; u64 nb=(u64)bx*by*ns; GLsizeiptr inb=(GLsizeiptr)(nb*16), outb=(GLsizeiptr)((u64)lw*lh*ns*4);
  GLint prev=0; glGetIntegerv(0x8B8D /* GL_CURRENT_PROGRAM */,&prev);
  glBindBuffer(GL_SHADER_STORAGE_BUFFER,g_astc_buf[0]); glBufferData(GL_SHADER_STORAGE_BUFFER,inb,lin,GL_STREAM_DRAW);   /* orphaned each time: no wait on the last decode */
  glBindBuffer(GL_SHADER_STORAGE_BUFFER,g_astc_buf[1]); glBufferData(GL_SHADER_STORAGE_BUFFER,outb,NULL,GL_STREAM_COPY);
  glBindBuffer(GL_SHADER_STORAGE_BUFFER,0);
  glBindBufferBase(GL_SHADER_STORAGE_BUFFER,(GLuint)g_astc_bi,g_astc_buf[0]); glBindBufferBase(GL_SHADER_STORAGE_BUFFER,(GLuint)g_astc_bo,g_astc_buf[1]);
  glUseProgram(g_astc_prog);
  u64 groups=(nb+63)/64; u32 gx= groups>32768? 32768 : (u32)groups, gy=(u32)((groups+gx-1)/gx);
  { GLint d[4]={(GLint)lw,(GLint)lh,(GLint)bw,(GLint)bh}, gr[4]={(GLint)bx,(GLint)by,(GLint)ns,(GLint)gx}; glUniform4iv(g_astc_ldim,1,d); glUniform4iv(g_astc_lgrid,1,gr); }
  glDispatchCompute(gx,gy,1);
  glMemoryBarrier(0x80 /* GL_PIXEL_BUFFER_BARRIER_BIT */);
  glUseProgram((GLuint)prev);
  { /* test only: SWR_ASTC_VERIFY=N compares the first N decodes with the CPU decoder, pixel for pixel */
    static int left=-1; if(left<0){ const char* e=getenv("SWR_ASTC_VERIFY"); left= e? atoi(e) : 0; }
    if(left>0){ left--; extern void astc_decode_image(const u8*,u32,u32,u32,u32,u8*); u64 npx=(u64)lw*lh*ns;
      u32* g=malloc(npx*4), *c=malloc(npx*4);
      if(g && c){ glBindBuffer(GL_SHADER_STORAGE_BUFFER,g_astc_buf[1]); glGetBufferSubData(GL_SHADER_STORAGE_BUFFER,0,(GLsizeiptr)(npx*4),g); glBindBuffer(GL_SHADER_STORAGE_BUFFER,0);
        for(u32 z=0;z<ns;z++) astc_decode_image(lin+(u64)z*bx*by*16,lw,lh,bw,bh,(u8*)(c+(u64)z*lw*lh));
        u64 bad=0, first=~0ull; for(u64 k=0;k<npx;k++) if(g[k]!=c[k]){ if(!bad) first=k; bad++; }
        if(bad) LOG("textures: ASTC GPU check %ux%ux%u block %ux%u: %llu of %llu pixels differ (first at %llu: GPU %08x CPU %08x)\n",lw,lh,ns,bw,bh,(unsigned long long)bad,(unsigned long long)npx,(unsigned long long)first,g[first],c[first]);
        else LOG("textures: ASTC GPU check %ux%ux%u block %ux%u: identical to the CPU decoder\n",lw,lh,ns,bw,bh); }
      free(g); free(c); } }
  glBindBuffer(GL_PIXEL_UNPACK_BUFFER,g_astc_buf[1]); }
