// SPDX-License-Identifier: GPL-2.0-or-later
//! Native GL1.1 evaluator state. Complete bounded control grids are copied once by GL.
#![allow(non_upper_case_globals)]
use crate::{gl_api::*, query};
const COEFFICIENTS: usize = 4 * DG_GL_MAX_EVAL_ORDER as usize * DG_GL_MAX_EVAL_ORDER as usize;
const VALUES: usize = COEFFICIENTS + 4;
pub(crate) fn components(target: u32) -> Option<(usize, usize)> {
    let (dimension, index) = if (GL_MAP1_COLOR_4..=GL_MAP1_VERTEX_4).contains(&target) {
        (1, target - GL_MAP1_COLOR_4)
    } else if (GL_MAP2_COLOR_4..=GL_MAP2_VERTEX_4).contains(&target) {
        (2, target - GL_MAP2_COLOR_4)
    } else {
        return None;
    };
    Some((dimension, [4, 1, 3, 1, 2, 3, 4, 3, 4][index as usize]))
}
pub(crate) fn map_function(f: u32) -> bool {
    matches!(
        f,
        FEnum_glMap1f | FEnum_glMap1d | FEnum_glMap2f | FEnum_glMap2d
    )
}
pub(crate) fn map_query(f: u32) -> bool {
    matches!(f, FEnum_glGetMapdv | FEnum_glGetMapfv | FEnum_glGetMapiv)
}
fn double(f: u32) -> bool {
    matches!(f, FEnum_glMap1d | FEnum_glMap2d | FEnum_glGetMapdv)
}
pub(crate) unsafe fn limit(api: &DreamGpuGlApi) -> Result<u32, u32> {
    let mut value = 0;
    unsafe { api.dg_glGetIntegerv.ok_or(DG_GL_ERROR_UNSUPPORTED)?(GL_MAX_EVAL_ORDER, &mut value) };
    Ok(value.clamp(0, DG_GL_MAX_EVAL_ORDER as i32) as u32)
}
pub(crate) fn query_count(target: u32, pname: u32, count: u32) -> bool {
    let Some((dim, k)) = components(target) else {
        return false;
    };
    match pname {
        GL_ORDER => count as usize == dim,
        GL_DOMAIN => count as usize == dim * 2,
        GL_COEFF => {
            count >= k as u32
                && count <= k as u32 * DG_GL_MAX_EVAL_ORDER.pow(dim as u32)
                && count.is_multiple_of(k as u32)
        }
        _ => false,
    }
}
pub(crate) fn validate(f: u32, a: &[u32; 8], data: &[u8]) -> u32 {
    let Some((dim, k)) = components(a[0]) else {
        return DG_GL_ERROR_BATCH;
    };
    let expected_dim = if matches!(f, FEnum_glMap1f | FEnum_glMap1d) {
        1
    } else if matches!(f, FEnum_glMap2f | FEnum_glMap2d) {
        2
    } else {
        return DG_GL_ERROR_UNSUPPORTED;
    };
    if dim != expected_dim
        || a[1] == 0
        || a[1] > DG_GL_MAX_EVAL_ORDER
        || (dim == 2 && (a[2] == 0 || a[2] > DG_GL_MAX_EVAL_ORDER))
    {
        return DG_GL_ERROR_BATCH;
    }
    let unit = if double(f) { 8 } else { 4 };
    let count = a[1] as usize * if dim == 2 { a[2] as usize } else { 1 } * k;
    if data.len() != (count + 2 * dim) * unit {
        return DG_GL_ERROR_BATCH;
    }
    let value = |n: usize| {
        if unit == 8 {
            f64::from_le_bytes(data[n * 8..n * 8 + 8].try_into().unwrap())
        } else {
            f32::from_le_bytes(data[n * 4..n * 4 + 4].try_into().unwrap()) as f64
        }
    };
    if value(0) == value(1) || (dim == 2 && value(2) == value(3)) {
        DG_GL_ERROR_BATCH
    } else {
        0
    }
}
pub(crate) fn empty(f: u32, a: &[u32; 5]) -> bool {
    a[1] as i32 > a[2] as i32
        || (f == FEnum_glEvalMesh2
            && (a[3] as i32 > a[4] as i32 || (a[0] == GL_FILL && a[3] == a[4])))
}
pub(crate) fn terminal(f: u32, a: &[u32; 5]) -> bool {
    a[2] == i32::MAX as u32 || (f == FEnum_glEvalMesh2 && a[4] == i32::MAX as u32)
}
pub(crate) fn mesh(f: u32, a: &[u32; 5]) -> bool {
    let dim = if f == FEnum_glEvalMesh1 { 1 } else { 2 };
    if !matches!(a[0], GL_POINT | GL_LINE | GL_FILL) || (dim == 1 && a[0] == GL_FILL) {
        return false;
    }
    if empty(f, a) {
        return true;
    }
    let width = (i64::from(a[2] as i32) - i64::from(a[1] as i32) + 1) as u64;
    let mut height = if dim == 2 {
        (i64::from(a[4] as i32) - i64::from(a[3] as i32) + 1) as u64
    } else {
        1
    };
    if dim == 2 && a[0] == GL_FILL {
        height -= 1;
    }
    let factor = if a[0] == GL_POINT || dim == 1 { 1 } else { 2 };
    width
        .checked_mul(height)
        .and_then(|n| n.checked_mul(factor))
        .is_some_and(|n| n <= u64::from(DG_GL_MAX_VERTICES))
}
/// Native providers may use signed inclusive loop counters. Preserve the spec's
/// native primitive/EvalPoint sequence with widened counters at terminal INT_MAX.
/// This executes only an already admitted bounded mesh; no CPU pixel rendering.
pub(crate) unsafe fn terminal_mesh(api: &DreamGpuGlApi, f: u32, a: &[u32; 5]) -> Result<(), u32> {
    if !mesh(f, a) {
        return Err(DG_GL_ERROR_LIMIT);
    }
    if empty(f, a) {
        return Ok(());
    }
    let begin = api.dg_glBegin.ok_or(DG_GL_ERROR_UNSUPPORTED)?;
    let end = api.dg_glEnd.ok_or(DG_GL_ERROR_UNSUPPORTED)?;
    let (i1, i2) = (i64::from(a[1] as i32), i64::from(a[2] as i32));
    if f == FEnum_glEvalMesh1 {
        let point = api.dg_glEvalPoint1.ok_or(DG_GL_ERROR_UNSUPPORTED)?;
        unsafe {
            begin(if a[0] == GL_POINT {
                GL_POINTS
            } else {
                GL_LINE_STRIP
            });
            for i in i1..=i2 {
                point(i as i32);
            }
            end();
        }
    } else {
        let point = api.dg_glEvalPoint2.ok_or(DG_GL_ERROR_UNSUPPORTED)?;
        let (j1, j2) = (i64::from(a[3] as i32), i64::from(a[4] as i32));
        unsafe {
            match a[0] {
                GL_POINT => {
                    begin(GL_POINTS);
                    for j in j1..=j2 {
                        for i in i1..=i2 {
                            point(i as i32, j as i32);
                        }
                    }
                    end();
                }
                GL_LINE => {
                    for j in j1..=j2 {
                        begin(GL_LINE_STRIP);
                        for i in i1..=i2 {
                            point(i as i32, j as i32);
                        }
                        end();
                    }
                    for i in i1..=i2 {
                        begin(GL_LINE_STRIP);
                        for j in j1..=j2 {
                            point(i as i32, j as i32);
                        }
                        end();
                    }
                }
                GL_FILL => {
                    for j in j1..j2 {
                        begin(GL_QUAD_STRIP);
                        for i in i1..=i2 {
                            point(i as i32, j as i32);
                            point(i as i32, (j + 1) as i32);
                        }
                        end();
                    }
                }
                _ => unreachable!(),
            }
        }
    }
    Ok(())
}
pub(crate) unsafe fn set(
    api: &DreamGpuGlApi,
    errors: *mut u32,
    f: u32,
    a: &[u32; 8],
    data: &[u8],
) -> Result<(), u32> {
    let valid = validate(f, a, data);
    if valid != 0 {
        return Err(valid);
    }
    let (dim, k) = components(a[0]).unwrap();
    let maximum = unsafe { limit(api)? };
    if a[1] > maximum || (dim == 2 && a[2] > maximum) {
        return Err(DG_GL_ERROR_LIMIT);
    }
    let get_error = api.dg_glGetError.ok_or(DG_GL_ERROR_UNSUPPORTED)?;
    unsafe {
        query::remember(api, errors)?;
    }
    let total = data.len() / if double(f) { 8 } else { 4 };
    // Maximum 4-domain +4*8*8 coefficients, with native alignment and exact numeric types.
    if double(f) {
        let mut values = [0f64; VALUES];
        for (i, value) in values.iter_mut().take(total).enumerate() {
            *value = f64::from_le_bytes(data[i * 8..i * 8 + 8].try_into().unwrap());
        }
        unsafe {
            if dim == 1 {
                api.dg_glMap1d.ok_or(DG_GL_ERROR_UNSUPPORTED)?(
                    a[0],
                    values[0],
                    values[1],
                    k as i32,
                    a[1] as i32,
                    values.as_ptr().add(2),
                );
            } else {
                api.dg_glMap2d.ok_or(DG_GL_ERROR_UNSUPPORTED)?(
                    a[0],
                    values[0],
                    values[1],
                    a[2] as i32 * k as i32,
                    a[1] as i32,
                    values[2],
                    values[3],
                    k as i32,
                    a[2] as i32,
                    values.as_ptr().add(4),
                );
            }
        }
    } else {
        let mut values = [0f32; VALUES];
        for (i, value) in values.iter_mut().take(total).enumerate() {
            *value = f32::from_le_bytes(data[i * 4..i * 4 + 4].try_into().unwrap());
        }
        unsafe {
            if dim == 1 {
                api.dg_glMap1f.ok_or(DG_GL_ERROR_UNSUPPORTED)?(
                    a[0],
                    values[0],
                    values[1],
                    k as i32,
                    a[1] as i32,
                    values.as_ptr().add(2),
                );
            } else {
                api.dg_glMap2f.ok_or(DG_GL_ERROR_UNSUPPORTED)?(
                    a[0],
                    values[0],
                    values[1],
                    a[2] as i32 * k as i32,
                    a[1] as i32,
                    values[2],
                    values[3],
                    k as i32,
                    a[2] as i32,
                    values.as_ptr().add(4),
                );
            }
        }
    }
    let error = unsafe { get_error() };
    unsafe { query::store(errors, error) };
    if error == 0 {
        Ok(())
    } else {
        Err(DG_GL_ERROR_HOST)
    }
}
pub(crate) unsafe fn get(
    api: &DreamGpuGlApi,
    errors: *mut u32,
    f: u32,
    target: u32,
    pname: u32,
    count: u32,
    out: *mut u8,
) -> Result<(), u32> {
    if !map_query(f) || !query_count(target, pname, count) {
        return Err(DG_GL_ERROR_BATCH);
    }
    let (dim, k) = components(target).unwrap();
    let get_error = api.dg_glGetError.ok_or(DG_GL_ERROR_UNSUPPORTED)?;
    let get_i = api.dg_glGetMapiv.ok_or(DG_GL_ERROR_UNSUPPORTED)?;
    let mut order = [0i32; 2];
    unsafe {
        query::remember(api, errors)?;
        get_i(target, GL_ORDER, order.as_mut_ptr());
    }
    let error = unsafe { get_error() };
    if error != 0 {
        unsafe { query::store(errors, error) };
        return Err(DG_GL_ERROR_HOST);
    }
    if order[..dim]
        .iter()
        .any(|&n| n < 1 || n > DG_GL_MAX_EVAL_ORDER as i32)
    {
        return Err(DG_GL_ERROR_LIMIT);
    }
    let expected = match pname {
        GL_ORDER => dim,
        GL_DOMAIN => dim * 2,
        _ => k * order[..dim].iter().map(|&n| n as usize).product::<usize>(),
    };
    if count as usize != expected {
        return Err(DG_GL_ERROR_LIMIT);
    }
    // Results never touch guest storage until native status and exact count are known.
    let mut doubles = [0f64; COEFFICIENTS];
    let mut floats = [0f32; COEFFICIENTS];
    let mut integers = [0i32; COEFFICIENTS];
    unsafe {
        match f {
            FEnum_glGetMapdv => api.dg_glGetMapdv.ok_or(DG_GL_ERROR_UNSUPPORTED)?(
                target,
                pname,
                doubles.as_mut_ptr(),
            ),
            FEnum_glGetMapfv => api.dg_glGetMapfv.ok_or(DG_GL_ERROR_UNSUPPORTED)?(
                target,
                pname,
                floats.as_mut_ptr(),
            ),
            _ => get_i(target, pname, integers.as_mut_ptr()),
        }
    }
    let error = unsafe { get_error() };
    if error != 0 {
        unsafe { query::store(errors, error) };
        return Err(DG_GL_ERROR_HOST);
    }
    for i in 0..expected {
        unsafe {
            if double(f) {
                core::ptr::copy_nonoverlapping(doubles[i].to_le_bytes().as_ptr(), out.add(i * 8), 8)
            } else {
                let value = if f == FEnum_glGetMapfv {
                    floats[i].to_bits()
                } else {
                    integers[i] as u32
                };
                core::ptr::copy_nonoverlapping(value.to_le_bytes().as_ptr(), out.add(i * 4), 4)
            }
        }
    }
    Ok(())
}
#[cfg(test)]
mod tests;
