#include <pspgu.h>
#include <pspgum.h>
#include <pspdisplay.h>
#include <pspkernel.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "sim.h"
#include "font.h"
#include "render_psp.h"

#define BUF_WIDTH 512
#define SW 480
#define SH 272
#define VRAM_FRAME (BUF_WIDTH * SH * 4)
#define MAX_3D 54000
#define MAX_2D 25000
#define VERTEX_TYPE (GU_TEXTURE_32BITF | GU_COLOR_8888 | GU_VERTEX_32BITF | GU_TRANSFORM_3D)
typedef struct {uint32_t c;float x,y,z;} Vertex;
typedef struct {float u,v;uint32_t c;float x,y,z;} TexVertex;
static unsigned int __attribute__((aligned(16))) command_list[262144];
static TexVertex __attribute__((aligned(16))) mesh[MAX_3D];
static Vertex __attribute__((aligned(16))) overlay[MAX_2D];
/* Actual 32x32 RGBA bitmap detail map; 4KiB, generated once, then shared by
   grass, asphalt, foliage, bark and architecture using dominant-plane UVs. */
static uint32_t __attribute__((aligned(16))) detail_texture[32*32];
static int nmesh,nover;
static float cx,cz;
static float player_ground;
static float sunlight=1.0f;
static int weather,material_detail,material_emissive,dark_lights;
/* CPU clipping is essential on the PSP: a ground triangle spanning behind the
   eye can otherwise be rejected by the GE, leaving a sky-coloured hole. */
static ScePspFVector3 camera_eye,camera_forward,camera_right,camera_up;
typedef struct {float x,y,z,d,length;} ClipPlane;
static ClipPlane clip_planes[5];
static float texture_origin_x,texture_origin_z;
static float plane_distance(const ClipPlane *p,float x,float y,float z){
 return p->x*x+p->y*y+p->z*z+p->d;
}
static void prepare_planes(void){
 texture_origin_x=cx-floorf(cx/128)*128;texture_origin_z=cz-floorf(cz/128)*128;
 for(int i=0;i<5;i++){
  ClipPlane *p=&clip_planes[i];
  if(i==0){p->x=camera_forward.x;p->y=camera_forward.y;p->z=camera_forward.z;}
  else{
   const ScePspFVector3 *basis=i<=2?&camera_right:&camera_up;
   float tangent=i<=2?1.205f:.683f,sign=(i==1||i==3)?1:-1;
   p->x=camera_forward.x*tangent+basis->x*sign;
   p->y=camera_forward.y*tangent+basis->y*sign;
   p->z=camera_forward.z*tangent+basis->z*sign;
  }
  p->d=-(p->x*camera_eye.x+p->y*camera_eye.y+p->z*camera_eye.z)-(i==0?.82f:0);
  p->length=sqrtf(p->x*p->x+p->y*p->y+p->z*p->z);
 }
}
/* Conservative world-space sphere test before trigonometry and mesh work. */
static int visible(float x,float y,float z,float radius){
 x-=cx;y-=player_ground;z-=cz;
 for(int i=0;i<5;i++)if(plane_distance(&clip_planes[i],x,y,z)<-radius*clip_planes[i].length)return 0;
 return 1;
}
static uint32_t day_color(uint32_t c){
 int r=(int)(((c)&255u)*sunlight), g=(int)(((c>>8)&255u)*sunlight),b=(int)(((c>>16)&255u)*sunlight);
 return 0xff000000u|((uint32_t)b<<16)|((uint32_t)g<<8)|(uint32_t)r;
}
#define RGB(r,g,b) (0xff000000u|((uint32_t)(b)<<16)|((uint32_t)(g)<<8)|(uint32_t)(r))
#define BLACK RGB(18,25,27)
#define WHITE RGB(234,238,232)
static float rh_absf(float a){return a<0?-a:a;}
static float distance(float x,float z){float dx=x-cx,dz=z-cz;return sqrtf(dx*dx+dz*dz);}
static unsigned hash(unsigned v){v=(v^61u)^(v>>16);v*=9;v^=v>>4;v*=0x27d4eb2du;return v^(v>>15);}
static unsigned cell_hash(int x,int z){return hash((unsigned)x*101177u ^ (unsigned)z*909091u);}
static void put(Vertex *v,int *count,int limit,float x,float y,float z,uint32_t c){
 if(*count>=limit)return;
 Vertex *p=&v[(*count)++];p->c=c;p->x=x;p->y=y;p->z=z;
}
static void mtri(float x1,float y1,float z1,float x2,float y2,float z2,
 float x3,float y3,float z3,uint32_t c){
 /* Trivial reject/accept needs only precomputed plane dot products. Clipping
    interpolation and polygon copies happen solely on boundary triangles. */
 float x[3]={x1-cx,x2-cx,x3-cx},y[3]={y1-player_ground,y2-player_ground,y3-player_ground},
       z[3]={z1-cz,z2-cz,z3-cz};
 unsigned crossing=0;
 for(int plane=0;plane<5;plane++){
  const ClipPlane *p=&clip_planes[plane];int outside=0;
  outside+=plane_distance(p,x[0],y[0],z[0])<0;
  outside+=plane_distance(p,x[1],y[1],z[1])<0;
  outside+=plane_distance(p,x[2],y[2],z[2])<0;
  if(outside==3)return;
  if(outside)crossing|=1u<<plane;
 }
 float ux=x2-x1,uy=y2-y1,uz=z2-z1,vx=x3-x1,vy=y3-y1,vz=z3-z1;
 float nx=rh_absf(uy*vz-uz*vy),ny=rh_absf(uz*vx-ux*vz),nz=rh_absf(ux*vy-uy*vx);
 TexVertex in[12],out[12];if(!material_emissive)c=day_color(c);
 for(int i=0;i<3;i++){
  in[i].x=x[i];in[i].y=y[i];in[i].z=z[i];in[i].c=c;
  if(!material_detail){in[i].u=.5f;in[i].v=.5f;}
  else if(ny>=nx&&ny>=nz){in[i].u=(x[i]+texture_origin_x)*8;in[i].v=(z[i]+texture_origin_z)*8;}
  else if(nx>nz){in[i].u=(z[i]+texture_origin_z)*8;in[i].v=(y[i]+player_ground)*8;}
  else{in[i].u=(x[i]+texture_origin_x)*8;in[i].v=(y[i]+player_ground)*8;}
 }
 if(!crossing){
  if(nmesh>MAX_3D-3)return;
  mesh[nmesh++]=in[0];mesh[nmesh++]=in[1];mesh[nmesh++]=in[2];return;
 }
 int count=3;TexVertex *polygon=in,*result=out;
 for(int plane=0;plane<5&&count;plane++){
  if(!(crossing&(1u<<plane)))continue;
  const ClipPlane *p=&clip_planes[plane];int next=0;
  float da=plane_distance(p,polygon[count-1].x,polygon[count-1].y,polygon[count-1].z);
  for(int i=0;i<count;i++){
   const TexVertex *a=&polygon[(i?i:count)-1],*b=&polygon[i];
   float db=plane_distance(p,b->x,b->y,b->z);
   if((da>=0)!=(db>=0)){
    float t=da/(da-db);
    result[next++]=(TexVertex){a->u+(b->u-a->u)*t,a->v+(b->v-a->v)*t,c,
       a->x+(b->x-a->x)*t,a->y+(b->y-a->y)*t,a->z+(b->z-a->z)*t};
   }
   if(db>=0)result[next++]=*b;da=db;
  }
  count=next;TexVertex *swap=polygon;polygon=result;result=swap;
 }
 for(int i=1;i+1<count;i++){
  if(nmesh>MAX_3D-3)return;
  mesh[nmesh++]=polygon[0];mesh[nmesh++]=polygon[i];mesh[nmesh++]=polygon[i+1];
 }
}
static void quad(float x1,float y1,float z1,float x2,float y2,float z2,
 float x3,float y3,float z3,float x4,float y4,float z4,uint32_t c){
 mtri(x1,y1,z1,x2,y2,z2,x3,y3,z3,c);
 mtri(x1,y1,z1,x3,y3,z3,x4,y4,z4,c);
}
static void flat_quad(float x1,float z1,float x2,float z2,float yadd,uint32_t color){
 if(!visible((x1+x2)*.5f,player_ground,(z1+z2)*.5f,rh_absf(x2-x1)+rh_absf(z2-z1)+20))return;
 float y1=rh_height(x1,z1)+yadd,y2=rh_height(x2,z1)+yadd;
 float y3=rh_height(x2,z2)+yadd,y4=rh_height(x1,z2)+yadd;
 quad(x1,y1,z1,x2,y2,z1,x2,y3,z2,x1,y4,z2,color);
}
static float rotation_angle=1e30f,rotation_s,rotation_c;
static void rotation(float a){
 if(a!=rotation_angle){rotation_angle=a;rotation_s=a==0?0:sinf(a);rotation_c=a==0?1:cosf(a);}
}
static void box3(float x,float y,float z,float w,float h,float l,float a,
 uint32_t front,uint32_t side,uint32_t roof){
 if(!visible(x,y+h*.5f,z,(w+h+l)*.5f))return;
 rotation(a);float sx=rotation_s,co=rotation_c,hx=w*.5f,hz=l*.5f;
 float X[4]={-hx,hx,hx,-hx},Z[4]={-hz,-hz,hz,hz},wx[4],wz[4];
 for(int i=0;i<4;i++){wx[i]=x+X[i]*co+Z[i]*sx;wz[i]=z-X[i]*sx+Z[i]*co;}
 // Opaque boxes only emit the faces visible from this eye. This avoids
 // generating and clipping the hidden half of every building and vehicle.
 float ex=cx+camera_eye.x-x,ez=cz+camera_eye.z-z;
 float lx=ex*co-ez*sx,lz=ex*sx+ez*co;
 if(lz<=-hz)quad(wx[0],y,wz[0],wx[1],y,wz[1],wx[1],y+h,wz[1],wx[0],y+h,wz[0],side);
 if(lx>=hx)quad(wx[1],y,wz[1],wx[2],y,wz[2],wx[2],y+h,wz[2],wx[1],y+h,wz[1],side);
 if(lz>=hz)quad(wx[2],y,wz[2],wx[3],y,wz[3],wx[3],y+h,wz[3],wx[2],y+h,wz[2],front);
 if(lx<=-hx)quad(wx[3],y,wz[3],wx[0],y,wz[0],wx[0],y+h,wz[0],wx[3],y+h,wz[3],side);
 if(player_ground+camera_eye.y>=y+h)quad(wx[0],y+h,wz[0],wx[1],y+h,wz[1],wx[2],y+h,wz[2],wx[3],y+h,wz[3],roof);
}
/* Geometry remains procedural: no missing texture/asset dependencies. */
static void local_quad(float x,float y,float z,float a,
 float x1,float y1,float z1,float x2,float y2,float z2,
 float x3,float y3,float z3,float x4,float y4,float z4,uint32_t c){
 rotation(a);float si=rotation_s,co=rotation_c;
 quad(x+x1*co+z1*si,y+y1,z-x1*si+z1*co,
      x+x2*co+z2*si,y+y2,z-x2*si+z2*co,
      x+x3*co+z3*si,y+y3,z-x3*si+z3*co,
      x+x4*co+z4*si,y+y4,z-x4*si+z4*co,c);
}
static void local_box(float x,float y,float z,float dx,float dy,float dz,float angle,
 float w,float h,float l,uint32_t a,uint32_t b,uint32_t c){
 rotation(angle);float s=rotation_s,co=rotation_c;
 box3(x+dx*co+dz*s,y+dy,z-dx*s+dz*co,w,h,l,angle,a,b,c);
}
typedef struct {int x,z;float height;int valid;} TerrainHeight;
static TerrainHeight terrain_heights[32*32];
static float terrain_height(int x,int z){
 TerrainHeight *h=&terrain_heights[((unsigned)x&31u)*32+((unsigned)z&31u)];
 if(!h->valid||h->x!=x||h->z!=z){h->x=x;h->z=z;h->height=rh_height(x*72.0f,z*72.0f);h->valid=1;}
 return h->height;
}
static void terrain(void){
 material_detail=1;
 /* Near tiles are 24m; distant tiles 72m. Fine tile boundaries interpolate
    the coarse corners, so LOD changes never open cracks in the ground. */
 const float step=72;
 int ix=(int)floorf(cx/step),iz=(int)floorf(cz/step);
 for(int x=ix-8;x<=ix+8;x++)for(int z=iz-8;z<=iz+8;z++){
  float x0=x*step,z0=z*step;
  float dist=distance(x0+36,z0+36);if(dist>580)continue;
  if(!visible(x0+36,player_ground,z0+36,75))continue;
  int n=dist<190?3:1;float sub=step/n;
  float corners[4]={terrain_height(x,z),terrain_height(x+1,z),terrain_height(x+1,z+1),terrain_height(x,z+1)};
  float height[4][4];
  for(int i=0;i<=n;i++)for(int j=0;j<=n;j++){
   float u=(float)i/n,v=(float)j/n;
   if(i==0||j==0||i==n||j==n)
    height[i][j]=(1-v)*((1-u)*corners[0]+u*corners[1])+v*((1-u)*corners[3]+u*corners[2]);
   else height[i][j]=rh_height(x0+i*sub,z0+j*sub);
  }
  for(int i=0;i<n;i++)for(int j=0;j<n;j++){
   float x1=x0+i*sub,z1=z0+j*sub,x2=x1+sub,z2=z1+sub;
   unsigned seed=cell_hash(x*3+i,z*3+j);int water=rh_water((x1+x2)*.5f,(z1+z2)*.5f);
   int biome=rh_biome((x1+x2)*.5f,(z1+z2)*.5f);
   uint32_t color=weather==2?((seed&1)?RGB(209,221,218):RGB(221,229,220)):
     ((seed&1)?RGB(95,146,61):RGB(98,148,62));
   if(weather!=2&&biome==1)color=(seed&1)?RGB(65,119,55):RGB(74,128,58);
   if(weather!=2&&biome==2)color=(seed&1)?RGB(117,128,105):RGB(132,139,116);
   if(water)color=(seed&1)?RGB(46,115,135):RGB(49,128,148);
   float inset=water?-.22f:-.40f;
   quad(x1,height[i][j]+inset,z1,x2,height[i+1][j]+inset,z1,
        x2,height[i+1][j+1]+inset,z2,x1,height[i][j+1]+inset,z2,color);
  }
 }
 material_detail=0;
}
/* Adjacent material bands, rather than coplanar layered quads, eliminate road
   z-fighting. Curves use exactly the simulation's road center functions. */
static void road_band(int axis,int grid,float a,float b,float lo,float hi,uint32_t color){
 int horizontal=axis&1,city=axis>1;
 static int last_axis=-1,last_grid;static float last_a,last_b,ca,cb,ya,yb;
 if(axis!=last_axis||grid!=last_grid||a!=last_a||b!=last_b){
  ca=city?grid*RH_CITY_BLOCK:horizontal?rh_road_z(grid,a):rh_road_x(grid,a);
  cb=city?grid*RH_CITY_BLOCK:horizontal?rh_road_z(grid,b):rh_road_x(grid,b);
  float ax=horizontal?a:ca,az=horizontal?ca:a,bx=horizontal?b:cb,bz=horizontal?cb:b;
  ya=(city?rh_height(ax,az):rh_road_height(ax,az,axis))+.12f;yb=(city?rh_height(bx,bz):rh_road_height(bx,bz,axis))+.12f;
  last_axis=axis;last_grid=grid;last_a=a;last_b=b;
 }
 if(horizontal)quad(a,ya,ca+lo,b,yb,cb+lo,b,yb,cb+hi,a,ya,ca+hi,color);
 else quad(ca+lo,ya,a,cb+lo,yb,b,cb+hi,yb,b,ca+hi,ya,a,color);
}
static void city_streets(uint32_t asphalt){
 int gx=(int)floorf(cx/RH_CITY_BLOCK),gz=(int)floorf(cz/RH_CITY_BLOCK);
 for(int axis=0;axis<2;axis++){
  int grid=axis?gz:gx,start=(int)floorf((axis?cx:cz)/40);
  for(int g=grid-2;g<=grid+2;g++)for(int k=start-14;k<=start+14;k++){
   float a=k*40.0f,b=a+40,center=g*RH_CITY_BLOCK,mid=a+20;
   int tile=(int)floorf(mid/RH_CITY_BLOCK);
   int urban=axis?(rh_city_tile(tile,g)||rh_city_tile(tile,g-1)):
                  (rh_city_tile(g,tile)||rh_city_tile(g-1,tile));
   if(!urban)continue;
   float x=axis?mid:center,z=axis?center:mid;
   if(distance(x,z)>560||!visible(x,player_ground,z,42))continue;
   /* Same asphalt as the main road makes their shared junction seamless. */
   road_band(axis+2,g,a,b,-6.2f,-.1f,asphalt);
   road_band(axis+2,g,a,b,-.1f,.1f,(k&1)?asphalt:RGB(232,210,147));
   road_band(axis+2,g,a,b,.1f,6.2f,asphalt);
   int rx=(int)floorf(x/RH_ROAD_SPACING+.5f),rz=(int)floorf(z/RH_ROAD_SPACING+.5f);
   float main_distance=fminf(rh_absf(x-rh_road_x(rx,z)),rh_absf(z-rh_road_z(rz,x)));
   if(main_distance>30){
    road_band(axis+2,g,a,b,-10,-6.2f,RGB(141,151,145));
    road_band(axis+2,g,a,b,6.2f,10,RGB(141,151,145));
   }
  }
 }
}
static void roads(void){
 const float spacing=RH_ROAD_SPACING;
 uint32_t asphalt=weather==3?RGB(100,150,178):weather==2?RGB(158,174,178):
                    weather==1?RGB(39,53,62):RGB(49,57,64);
 city_streets(asphalt);
 for(int axis=0;axis<2;axis++){
  float across=axis?cz:cx,along=axis?cx:cz;
  int grid=(int)floorf(across/spacing+.5f),start=(int)floorf(along/20);
  for(int g=grid-1;g<=grid+1;g++)for(int k=start-29;k<=start+29;k++){
   float a=k*20.0f,b=a+20,center=axis?rh_road_z(g,a+10):rh_road_x(g,a+10);
   if(distance(axis?a+10:center,axis?center:a+10)>550)continue;
   if(!visible(axis?a+10:center,player_ground,axis?center:a+10,36))continue;
   /* The horizontal road owns each crossing; vertical segments stop exactly
      at its boundaries, including shoulders. No overlapping asphalt. */
   if(!axis && !rh_bridge_info(g,(int)floorf((a+10)/spacing+.5f),NULL,NULL)){
    float junction=floorf((a+10)/spacing+.5f)*spacing;
    if(a<junction+8.7f&&b>junction-8.7f){
     if(a<junction-8.7f)b=junction-8.7f;
     else if(b>junction+8.7f)a=junction+8.7f;
     else continue;
    }
   }
   road_band(axis,g,a,b,-8.7f,-6.2f,RGB(129,131,105));
   road_band(axis,g,a,b,6.2f,8.7f,RGB(129,131,105));
   road_band(axis,g,a,b,-6.2f,-5.98f,WHITE);
   road_band(axis,g,a,b,5.98f,6.2f,WHITE);
   /* All paint/glints are adjacent strips in the same surface; no depth
      offsets, coplanar overlays or shimmering overlapping triangles. */
   road_band(axis,g,a,b,-5.98f,-.10f,asphalt);
   road_band(axis,g,a,b,-.10f,.10f,(k&1)?asphalt:RGB(236,213,150));
   if((weather==3||weather==1)&&(k%3==0)){
    road_band(axis,g,a,b,.10f,1.5f,asphalt);
    road_band(axis,g,a,b,1.5f,2.1f,weather==3?RGB(158,199,211):RGB(78,102,114));
    road_band(axis,g,a,b,2.1f,5.98f,asphalt);
   }else road_band(axis,g,a,b,.10f,5.98f,asphalt);
   /* Guardrails identify bridges and keep the water crossing readable. */
   float mid=(a+b)*.5f,road=axis?rh_road_z(g,mid):rh_road_x(g,mid);
   int bridge=!axis && rh_bridge_info(g,(int)floorf(mid/spacing+.5f),NULL,NULL) && rh_absf(mid-floorf(mid/spacing+.5f)*spacing)<RH_BRIDGE_HALF_LENGTH;
   if(bridge||rh_water(axis?mid:road,axis?road:mid))for(int side=-1;side<=1;side+=2){
    float x=axis?mid:road+side*7.5f,z=axis?road+side*7.5f:mid;
    box3(x,rh_road_height(x,z,axis)+.75f,z,axis?18:.16f,.24f,axis?.16f:18,0,
         RGB(165,179,174),RGB(153,169,170),RGB(206,216,208));
   }
   if(bridge){
    float deck=rh_road_height(road,mid,0),ground=rh_height(road,mid);
    if(deck-ground>.8f){
     box3(road,deck-.65f,mid,15.2f,.62f,20,0,RGB(110,120,119),RGB(100,112,114),RGB(143,151,148));
     if(k%5==0){
      float h=deck-ground-.65f;
      if(h>0)for(int side=-1;side<=1;side+=2)
       box3(road+side*5.6f,ground,mid,1.4f,h,1.5f,0,RGB(119,128,125),RGB(99,113,112),RGB(150,157,151));
     }
    }
   }
  }
 }
 /* Existing depots and petrol stations retain straight, short access spurs. */
 int ix=(int)floorf(cx/spacing+.5f),iz=(int)floorf(cz/spacing+.5f);
 for(int x=ix-1;x<=ix+1;x++)for(int z=iz-1;z<=iz+1;z++){
  float bx=x*spacing,bz=z*spacing;if(distance(bx,bz)>580)continue;
  float dx=rh_road_x(x,bz+135),sx=rh_road_x(x,bz-130);
  for(int k=0;k<6;k++){
   flat_quad(dx+k*21,bz+130,dx+(k+1)*21,bz+140,.13f,asphalt);
   flat_quad(sx-(k+1)*21,bz-135,sx-k*21,bz-125,.13f,asphalt);
  }
  flat_quad(dx+75,bz+113,dx+156,bz+159,.16f,asphalt);
  flat_quad(sx-156,bz-155,sx-75,bz-106,.16f,asphalt);
 }
}
static void tree(float x,float z,unsigned seed,float y){
 if(distance(x,z)>390||!visible(x,player_ground+6,z,25))return;
 material_detail=1;float scale=3.2f+(seed&7)*.37f;
 box3(x,y,z,.62f,scale,.62f,0,RGB(86,71,49),RGB(92,76,52),RGB(84,69,44));
 if(distance(x,z)<110){
  box3(x+.33f,y+.5f,z,.06f,scale*.68f,.42f,0,RGB(124,91,58),RGB(117,82,47),RGB(132,96,59));
 }
 float h=scale*1.1f,y0=y+scale*.75f;
 float X[4]={-2.3f,2.3f,2.3f,-2.3f},Z[4]={-2.3f,-2.3f,2.3f,2.3f};
 uint32_t leaf=(seed&1)?RGB(51,107,55):RGB(65,120,62);
 for(int k=0;k<4;k++){int k2=(k+1)%4;
  mtri(x+X[k],y0,z+Z[k],x+X[k2],y0,z+Z[k2],x,y0+h*2,z,leaf);
  mtri(x+X[k]*.78f,y0+h*.8f,z+Z[k]*.78f,x+X[k2]*.78f,y0+h*.8f,
       z+Z[k2]*.78f,x,y0+h*2.7f,z,weather==2?RGB(214,226,215):RGB(67,135,62));
 }
 material_detail=0;
}
static void lamppost(float x,float z,float a){
 if(distance(x,z)>325.0f||!visible(x,player_ground+4,z,25))return;
 float y=rh_height(x,z);rotation(a);float si=rotation_s,co=rotation_c;
 box3(x,y,z,.15f,7.4f,.15f,0,RGB(112,124,125),RGB(95,111,114),RGB(142,146,138));
 box3(x+si*1.5f,y+7.25f,z+co*1.5f,3.0f,.12f,.16f,a,
  RGB(100,114,118),RGB(99,108,113),RGB(98,112,114));
 box3(x+si*2.8f,y+7.10f,z+co*2.8f,.9f,.17f,.72f,0,
  RGB(246,227,162),RGB(210,209,175),RGB(242,235,196));
}
static void highway_sign(float x,float z,float a,int seed){
 if(distance(x,z)>355||!visible(x,player_ground+3,z,25))return;
 float y=rh_height(x,z);
 box3(x,y,z,.18f,5.5f,.18f,0,RGB(113,127,125),RGB(100,117,115),RGB(151,157,145));
 local_box(x,y,z,0,5.25f,0,a,4.8f,1.3f,.16f,
   RGB(30,111,94),RGB(22,85,75),RGB(31,118,94));
 local_box(x,y,z,-1.45f,5.38f,.11f,a,1.25f,.16f,.09f,
   RGB(240,238,224),RGB(224,227,220),RGB(247,249,238));
 local_box(x,y,z,.36f,5.05f,.11f,a,2.2f,.11f,.09f,
   RGB(236,242,231),RGB(224,234,228),RGB(247,249,240));
 if(seed&1)local_box(x,y,z,.35f,5.48f,.11f,a,2.1f,.10f,.09f,
   RGB(236,242,231),RGB(224,234,228),RGB(247,249,240));
}
static void facade_band(float x,float y,float z,float w,float h,float l,uint32_t color){
 float x1=x-w*.5f,x2=x+w*.5f,z1=z-l*.5f,z2=z+l*.5f;
 if(cz+camera_eye.z<=z1)quad(x1,y,z1,x2,y,z1,x2,y+h,z1,x1,y+h,z1,color);
 if(cz+camera_eye.z>=z2)quad(x1,y,z2,x2,y,z2,x2,y+h,z2,x1,y+h,z2,color);
 if(cx+camera_eye.x<=x1)quad(x1,y,z1,x1,y,z2,x1,y+h,z2,x1,y+h,z1,color);
 if(cx+camera_eye.x>=x2)quad(x2,y,z1,x2,y,z2,x2,y+h,z2,x2,y+h,z1,color);
}
static void building(float x,float z,float w,float l,float h,uint32_t color){
 if(distance(x,z)>475||!visible(x,player_ground+h*.5f,z,(w+l+h)*.5f+20))return;
 float y=rh_height(x,z);
 if(distance(x,z)<425){
  /* Glass is part of each wall, not laid over a solid wall: depth precision
     never causes flickering windows at the end of a long city street. */
  float storey=distance(x,z)>220?7:3.5f;
  for(int floor=0;floor<22&&floor*storey<h;floor++){
   float a=floor*storey,remain=h-a;
   float low=remain<storey*.4f?remain:storey*.4f;
   facade_band(x,y+a,z,w,low,l,color);remain-=low;a+=low;
   if(remain<=0)continue;
   float glass_h=remain<storey*.42f?remain:storey*.42f;
   uint32_t glass=sunlight<.52f&&floor%3!=1?RGB(224,191,111):RGB(61,112,136);
   facade_band(x,y+a,z,w,glass_h,l,glass);remain-=glass_h;a+=glass_h;
   if(remain>0)facade_band(x,y+a,z,w,remain<storey*.18f?remain:storey*.18f,l,color);
  }
  if(h>32)box3(x,y+h,z,w*.55f,2.3f,l*.6f,0,
    RGB(117,141,147),RGB(101,125,134),RGB(140,157,156));
 }else facade_band(x,y,z,w,h,l,color);
 box3(x,y+h-.18f,z,w+.5f,.28f,l+.5f,0,RGB(66,71,73),RGB(62,71,77),RGB(78,85,90));
 if(distance(x,z)<100)
  box3(x,y+.08f,z+l*.5f+.17f,w*.25f,3.0f,.14f,0,
       RGB(40,48,53),RGB(42,52,57),RGB(49,58,61));
}
static void station(float bx,float bz){
 float z=bz-130,x=rh_road_x((int)floorf(bx/RH_ROAD_SPACING+.5f),z)-113;if(distance(x,z)>540||!visible(x,player_ground+5,z,90))return;
 float y=rh_height(x,z);
 box3(x+19,y,z,29,5,12,0,RGB(202,209,199),RGB(189,201,193),RGB(97,112,112));
 box3(x-8,y+4.6f,z,28,.65f,14,0,RGB(239,231,224),RGB(210,221,209),RGB(49,143,143));
 for(int k=-1;k<=1;k++){
  box3(x-9,y,z+k*3.9f,1.4f,4.8f,1.4f,0,WHITE,RGB(177,189,189),RGB(52,156,155));
  box3(x-9,y+1.1f,z+k*3.9f,1.5f,1.9f,.20f,0,RGB(36,41,45),RGB(40,45,47),RGB(35,52,55));
 }
 box3(x-16,y,z+6,.65f,7,.65f,0,RGB(211,209,186),RGB(202,199,176),RGB(240,232,214));
 box3(x-16,y+5.7f,z+6,5,1.2f,.32f,0,RGB(235,242,231),RGB(241,243,235),RGB(57,165,167));
 // Forecourt price display, painted kerbs and stop markers.
 box3(x+8,y+.11f,z-8,20,.20f,.30f,0,RGB(225,220,203),RGB(224,223,215),RGB(250,240,215));
 for(int k=0;k<3;k++){
  box3(x-9,y+.34f,z+k*3.9f,.80f,.65f,.75f,0,
    RGB(45,151,145),RGB(52,159,156),RGB(68,184,165));
 }

}
static void depot(float bx,float bz){
 float z=bz+135,x=rh_road_x((int)floorf(bx/RH_ROAD_SPACING+.5f),z)+114;
 if(distance(x,z)>540||!visible(x,player_ground+8,z,125))return;
 building(x+3,z+40,64,36,16,RGB(175,184,181));
 building(x+51,z-21,25,35,10,RGB(200,177,145));
 for(int k=0;k<5;k++){
  float yy=rh_height(x-36+k*11,z+25);
  box3(x-36+k*11,yy,z+25,1.0f,1.0f,10,0,RGB(118,102,75),RGB(128,111,91),RGB(136,117,94));
 }
 float y=rh_height(x-27,z-10);
 for(int k=0;k<4;k++)box3(x-27,y+k*1.1f,z-10,5,1,5,0,RGB(169,115,69),RGB(146,92,52),RGB(185,138,88));
 // Container stacks, pallet cages, two docks and loading-bay markings.
 for(int row=0;row<2;row++)for(int k=0;k<3;k++){
  float bx=x-58+k*12.0f,bz=z+49+row*7.0f;
  float by=rh_height(bx,bz)+.12f;
  box3(bx,by,bz,9,2.8f,5.8f,0,
    (k+row)&1?RGB(174,84,66):RGB(64,113,137),
    (k+row)&1?RGB(126,61,54):RGB(54,94,111),
    RGB(163,170,165));
  for(int stripe=-3;stripe<=3;stripe++)
   box3(bx+stripe*1.12f,by+1.2f,bz+2.99f,.12f,2.1f,.05f,0,
      RGB(201,202,186),RGB(180,185,183),RGB(214,219,200));
 }
 for(int dock=-1;dock<=1;dock++)
  box3(x+dock*13.0f,rh_height(x,z+57)+.1f,z+57,7.8f,3.5f,.18f,0,
   RGB(45,57,61),RGB(44,53,56),RGB(72,80,83));
 // Loading marker (raised yellow platform).
 box3(x,rh_height(x,z)+.08f,z,12,.15f,12,0,RGB(234,189,66),RGB(235,185,56),RGB(255,207,79));
}
typedef struct {int x,z,valid,present;float px,pz,y;} CachedTree;
static CachedTree cached_trees[32*32];
static void structures(void){
 int tx=(int)floorf(cx/RH_CITY_BLOCK),tz=(int)floorf(cz/RH_CITY_BLOCK);
 for(int ix=tx-2;ix<=tx+2;ix++)for(int iz=tz-2;iz<=tz+2;iz++){
  if(!rh_city_tile(ix,iz))continue;
  float bx=(ix+.5f)*RH_CITY_BLOCK,bz=(iz+.5f)*RH_CITY_BLOCK;
  if(distance(bx,bz)>820||!visible(bx,player_ground+35,bz,340))continue;
  unsigned seed=cell_hash(ix,iz);
  for(int k=0;k<16;k++){
   float x=bx+(k%4-1.5f)*66,z=bz+(k/4-1.5f)*66;
   if(distance(x,z)>475||rh_road_distance(x,z)<23||rh_water(x,z))continue;
   float h=18+(cell_hash(ix*17+k,iz*13+k)&255)*(.204f);
   building(x,z,24,28,h,(seed+k)&1?RGB(187,177,155):RGB(148,175,179));
  }
 }
 float sx=RH_ROAD_SPACING;
 for(int ix=(int)floorf(cx/sx+.5f)-1;ix<=(int)floorf(cx/sx+.5f)+1;ix++)
  for(int iz=(int)floorf(cz/sx+.5f)-1;iz<=(int)floorf(cz/sx+.5f)+1;iz++){
   float bx=ix*sx,bz=iz*sx;
   if(distance(bx,bz)>610)continue;
   depot(bx,bz);station(bx,bz);
   if(rh_settlement_kind(ix,iz)==2 && distance(bx,bz)<210){
    for(int direction=-1;direction<=1;direction+=2)for(int stripe=-3;stripe<=3;stripe++){
     float xx=bx+stripe*1.7f,zz=bz+direction*15;
     box3(xx,rh_height(xx,zz)+.27f,zz,1.0f,.025f,3.2f,0,WHITE,WHITE,WHITE);
     xx=bx+direction*15;zz=bz+stripe*1.7f;
     box3(xx,rh_height(xx,zz)+.27f,zz,3.2f,.025f,1.0f,0,WHITE,WHITE,WHITE);
    }
   }
   for(int k=-2;k<=2;k++){
    float offset=k*98.0f;
    lamppost(rh_road_x(ix,bz+offset)+10.3f,bz+offset,RH_PI/2);
    if((k&1)==0)lamppost(bx+offset,rh_road_z(iz,bx+offset)-10.3f,RH_PI);
   }
   highway_sign(rh_road_x(ix,bz+290)+10.7f,bz+290.0f,RH_PI/2,(int)cell_hash(ix,iz));
   unsigned seed=cell_hash(ix,iz);int kind=rh_settlement_kind(ix,iz);
   int count=kind==1?5:0;
   for(int k=0;k<count;k++){
    float xx=bx+(k%4-1.5f)*66.0f,zz=bz+(k/4<2?-1:1)*(210+(k/4%2)*71);
    if(rh_road_distance(xx,zz)<23||rh_water(xx,zz))continue;
    float h=kind==2?18+(float)((seed>>(k&15))&15)*2.4f:5+(k%3)*1.4f;
    building(xx,zz,kind==2?24:17,kind==2?28:20,h,
      (k&1)?RGB(203,178,144):RGB(153,177,173));
    if(kind==1)box3(xx,rh_height(xx,zz)+h,zz,19,.65f,22,0,
       RGB(148,65,47),RGB(133,71,49),RGB(171,83,52));
   }
  }
 int px=(int)floorf(cx/40),pz=(int)floorf(cz/40);
 for(int x=px-10;x<=px+10;x++)for(int z=pz-10;z<=pz+10;z++){
  float xx,zz;unsigned seed=cell_hash(x,z);
  if(!visible(x*40+20,player_ground+6,z*40+20,55))continue;
  CachedTree *ct=&cached_trees[((unsigned)x&31u)*32+((unsigned)z&31u)];
  if(!ct->valid||ct->x!=x||ct->z!=z){
   ct->x=x;ct->z=z;ct->valid=1;ct->present=rh_tree_at(x,z,&ct->px,&ct->pz);
   if(ct->present)ct->y=rh_height(ct->px,ct->pz);
  }
  if(ct->present)tree(ct->px,ct->pz,seed,ct->y);
  /* Close grass blades are small crossed triangular patches, never alpha
     billboard forests or expensive transparent overdraw. */
  if(distance(x*40+20,z*40+20)<90 && (seed&1) && weather!=2){
   xx=x*40+18;zz=z*40+18;
   if(rh_road_distance(xx,zz)>12&&!rh_water(xx,zz))for(int k=0;k<4;k++){
    float xx2=xx+k*.51f,y=rh_height(xx2,zz);
    mtri(xx2-.15f,y,zz,xx2+.15f,y,zz,xx2+.08f,y+.8f,zz,RGB(104,154,57));
   }
  }
 }
}

/* Radial wheel profile (10-sided) with a separate aluminium rim, much less
   blocky than the v0.1 cube-wheel placeholder. */
static const float ring_s[11]={0,.587785252f,.951056516f,.951056516f,.587785252f,0,-.587785252f,-.951056516f,-.951056516f,-.587785252f,0};
static const float ring_c[11]={1,.809016994f,.309016994f,-.309016994f,-.809016994f,-1,-.809016994f,-.309016994f,.309016994f,.809016994f,1};
static void wheel(float x,float y,float z,float heading,float lx,float lz,float r,float width){
 rotation(heading);float c=rotation_c,s=rotation_s;
 float wcx=x+lx*c+lz*s,wcz=z-lx*s+lz*c;
 for(int side=-1;side<=1;side+=2){
  float ox=side*width*.5f;
  for(int i=0;i<10;i++){
   float sa=ring_s[i],sb=ring_s[i+1],ca=ring_c[i],cb=ring_c[i+1];
   float y1=y+sa*r,y2=y+sb*r;
   float z1=ca*r,z2=cb*r;
   float xa=wcx+(ox*c+z1*s),za=wcz+(-ox*s+z1*c);
   float xb=wcx+(ox*c+z2*s),zb=wcz+(-ox*s+z2*c);
   float xo=wcx+ox*c,zo=wcz-ox*s;
   mtri(xa,y1,za,xb,y2,zb,xo,y,zo,RGB(31,34,36));
   float rim=.49f,ry1=y+sa*r*rim,ry2=y+sb*r*rim;
   float rz1=ca*r*rim,rz2=cb*r*rim;
   mtri(wcx+(ox*c+rz1*s),ry1,wcz+(-ox*s+rz1*c),
        wcx+(ox*c+rz2*s),ry2,wcz+(-ox*s+rz2*c),
        xo,y,zo,RGB(163,174,172));
   if(side==1){
    float bx=wcx+(-ox*c+z1*s),bz=wcz-(-ox*s-z1*c);
    float ex=wcx+(-ox*c+z2*s),ez=wcz-(-ox*s-z2*c);
    quad(xa,y1,za,xb,y2,zb,ex,y2,ez,bx,y1,bz,RGB(36,39,42));
   }
  }
 }
}
static void draw_wheels(float x,float y,float z,float yaw,float offset){
 for(int k=0;k<3;k++)for(int side=-1;side<=1;side+=2){
  static const float axles[3]={-2.75f,-1.35f,1.95f};
  wheel(x,y+.60f,z,yaw,side*(1.27f+offset),axles[k],.60f,.34f);
 }
}
static float trailer_center_x,trailer_center_z;
static int personal_rig;
static int parked_rig_level=-1;
static void draw_truck(float x,float z,float heading,int model,int trailer,float trailer_heading){
 if(distance(x,z)>475||!visible(x,player_ground+2,z,22))return;
 float y=(personal_rig?player_ground:parked_rig_level>=0?rh_road_height(x,z,parked_rig_level?0:1):rh_vehicle_height(x,z,heading))+.10f;
 uint32_t body=model==0?RGB(45,131,177):(model==1?RGB(184,51,48):RGB(210,210,214));
 uint32_t dark=model==0?RGB(29,91,127):(model==1?RGB(124,34,34):RGB(149,149,157));
 local_box(x,y,z,0,.94f,-1.2f,heading,2.45f,.39f,6.0f,RGB(45,45,48),RGB(52,52,54),RGB(64,67,70));
 local_box(x,y,z,0,1.10f,1.14f,heading,2.54f,2.7f,3.25f,body,dark,body);
 // Angled windshield, a real sloped surface rather than a simple cuboid.
 local_quad(x,y,z,heading,-1.07f,1.95f,3.03f,1.07f,1.95f,3.03f,
  1.00f,3.21f,2.47f,-1.00f,3.21f,2.47f,RGB(70,117,141));
 local_quad(x,y,z,heading,-.035f,1.94f,3.05f,.035f,1.94f,3.05f,
  .035f,3.21f,2.49f,-.035f,3.21f,2.49f,RGB(41,73,91));
 local_box(x,y,z,0,1.10f,2.91f,heading,2.40f,.52f,.18f,
     RGB(52,54,57),RGB(52,54,57),RGB(70,72,75));
 local_box(x,y,z,-1.30f,2.0f,2.25f,heading,.46f,.40f,.27f,RGB(42,43,47),RGB(40,42,46),BLACK);
 local_box(x,y,z,1.30f,2.0f,2.25f,heading,.46f,.40f,.27f,RGB(42,43,47),RGB(40,42,46),BLACK);
 for(int sign=-1;sign<=1;sign+=2)
  local_box(x,y,z,sign*1.03f,1.30f,3.04f,heading,.40f,.22f,.11f,
   RGB(255,240,199),RGB(255,240,199),RGB(252,250,219));
 // Front fascia: grille slats, wind deflector, side windows, mirrors,
 // marker lamps and painted details. Colours correspond to the selected rig.
 local_box(x,y,z,0,3.83f,1.16f,heading,2.20f,.27f,2.20f,
   dark,dark,body);
 for(int k=0;k<4;k++)local_box(x,y,z,0,1.15f+k*.20f,3.052f,heading,
   1.44f,.07f,.075f,RGB(137,151,158),RGB(115,126,132),RGB(172,178,178));
 for(int side=-1;side<=1;side+=2){
  local_box(x,y,z,side*1.30f,2.49f,1.13f,heading,.07f,.85f,1.18f,
   RGB(56,97,119),RGB(40,70,89),RGB(77,123,143));
  local_box(x,y,z,side*1.39f,2.02f,2.62f,heading,.40f,.10f,.32f,
   RGB(124,130,136),RGB(109,112,120),RGB(154,156,158));
  local_box(x,y,z,side*.95f,3.74f,3.04f,heading,.21f,.12f,.15f,
   RGB(242,199,105),RGB(245,202,105),RGB(243,208,112));
  local_box(x,y,z,side*1.07f,1.25f,-3.17f,heading,.30f,.18f,.14f,
   RGB(214,55,43),RGB(192,47,38),RGB(244,77,55));
 }
 if(personal_rig){
  for(int side=-1;side<=1;side+=2){
   /* Separate tanks, retaining straps, access steps and door hardware. */
   local_box(x,y,z,side*1.23f,.55f,.10f,heading,.42f,.58f,1.5f,RGB(118,136,146),RGB(102,119,131),RGB(157,171,177));
   for(int k=-1;k<=1;k+=2)local_box(x,y,z,side*1.46f,.56f,.10f+k*.48f,heading,.035f,.57f,.07f,RGB(54,65,72),RGB(55,64,72),RGB(72,82,88));
   local_box(x,y,z,side*1.35f,.45f,1.40f,heading,.32f,.14f,.90f,RGB(115,131,138),RGB(75,89,99),RGB(172,181,181));
   local_box(x,y,z,side*1.35f,.73f,1.60f,heading,.26f,.11f,.58f,RGB(107,125,133),RGB(73,87,95),RGB(162,176,177));
   local_box(x,y,z,side*1.325f,2.12f,.40f,heading,.08f,.09f,.31f,RGB(185,195,195),RGB(160,173,179),WHITE);
   local_box(x,y,z,side*1.29f,1.16f,.50f,heading,.035f,.98f,.045f,dark,dark,dark);
   local_box(x,y,z,side*1.29f,1.16f,2.05f,heading,.035f,.73f,.045f,dark,dark,dark);
   local_box(x,y,z,side*1.46f,1.17f,-.69f,heading,.13f,2.65f,.13f,RGB(121,140,150),RGB(98,119,129),RGB(198,206,207));
   local_box(x,y,z,side*1.46f,3.78f,-.69f,heading,.19f,.11f,.19f,RGB(100,119,129),RGB(108,129,139),RGB(189,199,201));
   local_box(x,y,z,side*1.10f,1.18f,-2.1f,heading,.40f,.15f,2.65f,dark,RGB(37,45,50),dark);
   local_box(x,y,z,side*1.20f,.12f,-3.45f,heading,.38f,.62f,.10f,BLACK,BLACK,RGB(55,62,65));
  }
  local_box(x,y,z,0,1.30f,-2.30f,heading,1.35f,.12f,1.10f,RGB(63,73,79),RGB(61,70,75),RGB(92,103,108));
  local_box(x,y,z,0,1.43f,-2.3f,heading,.26f,.16f,.27f,BLACK,BLACK,RGB(139,149,151));
  local_box(x,y,z,0,1.06f,3.16f,heading,.70f,.17f,.035f,WHITE,WHITE,RGB(181,196,199));
  local_quad(x,y,z,heading,-1.05f,3.94f,1.15f,1.05f,3.94f,1.15f,.90f,4.23f,-.12f,-.90f,4.23f,-.12f,body);
 }
 draw_wheels(x,y,z,heading,0);
 if(trailer){
  float tx=trailer_center_x,tz=trailer_center_z;
  float ty=(parked_rig_level>=0?rh_road_height(tx,tz,parked_rig_level?0:1):rh_vehicle_height(tx,tz,trailer_heading))+.76f;
  uint32_t cargo=model==2?RGB(166,170,166):RGB(219,223,210);
  local_box(tx,ty,tz,0,.66f,0,trailer_heading,2.62f,2.63f,8.6f,
   cargo,RGB(185,194,190),RGB(236,235,228));
  if(personal_rig){
   local_box(tx,ty,tz,0,.28f,0,trailer_heading,2.40f,.25f,8.5f,RGB(58,69,78),RGB(47,57,65),RGB(78,91,100));
   for(int side=-1;side<=1;side+=2){
    local_box(tx,ty,tz,side*1.327f,.78f,0,trailer_heading,.065f,.09f,8.2f,RGB(231,232,207),RGB(217,223,205),WHITE);
    local_box(tx,ty,tz,side*1.327f,1.52f,0,trailer_heading,.050f,.22f,7.9f,body,dark,body);
    local_box(tx,ty,tz,side*1.327f,3.22f,0,trailer_heading,.07f,.08f,8.5f,RGB(177,191,195),RGB(155,174,180),WHITE);
    for(int k=0;k<5;k++){
     float zz=-3.5f+k*1.75f;
     local_box(tx,ty,tz,side*1.335f,.98f,zz,trailer_heading,.04f,2.08f,.045f,RGB(196,208,207),RGB(178,193,197),RGB(223,227,219));
     material_emissive=1;
     local_box(tx,ty,tz,side*1.37f,.73f,zz,trailer_heading,.06f,.10f,.17f,RGB(235,164,51),RGB(236,174,59),RGB(253,198,83));
     material_emissive=0;
    }
    local_box(tx,ty,tz,side*.66f,.72f,-4.34f,trailer_heading,1.24f,2.47f,.06f,RGB(205,214,210),RGB(184,199,200),RGB(229,232,218));
    local_box(tx,ty,tz,side*.39f,.88f,-4.40f,trailer_heading,.05f,2.21f,.055f,RGB(130,151,162),RGB(125,148,157),WHITE);
    for(int k=0;k<3;k++)local_box(tx,ty,tz,side*1.16f,1.0f+k*.87f,-4.42f,trailer_heading,.19f,.10f,.07f,RGB(155,174,183),RGB(133,153,165),WHITE);
    local_box(tx,ty,tz,side*1.1f,-.24f,-4.0f,trailer_heading,.46f,.58f,.08f,BLACK,BLACK,BLACK);
   }
   local_box(tx,ty,tz,0,.35f,-4.47f,trailer_heading,2.35f,.14f,.17f,RGB(183,197,198),RGB(148,167,177),WHITE);
  }
  for(int k=0;k<3;k++)for(int side=-1;side<=1;side+=2)
   wheel(tx,ty-.12f,tz,trailer_heading,side*1.3f,-3.3f+k*.85f,.58f,.33f);
  local_box(tx,ty,tz,0,.53f,-4.50f,trailer_heading,2.3f,.13f,.05f,
     RGB(233,53,44),RGB(228,56,52),RGB(225,53,44));
 }
}
/* Compact, untextured silhouettes keep the selectable rigs within the PSP mesh budget. */
static void draw_light_transport(float x,float z,float a,float ta,int mode,int occupied,int towing){
 float y=rh_vehicle_height(x,z,a)+.08f;
 if(distance(x,z)>475||!visible(x,y+1,z,18))return;
 uint32_t body=mode==1?RGB(165,70,42):RGB(215,49,38);
 uint32_t dark=RGB(47,56,61), glass=RGB(73,121,140);
 if(mode==3){
  for(int k=-1;k<=1;k+=2)wheel(x,y+.48f,z,a,0,k*.88f,.48f,.12f);
  local_box(x,y,z,0,.45f,0,a,.10f,.12f,1.60f,dark,dark,dark);
  local_box(x,y,z,0,.52f,-.35f,a,.11f,.72f,.12f,RGB(41,100,165),dark,body);
  local_box(x,y,z,0,1.08f,.55f,a,.09f,.56f,.10f,dark,dark,dark);
  local_box(x,y,z,0,1.57f,.55f,a,.75f,.08f,.10f,dark,dark,dark);
  local_box(x,y,z,0,1.18f,-.34f,a,.34f,.10f,.37f,BLACK,BLACK,dark);
  if(occupied){
   uint32_t shirt=RGB(55,105,160),pants=RGB(43,49,58),skin=RGB(213,166,125);
   local_box(x,y,z,0,1.19f,-.20f,a,.42f,.53f,.30f,shirt,shirt,shirt);
   local_box(x,y,z,0,1.72f,-.08f,a,.29f,.29f,.27f,skin,skin,skin);
   for(int side=-1;side<=1;side+=2){
    local_box(x,y,z,side*.23f,.72f,-.20f,a,.16f,.48f,.20f,pants,pants,pants);
    local_box(x,y,z,side*.27f,1.26f,.20f,a,.13f,.16f,.68f,shirt,shirt,skin);
   }
  }
  return;
 }
 local_box(x,y,z,0,.45f,0,a,1.92f,.48f,4.35f,body,dark,body);
 local_box(x,y,z,0,.92f,mode==1?.35f:0,a,1.72f,.72f,mode==1?1.85f:2.10f,glass,dark,glass);
 if(mode==1)local_box(x,y,z,0,.78f,-1.45f,a,1.80f,.23f,1.35f,dark,dark,body);
 else {
  /* Low nose, dark cockpit, rear wing and contrasting racing stripe. */
  local_box(x,y,z,0,.62f,1.65f,a,1.70f,.13f,.80f,body,dark,body);
  local_box(x,y,z,0,1.12f,-.20f,a,1.35f,.18f,1.25f,dark,dark,glass);
  local_box(x,y,z,0,.68f,-1.90f,a,1.98f,.10f,.48f,dark,dark,dark);
  for(int side=-1;side<=1;side+=2)
   local_box(x,y,z,side*.72f,.75f,-1.85f,a,.10f,.38f,.12f,dark,dark,dark);
  local_box(x,y,z,0,1.10f,-1.88f,a,2.06f,.10f,.45f,dark,dark,body);
  local_box(x,y,z,0,.94f,1.40f,a,.24f,.025f,1.15f,RGB(238,235,214),dark,WHITE);
 }
 for(int k=-1;k<=1;k+=2)for(int side=-1;side<=1;side+=2)
  wheel(x,y+.43f,z,a,side*.94f,k*1.42f,.43f,.25f);
 if(!towing)return;
 /* Towed boat or garden cart, coupled behind the car. */
 float d=mode==1?1.85f:1.55f,tx=x-sinf(a)*2.30f-sinf(ta)*d,tz=z-cosf(a)*2.30f-cosf(ta)*d;
 float ty=rh_vehicle_height(tx,tz,ta)+.08f;
 local_box(tx,ty,tz,0,.30f,0,ta,1.65f,.16f,2.65f,dark,dark,dark);
 for(int side=-1;side<=1;side+=2)wheel(tx,ty+.32f,tz,ta,side*.77f,-.45f,.32f,.20f);
 local_box(tx,ty,tz,0,.31f,1.65f,ta,.12f,.12f,1.1f,dark,dark,dark);
 if(mode==1){
  local_box(tx,ty,tz,0,.51f,0,ta,1.45f,.38f,2.55f,RGB(230,231,217),RGB(115,155,170),RGB(238,240,228));
  local_quad(tx,ty,tz,ta,-.72f,.74f,1.26f,.72f,.74f,1.26f,0,.58f,1.85f,0,.39f,1.28f,RGB(222,228,220));
  local_box(tx,ty,tz,0,.88f,-.55f,ta,1.0f,.22f,.35f,RGB(122,98,70),dark,RGB(166,145,107));
 }else{
  local_box(tx,ty,tz,0,.43f,0,ta,1.45f,.54f,1.85f,RGB(89,133,62),RGB(62,101,43),RGB(113,155,77));
  local_box(tx,ty,tz,0,.94f,0,ta,1.15f,.10f,1.55f,RGB(126,93,57),dark,RGB(144,108,65));
 }
}
static void draw_person(float x,float z,float yaw,float ground,float time){
 float swing=sinf(time*7)*.16f;
 uint32_t shirt=RGB(55,105,160),pants=RGB(43,49,58),skin=RGB(213,166,125);
 local_box(x,ground,z,0,.82f,0,yaw,.50f,.68f,.27f,shirt,shirt,shirt);
 local_box(x,ground,z,0,1.52f,0,yaw,.30f,.30f,.28f,skin,skin,skin);
 for(int side=-1;side<=1;side+=2){
  local_box(x,ground,z,side*.17f,.10f,side*swing,yaw,.20f,.70f,.22f,pants,pants,pants);
  local_box(x,ground,z,side*.35f,.84f,-side*swing,yaw,.15f,.55f,.17f,shirt,shirt,skin);
 }
}
static void vehicle_lenses(float x,float y,float z,float yaw,int truck){
 material_emissive=1;
 float front=truck?3.13f:2.18f,rear=truck?-3.25f:-2.18f,height=truck?1.37f:.73f;
 for(int side=-1;side<=1;side+=2){
  local_box(x,y,z,side*(truck?1.03f:.73f),height,front,yaw,.34f,.15f,.065f,
       RGB(255,240,196),RGB(255,232,174),RGB(255,244,211));
  local_box(x,y,z,side*(truck?1.03f:.73f),height,rear,yaw,.32f,.16f,.065f,
       RGB(245,49,35),RGB(233,45,32),RGB(255,71,43));
 }
 material_emissive=0;
}
static void headlight_ground(float x,float z,float yaw,float nose){
 if(!dark_lights||distance(x,z)>90||!visible(x,player_ground,z,35))return;
 rotation(yaw);float fs=rotation_s,fc=rotation_c;
 static const float along[4]={0,5,12,22},half[4]={1,1.5f,2.7f,4.5f};
 material_emissive=1;
 for(int k=0;k<3;k++){
  float a=nose+along[k],b=nose+along[k+1];
  float ax=x+fs*a,az=z+fc*a,bx=x+fs*b,bz=z+fc*b;
  float y1=rh_height(ax-fc*half[k],az+fs*half[k])+.25f;
  float y2=rh_height(ax+fc*half[k],az-fs*half[k])+.25f;
  float y3=rh_height(bx+fc*half[k+1],bz-fs*half[k+1])+.25f;
  float y4=rh_height(bx-fc*half[k+1],bz+fs*half[k+1])+.25f;
  uint32_t c=((uint32_t)(k==0?37:k==1?21:8)<<24)|0x00b7ecf4u;
  quad(ax-fc*half[k],y1,az+fs*half[k],ax+fc*half[k],y2,az-fs*half[k],
       bx+fc*half[k+1],y3,bz-fs*half[k+1],bx-fc*half[k+1],y4,bz+fs*half[k+1],c);
 }
 material_emissive=0;
}
static void traffic(const Game *g){
 for(int i=0;i<g->traffic_count;i++){
  const Traffic *t=&g->cars[i];float range=distance(t->x,t->z);
  if(range>470||!visible(t->x,player_ground+2,t->z,22))continue;
  float x=t->x,z=t->z,a=t->yaw,y=rh_vehicle_height(x,z,a)+.13f;
  uint32_t cc=t->variant==0?RGB(76,94,152):t->variant==1?RGB(189,180,164):
    t->variant==2?RGB(132,47,51):RGB(46,106,89);
  headlight_ground(x,z,a,t->variant==3?3.3f:2.3f);
  if(t->variant==3){draw_truck(x,z,a,0,0,a);vehicle_lenses(x,y,z,a,1);continue;}
  local_box(x,y,z,0,.42f,0,a,1.95f,.63f,4.2f,cc,cc,cc);
  if(range<80){
   local_box(x,y,z,0,1.02f,1.22f,a,1.85f,.21f,1.55f,cc,cc,cc);
   local_box(x,y,z,0,.98f,-1.7f,a,1.85f,.19f,.76f,cc,cc,cc);
   local_quad(x,y,z,a,-.87f,1.10f,.81f,.87f,1.10f,.81f,.77f,1.76f,.27f,-.77f,1.76f,.27f,RGB(75,121,146));
   local_quad(x,y,z,a,-.85f,1.10f,-1.32f,.85f,1.10f,-1.32f,.76f,1.76f,-.88f,-.76f,1.76f,-.88f,RGB(67,109,130));
   local_box(x,y,z,0,1.76f,-.30f,a,1.58f,.12f,1.23f,cc,cc,cc);
   for(int side=-1;side<=1;side+=2){
    float sx=side*.94f;
    local_quad(x,y,z,a,sx,1.10f,-1.18f,sx,1.10f,.68f,side*.78f,1.75f,.23f,side*.78f,1.75f,-.84f,RGB(68,113,135));
    local_box(x,y,z,side*.97f,1.08f,-.24f,a,.045f,.06f,.29f,RGB(187,197,197),RGB(165,178,180),RGB(204,211,205));
    local_box(x,y,z,side*.89f,1.12f,-.40f,a,.08f,.62f,.10f,cc,cc,cc);
    local_box(x,y,z,side*1.07f,1.26f,.60f,a,.21f,.14f,.25f,cc,BLACK,cc);
   }
   local_box(x,y,z,0,.48f,2.16f,a,1.77f,.18f,.12f,RGB(43,49,52),RGB(52,57,59),RGB(98,112,119));
   local_box(x,y,z,0,.48f,-2.16f,a,1.77f,.18f,.12f,RGB(43,49,52),RGB(52,57,59),RGB(98,112,119));
   local_box(x,y,z,0,.67f,2.18f,a,.74f,.14f,.035f,RGB(28,34,38),BLACK,BLACK);
   local_box(x,y,z,0,.45f,-2.24f,a,.48f,.13f,.025f,RGB(207,217,211),WHITE,WHITE);
  }else local_box(x,y,z,0,1.05f,-.12f,a,1.7f,.75f,2.15f,RGB(72,111,133),RGB(61,97,117),cc);
  for(int zz=-1;zz<=1;zz+=2)for(int side=-1;side<=1;side+=2){
   if(range<45)wheel(x,y+.35f,z,a,side*.97f,zz*1.35f,.35f,.18f);
   else local_box(x,y,z,side*.92f,.20f,zz*1.35f,a,.28f,.52f,.64f,BLACK,BLACK,RGB(42,46,49));
  }
  if(range<190)vehicle_lenses(x,y,z,a,0);
 }
}
static void service_van(const Game *g){
 if(!g->service_phase||distance(g->service_x,g->service_z)>475||!visible(g->service_x,player_ground+2,g->service_z,12))return;
 float x=g->service_x,z=g->service_z,y=rh_height(x,z)+.12f,a=g->service_yaw;
 local_box(x,y,z,0,.65f,-.6f,a,2.2f,2.3f,3.2f,RGB(223,220,189),RGB(206,207,184),RGB(239,236,212));
 local_box(x,y,z,0,.65f,1.65f,a,2.15f,1.7f,1.4f,RGB(232,192,68),RGB(221,178,58),RGB(248,213,94));
 local_box(x,y,z,0,1.85f,1.45f,a,1.95f,.7f,1.2f,RGB(73,116,136),RGB(58,95,113),RGB(232,192,68));
 local_box(x,y,z,0,2.91f,-.4f,a,1.1f,.15f,.5f,RGB(239,151,52),RGB(231,136,42),RGB(253,186,74));
 for(int side=-1;side<=1;side+=2){
  local_box(x,y,z,side*1.12f,1.3f,-.6f,a,.08f,.32f,3.1f,RGB(233,179,56),RGB(227,170,49),RGB(244,197,69));
  for(int axle=-1;axle<=1;axle+=2)local_box(x,y,z,side*1.06f,.22f,axle*1.5f,a,.32f,.66f,.72f,BLACK,BLACK,RGB(54,59,60));
 }
}
static void objective_marker(const Game *g){
 float x,z;rh_depot(g->job.stage?g->job.dest_x:g->job.pickup_x,
                   g->job.stage?g->job.dest_z:g->job.pickup_z,&x,&z);
 float range=distance(x,z);if(range>600)return;
 float y=rh_height(x,z)+18;
 if(!visible(x,y+4,z,12))return;
 float width=3+range*.009f,rx=camera_right.x,rz=camera_right.z;
 uint32_t c=g->job.stage?RGB(113,231,185):RGB(255,214,74);
 material_emissive=1;
 quad(x-rx*width*.3f,y+3,z-rz*width*.3f,x+rx*width*.3f,y+3,z+rz*width*.3f,
      x+rx*width*.3f,y+9,z+rz*width*.3f,x-rx*width*.3f,y+9,z-rz*width*.3f,c);
 mtri(x-rx*width,y+3,z-rz*width,x+rx*width,y+3,z+rz*width,x,y,z,c);
 /* Painted bay corners identify the exact action zone at ground level. */
 if(range<200)for(int a=-1;a<=1;a+=2)for(int b=-1;b<=1;b+=2){
  float px=x+a*6,pz=z+b*6;
  box3(px,rh_height(px,pz)+.3f,pz,.25f,1.8f,.25f,0,c,c,c);
 }
 material_emissive=0;
}
static void tri2d(float x1,float y1,float x2,float y2,float x3,float y3,uint32_t c){
 if(nover>MAX_2D-3)return;
 put(overlay,&nover,MAX_2D,x1,y1,0,c);put(overlay,&nover,MAX_2D,x2,y2,0,c);
 put(overlay,&nover,MAX_2D,x3,y3,0,c);
}
static void rect(float x,float y,float w,float h,uint32_t c){
 tri2d(x,y,x+w,y,x+w,y+h,c);tri2d(x,y,x+w,y+h,x,y+h,c);
}
// Decode UTF-8 Russian text without PSP OS fonts or external font files.
static unsigned int rh_utf8_next(const unsigned char **text){
 unsigned int cp=*(*text)++;
 if(cp<0x80u)return cp;
 if(cp>=0xC2u&&cp<=0xDFu && **text>=0x80u && **text<=0xBFu){
  cp=((cp&0x1Fu)<<6)|(**text&0x3Fu);(*text)++;return cp;
 }
 // Unsupported or truncated sequence: replace with a visible question mark.
 while(**text>=0x80u&&**text<=0xBFu)(*text)++;
 return '?';
}
static const unsigned char *rh_font_glyph(unsigned int cp){
 if(cp>=32u&&cp<=127u)return rh_font[cp-32];
 if(cp>=0x0410u&&cp<=0x042Fu)return rh_font_ru[cp-0x0410u];
 if(cp==0x0401u)return rh_font_ru[32];
 if(cp>=0x0430u&&cp<=0x044Fu)return rh_font_ru[33+cp-0x0430u];
 if(cp==0x0451u)return rh_font_ru[65];
 return rh_font['?'-32];
}
static void label(float x,float y,const char *txt,float scale,uint32_t color){
 (void)scale;scale=1.0f;x=floorf(x+.5f);y=floorf(y+.5f);
 const unsigned char *p=(const unsigned char *)txt;
 while(*p && x+6<SW){
  unsigned int cp=rh_utf8_next(&p);
  const unsigned char *rows=rh_font_glyph(cp);
  // One rectangle per connected horizontal run reduces the PSP HUD vertex load.
  for(int iy=0;iy<9;iy++){
   int ix=0;
   while(ix<7){
    if(!(rows[iy]&(1u<<(6-ix)))){ix++;continue;}
    int left=ix++;
    while(ix<7&&(rows[iy]&(1u<<(6-ix))))ix++;
    rect(x+left*scale,y+iy*scale,(ix-left)*scale,scale,color);
   }
  }
  x+=7*scale;
 }
}

static float limitf(float x,float lo,float hi){return x<lo?lo:x>hi?hi:x;}
static void line2d(float x1,float y1,float x2,float y2,float width,uint32_t color){
 float dx=x2-x1,dy=y2-y1,len=sqrtf(dx*dx+dy*dy);if(len<.01f)return;
 float nx=-dy/len*width*.5f,ny=dx/len*width*.5f;
 tri2d(x1+nx,y1+ny,x2+nx,y2+ny,x2-nx,y2-ny,color);
 tri2d(x1+nx,y1+ny,x2-nx,y2-ny,x1-nx,y1-ny,color);
}
static void map_line(float x1,float y1,float x2,float y2,float width,uint32_t color){
 const float left=359,right=467,top=11,bottom=88;
 float dx=x2-x1,dy=y2-y1,t0=0,t1=1;
 float p[4]={-dx,dx,-dy,dy},q[4]={x1-left,right-x1,y1-top,bottom-y1};
 for(int i=0;i<4;i++){
  if(rh_absf(p[i])<.001f){if(q[i]<0)return;continue;}
  float t=q[i]/p[i];if(p[i]<0){if(t>t1)return;if(t>t0)t0=t;}
  else{if(t<t0)return;if(t<t1)t1=t;}
 }
 line2d(x1+dx*t0,y1+dy*t0,x1+dx*t1,y1+dy*t1,width,color);
}
static void minimap(const Game *g){
 const float mx=413,my=49,scale=.095f,px=g->player.x,pz=g->player.z;
 rect(355,7,116,101,RGB(16,34,43));rect(358,10,110,80,RGB(67,105,75));
 /* World X is mirrored to agree with the +Z PSP view: screen-right is -X. */
 for(int ix=-4;ix<=4;ix++)for(int iz=-3;iz<=3;iz++){
  float x=px+ix*128,z=pz+iz*128;
  if(rh_water(x,z))rect(limitf(mx-ix*128*scale-6,359,455),limitf(my-iz*128*scale-6,11,77),12,12,RGB(42,104,132));
 }
 int gx=(int)floorf(px/RH_ROAD_SPACING+.5f),gz=(int)floorf(pz/RH_ROAD_SPACING+.5f);
 int ctx=(int)floorf(px/RH_CITY_BLOCK),ctz=(int)floorf(pz/RH_CITY_BLOCK);
 for(int ix=ctx-2;ix<=ctx+2;ix++)for(int iz=ctz-2;iz<=ctz+2;iz++){
  if(!rh_city_tile(ix,iz))continue;
  float bx=ix*RH_CITY_BLOCK,bz=iz*RH_CITY_BLOCK;
  float x1=mx-(bx-px)*scale,x2=mx-(bx+RH_CITY_BLOCK-px)*scale;
  float y1=my-(bz-pz)*scale,y2=my-(bz+RH_CITY_BLOCK-pz)*scale;
  map_line(x1,y1,x2,y1,2,RGB(45,57,62));map_line(x1,y2,x2,y2,2,RGB(45,57,62));
  map_line(x1,y1,x1,y2,2,RGB(45,57,62));map_line(x2,y1,x2,y2,2,RGB(45,57,62));
  for(int k=0;k<16;k++){
   float x=bx+RH_CITY_BLOCK*.5f+(k%4-1.5f)*66,z=bz+RH_CITY_BLOCK*.5f+(k/4-1.5f)*66;
   float sx=mx-(x-px)*scale,sy=my-(z-pz)*scale;
   if(sx>361&&sx<465&&sy>13&&sy<86&&!rh_water(x,z))rect(sx-1,sy-1,3,3,RGB(164,175,166));
  }
 }

 for(int road=-1;road<=1;road++)for(int k=-7;k<7;k++){
  float a=pz+k*72,b=a+72;
  map_line(mx-(rh_road_x(gx+road,a)-px)*scale,my-(a-pz)*scale,
           mx-(rh_road_x(gx+road,b)-px)*scale,my-(b-pz)*scale,3,RGB(45,57,62));
  a=px+k*90;b=a+90;
  map_line(mx-(a-px)*scale,my-(rh_road_z(gz+road,a)-pz)*scale,
           mx-(b-px)*scale,my-(rh_road_z(gz+road,b)-pz)*scale,3,RGB(45,57,62));
 }
 for(int ix=gx-1;ix<=gx+1;ix++)for(int iz=gz-1;iz<=gz+1;iz++){
  int kind=rh_settlement_kind(ix,iz),count=kind==2?16:kind==1?5:0;
  for(int k=0;k<count;k++){
   float x=ix*RH_ROAD_SPACING+(k%4-1.5f)*66,z=iz*RH_ROAD_SPACING+(k/4<2?-1:1)*(210+(k/4%2)*71);
   float sx=mx-(x-px)*scale,sy=my-(z-pz)*scale;
   if(sx>361&&sx<465&&sy>13&&sy<86&&!rh_water(x,z))rect(sx-1,sy-1,3,3,RGB(164,175,166));
  }
 }
 for(int k=0;k<8;k++){
  int cellx=(int)floorf(px/40)+(k%4-2)*6,cellz=(int)floorf(pz/40)+(k/4?5:-5);
  float x,z;if(!rh_tree_at(cellx,cellz,&x,&z))continue;
  float sx=mx-(x-px)*scale,sy=my-(z-pz)*scale;
  if(sx>361&&sx<465&&sy>13&&sy<86)tri2d(sx,sy-2,sx-2,sy+2,sx+2,sy+2,RGB(31,80,46));
 }
 float wx,wz;rh_waypoint(g,&wx,&wz);
 float tx=mx-(wx-px)*scale,ty=my-(wz-pz)*scale;
 /* Broken yellow route points at the next road waypoint, not a distant city. */
 for(int k=0;k<8;k++){
  float a=k/8.0f,b=a+.065f;
  map_line(mx+(tx-mx)*a,my+(ty-my)*a,mx+(tx-mx)*b,my+(ty-my)*b,2,RGB(255,213,85));
 }
 tx=limitf(tx,362,464);ty=limitf(ty,14,85);rect(tx-2,ty-2,4,4,RGB(255,225,124));
 rh_depot(g->job.stage?g->job.dest_x:g->job.pickup_x,g->job.stage?g->job.dest_z:g->job.pickup_z,&wx,&wz);
 tx=limitf(mx-(wx-px)*scale,363,463);ty=limitf(my-(wz-pz)*scale,15,84);
 rect(tx-3,ty-3,7,7,RGB(28,41,45));rect(tx-2,ty-2,5,5,g->job.stage?RGB(117,232,185):RGB(248,178,64));
 float fx=-sinf(g->player.yaw),fy=-cosf(g->player.yaw),rx=-fy,ry=fx;
 tri2d(mx+fx*6,my+fy*6,mx-fx*4+rx*3,my-fy*4+ry*3,mx-fx*4-rx*3,my-fy*4-ry*3,WHITE);

 /* Города ТЗ на мини-карте */
 {
  static const int cgx[4]={0,4,-3,2}; static const int cgz[4]={0,3,5,-4};
  static const char *cn[4]={"ПРВ","СВД","ГРЗ","МРС"};
  for(int ci=0;ci<4;ci++){
   float wx=cgx[ci]*RH_ROAD_SPACING, wz=cgz[ci]*RH_ROAD_SPACING;
   float sx=mx-(wx-px)*scale, sy=my-(wz-pz)*scale;
   if(sx>361&&sx<465&&sy>13&&sy<86){
    rect(sx-3,sy-3,7,7,RGB(255,180,60));
    label(sx+5,sy-4,cn[ci],0.7f,RGB(255,220,150));
   }
  }
 }
 label(361,95,"КАРТА",1,WHITE);label(425,95,"ПУТЬ",1,RGB(255,217,113));
}
static int mirror_point(const Game *g,int side,float wx,float wz,float height,float *x,float *y){
 float fs=sinf(g->player.yaw),fc=cosf(g->player.yaw);
 float ex=g->player.x+side*fc*1.45f,ez=g->player.z-side*fs*1.45f;
 float dx=wx-ex,dz=wz-ez,depth=-(dx*fs+dz*fc);if(depth<1.5f||depth>240)return 0;
 float horizontal=-dx*fc+dz*fs;
 float left=side>0?12:416,top=114;
 *x=left+26+horizontal/depth*29;
 *y=top+24+(3.18f-height)/depth*66;
 return 1;
}
/* A mirror is only 52 pixels wide. Render bounded 2D silhouettes instead of
   clipping many projected 3D faces against five planes on every frame. */
static int mirror_sprite(const Game *g,int side,float x,float z,float ground,
                         float width,float height,uint32_t body,uint32_t detail){
 float sx,sy;
 if(!mirror_point(g,side,x,z,ground-player_ground,&sx,&sy))return 0;
 float fs=sinf(g->player.yaw),fc=cosf(g->player.yaw);
 float depth=-((x-g->player.x)*fs+(z-g->player.z)*fc);
 float left=side>0?12:416,top=114,bottom=top+87;
 float half=width*14.5f/depth,h=height*66.0f/depth;
 float x0=limitf(sx-half,left+1,left+51),x1=limitf(sx+half,left+1,left+51);
 float y0=limitf(sy-h,top+1,bottom),y1=limitf(sy,top+1,bottom);
 if(x1-x0<.5f||y1-y0<.5f)return 0;
 rect(x0,y0,x1-x0,y1-y0,body);
 float detail_y=limitf(y0+(y1-y0)*.23f,top+1,bottom);
 rect(x0,detail_y,x1-x0,(y1-y0)*.13f,detail);
 return 1;
}
static void rear_mirrors(const Game *g){
 float fs=sinf(g->player.yaw),fc=cosf(g->player.yaw);
 static const float depths[5]={180,85,38,16,5};
 for(int side=-1;side<=1;side+=2){
  float left=side>0?12:416,top=114;
  rect(left-2,top-2,56,92,RGB(16,23,29));
  rect(left,top,52,25,day_color(RGB(126,163,180)));
  rect(left,top+25,52,63,day_color(RGB(83,121,68)));
  for(int k=0;k<4;k++){
   float x[4],y[4];int valid=1;
   for(int j=0;j<4;j++){
    float d=depths[k+(j>=2)],wx=g->player.x-fs*d,wz=g->player.z-fc*d;
    int axis=rh_absf(fc)>=rh_absf(fs),grid=(int)floorf((axis?wx:wz)/RH_ROAD_SPACING+.5f);
    if(axis)wx=rh_road_x(grid,wz)+(j==0||j==3?-6.2f:6.2f);
    else wz=rh_road_z(grid,wx)+(j==0||j==3?-6.2f:6.2f);
    if(!mirror_point(g,side,wx,wz,rh_road_height(wx,wz,axis?0:1)-player_ground,&x[j],&y[j])){valid=0;x[j]=left+26;y[j]=top+24;}
    x[j]=limitf(x[j],left+1,left+51);y[j]=limitf(y[j],top+1,top+87);
   }
   if(valid){tri2d(x[0],y[0],x[1],y[1],x[2],y[2],day_color(RGB(47,56,63)));
    tri2d(x[0],y[0],x[2],y[2],x[3],y[3],day_color(RGB(47,56,63)));}
  }
  int drawn=0;
  for(int i=0;i<g->traffic_count&&i<RH_MAX_TRAFFIC&&drawn<6;i++){
   const Traffic *t=&g->cars[i];
   float dx=t->x-g->player.x,dz=t->z-g->player.z,depth=-(dx*fs+dz*fc);
   if(depth<1.5f||depth>120)continue;
   drawn+=mirror_sprite(g,side,t->x,t->z,rh_height(t->x,t->z),1.95f,1.4f,
                        day_color(RGB(128,109,87)),day_color(RGB(67,104,122)));
  }
  if(g->service_phase)mirror_sprite(g,side,g->service_x,g->service_z,
      rh_height(g->service_x,g->service_z),2.2f,2.7f,RGB(228,196,84),RGB(67,103,119));
  if(g->job.stage==1){
   float x=g->player.trailer_x,z=g->player.trailer_z;
   mirror_sprite(g,side,x,z,rh_height(x,z),2.67f,4.05f,
      day_color(RGB(201,214,214)),RGB(222,205,142));
  }
  line2d(left+1,top+87,left+51,top+87,1,RGB(125,149,151));
 }
}
static void cockpit(const Game *g){
 if(!g->player.camera||g->on_foot||g->transport_mode==3)return;
#ifdef RH_DIAG_NO_MIRRORS
 /* Hardware diagnosis: retain mirror housings but skip projected contents. */
 rect(10,112,56,92,RGB(16,23,29));rect(14,116,48,84,RGB(64,76,79));
 rect(414,60,56,92,RGB(16,23,29));rect(418,64,48,84,RGB(64,76,79));
#else
 rear_mirrors(g);
#endif
 // Hand-built 2D cabin overlay; matches LHD driver's seat.
 rect(0,0,10,205,RGB(30,34,35));
 rect(468,0,12,205,RGB(30,34,35));
 rect(0,185,480,87,RGB(26,31,35));
 rect(0,182,480,12,RGB(48,51,52));
 rect(10,196,460,4,RGB(61,66,68));
 rect(286,197,140,35,RGB(41,45,47));
 rect(306,204,98,17,RGB(58,70,72));
 rect(339,210,29,5,RGB(166,173,159));
 // Solid steering wheel ring, with central badge and three spokes.
 const float mx=136,my=221,r0=29,r1=23;
 for(int i=0;i<24;i++){
  float a=i*RH_PI/12,b=(i+1)*RH_PI/12;
  float c1=cosf(a),s1=sinf(a),c2=cosf(b),s2=sinf(b);
  tri2d(mx+r0*c1,my+r0*s1,mx+r0*c2,my+r0*s2,mx+r1*c2,my+r1*s2,RGB(20,23,25));
  tri2d(mx+r0*c1,my+r0*s1,mx+r1*c2,my+r1*s2,mx+r1*c1,my+r1*s1,RGB(20,23,25));
 }
 rect(116,217,40,7,RGB(30,35,36));
 rect(133,214,6,29,RGB(30,35,36));
 rect(128,213,16,14,RGB(61,66,66));
 /* Two readable instruments with speed/RPM needles, stitched dash and vents. */
 for(int gidx=0;gidx<2;gidx++){
  float gx=194+gidx*43,gy=207;
  for(int i=0;i<18;i++){
   float a=i*RH_PI/9,b=(i+1)*RH_PI/9;
   tri2d(gx,gy,gx+cosf(a)*15,gy+sinf(a)*15,gx+cosf(b)*15,gy+sinf(b)*15,RGB(12,20,25));
  }
  for(int i=0;i<7;i++){
   float a=-RH_PI*.85f+i*RH_PI*.23f;
   rect(gx+cosf(a)*12,gy+sinf(a)*12,1,2,RGB(183,207,189));
  }
  float value=gidx?g->player.rpm/2600.0f:g->player.speed_abs*3.6f/140;
  if(value>1)value=1;float a=-RH_PI*.85f+value*RH_PI*1.4f;
  tri2d(gx-1,gy,gx+1,gy,gx+cosf(a)*11,gy+sinf(a)*11,RGB(238,128,82));
 }
 for(int i=0;i<5;i++){
  rect(285,204+i*3,16,1,RGB(13,21,25));rect(410,204+i*3,16,1,RGB(13,21,25));
 }
 rect(335,228,63,3,RGB(16,23,26));
 rect(335,228,63*(g->player.fuel/RH_TRUCKS[g->player.model].tank_l),3,RGB(94,163,126));
 rect(14,186,452,1,RGB(129,116,86));
 if(g->weather==1){
  float sweep=sinf(g->sim_t*3.5f);
  for(int w=0;w<2;w++){
   float bx=105+w*211,by=184,tx=bx+40+sweep*59,ty=101+rh_absf(sweep)*13;
   tri2d(bx-1,by,bx+2,by,tx,ty,RGB(17,24,29));
   tri2d(tx-13,ty+2,tx+29,ty-22,tx+29,ty-19,RGB(16,23,27));
   tri2d(tx-13,ty+2,tx+29,ty-19,tx-13,ty+5,RGB(16,23,27));
  }
 }
}
static void service_status(const Game *g,int on_map){
 if(!g->service_phase)return;
 float duration=g->service_phase==1?50:g->service_phase==2?30:8;
 float progress=limitf(g->service_timer/duration,0,1);
 int remaining=(int)ceilf(duration-g->service_timer);if(remaining<0)remaining=0;
 float x=on_map?58:76,y=on_map?179:112,w=on_map?363:268;
 char text[96];
 if(g->service_phase==1)snprintf(text,sizeof text,"ПОМОЩЬ В ПУТИ: %d СЕК.",remaining);
 else if(g->service_phase==2)snprintf(text,sizeof text,"ЗАПРАВКА: %d СЕК.",remaining);
 else snprintf(text,sizeof text,"ПОМОЩЬ: ТОПЛИВО ДОСТАВЛЕНО");
 rect(x,y,w,31,RGB(19,39,47));label(x+7,y+5,text,1,RGB(249,218,119));
 rect(x+7,y+21,w-14,4,RGB(51,70,74));rect(x+7,y+21,(w-14)*progress,4,RGB(103,202,160));
}
static void hud(const Game *g){
 char buf[160];const Player *p=&g->player;

 if(g->state == ST_MENU){
  /* полупрозрачные панели — грузовик виден и крутится */
  rect(0,0,480,70,RGB(12,20,28));
  label(70,12,"ДАЛЬНИЙ РЕЙС 2.0",1.7f,RGB(239,212,145));
  label(110,42,"СИМУЛЯТОР ДАЛЬНОБОЙЩИКА",1.05f,RGB(160,190,200));
  rect(0,200,480,72,RGB(10,16,22));
  label(50,208,"ПРИВОЛЬСК · СЕВЕРОДОНСК · ГРУЗОВОЙ · МОРСК",0.95f,RGB(140,170,180));
  if(g->menu_blink)
   label(110,235,"НАЖМИ КРЕСТ ЧТОБЫ НАЧАТЬ",1.15f,WHITE);
  else
   label(110,235,"НАЖМИ КРЕСТ ЧТОБЫ НАЧАТЬ",1.15f,RGB(90,100,110));
  return;
 }

 if(g->state == ST_GARAGE){
  rect(8,8,280,52,RGB(18,28,34));
  label(16,14,"ГАРАЖ",1.3f,RGB(239,212,145));
  snprintf(buf,sizeof buf,"%s   %.0f Р.", RH_TRUCKS[p->model].name, g->bank);
  label(16,34,buf,1.05f,WHITE);
  rect(8,220,464,44,RGB(18,28,34));
  label(16,228,"СТИК: ОСМОТР   КРЕСТ: ВЫЕХАТЬ   КРУГ: КОМПЬЮТЕР",1.05f,RGB(247,217,151));
  return;
 }

 if(g->state == ST_COMPUTER){
  rect(60,40,360,190,RGB(18,30,36));
  rect(60,40,360,28,RGB(36,88,100));
  label(80,48,"КОМПЬЮТЕР ЗАКАЗОВ",1.3f,WHITE);
  for(int i=0;i<3;i++){
   int y=80+i*32;
   if(i==g->computer_sel) rect(70,y-4,340,28,RGB(43,84,91));
   int from = g->home_city % 4;
   int to = (from + 1 + i) % 4;
   snprintf(buf,sizeof buf,"%d. %s -> %s  ~%d Р.", i+1, rh_city_name(from), rh_city_name(to), 18000+i*4500);
   label(80,y,buf,1.05f, i==g->computer_sel ? RGB(249,218,119) : WHITE);
  }
  label(80,200,"КРЕСТ: ВЗЯТЬ   КРУГ: НАЗАД",1.0f,RGB(247,217,151));
  return;
 }

 if(g->state == ST_UNLOAD){
  rect(70,50,340,170,RGB(18,30,36));
  rect(70,50,340,28,RGB(36,88,100));
  label(90,58,"СДАЧА ГРУЗА",1.3f,WHITE);
  static const char *opts[]={"ПРОСТО БРОСИТЬ   +0 Р.","ЗАЕХАТЬ ПЕРЕДОМ  +1000 Р.","АККУРАТНО ЗАДОМ  +2500 Р."};
  /* подсказка по заезду */
  if(g->approach_dot < -0.35f)
   label(90,78,"(обнаружен заезд ЗАДОМ)",0.9f,RGB(120,200,140));
  else if(g->approach_dot > 0.35f)
   label(90,78,"(обнаружен заезд ПЕРЕДОМ)",0.9f,RGB(200,180,120));
  for(int i=0;i<3;i++){
   int y=95+i*30;
   if(i==g->unload_sel) rect(80,y-4,320,26,RGB(43,84,91));
   label(90,y,opts[i],1.05f, i==g->unload_sel ? RGB(249,218,119) : WHITE);
  }
  label(90,195,"КРЕСТ: ПОДТВЕРДИТЬ",1.0f,RGB(247,217,151));
  return;
 }


 if(g->state == ST_RADIO){
  rect(90,50,300,160,RGB(18,30,36));
  rect(90,50,300,28,RGB(36,88,100));
  label(110,58,"РАДИО",1.3f,WHITE);
  static const char *stations[]={
   "1. ДОРОЖНОЕ — Шансон",
   "2. ЕВРОПА ПЛЮС — Хиты",
   "3. АВТОРАДИО — Рок",
   "4. МАЯК — Новости"
  };
  for(int i=0;i<4;i++){
   int y=90+i*26;
   if(i==g->radio_station) rect(100,y-3,280,24,RGB(43,84,91));
   label(110,y,stations[i],1.05f, i==g->radio_station ? RGB(249,218,119) : WHITE);
  }
  label(110,195,"ВВЕРХ/ВНИЗ: СТАНЦИЯ   ВЛЕВО: ЗАКРЫТЬ",0.95f,RGB(247,217,151));
  return;
 }
 if(g->state == ST_PHONE){
  rect(70,40,340,190,RGB(18,30,36));
  rect(70,40,340,28,RGB(36,88,100));
  label(90,48,"ТЕЛЕФОН — ЗАКАЗЫ РЯДОМ",1.2f,WHITE);
  for(int i=0;i<3;i++){
   int y=85+i*32;
   if(i==g->phone_sel) rect(80,y-4,320,28,RGB(43,84,91));
   int to = (g->current_city + 1 + i) % 4;
   snprintf(buf,sizeof buf,"%d. В %s   ~%d Р.", i+1, rh_city_name(to), 12000+i*3500);
   label(90,y,buf,1.05f, i==g->phone_sel ? RGB(249,218,119) : WHITE);
  }
  label(90,200,"КРЕСТ: ВЗЯТЬ   КРУГ: ЗАКРЫТЬ",1.0f,RGB(247,217,151));
  return;
 }

 if(g->paused){
  rect(35,35,410,202,RGB(22,34,39));
  rect(35,35,410,29,RGB(35,84,95));
  label(51,44,"ДАЛЬНИЙ РЕЙС 2.0 : ПАУЗА",1,WHITE);
  label(52,76,"СТИК: РУЛЬ   X: ГАЗ   []: ТОРМОЗ",1,WHITE);
  label(52,91,"O: ДЕЙСТВИЕ/ТЕЛЕФОН   TREUG: ВИД",1,WHITE);
  label(52,106,"L/R: ВЫГЛЯНУТЬ   ВНИЗ: КАРТА   ВВЕРХ: РАДИО",1,WHITE);
  label(52,121,"START: ПАУЗА",1,WHITE);
  static const char *wm[]={"АВТО","ЯСНО","ДОЖДЬ","СНЕГ","ГОЛОЛЕД"};
  static const char *tm[]={"АВТО","ДЕНЬ","ЗАКАТ","НОЧЬ","РАССВЕТ"};
  rect(48,145+g->settings_row*26,384,22,RGB(43,84,91));
  snprintf(buf,sizeof buf,"ПОГОДА: < %s >",wm[g->weather_mode>=0&&g->weather_mode<5?g->weather_mode:0]);
  label(58,151,buf,1,WHITE);
  snprintf(buf,sizeof buf,"ВРЕМЯ:  < %s >",tm[g->time_mode>=0&&g->time_mode<5?g->time_mode:0]);
  label(58,177,buf,1,WHITE);
  label(52,211,"ВВЕРХ/ВНИЗ: ВЫБОР  ВЛЕВО/ВПРАВО: СМЕНА",1,RGB(247,217,151));
  return;
 }
 if(g->map_open){
  rect(44,45,391,183,RGB(20,39,45));
  rect(44,45,391,26,RGB(36,88,100));
  label(60,53,"ЗАКАЗ НА ПЕРЕВОЗКУ",1.40f,WHITE);
  snprintf(buf,sizeof buf,"ПОГРУЗКА: %d, %d",g->job.pickup_x,g->job.pickup_z);
  label(64,86,buf,1.13f,WHITE);
  snprintf(buf,sizeof buf,"ВЫГРУЗКА: %d, %d",g->job.dest_x,g->job.dest_z);
  label(64,109,buf,1.13f,WHITE);
  snprintf(buf,sizeof buf,"МАРШРУТ: %.1f КМ   ГРУЗ: %d Т",g->job.distance_km,g->job.cargo_tons);
  label(64,134,buf,1.06f,RGB(246,216,143));
  snprintf(buf,sizeof buf,"ОПЛАТА: %.0f РУБ.  РЕЙСОВ: %d",g->job.payout,g->completed);
  label(64,158,buf,1.04f,WHITE);
  if(g->service_phase)service_status(g,1);
  else if(p->fuel<.01f){
   label(60,185,"ТОПЛИВО ЗАКОНЧИЛОСЬ",1,RGB(249,218,119));
   label(60,203,"КРУГ: ВЫЗВАТЬ ПОМОЩЬ С ТОПЛИВОМ",1,WHITE);
  }else label(64,195,"СЕЛЕКТ : ВЕРНУТЬСЯ К ДОРОГЕ",1.12f,RGB(248,215,142));
  return;
 }
 cockpit(g);

 /* ===== PLAY HUD 2.0 (по ТЗ) ===== */

 /* Верхний левый угол */
 rect(6,6,250,48,RGB(16,26,32));
 label(12,10,"ДАЛЬНИЙ РЕЙС 2.0",1.15f,RGB(239,212,145));
 snprintf(buf,sizeof buf,"КАСА: %.0f Р.  РЕЙСОВ: %d", g->bank, g->completed);
 label(12,28,buf,0.95f,WHITE);
 /* город обновляется в sim, здесь только отображаем */


 /* Верхний правый угол — задача + расстояние + стрелка */
 {
  float tx,tz; rh_waypoint(g,&tx,&tz);
  float dist_km = rh_job_target_distance(g)/1000.0f;
  float a=atan2f(tx-p->x,tz-p->z)-p->yaw;
  while(a>RH_PI)a-=2*RH_PI;
  while(a< -RH_PI)a+=2*RH_PI;
  const char *task = "СВОБОДНАЯ ЕЗДА";
  if(g->job.stage==0) task = "К ПОГРУЗКЕ";
  else if(g->job.stage==1) task = "К ВЫГРУЗКЕ";
  else if(g->job.stage<0) task = "К ГАРАЖУ";
  const char *arrow = (a>0.45f)?"◄":(a<-0.45f)?"►":"▲";
  rect(300,6,174,48,RGB(16,26,32));
  label(308,10,task,1.05f,RGB(247,213,128));
  snprintf(buf,sizeof buf,"%s  %.1f КМ", arrow, dist_km);
  label(308,30,buf,1.05f,WHITE);
 }

 /* Нижний левый — телеметрия */
 rect(6,210,230,56,RGB(16,26,32));
 snprintf(buf,sizeof buf,"%03d КМ/Ч  ПЕР.%d  %04d ОБ",
          (int)(p->speed_abs*3.6f), p->gear, (int)p->rpm);
 label(12,216,buf,0.95f,WHITE);
 int has_trailer = (g->job.stage==1 && !g->parked_valid) ? 1 : 0;
 snprintf(buf,sizeof buf,"ТОПЛИВО: %3.0f Л   ПРИЦЕП: %s",
          p->fuel, has_trailer ? "ДА" : "НЕТ");
 label(12,236,buf,0.90f,RGB(188,210,205));
 /* fuel bar */
 rect(12,252,200,6,RGB(30,40,45));
 float fuel_ratio = p->fuel / RH_TRUCKS[p->model].tank_l;
 if(fuel_ratio<0)fuel_ratio=0; if(fuel_ratio>1)fuel_ratio=1;
 rect(12,252,(int)(200*fuel_ratio),6, fuel_ratio<0.2f ? RGB(200,60,40) : RGB(94,163,126));

 /* Нижний центр — контекстные подсказки */
 {
  float tx,tz;
  int ix=(int)floorf(p->x/RH_ROAD_SPACING+.5f);
  int iz=(int)floorf(p->z/RH_ROAD_SPACING+.5f);
  rh_station(ix,iz,&tx,&tz);
  if(!g->on_foot && g->transport_mode!=3 && rh_dist2(p->x,p->z,tx,tz)<27.0f && p->speed_abs<2.0f){
   rect(100,180,280,24,RGB(21,51,52));
   label(120,186,"КРУГ: ЗАПРАВИТЬСЯ",1.15f,WHITE);
  }
  rh_depot(g->job.stage?g->job.dest_x:g->job.pickup_x,
           g->job.stage?g->job.dest_z:g->job.pickup_z,&tx,&tz);
  float bay_distance=rh_dist2(p->x,p->z,tx,tz);
  if(!g->on_foot && g->transport_mode!=3 && !g->parked_valid && bay_distance<135 && !g->job.stage){
   rect(70,150,340,22,RGB(21,44,48));
   label(90,155,"ЕДЬ К ЖЁЛТОМУ ДОКУ",1.0f,RGB(255,219,102));
  }
  if(!g->on_foot && g->transport_mode!=3 && !g->parked_valid && bay_distance<32){
   rect(70,175,340,24,RGB(21,51,52));
   if(p->speed_abs<1.4f)
    label(90,182, g->job.stage ? "КРУГ: ВЫГРУЗИТЬ ГРУЗ" : "КРУГ: ПРИЦЕПИТЬ ПРИЦЕП", 1.0f, WHITE);
   else
    label(90,182,"ОСТАНОВИСЬ В ЖЁЛТОЙ ЗОНЕ",1.0f,WHITE);
  }
 }

 /* мини-карта */
 minimap(g);
 service_status(g,0);

 if(p->fuel<.01f&&!g->service_phase){
  rect(76,112,268,27,RGB(49,40,31));
  label(84,117,"НЕТ ТОПЛИВА. СЕЛЕКТ: ПОМОЩЬ",1,RGB(255,217,126));
 }

 if(g->notice_ticks){
  static const char *msg[]={"","ГРУЗ ЗАГРУЖЕН","РЕЙС ВЫПОЛНЕН","ЗАПРАВКА ОПЛАЧЕНА","ТЯГАЧ СМЕНЁН","НЕ ХВАТАЕТ ДЕНЕГ","ТЯГАЧ ОТРЕМОНТИРОВАН"};
  rect(82,78,317,25,RGB(19,45,45));
  int idx=g->last_notice;
  if(idx<0||idx>=7)idx=0;
  label(96,85,msg[idx],1.14f,RGB(248,227,158));
 }
}
static int project_direction(float x,float y,float z,float *sx,float *sy){
 float d=x*camera_forward.x+y*camera_forward.y+z*camera_forward.z;
 if(d<.08f)return 0;
 float horizontal=x*camera_right.x+y*camera_right.y+z*camera_right.z;
 float vertical=x*camera_up.x+y*camera_up.y+z*camera_up.z;
 *sx=240+horizontal/d*205.1f;*sy=136-vertical/d*205.4f;
 return *sx> -90&&*sx<570&&*sy> -60&&*sy<300;
}
static uint32_t mix_color(uint32_t a,uint32_t b,float t){
 if(t<0)t=0;if(t>1)t=1;
 int ar=a&255,ag=(a>>8)&255,ab=(a>>16)&255;
 int br=b&255,bg=(b>>8)&255,bb=(b>>16)&255;
 return RGB((int)(ar+(br-ar)*t),(int)(ag+(bg-ag)*t),(int)(ab+(bb-ab)*t));
}
static void celestial_disc(float sx,float sy,float radius,uint32_t color){
 for(int i=0;i<18;i++){
  float a=i*RH_PI/9,b=(i+1)*RH_PI/9;
  tri2d(sx,sy,sx+cosf(a)*radius,sy+sinf(a)*radius,
        sx+cosf(b)*radius,sy+sinf(b)*radius,color);
 }
}
static void celestial_glow(float sx,float sy,float radius,uint32_t center,uint32_t edge){
 for(int i=0;i<18;i++){
  if(nover>MAX_2D-3)return;
  float a=i*RH_PI/9,b=(i+1)*RH_PI/9;
  put(overlay,&nover,MAX_2D,sx,sy,0,center);
  put(overlay,&nover,MAX_2D,sx+cosf(a)*radius,sy+sinf(a)*radius,0,edge);
  put(overlay,&nover,MAX_2D,sx+cosf(b)*radius,sy+sinf(b)*radius,0,edge);
 }
}
static int sky(const Game *g,float sun){
 float angle=(g->clock_hours-6)*(RH_PI/12);
 float altitude=sinf(angle),sunx=cosf(angle),suny=altitude,sunz=.24f*altitude;
 float daylight=altitude*1.7f+.09f;if(daylight<0)daylight=0;if(daylight>1)daylight=1;
 float twilight=1-rh_absf(altitude)/.30f;if(twilight<0)twilight=0;
 uint32_t top=mix_color(RGB(4,9,25),RGB(65,133,193),daylight);
 uint32_t horizon=mix_color(RGB(14,25,47),RGB(156,195,207),daylight);
 horizon=mix_color(horizon,RGB(239,132,105),twilight*.82f);
 if(weather==1||weather==2){
  uint32_t overcast=mix_color(RGB(17,23,38),RGB(119,141,151),daylight);
  top=mix_color(top,overcast,.65f);horizon=mix_color(horizon,overcast,.50f);
 }
 for(int y=0;y<SH;y+=12){
  float t=(float)y/(SH-1);t=t*t;
  rect(0,y,SW,y+12>SH?SH-y:12,mix_color(top,horizon,t));
 }
 float sx,sy;
 if(!weather&&altitude>=-.025f&&project_direction(sunx,suny,sunz,&sx,&sy)){
  /* Warm, compact halo follows the projected sun, not the camera. */
  if(twilight>0){
   float t=sy/(SH-1);t=t*t;
   celestial_glow(sx,sy,46,RGB(251,185,113),mix_color(top,horizon,t));
  }
  celestial_disc(sx,sy,twilight>.1f?13:10,RGB(255,240,165));
 }
 if(!weather&&altitude<-.08f){
  /* Moon azimuth is opposite the sun. Lower lunar elevation keeps the moon
     in the driver's sky while preserving a direction tied to world time. */
  float mx=-sunx,mz=-sunz,ml=sqrtf(mx*mx+mz*mz);
  if(ml>.001f&&project_direction(mx/ml,.18f-altitude*.22f,mz/ml,&sx,&sy)){
   celestial_disc(sx,sy,10,RGB(224,233,232));
   celestial_disc(sx+4,sy-3,9,mix_color(top,horizon,(sy/(SH-1))*(sy/(SH-1))));
   rect(sx-5,sy+1,2,2,RGB(169,191,201));
  }
  for(int k=0;k<42;k++){
   unsigned seed=hash((unsigned)k*4397u);
   float bearing=(seed&1023)*(2*RH_PI/1024),height=.16f+((seed>>10)&255)/350.0f;
   if(project_direction(sinf(bearing),height,cosf(bearing),&sx,&sy))
    rect(floorf(sx),floorf(sy),1,1,(seed&1)?RGB(147,169,199):RGB(205,214,222));
  }
 }
 for(int k=0;k<7;k++){
  float bearing=k*2*RH_PI/7+g->sim_t*.00012f;
  if(!project_direction(sinf(bearing),.25f+(k%3)*.09f,cosf(bearing),&sx,&sy))continue;
  uint32_t c=sun>.1f?RGB(185,204,201):RGB(49,60,76);
  rect(sx-31,sy,62,4,c);rect(sx-18,sy-4,32,4,c);
 }
 if(!weather&&sun>.12f){
  int gx=(int)floorf(cx/1024),gz=(int)floorf(cz/1024);
  for(int ix=gx-1;ix<=gx+1;ix++)for(int iz=gz-1;iz<=gz+1;iz++){
   float wx=ix*1024+266+sinf(g->sim_t*.008f)*70;
   float wz=iz*1024+656+cosf(g->sim_t*.007f)*55;
   float dx=wx-cx-camera_eye.x,dy=66-player_ground-camera_eye.y,dz=wz-cz-camera_eye.z;
   if(!project_direction(dx,dy,dz,&sx,&sy))continue;
   float range=sqrtf(dx*dx+dz*dz),size=range<150?3:1.5f;
   for(int k=0;k<3;k++){
    float x=sx+k*size*4,y=sy+k%2*size*2,wing=sinf(g->sim_t*4+k)*size;
    tri2d(x-size*2,y+wing,x,y,x-1,y+1,RGB(45,60,65));
    tri2d(x+size*2,y+wing,x,y,x+1,y+1,RGB(45,60,65));
   }
  }
 }
 return nover;
}
static void precipitation(const Game *g){
 if(g->paused||g->map_open||!(weather==1||weather==2))return;
 int bottom=g->player.camera?182:SH;
 for(int i=0;i<65;i++){
  unsigned seed=hash((unsigned)i*7919u);
  float speed=weather==1?170:31;
  float yy=fmodf((seed%271)+g->sim_t*speed,(float)bottom);
  float xx=fmodf((seed%479)+g->sim_t*(weather==1?-23:8),480);
  if(xx<0)xx+=480;
  if(weather==1)tri2d(xx,yy,xx-1,yy+7,xx+1,yy+1,RGB(150,187,203));
  else rect(floorf(xx),floorf(yy),2,2,RGB(222,233,230));
 }
}

void rh_render_init(void){
 for(int y=0;y<32;y++)for(int x=0;x<32;x++){
  unsigned seed=cell_hash(x,y);int grey=247+(seed%9);
  detail_texture[y*32+x]=RGB(grey,grey,grey);
 }
 detail_texture[0]=RGB(255,255,255);
 sceKernelDcacheWritebackRange(detail_texture,sizeof detail_texture);
 sceGuInit();sceGuStart(GU_DIRECT,command_list);
 sceGuDrawBuffer(GU_PSM_8888,(void*)0,BUF_WIDTH);
 sceGuDispBuffer(SW,SH,(void*)VRAM_FRAME,BUF_WIDTH);
 sceGuDepthBuffer((void*)(VRAM_FRAME*2),BUF_WIDTH);
 sceGuOffset(2048-SW/2,2048-SH/2);sceGuViewport(2048,2048,SW,SH);
 sceGuDepthRange(65535,0);sceGuScissor(0,0,SW,SH);
 sceGuEnable(GU_SCISSOR_TEST);sceGuEnable(GU_DEPTH_TEST);
 sceGuDepthFunc(GU_GEQUAL);sceGuDisable(GU_CULL_FACE);sceGuEnable(GU_CLIP_PLANES);
 sceGuShadeModel(GU_SMOOTH);sceGuDisable(GU_TEXTURE_2D);
 sceGuFinish();sceGuSync(GU_SYNC_FINISH,GU_SYNC_WHAT_DONE);sceGuDisplay(GU_TRUE);
}
/* Camera follows the vehicle smoothly rather than snapping around its pivot. */
static float follow_yaw, follow_time, follow_x, follow_z;
static int follow_valid, follow_mode;
static float smooth_camera_yaw(const Game *g,int looking){
 float desired=(g->on_foot?g->person_yaw:g->player.yaw)+looking*.75f;
 float dt=g->sim_t-follow_time;
 float px=g->on_foot?g->person_x:g->player.x,pz=g->on_foot?g->person_z:g->player.z;
 float dx=px-follow_x,dz=pz-follow_z;
 float abrupt=desired-follow_yaw;
 while(abrupt>RH_PI)abrupt-=2*RH_PI;
 while(abrupt< -RH_PI)abrupt+=2*RH_PI;
 if(!follow_valid||dt<0||dt>.5f||(dt==0&&rh_absf(abrupt)>2.5f)||dx*dx+dz*dz>2500||follow_mode!=(g->player.camera+2*g->on_foot)){
  follow_yaw=desired;follow_valid=1;
 }else{
  float difference=desired-follow_yaw;
  while(difference>RH_PI)difference-=2*RH_PI;
  while(difference< -RH_PI)difference+=2*RH_PI;
  if(dt>0)follow_yaw+=difference*(1-expf(-dt*(g->player.camera?8.0f:4.0f)));
 }
 follow_time=g->sim_t;follow_x=px;follow_z=pz;follow_mode=g->player.camera+2*g->on_foot;
 return follow_yaw;
}

void rh_render_frame(const Game *g,int looking){
 const Player *p=&g->player;cx=g->on_foot?g->person_x:p->x;cz=g->on_foot?g->person_z:p->z;
 player_ground=g->on_foot?rh_road_height(cx,cz,g->person_level?0:1):rh_player_height(g);
 nmesh=0;nover=0;
 float sun=sinf((g->clock_hours-6.0f)*(RH_PI/12.0f));
 if(sun<0)sun=0;
 sunlight=0.29f+.71f*sun;
 weather=g->weather;dark_lights=sunlight<.55f||weather==1||weather==2;

 ScePspFVector3 eye,target,up={0,1,0};
 int nsky=0;

 float yaw=0.0f;
 /* Гараж/меню: орбита вокруг грузовика (тот же GPU-путь, без return) */
 if(g->state==ST_MENU || g->state==ST_GARAGE || g->state==ST_COMPUTER){
  yaw=g->garage_cam_yaw;
  float fs=sinf(yaw), fc=cosf(yaw);
  float dist=g->garage_cam_dist;
  float height=g->garage_cam_height;
  if(dist<8.0f) dist=8.0f;
  if(dist>24.0f) dist=24.0f;
  if(height<2.5f) height=2.5f;
  if(height>9.0f) height=9.0f;
  eye.x=-fs*dist; eye.y=height; eye.z=-fc*dist;
  target.x=0.0f; target.y=2.0f; target.z=0.0f;
 }else{
  yaw=smooth_camera_yaw(g,looking);
  float fs=sinf(yaw),fc=cosf(yaw);
  if(g->on_foot){
   eye.x=-fs*7;eye.y=3.2f;eye.z=-fc*7;
   target.x=sinf(g->person_yaw)*1.2f;target.y=1.1f;target.z=cosf(g->person_yaw)*1.2f;
  }else if(p->camera&&g->transport_mode==3){
   eye.x=0;eye.y=1.87f;eye.z=0;
   target.x=fs*70;target.y=eye.y-.42f;target.z=fc*70;
  }else if(p->camera){
   eye.x=cosf(p->yaw)*.74f;eye.y=3.18f;eye.z=-sinf(p->yaw)*.74f;
   target.x=eye.x+fs*70;target.y=eye.y-.42f;target.z=eye.z+fc*70;
  }else{
   eye.x=-fs*17;eye.y=7.3f;eye.z=-fc*17;
   target.x=sinf(p->yaw)*3.0f;target.y=2.4f;target.z=cosf(p->yaw)*3.0f;
  }
 }
 float offroad=rh_road_distance(cx,cz)-9;
 if(g->state==ST_PLAY && offroad>0&&p->speed_abs>2){
  float factor=p->speed_abs/25;if(factor>1)factor=1;
  float rough=offroad/8;if(rough>1)rough=1;
  float amplitude=(p->camera?.16f:.08f)*factor*rough;
  float vertical=(sinf(g->sim_t*18)+sinf(g->sim_t*29)*.4f)*amplitude;
  float lateral=sinf(g->sim_t*23)*amplitude*.35f;
  eye.y+=vertical;target.y+=vertical*.35f;
  eye.x+=lateral*cosf(yaw);eye.z-=lateral*sinf(yaw);
  target.x+=lateral*.4f*cosf(yaw);target.z-=lateral*.4f*sinf(yaw);
 }
 camera_eye=eye;camera_forward=(ScePspFVector3){target.x-eye.x,target.y-eye.y,target.z-eye.z};
 float len=sqrtf(camera_forward.x*camera_forward.x+camera_forward.y*camera_forward.y+camera_forward.z*camera_forward.z);
 camera_forward.x/=len;camera_forward.y/=len;camera_forward.z/=len;
 float horizontal_len=sqrtf(camera_forward.x*camera_forward.x+camera_forward.z*camera_forward.z);
 camera_right=(ScePspFVector3){-camera_forward.z/horizontal_len,0,camera_forward.x/horizontal_len};
 camera_up=(ScePspFVector3){-camera_right.z*camera_forward.y,
   camera_right.z*camera_forward.x-camera_right.x*camera_forward.z,camera_right.x*camera_forward.y};
 prepare_planes();
 terrain();roads();structures();if(!g->on_foot)headlight_ground(cx,cz,p->yaw,3.3f);traffic(g);service_van(g);objective_marker(g);
 if(g->transport_mode!=0&&g->job.stage==1&&g->parked_valid&&distance(g->parked_x,g->parked_z)<475){
  trailer_center_x=g->parked_x-sinf(g->parked_yaw)*RH_TRAILER_HITCH-sinf(g->parked_trailer_yaw)*RH_TRAILER_CENTER;
  trailer_center_z=g->parked_z-cosf(g->parked_yaw)*RH_TRAILER_HITCH-cosf(g->parked_trailer_yaw)*RH_TRAILER_CENTER;
  parked_rig_level=g->parked_level;
  draw_truck(g->parked_x,g->parked_z,g->parked_yaw,p->model,1,g->parked_trailer_yaw);
  parked_rig_level=-1;
 }
 trailer_center_x=p->trailer_x;trailer_center_z=p->trailer_z;
 if(g->on_foot||p->camera==0){
  if(g->transport_mode==0){personal_rig=!g->on_foot;draw_truck(p->x,p->z,p->yaw,p->model,g->job.stage==1,p->trailer_yaw);personal_rig=0;vehicle_lenses(p->x,rh_vehicle_height(p->x,p->z,p->yaw)+.1f,p->z,p->yaw,1);}
  else draw_light_transport(p->x,p->z,p->yaw,p->trailer_yaw,g->transport_mode,!g->on_foot,g->job.stage==1&&!g->parked_valid);
 }
 if(g->on_foot)draw_person(g->person_x,g->person_z,g->person_yaw,player_ground,g->sim_t);
 nsky=sky(g,sun);precipitation(g);hud(g);

 sceKernelDcacheWritebackRange(mesh,nmesh*sizeof(TexVertex));
 sceKernelDcacheWritebackRange(overlay,nover*sizeof(Vertex));
 sceGuStart(GU_DIRECT,command_list);
 sceGuClearColor(RGB(80,132,167));sceGuClearDepth(0);
 sceGuClear(GU_COLOR_BUFFER_BIT|GU_DEPTH_BUFFER_BIT);
 sceGuDisable(GU_DEPTH_TEST);sceGuDisable(GU_CLIP_PLANES);
 sceGuDrawArray(GU_TRIANGLES,GU_COLOR_8888|GU_VERTEX_32BITF|GU_TRANSFORM_2D,nsky,0,overlay);
 sceGuEnable(GU_DEPTH_TEST);sceGuEnable(GU_CLIP_PLANES);
 sceGumMatrixMode(GU_PROJECTION);sceGumLoadIdentity();
 sceGumPerspective(67.0f,(float)SW/(float)SH,.8f,620.0f);
 sceGumMatrixMode(GU_VIEW);sceGumLoadIdentity();sceGumLookAt(&eye,&target,&up);
 sceGumMatrixMode(GU_MODEL);sceGumLoadIdentity();
 sceGuEnable(GU_TEXTURE_2D);sceGuEnable(GU_BLEND);
 sceGuBlendFunc(GU_ADD,GU_SRC_ALPHA,GU_ONE_MINUS_SRC_ALPHA,0,0);
 sceGuTexMode(GU_PSM_8888,0,0,0);sceGuTexImage(0,32,32,32,detail_texture);
 sceGuTexFunc(GU_TFX_MODULATE,GU_TCC_RGBA);sceGuTexFilter(GU_NEAREST,GU_NEAREST);
 sceGuTexWrap(GU_REPEAT,GU_REPEAT);sceGuTexScale(1.0f/32,1.0f/32);sceGuTexOffset(0,0);
 sceGumDrawArray(GU_TRIANGLES,VERTEX_TYPE,nmesh,0,mesh);
 sceGuDisable(GU_TEXTURE_2D);sceGuDisable(GU_BLEND);
 sceGuDisable(GU_DEPTH_TEST);sceGuDisable(GU_CLIP_PLANES);
 sceGuDrawArray(GU_TRIANGLES,GU_COLOR_8888|GU_VERTEX_32BITF|GU_TRANSFORM_2D,nover-nsky,0,overlay+nsky);
 sceGuEnable(GU_DEPTH_TEST);sceGuEnable(GU_CLIP_PLANES);
 sceGuFinish();sceGuSync(GU_SYNC_FINISH,GU_SYNC_WHAT_DONE);
 sceDisplayWaitVblankStart();sceGuSwapBuffers();
}
void rh_render_end(void){sceGuTerm();}
