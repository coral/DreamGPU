#!/usr/bin/env python3
"""Exercise the actual Wine adapter: packed RHW/colors, indices and fallback."""
from pathlib import Path
import os
import subprocess
import tempfile
ROOT = Path(__file__).resolve().parents[3]
CODE = r'''
#include <cassert>
#include <cstdint>
#include <cstring>
#include <cmath>
#include <vector>
using UINT=unsigned; using GLenum=unsigned; using BOOL=int;
#define TRUE 1
#define FALSE 0
#define GL_EXTCALL(name) gl_info->gl_ops.ext.p_##name
#define WINED3D_FFP_POSITION 0
#define WINED3D_FFP_DIFFUSE 1
#define WINED3D_FFP_SPECULAR 2
#define WINED3D_FFP_TEXCOORD0 3
#define WINED3D_RS_FOGENABLE 0
#define WINED3D_RS_FOGTABLEMODE 1
#define WINED3D_TSS_TEXCOORD_INDEX 0
#define WINED3D_FOG_NONE 0
#define WINED3DFMT_R32G32B32A32_FLOAT 1
#define WINED3DFMT_B8G8R8A8_UNORM 2
#define WINED3DFMT_R32G32_FLOAT 3
#define ARB_MULTITEXTURE 0
#define ARB_VERTEX_BUFFER_OBJECT 1
#define EXT_SECONDARY_COLOR 2
#define EXT_FOG_COORD 3
#define GL_FLOAT 1
#define GL_UNSIGNED_BYTE 2
#define GL_VERTEX_ARRAY 0
#define GL_COLOR_ARRAY 1
#define GL_TEXTURE_COORD_ARRAY 2
#define GL_SECONDARY_COLOR_ARRAY_EXT 3
#include "dg-wine-vertex-pack.h"
static const void *pointers[4]; static unsigned strides[4], enabled;
static unsigned draws, calls; static std::vector<dg_wine_vertex> seen;
static unsigned char last_color[4], last_secondary[3]; static float last_uv[2];
static void vp(int,unsigned,unsigned s,const void*p){++calls;pointers[0]=p;strides[0]=s;}
static void cp(int,unsigned,unsigned s,const void*p){++calls;pointers[1]=p;strides[1]=s;}
static void tp(int,unsigned,unsigned s,const void*p){++calls;pointers[2]=p;strides[2]=s;}
static void sp(int,unsigned,unsigned s,const void*p){++calls;pointers[3]=p;strides[3]=s;}
static void en(unsigned i){++calls; enabled|=1u<<i;}
static void dis(unsigned i){++calls; enabled&=~(1u<<i);}
static void normal(float x,float y,float z){++calls;assert(x==0&&y==0&&z==0);}
static void secondary(float x,float y,float z){++calls;assert(x==0&&y==0&&z==0);}
static void color(unsigned char r,unsigned char g,unsigned char b,unsigned char a){++calls;last_color[0]=r;last_color[1]=g;last_color[2]=b;last_color[3]=a;}
static void sec(const unsigned char*p){++calls;memcpy(last_secondary,p,3);}
static void uv(const float*p){++calls;memcpy(last_uv,p,8);}
static void draw(unsigned primitive,int first,unsigned n){
    ++calls;++draws; assert(primitive==99&&first==0&&(enabled&7)==7);
    seen.clear();for(unsigned i=0;i<n;++i) {
        dg_wine_vertex v{};
        memcpy(v.position,(const char*)pointers[0]+i*strides[0],16);
        memcpy(v.diffuse,(const char*)pointers[1]+i*strides[1],4);
        memcpy(v.texture,(const char*)pointers[2]+i*strides[2],8);
        if(enabled&8)memcpy(v.specular,(const char*)pointers[3]+i*strides[3],3);
        seen.push_back(v);
    }
}
struct gl_functions {
 decltype(&normal) p_glNormal3f=normal;
 decltype(&vp) p_glVertexPointer=vp,p_glColorPointer=cp,p_glTexCoordPointer=tp;
 decltype(&en) p_glEnableClientState=en,p_glDisableClientState=dis;
 decltype(&draw) p_glDrawArrays=draw;
 decltype(&color) p_glColor4ub=color;
 decltype(&uv) p_glTexCoord2fv=uv;
};
struct ext_functions {
 decltype(&secondary) p_glSecondaryColor3fEXT=secondary;
 decltype(&sp) p_glSecondaryColorPointerEXT=sp;
 decltype(&sec) p_glSecondaryColor3ubvEXT=sec;
};
struct wined3d_gl_info {bool supported[4]{};struct {unsigned textures=1,texture_coords=1;}limits;struct {gl_functions gl;ext_functions ext;}gl_ops;};
struct format {unsigned id;};
struct wined3d_stream_info_element {const format *format;struct {const void*addr;unsigned buffer_object;}data;unsigned stride;};
struct wined3d_stream_info {wined3d_stream_info_element elements[4];unsigned use_map;bool position_transformed;};
struct wined3d_state {unsigned render_states[2]{},texture_states[1][1]{};void*textures[1];int base_vertex_index=0;bool ps=false;};
static bool use_ps(const wined3d_state*s){return s->ps;}
struct d3d_info {bool xyzrhw=false;struct {unsigned ffp_blend_stages=1;}limits;};
struct wined3d_context {const wined3d_gl_info*gl_info;d3d_info*d3d_info; bool use_immediate_mode_draw=true,namedArraysLoaded=false,numberedArraysLoaded=false;unsigned num_untracked_materials=0;bool fog_coord=false;unsigned tex_unit_map[1]{};};
struct wined3d_device {wined3d_state state;};
#include "wine-vertex-array.h"
int main(){
 wined3d_gl_info gl;gl.supported[EXT_SECONDARY_COLOR]=true;d3d_info di;wined3d_context ctx{&gl,&di};
 wined3d_device dev;dev.state.textures[0]=&dev;
 struct Input {float pos[4],uv[2];unsigned char color[4],spec[4];};
 Input data[4]={{{2,4,6,2},{.25f,.5f},{1,2,3,4},{5,6,7,8}},
 {{3,6,9,0},{.75f,1},{9,10,11,12},{13,14,15,16}},
 {{4,8,12,1},{1.25f,1.5f},{17,18,19,20},{21,22,23,24}},
 {{5,10,15,-2},{1.75f,2},{25,26,27,28},{29,30,31,32}}};
 format pf{1},cf{2},tf{3};
 wined3d_stream_info si{{{&pf,{data[0].pos,0},sizeof(Input)},
 {&cf,{data[0].color,0},sizeof(Input)},{&cf,{data[0].spec,0},sizeof(Input)},
 {&tf,{data[0].uv,0},sizeof(Input)}},15,true};
 assert(dg_wine_draw_transformed(&dev,&ctx,&si,4,99,nullptr,0,0));
 assert(draws==1&&enabled==0);for(auto p:pointers)assert(!p);
 const float expected[4][4]={{1,2,3,.5f},{3,6,9,1},{4,8,12,1},{-2.5f,-5,-7.5f,-.5f}};
 for(unsigned i=0;i<4;++i){assert(!memcmp(seen[i].position,expected[i],16));assert(!memcmp(seen[i].texture,data[i].uv,8));assert(seen[i].diffuse[0]==data[i].color[2]&&seen[i].diffuse[3]==data[i].color[3]);assert(seen[i].specular[0]==data[i].spec[2]);}
 assert(last_color[0]==27&&last_secondary[0]==31&&last_uv[0]==1.75f);
 uint16_t indices[]={999,4,2,1};dev.state.base_vertex_index=-1;
 assert(dg_wine_draw_transformed(&dev,&ctx,&si,3,99,indices,2,1));
 assert(seen[0].position[0]==-2.5f&&seen[1].position[0]==3&&seen[2].position[0]==1);
 uint32_t wide[]={3,0};dev.state.base_vertex_index=0;
 assert(dg_wine_draw_transformed(&dev,&ctx,&si,2,99,wide,4,0));assert(seen[0].position[0]==-2.5f);
 gl.supported[ARB_MULTITEXTURE]=true;si.use_map |= 1u<<(WINED3D_FFP_TEXCOORD0+1);
 assert(dg_wine_draw_transformed(&dev,&ctx,&si,4,99,nullptr,0,0));
 unsigned saved=calls;
 gl.limits.textures=2;assert(!dg_wine_draw_transformed(&dev,&ctx,&si,4,99,nullptr,0,0));gl.limits.textures=1;
 di.limits.ffp_blend_stages=2;assert(!dg_wine_draw_transformed(&dev,&ctx,&si,4,99,nullptr,0,0));di.limits.ffp_blend_stages=1;
 assert(!dg_wine_draw_transformed(&dev,&ctx,&si,257,99,(void*)1,2,0));
 ctx.namedArraysLoaded=true;assert(!dg_wine_draw_transformed(&dev,&ctx,&si,4,99,nullptr,0,0));ctx.namedArraysLoaded=false;
 ctx.num_untracked_materials=1;assert(!dg_wine_draw_transformed(&dev,&ctx,&si,4,99,nullptr,0,0));ctx.num_untracked_materials=0;
 gl.supported[EXT_FOG_COORD]=true;dev.state.render_states[0]=1;assert(!dg_wine_draw_transformed(&dev,&ctx,&si,4,99,nullptr,0,0));dev.state.render_states[0]=0;
 gl.supported[ARB_VERTEX_BUFFER_OBJECT]=true;assert(!dg_wine_draw_transformed(&dev,&ctx,&si,4,99,nullptr,0,0));gl.supported[ARB_VERTEX_BUFFER_OBJECT]=false;
 dev.state.base_vertex_index=-5;assert(!dg_wine_draw_transformed(&dev,&ctx,&si,2,99,wide,4,0));dev.state.base_vertex_index=0;
 assert(saved==calls);si.use_map &= ~(1u<<WINED3D_FFP_SPECULAR);
 assert(dg_wine_draw_transformed(&dev,&ctx,&si,4,99,nullptr,0,0));assert(enabled==0);
 const void *out=nullptr;assert(!dg_wine_vertex_address((void*)(UINTPTR_MAX-3),16,1,4,&out));
 assert(!dg_wine_vertex_address(data,UINT32_MAX,INT64_MAX,4,&out));
}
'''
with tempfile.TemporaryDirectory(prefix='dreamgpu-vertex-arrays-') as tmp:
    tmp = Path(tmp)
    (tmp/'test.cpp').write_text(CODE)
    (tmp/'dg-wine-vertex-pack.h').write_text((ROOT/'guest/d3d/wine-vertex-pack.h').read_text())
    exe=tmp/'test'
    subprocess.run([os.environ.get('CXX','clang++'),'-std=c++23','-O1','-g','-Wall','-Wextra','-Werror',
                    '-fsanitize=address,undefined','-I'+str(tmp),'-I'+str(ROOT/'guest/d3d'),str(tmp/'test.cpp'),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True)
print('PASS actual Wine vertex-array conversion, indices, state restoration and fallback')
