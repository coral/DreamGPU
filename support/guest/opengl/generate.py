#!/usr/bin/env python3
"""Generate only the implemented, pointer-free scalar OpenGL exports."""
from pathlib import Path
ROOT=Path(__file__).resolve().parents[3]/"guest/opengl"
# Name, public C types, native scalar words. Double arguments occupy two words;
# GLboolean arguments are widened individually, never copied as packed bytes.
calls={
 'glClearColor':['GLclampf']*4,'glClear':['GLbitfield'],
 'glEnable':['GLenum'],'glDisable':['GLenum'],
 'glViewport':['GLint','GLint','GLsizei','GLsizei'],'glScissor':['GLint','GLint','GLsizei','GLsizei'],
 'glMatrixMode':['GLenum'],'glLoadIdentity':[],'glPushMatrix':[],'glPopMatrix':[],
 'glTranslatef':['GLfloat']*3,'glRotatef':['GLfloat']*4,'glScalef':['GLfloat']*3,
 'glOrtho':['GLdouble']*6,'glFrustum':['GLdouble']*6,
 'glVertex4f':['GLfloat']*4,'glTexCoord4f':['GLfloat']*4,'glVertex2f':['GLfloat']*2,'glVertex3f':['GLfloat']*3,'glColor3f':['GLfloat']*3,'glColor4f':['GLfloat']*4,
 'glSecondaryColor3fEXT':['GLfloat']*3,'glNormal3f':['GLfloat']*3,'glTexCoord2f':['GLfloat']*2,
 'glBlendFunc':['GLenum']*2,'glAlphaFunc':['GLenum','GLclampf'],
 'glDepthFunc':['GLenum'],'glDepthMask':['GLboolean'],'glClearDepth':['GLclampd'],'glDepthRange':['GLclampd']*2,
 'glColorMask':['GLboolean']*4,'glClearStencil':['GLint'],'glStencilMask':['GLuint'],
 'glStencilFunc':['GLenum','GLint','GLuint'],'glStencilOp':['GLenum']*3,
 'glCullFace':['GLenum'],'glFrontFace':['GLenum'],'glShadeModel':['GLenum'],
 'glPolygonMode':['GLenum']*2,'glPolygonOffset':['GLfloat']*2,
 'glLineWidth':['GLfloat'],'glPointSize':['GLfloat'],'glLineStipple':['GLint','GLushort'],
 'glTexParameteri':['GLenum','GLenum','GLint'],
 'glTexParameterf':['GLenum','GLenum','GLfloat'],'glTexEnvi':['GLenum','GLenum','GLint'],
 'glTexEnvf':['GLenum','GLenum','GLfloat'],'glColorMaterial':['GLenum']*2,
 'glLightf':['GLenum','GLenum','GLfloat'],'glMaterialf':['GLenum','GLenum','GLfloat'],
 'glFogf':['GLenum','GLfloat'],'glLightModelf':['GLenum','GLfloat'],'glTexGenf':['GLenum','GLenum','GLfloat'],
 'glHint':['GLenum','GLenum'],
 'glCopyTexImage2D':['GLenum','GLint','GLenum','GLint','GLint','GLsizei','GLsizei','GLint'],
 'glCopyTexSubImage2D':['GLenum','GLint','GLint','GLint','GLint','GLint','GLsizei','GLsizei'],
 # ARB_multitexture: the frontend checks negotiation and the unit enum.
 'glActiveTexture':['GLenum'],'glActiveTextureARB':['GLenum'],
 'glMultiTexCoord4f':['GLenum']+['GLfloat']*4,'glMultiTexCoord4fARB':['GLenum']+['GLfloat']*4,
 # Fixed-function state: the frontend checks negotiation; the host validates enums.
 'glBlendColor':['GLclampf']*4,'glBlendColorEXT':['GLclampf']*4,
 'glBlendEquation':['GLenum'],'glBlendEquationEXT':['GLenum'],
 'glBlendFuncSeparate':['GLenum']*4,'glBlendFuncSeparateEXT':['GLenum']*4,
 'glBlendEquationSeparate':['GLenum']*2,'glBlendEquationSeparateEXT':['GLenum']*2,
 'glStencilOpSeparate':['GLenum']*4,'glStencilOpSeparateATI':['GLenum']*4,
 'glStencilFuncSeparate':['GLenum','GLenum','GLint','GLuint'],
 'glPointParameterf':['GLenum','GLfloat'],'glPointParameterfARB':['GLenum','GLfloat'],
 'glPointParameterfEXT':['GLenum','GLfloat'],
 # EXT_fog_coord: the frontend checks negotiation; legal inside Begin/End.
 'glFogCoordf':['GLfloat'],'glFogCoordfEXT':['GLfloat'],
 # EXT_texture3D copies: the frontend checks negotiation, target, level and size.
 'glCopyTexSubImage3D':['GLenum','GLint','GLint','GLint','GLint','GLint','GLint','GLsizei','GLsizei'],
 'glCopyTexSubImage3DEXT':['GLenum','GLint','GLint','GLint','GLint','GLint','GLint','GLsizei','GLsizei'],
}
exports={'wglCreateContext':4,'wglMakeCurrent':8,'wglDeleteContext':4,'wglGetCurrentContext':0,'wglGetCurrentDC':0,
 'wglGetPixelFormat':4,'wglShareLists':8,'wglSwapBuffers':4,'wglChoosePixelFormat':8,'wglDescribePixelFormat':16,'wglSetPixelFormat':12,
 'glBindTexture':8,'glBegin':4,'glEnd':0,'glFlush':0,'glFinish':0,'glGetError':0}
exports.update({'glPixelStorei':8,'glTexImage2D':36,'glTexSubImage2D':36,'glGetIntegerv':8,'glGetBooleanv':8,'glGetFloatv':8,'glGetDoublev':8,
 'glGetTexParameterfv':12,'glGetTexParameteriv':12,'glGetTexEnvfv':12,'glGetTexEnviv':12,
 'glGetTexLevelParameterfv':16,'glGetTexLevelParameteriv':16,'glIsEnabled':4,'glIsTexture':4,'glGetString':4})
exports.update({'glVertex3fv':4,'glColor3fv':4,'glColor4fv':4,'glTexCoord2fv':4,'glNormal3fv':4,
 'glColor4ub':16,'glColor3ubv':4,'glColor4ubv':4,'glFogfv':8,'glFogi':8,'glDeleteTextures':8,
 'wglGetProcAddress':4,'glDrawBuffer':4,'glReadBuffer':4,'glReadPixels':28})
exports.update({'glVertexPointer':16,'glColorPointer':16,'glNormalPointer':12,'glTexCoordPointer':16,
 'glEnableClientState':4,'glDisableClientState':4,'glGetPointerv':8,'glDrawArrays':12,'glDrawElements':16})
exports.update({name:12 for name in ('glTexParameterfv','glTexParameteriv','glTexEnvfv','glTexEnviv','glLightfv','glMaterialfv','glTexGenfv','glTexGendv','glTexGeni','glGetLightfv','glGetLightiv','glGetMaterialfv','glGetMaterialiv','glGetTexGenfv','glGetTexGeniv','glGetTexGendv')})
exports.update({'glGenTextures':8,'glLightModelfv':8,'glLightModeliv':8,'glClipPlane':8,'glGetClipPlane':8,'glVertex2fv':4,'glVertex4fv':4,'glTexCoord4fv':4})
exports.update({'glVertex2i':8,'glVertex2sv':4,'glVertex4s':16,'glTexCoord1f':4,'glTexCoord3f':12,
 'glTexCoord1fv':4,'glTexCoord3fv':4,'glTexCoord2sv':4,'glTexCoord4sv':4,'glColor4sv':4,'glColor4usv':4,'glFogiv':8,'glLightModeli':8})
exports.update({'glPushAttrib':4,'glPopAttrib':0,'glGetTexImage':20,'glTexImage1D':32,'glTexSubImage1D':28})
# Shared ICD implementations linked into the private Wine provider.
exports.update({'glPushClientAttrib':4,'glPopClientAttrib':0,'glPixelTransferf':8,
 'glPixelTransferi':8,'glPixelZoom':8,'glRasterPos3f':12,'glDrawPixels':20})
exports['glSecondaryColorPointerEXT']=16
for suffix in ('b','s','i','d','ub','us','ui'):
 exports['glSecondaryColor3'+suffix+'EXT']=24 if suffix=='d' else 12
 exports['glSecondaryColor3'+suffix+'vEXT']=4
exports['glSecondaryColor3fvEXT']=4
# ARB_multitexture under core and ARB names. ActiveTexture and MultiTexCoord4f
# bodies are generated scalar calls below, with these same argument sizes.
for suffix in ('','ARB'):
 exports['glActiveTexture'+suffix]=4
 exports['glClientActiveTexture'+suffix]=4
 for count in range(1,5):
  for kind,size in (('f',4),('d',8),('i',4),('s',4)):
   exports[f'glMultiTexCoord{count}{kind}{suffix}']=4+count*size
   exports[f'glMultiTexCoord{count}{kind}v{suffix}']=8
# Fixed-function state. StencilFuncSeparateATI and PointParameterfv are written
# by hand (fixed.cpp); the other bodies are generated below with these sizes.
for name,size in (('glBlendColor',16),('glBlendEquation',4),('glBlendFuncSeparate',16),
                  ('glBlendEquationSeparate',8)):
 for suffix in ('','EXT'):
  exports[name+suffix]=size
for suffix in ('','ATI'):
 exports['glStencilOpSeparate'+suffix]=16
 exports['glStencilFuncSeparate'+suffix]=16
for suffix in ('','ARB','EXT'):
 exports['glPointParameterf'+suffix]=8
 exports['glPointParameterfv'+suffix]=8
# ARB_texture_compression, TEXTURE_2D only, written by hand (compressed.cpp).
# No 1D/3D variants: lookup must not find what the host cannot execute.
for suffix in ('','ARB'):
 exports['glCompressedTexImage2D'+suffix]=32
 exports['glCompressedTexSubImage2D'+suffix]=36
 exports['glGetCompressedTexImage'+suffix]=12
# EXT_fog_coord under core and EXT names. FogCoordf bodies are generated below;
# the vector/double forms (fog.cpp) and the pointer (arrays.cpp) are by hand.
for suffix in ('','EXT'):
 for name,size in (('glFogCoordf',4),('glFogCoordfv',4),('glFogCoordd',8),('glFogCoorddv',4),
                   ('glFogCoordPointer',12)):
  exports[name+suffix]=size
# ARB_fragment_program under ARB names only, written by hand (program.cpp).
for name,size in (('glGenProgramsARB',8),('glDeleteProgramsARB',8),('glBindProgramARB',8),
                  ('glIsProgramARB',4),('glProgramStringARB',16)):
 exports[name]=size
for kind in ('Env','Local'):
 for suffix,size in (('4f',24),('4fv',12),('4d',40),('4dv',12)):
  exports[f'glProgram{kind}Parameter{suffix}ARB']=size
for kind in ('Env','Local'):
 for suffix in ('fv','dv'):
  exports[f'glGetProgram{kind}Parameter{suffix}ARB']=12
exports['glGetProgramivARB']=12
exports['glGetProgramStringARB']=12
# ARB_vertex_program generic attributes under ARB names only, by hand (attrib.cpp);
# the program functions above serve its target too.
for count in range(1,5):
 for kind,size in (('s',4),('f',4),('d',8)):
  exports[f'glVertexAttrib{count}{kind}ARB']=4+count*size
  if count<4 or kind!='s':
   exports[f'glVertexAttrib{count}{kind}vARB']=8
for kind in ('b','s','i','ub','us','ui'):
 exports[f'glVertexAttrib4{kind}vARB']=8
 exports[f'glVertexAttrib4N{kind}vARB']=8
exports['glVertexAttrib4NubARB']=20
exports['glVertexAttribPointerARB']=24
exports['glEnableVertexAttribArrayARB']=4
exports['glDisableVertexAttribArrayARB']=4
for kind in ('d','f','i'):
 exports[f'glGetVertexAttrib{kind}vARB']=12
exports['glGetVertexAttribPointervARB']=12
# EXT_texture3D images under core and EXT names, written by hand (texture.cpp);
# the copies are generated scalar calls above.
for suffix in ('','EXT'):
 exports['glTexImage3D'+suffix]=40
 exports['glTexSubImage3D'+suffix]=44
# ARB_vertex_buffer_object under ARB names only, written by hand (buffer.cpp).
for name,size in (('glGenBuffersARB',8),('glBindBufferARB',8),('glDeleteBuffersARB',8),
                  ('glIsBufferARB',4),('glBufferDataARB',16),('glBufferSubDataARB',16),
                  ('glGetBufferSubDataARB',16),('glMapBufferARB',8),('glUnmapBufferARB',4),
                  ('glGetBufferParameterivARB',12),('glGetBufferPointervARB',12)):
 exports[name]=size
lines=['/* Generated by generate.py; do not add unimplemented exports. */']
for name,types in calls.items():
 parameters=', '.join(f'{ty} a{i}' for i,ty in enumerate(types)) or 'void'
 words=sum(2 if ty in ('GLdouble','GLclampd') else 1 for ty in types)
 lines.append(f'JGL_PACKING_ONLY void APIENTRY {name}({parameters}) {{')
 if types:
  lines.append(f'    ULONG words[{words}];');at=0
  for i,ty in enumerate(types):
   count=2 if ty in ('GLdouble','GLclampd') else 1
   # The packing-only target keeps these fixed-size bit copies in integer
   # registers, without byte-loop memcpy or x87 argument conversions.
   if ty in ('GLfloat','GLclampf','GLdouble','GLclampd'):lines.append(f'    __builtin_memcpy(words + {at}, &a{i}, {count*4});')
   else:lines.append(f'    words[{at}] = (ULONG)a{i};')
   at+=count
 # Extension aliases submit their core function.
 native_name = name[:-3] if name.endswith(('ARB','EXT','ATI')) else name
 lines.append(f'    Scalar(FEnum_{native_name}, {words}, '+('words' if types else 'NULL')+');\n}')
 exports[name]=words*4
for name in ('glLoadMatrixf','glMultMatrixf','glLoadMatrixd','glMultMatrixd'):
 ty, count = ('GLdouble',32) if name.endswith('d') else ('GLfloat',16)
 lines.append(f'JGL_PACKING_ONLY void APIENTRY {name}(const {ty} *matrix) {{ if (matrix) Scalar(FEnum_{name}, {count}, matrix); }}')
 exports[name]=4
(ROOT/'scalar.inc').write_text('\n'.join(lines)+'\n')
(ROOT/'frontend.def').write_text('LIBRARY dgpugl\nEXPORTS\n'+''.join(f'    {name}={name}@{size}\n' for name,size in exports.items()))
