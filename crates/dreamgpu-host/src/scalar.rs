// SPDX-License-Identifier: GPL-2.0-or-later
//! Native fixed-function execution. Bindings are generated from platform GL headers;
//! resource hooks retain only texture/attribute/attachment operations during porting.
#![allow(non_upper_case_globals)]
use crate::gl_api::*;
use core::ffi::c_void;

pub type ResourceHook = unsafe extern "C" fn(*mut c_void, u32, *const u8) -> u32;

pub(crate) fn allowed_in_begin(function: u32) -> bool {
    matches!(
        function,
        FEnum_glEnd
            | FEnum_glEvalCoord1d
            | FEnum_glEvalCoord2d
            | FEnum_glEvalPoint1
            | FEnum_glEvalPoint2
            | FEnum_glEdgeFlag
            | FEnum_glIndexd
            | FEnum_glVertex2f
            | FEnum_glVertex3f
            | FEnum_glVertex4f
            | FEnum_glTexCoord4f
            | FEnum_glColor3f
            | FEnum_glColor4f
            | FEnum_glSecondaryColor3f
            | FEnum_glTexCoord2f
            | FEnum_glNormal3f
            | FEnum_glMaterialf
    )
}

unsafe fn execute(
    api: &DreamGpuGlApi,
    in_begin: *mut u32,
    hook: ResourceHook,
    opaque: *mut c_void,
    function: u32,
    args: &[u8],
) -> Result<(), u32> {
    let u = |n: usize| -> Result<u32, u32> {
        Ok(u32::from_le_bytes(
            args.get(n * 4..n * 4 + 4).ok_or(1u32)?.try_into().unwrap(),
        ))
    };
    let f = |n: usize| -> Result<f32, u32> { Ok(f32::from_bits(u(n)?)) };
    let d = |n: usize| -> Result<f64, u32> {
        Ok(f64::from_bits(
            u64::from(u(n)?) | (u64::from(u(n + 1)?) << 32),
        ))
    };
    if unsafe { *in_begin } != 0 && !allowed_in_begin(function) {
        return Err(4);
    }
    match function {
        FEnum_glPushAttrib
        | FEnum_glPopAttrib
        | FEnum_glDrawBuffer
        | FEnum_glReadBuffer
        | FEnum_glHint
        | FEnum_glBindTexture
        | FEnum_glCopyTexImage1D
        | FEnum_glCopyTexSubImage1D
        | FEnum_glCopyTexImage2D
        | FEnum_glCopyTexSubImage2D
        | FEnum_glTexParameteri
        | FEnum_glTexParameterf
        | FEnum_glTexEnvi
        | FEnum_glTexEnvf => {
            let error = unsafe { hook(opaque, function, args.as_ptr()) };
            if error != 0 {
                return Err(error);
            }
        }
        FEnum_glBegin => {
            let mode = u(0)?;
            if mode > 9 {
                return Err(4);
            } // GL_POLYGON is the last primitive.
            let begin = api.dg_glBegin.ok_or(3u32)?;
            let error = unsafe { hook(opaque, function, args.as_ptr()) };
            if error != 0 {
                return Err(error);
            }
            unsafe {
                *in_begin = 1;
                begin(mode);
            }
        }
        FEnum_glEnd => {
            if unsafe { *in_begin } == 0 {
                return Err(4);
            }
            unsafe {
                api.dg_glEnd.ok_or(3u32)?();
                *in_begin = 0;
            }
        }
        FEnum_glListBase => unsafe { api.dg_glListBase.ok_or(3u32)?(u(0)?) },
        FEnum_glInitNames => unsafe { api.dg_glInitNames.ok_or(3u32)?() },
        FEnum_glPopName => unsafe { api.dg_glPopName.ok_or(3u32)?() },
        FEnum_glLoadName => unsafe { api.dg_glLoadName.ok_or(3u32)?(u(0)?) },
        FEnum_glPushName => unsafe { api.dg_glPushName.ok_or(3u32)?(u(0)?) },
        FEnum_glPassThrough => unsafe { api.dg_glPassThrough.ok_or(3u32)?(f(0)?) },
        FEnum_glMapGrid1d => unsafe { api.dg_glMapGrid1d.ok_or(3u32)?(u(0)? as i32, d(1)?, d(3)?) },
        FEnum_glMapGrid2d => unsafe {
            api.dg_glMapGrid2d.ok_or(3u32)?(u(0)? as i32, d(1)?, d(3)?, u(5)? as i32, d(6)?, d(8)?)
        },
        FEnum_glEvalCoord1d => unsafe { api.dg_glEvalCoord1d.ok_or(3u32)?(d(0)?) },
        FEnum_glEvalCoord2d => unsafe { api.dg_glEvalCoord2d.ok_or(3u32)?(d(0)?, d(2)?) },
        FEnum_glEvalPoint1 => unsafe { api.dg_glEvalPoint1.ok_or(3u32)?(u(0)? as i32) },
        FEnum_glEvalPoint2 => unsafe {
            api.dg_glEvalPoint2.ok_or(3u32)?(u(0)? as i32, u(1)? as i32)
        },
        FEnum_glEvalMesh1 | FEnum_glEvalMesh2 => {
            let a = [
                u(0)?,
                u(1)?,
                u(2)?,
                if function == FEnum_glEvalMesh2 {
                    u(3)?
                } else {
                    0
                },
                if function == FEnum_glEvalMesh2 {
                    u(4)?
                } else {
                    0
                },
            ];
            if !crate::evaluator::mesh(function, &a) {
                return Err(DG_GL_ERROR_LIMIT);
            }
            if (function == FEnum_glEvalMesh1 && api.dg_glEvalMesh1.is_none())
                || (function == FEnum_glEvalMesh2 && api.dg_glEvalMesh2.is_none())
            {
                return Err(DG_GL_ERROR_UNSUPPORTED);
            }
            if crate::evaluator::empty(function, &a) {
                return Ok(());
            }
            // EvalMesh contains native Begin/End operations. Reuse the same
            // logical texture-definition and fence admission as an explicit Begin.
            let mode = GL_POINTS.to_le_bytes();
            let admission = unsafe { hook(opaque, FEnum_glBegin, mode.as_ptr()) };
            if admission != 0 {
                return Err(admission);
            }
            if crate::evaluator::terminal(function, &a) {
                return unsafe { crate::evaluator::terminal_mesh(api, function, &a) };
            }
            unsafe {
                if function == FEnum_glEvalMesh1 {
                    api.dg_glEvalMesh1.ok_or(3u32)?(a[0], a[1] as i32, a[2] as i32)
                } else {
                    api.dg_glEvalMesh2.ok_or(3u32)?(
                        a[0],
                        a[1] as i32,
                        a[2] as i32,
                        a[3] as i32,
                        a[4] as i32,
                    )
                }
            }
        }
        FEnum_glEdgeFlag => unsafe { api.dg_glEdgeFlag.ok_or(3u32)?((u(0)? != 0) as u8) },
        FEnum_glIndexd => unsafe { api.dg_glIndexd.ok_or(3u32)?(d(0)?) },
        FEnum_glClearAccum => unsafe {
            api.dg_glClearAccum.ok_or(3u32)?(f(0)?, f(1)?, f(2)?, f(3)?)
        },
        FEnum_glClearIndex => unsafe { api.dg_glClearIndex.ok_or(3u32)?(f(0)?) },
        FEnum_glIndexMask => unsafe { api.dg_glIndexMask.ok_or(3u32)?(u(0)?) },
        FEnum_glAccum => unsafe { api.dg_glAccum.ok_or(3u32)?(u(0)?, f(1)?) },
        FEnum_glLogicOp => unsafe { api.dg_glLogicOp.ok_or(3u32)?(u(0)?) },
        FEnum_glCopyPixels => unsafe {
            api.dg_glCopyPixels.ok_or(3u32)?(
                u(0)? as i32,
                u(1)? as i32,
                u(2)? as i32,
                u(3)? as i32,
                u(4)?,
            )
        },
        FEnum_glPixelZoom => unsafe { api.dg_glPixelZoom.ok_or(3u32)?(f(0)?, f(1)?) },
        FEnum_glPixelTransferf => unsafe { api.dg_glPixelTransferf.ok_or(3u32)?(u(0)?, f(1)?) },
        FEnum_glPixelTransferi => unsafe {
            api.dg_glPixelTransferi.ok_or(3u32)?(u(0)?, u(1)? as i32)
        },
        FEnum_glRasterPos4d => unsafe {
            api.dg_glRasterPos4d.ok_or(3u32)?(d(0)?, d(2)?, d(4)?, d(6)?)
        },
        FEnum_glClear => unsafe { api.dg_glClear.ok_or(3u32)?(u(0)? as _) },
        FEnum_glClearColor => unsafe {
            api.dg_glClearColor.ok_or(3u32)?(f(0)?, f(1)?, f(2)?, f(3)?)
        },
        FEnum_glClearDepth => unsafe { api.dg_glClearDepth.ok_or(3u32)?(d(0)?) },
        FEnum_glViewport => unsafe {
            api.dg_glViewport.ok_or(3u32)?(u(0)? as _, u(1)? as _, u(2)? as _, u(3)? as _)
        },
        FEnum_glScissor => unsafe {
            api.dg_glScissor.ok_or(3u32)?(u(0)? as _, u(1)? as _, u(2)? as _, u(3)? as _)
        },
        FEnum_glFlush => unsafe { api.dg_glFlush.ok_or(3u32)?() },
        FEnum_glFinish => unsafe { api.dg_glFinish.ok_or(3u32)?() },
        FEnum_glEnable => unsafe { api.dg_glEnable.ok_or(3u32)?(u(0)? as _) },
        FEnum_glDisable => unsafe { api.dg_glDisable.ok_or(3u32)?(u(0)? as _) },
        FEnum_glMatrixMode => unsafe { api.dg_glMatrixMode.ok_or(3u32)?(u(0)? as _) },
        FEnum_glLoadIdentity => unsafe { api.dg_glLoadIdentity.ok_or(3u32)?() },
        FEnum_glPushMatrix => unsafe { api.dg_glPushMatrix.ok_or(3u32)?() },
        FEnum_glPopMatrix => unsafe { api.dg_glPopMatrix.ok_or(3u32)?() },
        FEnum_glDepthFunc => unsafe { api.dg_glDepthFunc.ok_or(3u32)?(u(0)? as _) },
        FEnum_glDepthMask => unsafe { api.dg_glDepthMask.ok_or(3u32)?((u(0)? != 0) as _) },
        FEnum_glDepthRange => unsafe { api.dg_glDepthRange.ok_or(3u32)?(d(0)?, d(2)?) },
        FEnum_glColorMask => unsafe {
            api.dg_glColorMask.ok_or(3u32)?(
                (u(0)? != 0) as _,
                (u(1)? != 0) as _,
                (u(2)? != 0) as _,
                (u(3)? != 0) as _,
            )
        },
        FEnum_glAlphaFunc => unsafe { api.dg_glAlphaFunc.ok_or(3u32)?(u(0)? as _, f(1)?) },
        FEnum_glClearStencil => unsafe { api.dg_glClearStencil.ok_or(3u32)?(u(0)? as _) },
        FEnum_glStencilMask => unsafe { api.dg_glStencilMask.ok_or(3u32)?(u(0)? as _) },
        FEnum_glStencilFunc => unsafe {
            api.dg_glStencilFunc.ok_or(3u32)?(u(0)? as _, u(1)? as _, u(2)? as _)
        },
        FEnum_glStencilOp => unsafe {
            api.dg_glStencilOp.ok_or(3u32)?(u(0)? as _, u(1)? as _, u(2)? as _)
        },
        FEnum_glCullFace => unsafe { api.dg_glCullFace.ok_or(3u32)?(u(0)? as _) },
        FEnum_glFrontFace => unsafe { api.dg_glFrontFace.ok_or(3u32)?(u(0)? as _) },
        FEnum_glPolygonMode => unsafe { api.dg_glPolygonMode.ok_or(3u32)?(u(0)? as _, u(1)? as _) },
        FEnum_glPolygonOffset => unsafe { api.dg_glPolygonOffset.ok_or(3u32)?(f(0)?, f(1)?) },
        FEnum_glLineWidth => unsafe { api.dg_glLineWidth.ok_or(3u32)?(f(0)?) },
        FEnum_glLineStipple => unsafe { api.dg_glLineStipple.ok_or(3u32)?(u(0)? as _, u(1)? as _) },
        FEnum_glPointSize => unsafe { api.dg_glPointSize.ok_or(3u32)?(f(0)?) },
        FEnum_glShadeModel => unsafe { api.dg_glShadeModel.ok_or(3u32)?(u(0)? as _) },
        FEnum_glNormal3f => unsafe { api.dg_glNormal3f.ok_or(3u32)?(f(0)?, f(1)?, f(2)?) },
        FEnum_glColorMaterial => unsafe {
            api.dg_glColorMaterial.ok_or(3u32)?(u(0)? as _, u(1)? as _)
        },
        FEnum_glFogf => unsafe { api.dg_glFogf.ok_or(3u32)?(u(0)? as _, f(1)?) },
        FEnum_glLightModelf => unsafe { api.dg_glLightModelf.ok_or(3u32)?(u(0)? as _, f(1)?) },
        FEnum_glLightf => unsafe { api.dg_glLightf.ok_or(3u32)?(u(0)? as _, u(1)? as _, f(2)?) },
        FEnum_glMaterialf => unsafe {
            api.dg_glMaterialf.ok_or(3u32)?(u(0)? as _, u(1)? as _, f(2)?)
        },
        FEnum_glTexGenf => unsafe { api.dg_glTexGenf.ok_or(3u32)?(u(0)? as _, u(1)? as _, f(2)?) },
        FEnum_glBlendFunc => unsafe { api.dg_glBlendFunc.ok_or(3u32)?(u(0)? as _, u(1)? as _) },
        FEnum_glVertex2f => unsafe { api.dg_glVertex2f.ok_or(3u32)?(f(0)?, f(1)?) },
        FEnum_glTexCoord2f => unsafe { api.dg_glTexCoord2f.ok_or(3u32)?(f(0)?, f(1)?) },
        FEnum_glTexCoord4f => unsafe {
            api.dg_glTexCoord4f.ok_or(3u32)?(f(0)?, f(1)?, f(2)?, f(3)?)
        },
        FEnum_glVertex3f => unsafe { api.dg_glVertex3f.ok_or(3u32)?(f(0)?, f(1)?, f(2)?) },
        FEnum_glVertex4f => unsafe { api.dg_glVertex4f.ok_or(3u32)?(f(0)?, f(1)?, f(2)?, f(3)?) },
        FEnum_glSecondaryColor3f => unsafe {
            api.dg_glSecondaryColor3f.ok_or(3u32)?(f(0)?, f(1)?, f(2)?)
        },
        FEnum_glColor3f => unsafe { api.dg_glColor3f.ok_or(3u32)?(f(0)?, f(1)?, f(2)?) },
        FEnum_glColor4f => unsafe { api.dg_glColor4f.ok_or(3u32)?(f(0)?, f(1)?, f(2)?, f(3)?) },
        FEnum_glTranslatef => unsafe { api.dg_glTranslatef.ok_or(3u32)?(f(0)?, f(1)?, f(2)?) },
        FEnum_glScalef => unsafe { api.dg_glScalef.ok_or(3u32)?(f(0)?, f(1)?, f(2)?) },
        FEnum_glRotatef => unsafe { api.dg_glRotatef.ok_or(3u32)?(f(0)?, f(1)?, f(2)?, f(3)?) },
        FEnum_glOrtho => unsafe {
            api.dg_glOrtho.ok_or(3u32)?(d(0)?, d(2)?, d(4)?, d(6)?, d(8)?, d(10)?)
        },
        FEnum_glFrustum => unsafe {
            api.dg_glFrustum.ok_or(3u32)?(d(0)?, d(2)?, d(4)?, d(6)?, d(8)?, d(10)?)
        },
        FEnum_glLoadMatrixf | FEnum_glMultMatrixf => {
            let mut matrix = [0.0f32; 16];
            for (i, v) in matrix.iter_mut().enumerate() {
                *v = f(i)?;
            }
            let op = if function == FEnum_glLoadMatrixf {
                api.dg_glLoadMatrixf
            } else {
                api.dg_glMultMatrixf
            };
            unsafe {
                op.ok_or(3u32)?(matrix.as_ptr());
            }
        }
        FEnum_glLoadMatrixd | FEnum_glMultMatrixd => {
            let mut matrix = [0.0f64; 16];
            for (i, v) in matrix.iter_mut().enumerate() {
                *v = d(i * 2)?;
            }
            let op = if function == FEnum_glLoadMatrixd {
                api.dg_glLoadMatrixd
            } else {
                api.dg_glMultMatrixd
            };
            unsafe {
                op.ok_or(3u32)?(matrix.as_ptr());
            }
        }
        _ => return Err(3),
    }
    Ok(())
}

/// # Safety
/// Current native GL context belongs to the invoking render worker. `api` contains
/// generated platform GL function pointer types; `in_begin` is its render-owned
/// uint32 state. Args are immutable validated words. Resource hooks may update the
/// context but must not reenter this function; no state reference crosses a call.
#[no_mangle]
pub unsafe extern "C" fn dreamgpu_gl_scalar(
    api: *const DreamGpuGlApi,
    in_begin: *mut u32,
    hook: Option<ResourceHook>,
    opaque: *mut c_void,
    function: u32,
    args: *const u8,
    bytes: usize,
) -> u32 {
    if api.is_null() || in_begin.is_null() || args.is_null() || hook.is_none() || bytes > 128 {
        return 1;
    }
    match unsafe {
        execute(
            &*api,
            in_begin,
            hook.unwrap(),
            opaque,
            function,
            core::slice::from_raw_parts(args, bytes),
        )
    } {
        Ok(()) => 0,
        Err(error) => error,
    }
}

#[cfg(test)]
mod tests;
