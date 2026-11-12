// SPDX-License-Identifier: GPL-2.0-or-later
//! Opt-in diagnostic frame trace of executed GL records.
//!
//! An armed trace waits for a presentation boundary, then formats every record
//! the render worker executes during the next N presented frames as one text
//! line (plus indented vertex lines on request) through a caller-owned sink,
//! within a byte limit, and ends by itself. Lines show the record as the host
//! decodes it: the protocol function name, scalar words by argument kind,
//! payload size and a short prefix or decoded list. Formatting borrows only the
//! immutable record snapshot after its execution and allocates nothing. The
//! worker calls this module only while a capture is attached, so an idle device
//! pays one pointer check per record and no guest-visible behaviour changes.
#![allow(non_upper_case_globals)]
use crate::{
    arrays::raw::{self, Attribute},
    gl_api::*,
    gl_validation::function_words,
};
use core::{
    ffi::c_void,
    fmt::{self, Write},
};
mod names;

pub const IDLE: u32 = 0;
/// Waiting for the first presentation; nothing is written yet.
pub const ARMED: u32 = 1;
pub const ACTIVE: u32 = 2;
pub const DONE: u32 = 3;
/// Also dump the client vertex data and inline indices of array draws.
pub const VERTICES: u32 = 1;
pub const END_FRAMES: u32 = 1;
pub const END_LIMIT: u32 = 2;
pub const END_WRITE: u32 = 3;
pub const END_CANCELLED: u32 = 4;
pub const MAX_FRAMES: u32 = 1000;
pub const MIN_LIMIT: u64 = 4096;
pub const DEFAULT_LIMIT: u64 = 64 << 20;
pub const MAX_LIMIT: u64 = 1 << 30;
/// Bytes the end line may use beyond the limit.
pub const TRAILER: u64 = 256;
const HEX_PREFIX: usize = 32;
const LIST_ITEMS: usize = 16;
const TEXT_PREFIX: usize = 96;

/// Writes bytes; nonzero reports a failed write and ends the trace.
pub type Sink = unsafe extern "C" fn(*mut c_void, *const u8, usize) -> u32;

#[repr(C)]
pub struct Trace {
    pub state: u32,
    pub flags: u32,
    pub frames: u32,
    /// Presentations traced so far; a line's frame number is this plus one.
    pub completed: u32,
    pub reason: u32,
    pub reserved: u32,
    pub limit: u64,
    pub bytes: u64,
    pub records: u64,
    pub opaque: *mut c_void,
    pub write: Option<Sink>,
}
impl Default for Trace {
    fn default() -> Self {
        Self {
            state: IDLE,
            flags: 0,
            frames: 0,
            completed: 0,
            reason: 0,
            reserved: 0,
            limit: 0,
            bytes: 0,
            records: 0,
            opaque: core::ptr::null_mut(),
            write: None,
        }
    }
}

/// Buffered sink writer with a hard byte budget. A full budget or a failed
/// sink makes every later write fail, which aborts the record being formatted.
struct Out<'a> {
    t: &'a mut Trace,
    budget: u64,
    buf: [u8; 4096],
    len: usize,
    full: bool,
    failed: bool,
}
impl<'a> Out<'a> {
    fn new(t: &'a mut Trace, budget: u64) -> Self {
        Self {
            t,
            budget,
            buf: [0; 4096],
            len: 0,
            full: false,
            failed: false,
        }
    }
    fn flush(&mut self) {
        if self.len != 0 && !self.failed {
            let ok = self.t.write.is_some_and(|write| unsafe {
                write(self.t.opaque, self.buf.as_ptr(), self.len) == 0
            });
            if ok {
                self.t.bytes += self.len as u64;
            } else {
                self.failed = true;
            }
        }
        self.len = 0;
    }
}
impl Write for Out<'_> {
    fn write_str(&mut self, s: &str) -> fmt::Result {
        let mut s = s.as_bytes();
        while !s.is_empty() {
            if self.failed {
                return Err(fmt::Error);
            }
            if self.len == self.buf.len() {
                self.flush();
                continue;
            }
            let room = self.budget.saturating_sub(self.t.bytes + self.len as u64);
            if room == 0 {
                self.full = true;
                return Err(fmt::Error);
            }
            let n = s
                .len()
                .min(self.buf.len() - self.len)
                .min(usize::try_from(room).unwrap_or(usize::MAX));
            self.buf[self.len..self.len + n].copy_from_slice(&s[..n]);
            self.len += n;
            s = &s[n..];
        }
        Ok(())
    }
}

fn word(r: &[u8], at: usize) -> u32 {
    r.get(at..at + 4)
        .map_or(0, |b| u32::from_le_bytes(b.try_into().unwrap()))
}
fn bytes_at(r: &[u8], at: usize, n: usize) -> &[u8] {
    r.get(at..).map_or(&[], |tail| &tail[..n.min(tail.len())])
}

/// Floats print exactly in plain notation, extremes in exponent notation.
fn float(o: &mut Out, v: f64) -> fmt::Result {
    if v == 0.0 || !v.is_finite() || (1e-4..1e7).contains(&v.abs()) {
        write!(o, "{v}")
    } else {
        write!(o, "{v:e}")
    }
}
fn single(o: &mut Out, v: f32) -> fmt::Result {
    if v == 0.0 || !v.is_finite() || (1e-4..1e7).contains(&v.abs()) {
        write!(o, "{v}")
    } else {
        write!(o, "{v:e}")
    }
}
fn enumerant(o: &mut Out, v: u32) -> fmt::Result {
    match names::enumerant(v).filter(|_| v >= 0x100) {
        Some(name) => write!(o, "{v:#x}={name}"),
        None => write!(o, "{v:#x}"),
    }
}
fn primitive(o: &mut Out, v: u32) -> fmt::Result {
    const MODES: [&str; 10] = [
        "GL_POINTS",
        "GL_LINES",
        "GL_LINE_LOOP",
        "GL_LINE_STRIP",
        "GL_TRIANGLES",
        "GL_TRIANGLE_STRIP",
        "GL_TRIANGLE_FAN",
        "GL_QUADS",
        "GL_QUAD_STRIP",
        "GL_POLYGON",
    ];
    match MODES.get(v as usize) {
        Some(name) => write!(o, "{v:#x}={name}"),
        None => write!(o, "{v:#x}"),
    }
}
fn factor(o: &mut Out, v: u32) -> fmt::Result {
    match v {
        0 => o.write_str("0x0=GL_ZERO"),
        1 => o.write_str("0x1=GL_ONE"),
        _ => enumerant(o, v),
    }
}
fn hex(o: &mut Out, data: &[u8]) -> fmt::Result {
    if data.is_empty() {
        return Ok(());
    }
    o.write_str(" hex=")?;
    for b in data.iter().take(HEX_PREFIX) {
        write!(o, "{b:02x}")?;
    }
    if data.len() > HEX_PREFIX {
        o.write_str("..")?;
    }
    Ok(())
}

/// Argument kinds by scalar word: e enum, p primitive, k blend factor,
/// i signed, u unsigned, x hexadecimal, b boolean, f float, d double (two
/// words). Words beyond the signature print as hexadecimal.
fn signature(f: u32) -> &'static str {
    const F16: &str = "ffffffffffffffff";
    const D16: &str = "dddddddddddddddd";
    match f {
        FEnum_glBegin => "p",
        FEnum_glAccum
        | FEnum_glAlphaFunc
        | FEnum_glFogf
        | FEnum_glLightModelf
        | FEnum_glPixelTransferf
        | FEnum_glPointParameterf => "ef",
        FEnum_glPixelTransferi => "ei",
        FEnum_glActiveTexture
        | FEnum_glBlendEquation
        | FEnum_glCullFace
        | FEnum_glDepthFunc
        | FEnum_glDisable
        | FEnum_glDrawBuffer
        | FEnum_glEnable
        | FEnum_glEndQuery
        | FEnum_glFrontFace
        | FEnum_glGenerateMipmap
        | FEnum_glLogicOp
        | FEnum_glMatrixMode
        | FEnum_glReadBuffer
        | FEnum_glShadeModel => "e",
        FEnum_glBeginQuery
        | FEnum_glBindFramebuffer
        | FEnum_glBindRenderbuffer
        | FEnum_glBindProgramARB
        | FEnum_glBindTexture => "eu",
        FEnum_glBlendEquationSeparate
        | FEnum_glColorMaterial
        | FEnum_glHint
        | FEnum_glPolygonMode => "ee",
        FEnum_glBlendFunc => "kk",
        FEnum_glBlendFuncSeparate => "kkkk",
        FEnum_glBlitFramebuffer => "iiiiiiiixe",
        FEnum_glCallList | FEnum_glListBase | FEnum_glLoadName | FEnum_glPushName => "u",
        FEnum_glClear | FEnum_glPushAttrib | FEnum_glIndexMask | FEnum_glStencilMask => "x",
        FEnum_glClearDepth | FEnum_glIndexd | FEnum_glEvalCoord1d => "d",
        FEnum_glDepthRange | FEnum_glEvalCoord2d => "dd",
        FEnum_glRasterPos4d => "dddd",
        FEnum_glOrtho | FEnum_glFrustum => "dddddd",
        FEnum_glLoadMatrixd | FEnum_glMultMatrixd => D16,
        FEnum_glLoadMatrixf | FEnum_glMultMatrixf => F16,
        FEnum_glClearStencil | FEnum_glEvalPoint1 => "i",
        FEnum_glEvalPoint2 => "ii",
        FEnum_glColorMask => "bbbb",
        FEnum_glDepthMask | FEnum_glEdgeFlag => "b",
        FEnum_glCopyPixels => "iiiie",
        FEnum_glCopyTexImage1D => "eieiiii",
        FEnum_glCopyTexImage2D => "eieiiiii",
        FEnum_glCopyTexSubImage1D => "eiiiii",
        FEnum_glCopyTexSubImage2D => "eiiiiiii",
        FEnum_glCopyTexSubImage3D => "eiiiiiiii",
        FEnum_glEvalMesh1 => "eii",
        FEnum_glEvalMesh2 => "eiiii",
        FEnum_glMapGrid1d => "idd",
        FEnum_glMapGrid2d => "iddidd",
        FEnum_glFramebufferRenderbuffer => "eeeu",
        FEnum_glFramebufferTexture1D | FEnum_glFramebufferTexture2D => "eeeui",
        FEnum_glFramebufferTexture3D => "eeeuii",
        FEnum_glRenderbufferStorage => "eeii",
        FEnum_glLightf
        | FEnum_glMaterialf
        | FEnum_glTexEnvf
        | FEnum_glTexGenf
        | FEnum_glTexParameterf => "eef",
        FEnum_glTexEnvi | FEnum_glTexParameteri => "eee",
        FEnum_glLineStipple => "ix",
        FEnum_glMultiTexCoord4f => "effff",
        FEnum_glProgramEnvParameter4fARB | FEnum_glProgramLocalParameter4fARB => "euffff",
        FEnum_glVertexAttrib4fARB => "uffff",
        FEnum_glSetFenceNV => "ue",
        FEnum_glStencilFunc => "eix",
        FEnum_glStencilFuncSeparate => "eeix",
        FEnum_glStencilOp => "eee",
        FEnum_glStencilOpSeparate => "eeee",
        FEnum_glViewport | FEnum_glScissor => "iiii",
        FEnum_glBlendColor | FEnum_glClearAccum | FEnum_glClearColor | FEnum_glColor4f
        | FEnum_glRotatef | FEnum_glTexCoord4f | FEnum_glVertex4f => "ffff",
        FEnum_glColor3f
        | FEnum_glNormal3f
        | FEnum_glScalef
        | FEnum_glSecondaryColor3f
        | FEnum_glTranslatef
        | FEnum_glVertex3f => "fff",
        FEnum_glPixelZoom | FEnum_glPolygonOffset | FEnum_glTexCoord2f | FEnum_glVertex2f => "ff",
        FEnum_glClearIndex | FEnum_glFogCoordf | FEnum_glLineWidth | FEnum_glPassThrough
        | FEnum_glPointSize => "f",
        // DATA scalar words; payloads are described by `payload_kind`.
        FEnum_glDrawArrays | FEnum_glDrawArraysEXT => "puux",
        FEnum_glDrawElements => "pueux",
        FEnum_glVertexAttribPointerARB => "puuxx",
        FEnum_glDrawRangeElements | FEnum_glDrawRangeElementsEXT => "puuxxxueuu",
        FEnum_glTexImage1D | FEnum_glTexImage2D => "eieiiiee",
        FEnum_glTexSubImage1D | FEnum_glTexSubImage2D => "eiiiiiee",
        FEnum_glTexImage3D => "eieiiiiee",
        FEnum_glTexSubImage3D => "eiiiiiiiee",
        FEnum_glCompressedTexImage2D => "eieiiiu",
        FEnum_glCompressedTexSubImage2D => "eiiiiieu",
        FEnum_glLightfv
        | FEnum_glMaterialfv
        | FEnum_glTexGenfv
        | FEnum_glTexGendv
        | FEnum_glTexParameterfv
        | FEnum_glTexParameteriv
        | FEnum_glTexEnvfv
        | FEnum_glTexEnviv => "ee",
        FEnum_glFogfv | FEnum_glLightModelfv | FEnum_glPointParameterfv | FEnum_glClipPlane => "e",
        FEnum_glDeleteTextures
        | FEnum_glDeleteBuffersARB
        | FEnum_glDeleteProgramsARB
        | FEnum_glDeleteFramebuffers
        | FEnum_glDeleteRenderbuffers
        | FEnum_glDeleteQueries
        | FEnum_glDeleteFencesNV
        | FEnum_glPrioritizeTextures => "u",
        FEnum_glCallLists => "e",
        FEnum_glPixelMapfv | FEnum_glPixelMapuiv | FEnum_glPixelMapusv => "eu",
        FEnum_glBufferDataARB => "uue",
        FEnum_glBufferSubDataARB => "uu",
        FEnum_glProgramStringARB => "eeu",
        // QUERY words (always three; unused ones are zero).
        FEnum_glGetBooleanv | FEnum_glGetIntegerv | FEnum_glGetFloatv | FEnum_glGetDoublev
        | FEnum_glIsEnabled | FEnum_glGetString | FEnum_glGetClipPlane => "e",
        FEnum_glGetTexParameterfv
        | FEnum_glGetTexParameteriv
        | FEnum_glGetTexEnvfv
        | FEnum_glGetTexEnviv
        | FEnum_glGetLightfv
        | FEnum_glGetLightiv
        | FEnum_glGetMaterialfv
        | FEnum_glGetMaterialiv
        | FEnum_glGetTexGenfv
        | FEnum_glGetTexGeniv
        | FEnum_glGetTexGendv => "ee",
        FEnum_glGetTexLevelParameterfv | FEnum_glGetTexLevelParameteriv => "eie",
        _ => "",
    }
}

/// Labels for the scalar words of the array draws, whose positional meaning
/// is otherwise hard to read.
fn labels(f: u32) -> &'static [&'static str] {
    match f {
        FEnum_glDrawArrays | FEnum_glDrawArraysEXT => &["mode", "first", "count", "mask"],
        FEnum_glDrawElements => &["mode", "indices", "type", "vertices", "mask"],
        FEnum_glVertexAttribPointerARB => &["mode", "first", "count", "mask", "generic"],
        FEnum_glDrawRangeElements | FEnum_glDrawRangeElementsEXT => &[
            "mode", "base", "count", "mask", "generic", "buffered", "vertices", "type", "element",
            "offset",
        ],
        _ => &[],
    }
}

fn args(o: &mut Out, f: u32, a: &[u8], words: usize) -> fmt::Result {
    let sig = signature(f).as_bytes();
    let labels = labels(f);
    let (mut i, mut k) = (0, 0);
    while i < words {
        let kind = sig.get(k).copied().unwrap_or(b'x');
        let v = word(a, i * 4);
        o.write_char(' ')?;
        if let Some(label) = labels.get(k) {
            write!(o, "{label}=")?;
        }
        k += 1;
        match kind {
            b'd' if i + 1 < words => {
                let high = u64::from(word(a, i * 4 + 4));
                float(o, f64::from_bits(u64::from(v) | (high << 32)))?;
                i += 2;
                continue;
            }
            b'f' => single(o, f32::from_bits(v))?,
            b'i' => write!(o, "{}", v as i32)?,
            b'u' | b'b' => write!(o, "{v}")?,
            b'e' => enumerant(o, v)?,
            b'p' => primitive(o, v)?,
            b'k' => factor(o, v)?,
            _ => write!(o, "{v:#x}")?,
        }
        i += 1;
    }
    Ok(())
}

#[derive(Clone, Copy, PartialEq, Eq)]
enum Payload {
    Floats,
    Integers,
    Doubles,
    Names,
    Text,
    Draw,
    Bytes,
}
fn payload_kind(f: u32) -> Payload {
    match f {
        FEnum_glLightfv
        | FEnum_glMaterialfv
        | FEnum_glTexGenfv
        | FEnum_glTexParameterfv
        | FEnum_glTexEnvfv
        | FEnum_glFogfv
        | FEnum_glLightModelfv
        | FEnum_glPointParameterfv
        | FEnum_glPixelMapfv => Payload::Floats,
        FEnum_glTexParameteriv | FEnum_glTexEnviv => Payload::Integers,
        FEnum_glTexGendv | FEnum_glClipPlane => Payload::Doubles,
        FEnum_glDeleteTextures
        | FEnum_glDeleteBuffersARB
        | FEnum_glDeleteProgramsARB
        | FEnum_glDeleteFramebuffers
        | FEnum_glDeleteRenderbuffers
        | FEnum_glDeleteQueries
        | FEnum_glDeleteFencesNV
        | FEnum_glPixelMapuiv => Payload::Names,
        FEnum_glProgramStringARB => Payload::Text,
        FEnum_glDrawArrays
        | FEnum_glDrawArraysEXT
        | FEnum_glDrawElements
        | FEnum_glVertexAttribPointerARB
        | FEnum_glDrawRangeElements
        | FEnum_glDrawRangeElementsEXT => Payload::Draw,
        _ => Payload::Bytes,
    }
}

fn le32(b: &[u8]) -> u32 {
    u32::from_le_bytes(b[..4].try_into().unwrap())
}
fn le64(b: &[u8]) -> u64 {
    u64::from_le_bytes(b[..8].try_into().unwrap())
}
fn list(
    o: &mut Out,
    data: &[u8],
    unit: usize,
    item: impl Fn(&mut Out, &[u8]) -> fmt::Result,
) -> fmt::Result {
    o.write_str(" [")?;
    for (n, chunk) in data.chunks_exact(unit).enumerate() {
        if n == LIST_ITEMS {
            o.write_str(" ..")?;
            break;
        }
        if n != 0 {
            o.write_char(' ')?;
        }
        item(o, chunk)?;
    }
    o.write_char(']')
}

fn payload(o: &mut Out, f: u32, a: &[u32; 10], data: &[u8]) -> fmt::Result {
    match payload_kind(f) {
        Payload::Floats => list(o, data, 4, |o, b| single(o, f32::from_bits(le32(b)))),
        Payload::Integers => list(o, data, 4, |o, b| write!(o, "{}", le32(b) as i32)),
        Payload::Doubles => list(o, data, 8, |o, b| float(o, f64::from_bits(le64(b)))),
        Payload::Names => list(o, data, 4, |o, b| write!(o, "{}", le32(b))),
        Payload::Text => {
            o.write_str(" text=\"")?;
            for &b in data.iter().take(TEXT_PREFIX) {
                match b {
                    b'"' | b'\\' => write!(o, "\\{}", b as char)?,
                    0x20..=0x7e => o.write_char(b as char)?,
                    _ => write!(o, "\\x{b:02x}")?,
                }
            }
            o.write_str(if data.len() > TEXT_PREFIX {
                "\".."
            } else {
                "\""
            })
        }
        Payload::Draw => {
            draw_layout(o, f, a, data)?;
            hex(o, data)
        }
        Payload::Bytes => hex(o, data),
    }
}

const SLOTS: [&str; 16] = [
    "pos", "col", "nrm", "tc0", "col2", "idx", "edge", "tc1", "tc2", "tc3", "tc4", "tc5", "tc6",
    "tc7", "fog", "slot15",
];
fn slot_name(o: &mut Out, slot: usize) -> fmt::Result {
    match SLOTS.get(slot) {
        Some(name) => o.write_str(name),
        None => write!(o, "a{}", slot - 16),
    }
}
fn type_name(kind: u32) -> &'static str {
    match kind {
        GL_BYTE => "BYTE",
        GL_UNSIGNED_BYTE => "UNSIGNED_BYTE",
        GL_SHORT => "SHORT",
        GL_UNSIGNED_SHORT => "UNSIGNED_SHORT",
        GL_INT => "INT",
        GL_UNSIGNED_INT => "UNSIGNED_INT",
        GL_FLOAT => "FLOAT",
        GL_DOUBLE => "DOUBLE",
        _ => "?",
    }
}

/// The decoded vertex layout of an array draw: typed client sections and
/// buffer references, or the fixed float vertex of the original protocol.
#[expect(
    clippy::large_enum_variant,
    reason = "A short-lived stack value per traced draw; boxing would allocate."
)]
enum Layout {
    Fixed {
        attributes: u32,
        stride: usize,
        vertices: usize,
        index_type: u32,
        indices: usize,
    },
    Raw {
        slots: [Attribute; 32],
        client: u32,
        buffered: u32,
        references: [raw::Reference; 32],
        vertices: usize,
        first: u32,
        index_type: u32,
        index_offset: usize,
        indices: usize,
    },
}
/// Re-parses an executed record with the engine's own layout rules.
fn decode_layout(f: u32, a: &[u32; 10], data: &[u8]) -> Option<Layout> {
    match f {
        FEnum_glDrawRangeElements | FEnum_glDrawRangeElementsEXT => {
            let layout = raw::layout_buffer_draw(f, a, data, false)?;
            let enabled = (a[3] & DG_GL_ARRAY_EXT_MASK) | (a[4] << 16);
            let inline = layout.index_size != 0 && a[8] == 0;
            Some(Layout::Raw {
                slots: layout.slots,
                client: enabled & !a[5],
                buffered: a[5],
                references: layout.references,
                vertices: a[6] as usize,
                first: a[1],
                index_type: if inline { a[7] } else { 0 },
                index_offset: layout.indices,
                indices: if inline { a[2] as usize } else { 0 },
            })
        }
        FEnum_glDrawArrays | FEnum_glDrawArraysEXT | FEnum_glVertexAttribPointerARB
            if a[3] & DG_GL_ARRAY_RAW != 0 =>
        {
            let (mask, count) = (a[3], a[2]);
            let mut slots = [Attribute::default(); 32];
            let client = if f == FEnum_glVertexAttribPointerARB {
                slots = raw::layout_attribs(mask, a[4], count, data)?;
                (mask & DG_GL_ARRAY_EXT_MASK) | (a[4] << 16)
            } else if f == FEnum_glDrawArraysEXT {
                slots[..16].copy_from_slice(&raw::layout_extended(mask, count, data)?);
                mask & DG_GL_ARRAY_EXT_MASK
            } else {
                slots[..7].copy_from_slice(&raw::layout(mask, count, data)?);
                mask & DG_GL_ARRAY_MASK
            };
            Some(Layout::Raw {
                slots,
                client,
                buffered: 0,
                references: [raw::Reference::default(); 32],
                vertices: count as usize,
                first: 0,
                index_type: 0,
                index_offset: 0,
                indices: 0,
            })
        }
        FEnum_glDrawArrays | FEnum_glDrawElements => {
            let elements = f == FEnum_glDrawElements;
            let attributes = a[if elements { 4 } else { 3 }];
            if attributes & !DG_GL_ARRAY_MASK != 0 {
                return None;
            }
            let stride = if attributes & (DG_GL_ARRAY_INDEX | DG_GL_ARRAY_EDGE) != 0 {
                DG_GL_VERTEX_EXTENDED_BYTES
            } else if attributes & DG_GL_ARRAY_SECONDARY != 0 {
                DG_GL_VERTEX_SECONDARY_BYTES
            } else {
                DG_GL_VERTEX_BYTES
            } as usize;
            Some(Layout::Fixed {
                attributes,
                stride,
                vertices: a[if elements { 3 } else { 2 }] as usize,
                index_type: if elements { a[2] } else { 0 },
                indices: if elements { a[1] as usize } else { 0 },
            })
        }
        _ => None,
    }
}

const FIXED: [(u32, &str, usize, usize); 5] = [
    (DG_GL_ARRAY_POSITION, "pos", 0, 4),
    (DG_GL_ARRAY_COLOR, "col", 16, 4),
    (DG_GL_ARRAY_NORMAL, "nrm", 32, 3),
    (DG_GL_ARRAY_TEXCOORD, "tc0", 44, 4),
    (DG_GL_ARRAY_SECONDARY, "col2", 64, 3),
];

fn draw_layout(o: &mut Out, f: u32, a: &[u32; 10], data: &[u8]) -> fmt::Result {
    let Some(layout) = decode_layout(f, a, data) else {
        return o.write_str(" layout=?");
    };
    o.write_str(" layout=")?;
    let mut separator = "";
    match layout {
        Layout::Fixed { attributes, .. } => {
            for (bit, name, _, n) in FIXED {
                if attributes & bit != 0 {
                    write!(o, "{separator}{name}:FLOAT*{n}")?;
                    separator = ",";
                }
            }
            if attributes & DG_GL_ARRAY_INDEX != 0 {
                write!(o, "{separator}idx:DOUBLE*1")?;
                separator = ",";
            }
            if attributes & DG_GL_ARRAY_EDGE != 0 {
                write!(o, "{separator}edge:UNSIGNED_BYTE*1")?;
            }
        }
        Layout::Raw {
            slots,
            client,
            buffered,
            references,
            ..
        } => {
            for (slot, s) in slots.iter().enumerate() {
                if (client | buffered) & (1 << slot) == 0 {
                    continue;
                }
                o.write_str(separator)?;
                separator = ",";
                slot_name(o, slot)?;
                write!(o, ":{}*{}", type_name(s.kind), s.size)?;
                if s.normalized {
                    o.write_str("n")?;
                }
                if s.bgra {
                    o.write_str("bgra")?;
                }
                if buffered & (1 << slot) != 0 {
                    let r = references[slot];
                    write!(o, "@buffer{}+{}/{}", r.name, r.offset, r.stride)?;
                }
            }
        }
    }
    Ok(())
}

fn component(o: &mut Out, kind: u32, b: &[u8]) -> fmt::Result {
    match kind {
        GL_BYTE => write!(o, "{}", b[0] as i8),
        GL_UNSIGNED_BYTE => write!(o, "{}", b[0]),
        GL_SHORT => write!(o, "{}", i16::from_le_bytes([b[0], b[1]])),
        GL_UNSIGNED_SHORT => write!(o, "{}", u16::from_le_bytes([b[0], b[1]])),
        GL_INT => write!(o, "{}", le32(b) as i32),
        GL_UNSIGNED_INT => write!(o, "{}", le32(b)),
        GL_FLOAT => single(o, f32::from_bits(le32(b))),
        GL_DOUBLE => float(o, f64::from_bits(le64(b))),
        _ => Ok(()),
    }
}
fn tuple(o: &mut Out, kind: u32, unit: usize, size: usize, b: &[u8]) -> fmt::Result {
    o.write_char('(')?;
    for (n, c) in b.chunks_exact(unit).take(size).enumerate() {
        if n != 0 {
            o.write_char(' ')?;
        }
        component(o, kind, c)?;
    }
    o.write_char(')')
}

/// Indented vertex and inline index lines of an array draw (`VERTICES`).
/// Buffer-object slots appear only in the layout: their data is host state.
fn vertices(o: &mut Out, f: u32, a: &[u32; 10], data: &[u8]) -> fmt::Result {
    let Some(layout) = decode_layout(f, a, data) else {
        return Ok(());
    };
    let (index_type, index_offset, indices) = match layout {
        Layout::Fixed {
            attributes,
            stride,
            vertices,
            index_type,
            indices,
        } => {
            for v in 0..vertices {
                let base = v * stride;
                write!(o, "  v{v}")?;
                for (bit, name, at, n) in FIXED {
                    if attributes & bit != 0 {
                        write!(o, " {name}=")?;
                        tuple(o, GL_FLOAT, 4, n, bytes_at(data, base + at, n * 4))?;
                    }
                }
                if attributes & DG_GL_ARRAY_INDEX != 0 {
                    o.write_str(" idx=")?;
                    tuple(o, GL_DOUBLE, 8, 1, bytes_at(data, base + 80, 8))?;
                }
                if attributes & DG_GL_ARRAY_EDGE != 0 {
                    o.write_str(" edge=")?;
                    tuple(o, GL_UNSIGNED_BYTE, 1, 1, bytes_at(data, base + 88, 1))?;
                }
                o.write_char('\n')?;
            }
            (index_type, vertices * stride, indices)
        }
        Layout::Raw {
            slots,
            client,
            vertices,
            first,
            index_type,
            index_offset,
            indices,
            ..
        } => {
            for v in 0..vertices {
                write!(o, "  v{}", u64::from(first) + v as u64)?;
                for (slot, s) in slots.iter().enumerate() {
                    if client & (1 << slot) == 0 {
                        continue;
                    }
                    let n = s.size as usize * s.unit;
                    o.write_char(' ')?;
                    slot_name(o, slot)?;
                    o.write_char('=')?;
                    tuple(
                        o,
                        s.kind,
                        s.unit,
                        s.size as usize,
                        bytes_at(data, s.offset + v * n, n),
                    )?;
                }
                o.write_char('\n')?;
            }
            (index_type, index_offset, indices)
        }
    };
    let size = match index_type {
        GL_UNSIGNED_BYTE => 1,
        GL_UNSIGNED_SHORT => 2,
        GL_UNSIGNED_INT => 4,
        _ => return Ok(()),
    };
    let section = bytes_at(data, index_offset, indices * size);
    for (row, chunk) in section.chunks(LIST_ITEMS * size).enumerate() {
        write!(o, "  idx{}:", row * LIST_ITEMS)?;
        for i in chunk.chunks_exact(size) {
            write!(o, " {}", raw::index_value(i))?;
        }
        o.write_char('\n')?;
    }
    Ok(())
}

fn op_name(op: u32) -> Option<&'static str> {
    Some(match op {
        DG_GL_CREATE_CONTEXT => "CREATE_CONTEXT",
        DG_GL_DESTROY_CONTEXT => "DESTROY_CONTEXT",
        DG_GL_CREATE_DRAWABLE => "CREATE_DRAWABLE",
        DG_GL_DESTROY_DRAWABLE => "DESTROY_DRAWABLE",
        DG_GL_MAKE_CURRENT => "MAKE_CURRENT",
        DG_GL_CALL => "CALL",
        DG_GL_PRESENT => "PRESENT",
        DG_GL_CLOSE_CLIENT => "CLOSE_CLIENT",
        DG_GL_DESKTOP => "DESKTOP",
        DG_GL_DATA_CALL => "DATA_CALL",
        DG_GL_QUERY => "QUERY",
        _ => return None,
    })
}
/// Scalar words of a known function; unknown ones never pass validation.
fn scalar_words(f: u32) -> Option<usize> {
    let words = function_words(f);
    (words != u32::MAX).then_some((words & !DG_GL_FUNCTION_KIND_MASK) as usize)
}
fn function_name(o: &mut Out, f: u32) -> fmt::Result {
    match names::function(f) {
        Some(name) => o.write_str(name),
        None => write!(o, "fn{f}"),
    }
}

/// One record: `frame seq= stream= ctx=client:context:drawable OP function
/// arguments [data=bytes ...] [err=code]`, then indented vertex lines.
fn format(
    o: &mut Out,
    r: &[u8],
    frame: u32,
    sequence: u32,
    stream: u32,
    error: u32,
    flags: u32,
) -> fmt::Result {
    let op = word(r, DG_GL_OFF_OP as usize);
    write!(
        o,
        "{frame} seq={sequence} stream={stream} ctx={}:{}:{} ",
        word(r, DG_GL_OFF_CLIENT as usize),
        word(r, DG_GL_OFF_CONTEXT as usize),
        word(r, DG_GL_OFF_DRAWABLE as usize)
    )?;
    match op_name(op) {
        Some(name) => o.write_str(name)?,
        None => write!(o, "OP{op}")?,
    }
    let header = DG_GL_HEADER_BYTES as usize;
    let mut draw = None;
    match op {
        DG_GL_CALL | DG_GL_QUERY => {
            let f = word(r, header);
            o.write_char(' ')?;
            function_name(o, f)?;
            let available = r.len().saturating_sub(header + 4) / 4;
            let words = if op == DG_GL_QUERY {
                3
            } else {
                scalar_words(f).unwrap_or(available)
            };
            let words = words.min(available);
            args(o, f, bytes_at(r, header + 4, words * 4), words)?;
        }
        DG_GL_DATA_CALL => {
            let f = word(r, DG_GL_DATA_FUNCTION as usize);
            let declared = word(r, DG_GL_DATA_BYTES as usize);
            o.write_char(' ')?;
            function_name(o, f)?;
            let at = DG_GL_DATA_ARGS as usize;
            let words = scalar_words(f)
                .unwrap_or(0)
                .min(10)
                .min(r.len().saturating_sub(at) / 4);
            args(o, f, bytes_at(r, at, words * 4), words)?;
            let mut a = [0u32; 10];
            for (i, v) in a.iter_mut().enumerate().take(words) {
                *v = word(r, at + i * 4);
            }
            let data = bytes_at(r, at + words * 4, declared as usize);
            write!(o, " data={declared}")?;
            payload(o, f, &a, data)?;
            if payload_kind(f) == Payload::Draw && flags & VERTICES != 0 {
                draw = Some((f, a, data));
            }
        }
        DG_GL_CREATE_CONTEXT => write!(o, " - share={}", word(r, header))?,
        DG_GL_CREATE_DRAWABLE => write!(o, " - {}x{}", word(r, header), word(r, header + 4))?,
        DG_GL_PRESENT => {
            let flags = word(r, DG_GL_OFF_FLAGS as usize);
            write!(o, " - flags={flags:#x}")?;
            for (bit, name) in [
                (DG_GL_PRESENT_EXCLUSIVE, "exclusive"),
                (DG_GL_PRESENT_RETAIN, "retain"),
                (DG_GL_PRESENT_FRONT_ONLY, "front-only"),
                (DG_GL_PRESENT_NO_EXPORT, "no-export"),
                (DG_GL_PRESENT_BOUNDED, "bounded"),
            ] {
                if flags & bit != 0 {
                    write!(o, ",{name}")?;
                }
            }
            if flags & DG_GL_PRESENT_BOUNDED != 0 {
                write!(o, " {}x{}", word(r, header), word(r, header + 4))?;
            }
        }
        DG_GL_DESKTOP => {
            write!(o, " - op={}", word(r, header))?;
            hex(o, bytes_at(r, header, DG_DESKTOP_BYTES as usize))?;
        }
        DG_GL_DESTROY_CONTEXT
        | DG_GL_DESTROY_DRAWABLE
        | DG_GL_MAKE_CURRENT
        | DG_GL_CLOSE_CLIENT => o.write_str(" -")?,
        _ => {
            o.write_str(" -")?;
            hex(o, bytes_at(r, header, r.len()))?;
        }
    }
    if error != 0 {
        write!(o, " err={error}")?;
    }
    o.write_char('\n')?;
    if let Some((f, a, data)) = draw {
        vertices(o, f, &a, data)?;
    }
    Ok(())
}

/// Arms `t` (whose sink is set) and writes the header. Nonzero: 1 invalid
/// request or an already running trace, 2 failed write.
pub(crate) fn begin(t: &mut Trace, frames: u32, flags: u32, limit: u64) -> u32 {
    let limit = if limit == 0 { DEFAULT_LIMIT } else { limit };
    if matches!(t.state, ARMED | ACTIVE)
        || frames == 0
        || frames > MAX_FRAMES
        || flags & !VERTICES != 0
        || !(MIN_LIMIT..=MAX_LIMIT).contains(&limit)
    {
        return 1;
    }
    *t = Trace {
        state: ARMED,
        flags,
        frames,
        limit,
        opaque: t.opaque,
        write: t.write,
        ..Trace::default()
    };
    let mut o = Out::new(t, limit);
    let header = write!(
        o,
        "# dreamgpu frame trace 1: frames={frames} flags={flags:#x} limit={limit}\n\
         # frame seq= stream= ctx=client:context:drawable OP function arguments \
         [data=bytes [values]|hex=prefix] [err=code]; indented lines are vertices\n"
    );
    o.flush();
    if header.is_err() || o.failed {
        t.state = DONE;
        t.reason = END_WRITE;
        return 2;
    }
    0
}

pub(crate) fn record(t: &mut Trace, r: &[u8], sequence: u32, stream: u32, error: u32) -> u32 {
    let op = word(r, DG_GL_OFF_OP as usize);
    match t.state {
        ARMED => {
            // Start at a frame boundary: the next record begins frame 1.
            if op == DG_GL_PRESENT {
                t.state = ACTIVE;
            }
            return t.state;
        }
        ACTIVE => {}
        _ => return t.state,
    }
    let (frame, flags, limit) = (t.completed + 1, t.flags, t.limit);
    let mut o = Out::new(t, limit);
    let formatted = format(&mut o, r, frame, sequence, stream, error, flags);
    o.flush();
    let (full, failed) = (o.full, o.failed);
    t.records += 1;
    if failed {
        end(t, END_WRITE);
    } else if full || formatted.is_err() {
        end(t, END_LIMIT);
    } else if op == DG_GL_PRESENT {
        t.completed += 1;
        if t.completed >= t.frames {
            end(t, END_FRAMES);
        }
    }
    t.state
}

/// Ends an armed or active trace with `reason`, writing its end line.
pub(crate) fn end(t: &mut Trace, reason: u32) {
    if !matches!(t.state, ARMED | ACTIVE) {
        return;
    }
    t.state = DONE;
    t.reason = reason;
    if reason == END_WRITE {
        return;
    }
    let reason_name = match reason {
        END_FRAMES => "frames",
        END_LIMIT => "limit",
        END_CANCELLED => "cancelled",
        _ => "other",
    };
    let (completed, records, bytes, limit) = (t.completed, t.records, t.bytes, t.limit);
    let mut o = Out::new(t, limit + TRAILER);
    // A limit can cut a line short; start the end line on its own line.
    let _ = writeln!(
        o,
        "{}# end reason={reason_name} frames={completed} records={records} bytes={bytes}",
        if reason == END_LIMIT { "\n" } else { "" }
    );
    o.flush();
    if o.failed {
        t.reason = END_WRITE;
    }
}

/// # Safety
/// `t` is valid and exclusively owned by the caller; its sink is valid.
#[no_mangle]
pub unsafe extern "C" fn dreamgpu_trace_begin(
    t: *mut Trace,
    frames: u32,
    flags: u32,
    limit: u64,
) -> u32 {
    if t.is_null() {
        return 1;
    }
    begin(unsafe { &mut *t }, frames, flags, limit)
}

/// Called after each executed record while a trace is attached; returns the
/// trace state. Records outside the capture window are not formatted.
///
/// # Safety
/// `t` as for `dreamgpu_trace_begin`; `record` is the immutable, validated
/// `bytes`-byte record snapshot that was just executed.
#[no_mangle]
pub unsafe extern "C" fn dreamgpu_trace_record(
    t: *mut Trace,
    record: *const u8,
    bytes: u32,
    sequence: u32,
    stream: u32,
    error: u32,
) -> u32 {
    if t.is_null() {
        return IDLE;
    }
    let t = unsafe { &mut *t };
    if record.is_null() || bytes < DG_GL_HEADER_BYTES {
        return t.state;
    }
    let r = unsafe { core::slice::from_raw_parts(record, bytes as usize) };
    self::record(t, r, sequence, stream, error)
}

/// Ends an armed or active trace early, e.g. with `END_CANCELLED`.
///
/// # Safety
/// As for `dreamgpu_trace_begin`.
#[no_mangle]
pub unsafe extern "C" fn dreamgpu_trace_end(t: *mut Trace, reason: u32) {
    if !t.is_null() {
        end(unsafe { &mut *t }, reason);
    }
}

#[cfg(test)]
mod tests;
