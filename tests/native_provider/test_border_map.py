#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Execute actual private-provider border mapping with malformed bounds."""
from pathlib import Path
import subprocess,sys,tempfile

source=Path(sys.argv[1]).read_text()
start=source.index('   if (stImage->border_data) {', source.index('st_texture_image_map('))
end=source.index('   if (!stImage->pt)',start)
branch=source[start:end]
prefix=r'''
#include <stdint.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
typedef unsigned GLuint;typedef unsigned char GLubyte;
struct pipe_transfer {unsigned usage,stride;};
struct gl_texture_image {uint8_t *border_data;unsigned Width,TexFormat;struct pipe_transfer border_transfer;};
static unsigned _mesa_format_row_stride(unsigned f,unsigned w){assert(f==1);return w*4;}
static unsigned _mesa_get_format_bytes(unsigned f){assert(f==1);return 4;}
static GLubyte *map(struct gl_texture_image *stImage,unsigned usage,GLuint x,GLuint y,GLuint z,GLuint w,GLuint h,GLuint d,struct pipe_transfer **transfer){
'''
tests=r'''
return NULL;}
int main(void){
 uint8_t pixels[24];memset(pixels,0x5a,sizeof pixels);struct gl_texture_image im={pixels,6,1,{0,0}};struct pipe_transfer *t=NULL;
 const unsigned bad[][6]={{(unsigned)-1,0,0,1,1,1},{0,0,0,(unsigned)-1,1,1},{7,0,0,0,1,1},{5,0,0,2,1,1},{0,(unsigned)-1,0,1,1,1},{0,0,(unsigned)-1,1,1,1},{0,0,0,1,0,1},{0,0,0,1,1,2}};
 for(unsigned i=0;i<sizeof bad/sizeof*bad;i++){const unsigned*a=bad[i];assert(!map(&im,2,a[0],a[1],a[2],a[3],a[4],a[5],&t));assert(!im.border_transfer.usage&&!t);}
 assert(map(&im,2,5,0,0,1,1,1,&t)==pixels+20);assert(t==&im.border_transfer&&t->usage==2&&t->stride==24);
 im.border_transfer.usage=0;assert(map(&im,1,0,0,0,6,1,1,&t)==pixels);im.border_transfer.usage=0;
 assert(map(&im,1,6,0,0,0,1,1,&t)==pixels+24);
 for(unsigned i=0;i<24;i++)assert(pixels[i]==0x5a);
 puts("PASS actual unsigned image-map bounds: negative casts, overflow, all dimensions, terminal border, read-only access");
}
'''
with tempfile.TemporaryDirectory(prefix='dg-border-map-') as d:
    p=Path(d);(p/'test.c').write_text(prefix+branch+tests)
    subprocess.run(['cc','-std=c11','-O1','-g','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',str(p/'test.c'),'-o',str(p/'test')],check=True)
    subprocess.run([str(p/'test')],check=True)
