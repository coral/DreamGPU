//! EXT_framebuffer_object, EXT_framebuffer_blit and EXT_packed_depth_stencil
//! through the device: render-to-texture with colour, depth and stencil
//! attachments sampled afterwards; cube-face, 3D-slice and 1D attachments;
//! renderbuffers; completeness status of incomplete and unsupported
//! combinations; scaled, flipped, depth and out-of-bounds blits; binding zero
//! restoring the window for drawing, presentation and readback; ReadPixels
//! from framebuffer objects beyond the window and beyond their images;
//! deletion while bound and attached; renderbuffers shared and framebuffers
//! private between contexts; GenerateMipmap; display lists; and 2D depth
//! textures. Every GL error stays a GL error: no batch in this file may fail.
use super::fragment_program::{Device, start};
use super::textures::{call, data_call, receive, words};
use super::*;

const BIND_FRAMEBUFFER: u32 = 60;
const BIND_RENDERBUFFER: u32 = 72;
const BLIT: u32 = 131;
const CHECK_STATUS: u32 = 148;
const DELETE_FRAMEBUFFERS: u32 = 407;
const DELETE_RENDERBUFFERS: u32 = 425;
const FRAMEBUFFER_RENDERBUFFER: u32 = 631;
const FRAMEBUFFER_TEXTURE_1D: u32 = 637;
const FRAMEBUFFER_TEXTURE_2D: u32 = 639;
const FRAMEBUFFER_TEXTURE_3D: u32 = 641;
const GENERATE_MIPMAP: u32 = 661;
const GET_ATTACHMENT: u32 = 782;
const GET_RENDERBUFFER: u32 = 1011;
const RENDERBUFFER_STORAGE: u32 = 1970;

const VIEWPORT: u32 = 2871;
const VERTEX3F: u32 = 2538;
const VERTEX2F: u32 = 2524;
const TEXCOORD2F: u32 = 2179;
const COLOR4F: u32 = 219;
const DEPTH_FUNC: u32 = 441;
const CLEAR_DEPTH: u32 = 167;
const CLEAR_STENCIL: u32 = 181;
const STENCIL_FUNC: u32 = 2116;
const STENCIL_OP: u32 = 2121;
const TEX_IMAGE_3D: u32 = 2263;
const SCISSOR: u32 = 2032;
const GET_TEX_IMAGE: u32 = 1044;
const COPY_TEX_IMAGE_2D: u32 = 342;
const DELETE_TEXTURES: u32 = 432;
const GET_INTEGER: u32 = 809;
const READ_PIXELS: u32 = 1956;
const DRAW_BUFFER: u32 = 476;
const READ_BUFFER: u32 = 1952;
const BIND_TEXTURE: u32 = 78;
const TEX_PARAMETER_I: u32 = 2271;
const TEX_IMAGE_2D: u32 = 2260;
const TEX_IMAGE_1D: u32 = 2259;
const ENABLE: u32 = 521;
const DISABLE: u32 = 453;
const CLEAR: u32 = 154;
const CLEAR_COLOR: u32 = 163;
const BEGIN: u32 = 26;
const END: u32 = 534;
const NEW_LIST: u32 = 1592;
const END_LIST: u32 = 539;
const GEN_LISTS: u32 = 671;
const CALL_LIST: u32 = 146;
const TEX_ENV_I: u32 = 2246;
const PUSH_ATTRIB: u32 = 1909;
const POP_ATTRIB: u32 = 1711;

const FRAMEBUFFER: u32 = 0x8d40;
const READ_FRAMEBUFFER: u32 = 0x8ca8;
const DRAW_FRAMEBUFFER: u32 = 0x8ca9;
const RENDERBUFFER: u32 = 0x8d41;
const COLOR0: u32 = 0x8ce0;
const COLOR1: u32 = 0x8ce1;
const DEPTH: u32 = 0x8d00;
const STENCIL: u32 = 0x8d20;
const COMPLETE: u32 = 0x8cd5;
const INCOMPLETE_ATTACHMENT: u32 = 0x8cd6;
const MISSING_ATTACHMENT: u32 = 0x8cd7;
const UNSUPPORTED: u32 = 0x8cdd;
const FRAMEBUFFER_BINDING: u32 = 0x8ca6;
const READ_FRAMEBUFFER_BINDING: u32 = 0x8caa;
const RENDERBUFFER_BINDING: u32 = 0x8ca7;
const MAX_COLOR_ATTACHMENTS: u32 = 0x8cdf;
const MAX_RENDERBUFFER_SIZE: u32 = 0x84e8;
const OBJECT_TYPE: u32 = 0x8cd0;
const OBJECT_NAME: u32 = 0x8cd1;
const TEXTURE_LEVEL: u32 = 0x8cd2;
const CUBE_MAP_FACE: u32 = 0x8cd3;
const ZOFFSET: u32 = 0x8cd4;
const RB_WIDTH: u32 = 0x8d42;
const RB_HEIGHT: u32 = 0x8d43;
const RB_FORMAT: u32 = 0x8d44;
const RB_DEPTH_SIZE: u32 = 0x8d54;
const RB_STENCIL_SIZE: u32 = 0x8d55;
const TEXTURE: u32 = 0x1702;

const TEXTURE_1D: u32 = 0x0de0;
const TEXTURE_2D: u32 = 0x0de1;
const TEXTURE_3D: u32 = 0x806f;
const TEXTURE_CUBE_MAP: u32 = 0x8513;
const POSITIVE_X: u32 = 0x8515;
const NEGATIVE_Y: u32 = 0x8518;
const RGBA: u32 = 0x1908;
const RGBA8: u32 = 0x8058;
const UNSIGNED_BYTE: u32 = 0x1401;
const UNSIGNED_SHORT: u32 = 0x1403;
const DEPTH_COMPONENT: u32 = 0x1902;
const DEPTH_COMPONENT16: u32 = 0x81a5;
const DEPTH_COMPONENT24: u32 = 0x81a6;
const STENCIL_INDEX8: u32 = 0x8d48;
const DEPTH_STENCIL: u32 = 0x84f9;
const UNSIGNED_INT_24_8: u32 = 0x84fa;
const DEPTH24_STENCIL8: u32 = 0x88f0;
const NEAREST: u32 = 0x2600;
const LINEAR: u32 = 0x2601;
const MIN_FILTER: u32 = 0x2801;
const MAG_FILTER: u32 = 0x2800;
const DEPTH_TEST: u32 = 0x0b71;
const STENCIL_TEST: u32 = 0x0b90;
const SCISSOR_TEST: u32 = 0x0c11;
const LESS: u32 = 0x0201;
const ALWAYS: u32 = 0x0207;
const EQUAL: u32 = 0x0202;
const KEEP: u32 = 0x1e00;
const REPLACE: u32 = 0x1e01;
const COLOR_BIT: u32 = 0x4000;
const DEPTH_BIT: u32 = 0x0100;
const STENCIL_BIT: u32 = 0x0400;
const QUADS: u32 = 7;
const FRONT: u32 = 0x0404;
const BACK: u32 = 0x0405;
const NONE: u32 = 0;
const DRAW_BUFFER_NAME: u32 = 0x0c01;
const READ_BUFFER_NAME: u32 = 0x0c02;
const TEXTURE_ENV: u32 = 0x2300;
const TEXTURE_ENV_MODE: u32 = 0x2200;
const REPLACE_ENV: u32 = 0x1e01;
const COMPILE: u32 = 0x1300;
const COLOR_BUFFER_BIT: u32 = 0x4000;
const INVALID_ENUM: u32 = 0x0500;
const INVALID_VALUE: u32 = 0x0501;
const INVALID_OPERATION: u32 = 0x0502;
const INVALID_FRAMEBUFFER_OPERATION: u32 = 0x0506;
const READ_DEPTH: u32 = 0x10000;
const READ_STENCIL: u32 = 0x20000;
const DG_GL_RESULT_INT: u32 = 2;

const RED: [u8; 4] = [255, 0, 0, 255];
const GREEN: [u8; 4] = [0, 255, 0, 255];
const BLUE: [u8; 4] = [0, 0, 255, 255];
const YELLOW: [u8; 4] = [255, 255, 0, 255];
const MAGENTA: [u8; 4] = [255, 0, 255, 255];
const CYAN: [u8; 4] = [0, 255, 255, 255];
const WHITE: [u8; 4] = [255, 255, 255, 255];
const BLACK: [u8; 4] = [0, 0, 0, 255];
const ZERO: [u8; 4] = [0, 0, 0, 0];

fn f(v: f32) -> u32 {
    v.to_bits()
}
fn rgba(c: [u8; 4]) -> [u32; 4] {
    c.map(|v| f(f32::from(v) / 255.))
}
fn clear(color: [u8; 4]) -> Vec<(u32, Vec<u32>)> {
    vec![call(CLEAR_COLOR, &rgba(color)), call(CLEAR, &[COLOR_BIT])]
}
/// A flat quad over normalized device x0..x1, y0..y1 at depth z.
fn quad(x0: f32, y0: f32, x1: f32, y1: f32, z: f32, color: [u8; 4]) -> Vec<(u32, Vec<u32>)> {
    let mut out = vec![call(COLOR4F, &rgba(color)), call(BEGIN, &[QUADS])];
    for (x, y) in [(x0, y0), (x1, y0), (x1, y1), (x0, y1)] {
        out.push(call(VERTEX3F, &[f(x), f(y), f(z)]));
    }
    out.push(call(END, &[]));
    out
}
/// The whole viewport textured with texture coordinates 0..1.
fn textured() -> Vec<(u32, Vec<u32>)> {
    let mut out = vec![call(COLOR4F, &rgba(WHITE)), call(BEGIN, &[QUADS])];
    for (x, y) in [(-1f32, -1f32), (1., -1.), (1., 1.), (-1., 1.)] {
        out.push(call(TEXCOORD2F, &[f((x + 1.) / 2.), f((y + 1.) / 2.)]));
        out.push(call(VERTEX2F, &[f(x), f(y)]));
    }
    out.push(call(END, &[]));
    out
}
/// A NEAREST texture: zero-defined storage, or the given RGBA8 texels.
fn texture_2d(name: u32, internal: u32, w: u32, h: u32, texels: &[u8]) -> Vec<(u32, Vec<u32>)> {
    vec![
        call(BIND_TEXTURE, &[TEXTURE_2D, name]),
        call(TEX_PARAMETER_I, &[TEXTURE_2D, MIN_FILTER, NEAREST]),
        call(TEX_PARAMETER_I, &[TEXTURE_2D, MAG_FILTER, NEAREST]),
        data_call(
            TEX_IMAGE_2D,
            &[TEXTURE_2D, 0, internal, w, h, 0, RGBA, UNSIGNED_BYTE],
            texels,
        ),
    ]
}
fn storage(name: u32, format: u32, w: u32, h: u32) -> Vec<(u32, Vec<u32>)> {
    vec![
        call(BIND_RENDERBUFFER, &[RENDERBUFFER, name]),
        call(RENDERBUFFER_STORAGE, &[RENDERBUFFER, format, w, h]),
    ]
}
fn attach_2d(target: u32, point: u32, textarget: u32, name: u32) -> (u32, Vec<u32>) {
    call(FRAMEBUFFER_TEXTURE_2D, &[target, point, textarget, name, 0])
}
fn attach_rb(point: u32, name: u32) -> (u32, Vec<u32>) {
    call(
        FRAMEBUFFER_RENDERBUFFER,
        &[FRAMEBUFFER, point, RENDERBUFFER, name],
    )
}
fn delete(function: u32, names: &[u32]) -> (u32, Vec<u32>) {
    let bytes: Vec<u8> = names.iter().flat_map(|n| n.to_le_bytes()).collect();
    data_call(function, &[names.len() as u32], &bytes)
}
fn blit(src: [i32; 4], dst: [i32; 4], mask: u32, filter: u32) -> (u32, Vec<u32>) {
    let mut args: Vec<u32> = src.iter().chain(&dst).map(|v| *v as u32).collect();
    args.extend([mask, filter]);
    call(BLIT, &args)
}

impl Device {
    fn integer_for(&mut self, context: u32, pname: u32) -> u32 {
        self.word_for(context, GET_INTEGER, &[pname])
    }
    fn integer(&mut self, pname: u32) -> u32 {
        self.integer_for(1, pname)
    }
    fn status_for(&mut self, context: u32, target: u32) -> u32 {
        self.word_for(context, CHECK_STATUS, &[target])
    }
    fn status(&mut self, target: u32) -> u32 {
        self.status_for(1, target)
    }
    /// A w x h RGBA8 rectangle of the read framebuffer, bottom row first.
    /// Row by row: the test device's result page holds 512 bytes.
    fn rect_for(&mut self, context: u32, x: u32, y: u32, w: u32, h: u32) -> Vec<[u8; 4]> {
        assert!(w <= 128);
        let mut out = Vec::new();
        for row in y..y + h {
            let (kind, bytes) = self.query_for(context, READ_PIXELS, &[x, row, w | (1 << 16)]);
            assert_eq!(kind, DG_GL_RESULT_INT);
            out.extend_from_slice(bytes.as_chunks::<4>().0);
        }
        out
    }
    fn rect(&mut self, x: u32, y: u32, w: u32, h: u32) -> Vec<[u8; 4]> {
        self.rect_for(1, x, y, w, h)
    }
    fn at(&mut self, x: u32, y: u32) -> [u8; 4] {
        self.rect(x, y, 1, 1)[0]
    }
    fn depth_at(&mut self, x: u32, y: u32) -> f32 {
        f32::from_bits(
            words(
                &self
                    .query(READ_PIXELS, &[x | READ_DEPTH, y, 1 | (1 << 16)])
                    .1,
            )[0],
        )
    }
    fn stencil_at(&mut self, x: u32, y: u32) -> u32 {
        words(
            &self
                .query(READ_PIXELS, &[x | READ_STENCIL, y, 1 | (1 << 16)])
                .1,
        )[0]
    }
    /// Every texel of a level, through the texture read records.
    fn texels(&mut self, target: u32, level: u32, count: u32) -> Vec<[u8; 4]> {
        let mut out = Vec::new();
        while (out.len() as u32) < count {
            let first = out.len() as u32;
            let bytes = self.query(GET_TEXTURE_IMAGE, &[target, level, first]).1;
            out.extend_from_slice(bytes.as_chunks::<4>().0);
        }
        out.truncate(count as usize);
        out
    }
    fn attachment(&mut self, target: u32, point: u32, pname: u32) -> u32 {
        self.word(GET_ATTACHMENT, &[target, point, pname])
    }
    fn expect_error(&mut self, error: u32, label: &str) {
        assert_eq!(self.error(), error, "{label}");
        assert_eq!(self.error(), 0, "{label}: one error");
    }
}
const GET_TEXTURE_IMAGE: u32 = GET_TEX_IMAGE;

#[test]
#[ignore = "requires native host GPU and framebuffer object QEMU producer; diskless acceptance"]
fn qemu_framebuffer_objects_render_to_textures_and_restore_the_window() {
    let (mut d, server, directory) = start("fbo-rtt");
    let (device, queue, _) = native_gpu();
    let (wake, ready) = mpsc::sync_channel(1);
    server.set_callback(Arc::new(move |_| {
        let _ = wake.try_send(());
    }));
    // The window: 8x8, blue.
    d.submit(&clear(BLUE));
    assert_eq!(d.integer(FRAMEBUFFER_BINDING), 0);
    assert_eq!(d.status(FRAMEBUFFER), COMPLETE);
    let attachments = d.integer(MAX_COLOR_ATTACHMENTS);
    assert!((1..=4).contains(&attachments));
    assert!((16..=4096).contains(&d.integer(MAX_RENDERBUFFER_SIZE)));
    // A 16x16 colour texture with a packed depth/stencil renderbuffer, larger
    // than the window.
    let mut records = texture_2d(10, RGBA8, 16, 16, &[]);
    records.extend(storage(20, DEPTH24_STENCIL8, 16, 16));
    records.extend([
        call(BIND_FRAMEBUFFER, &[FRAMEBUFFER, 1]),
        attach_2d(FRAMEBUFFER, COLOR0, TEXTURE_2D, 10),
        attach_rb(DEPTH, 20),
        attach_rb(STENCIL, 20),
    ]);
    d.submit(&records);
    assert_eq!(d.status(FRAMEBUFFER), COMPLETE);
    assert_eq!(d.integer(FRAMEBUFFER_BINDING), 1);
    assert_eq!(d.integer(READ_FRAMEBUFFER_BINDING), 1);
    assert_eq!(d.integer(RENDERBUFFER_BINDING), 20);
    assert_eq!(d.integer(DRAW_BUFFER_NAME), COLOR0);
    for (pname, value) in [
        (RB_WIDTH, 16),
        (RB_HEIGHT, 16),
        (RB_FORMAT, DEPTH24_STENCIL8),
        (RB_DEPTH_SIZE, 24),
        (RB_STENCIL_SIZE, 8),
    ] {
        assert_eq!(d.word(GET_RENDERBUFFER, &[RENDERBUFFER, pname]), value);
    }
    // Renderbuffer storage starts zeroed: depth 0 and stencil 0.
    assert_eq!(d.depth_at(15, 15), 0.);
    assert_eq!(d.stencil_at(15, 15), 0);
    // Depth and stencil decide what lands in each half.
    let mut records = vec![call(VIEWPORT, &[0, 0, 16, 16])];
    let one = 1f64.to_bits();
    records.push(call(CLEAR_DEPTH, &[one as u32, (one >> 32) as u32]));
    records.push(call(CLEAR_STENCIL, &[0]));
    records.push(call(CLEAR_COLOR, &rgba(RED)));
    records.push(call(CLEAR, &[COLOR_BIT | DEPTH_BIT | STENCIL_BIT]));
    records.extend([
        call(ENABLE, &[DEPTH_TEST]),
        call(DEPTH_FUNC, &[LESS]),
        call(ENABLE, &[STENCIL_TEST]),
        call(STENCIL_FUNC, &[ALWAYS, 1, 0xff]),
        call(STENCIL_OP, &[KEEP, KEEP, REPLACE]),
    ]);
    records.extend(quad(-1., -1., 0., 1., 0., GREEN));
    records.extend([
        call(STENCIL_FUNC, &[EQUAL, 0, 0xff]),
        call(STENCIL_OP, &[KEEP, KEEP, KEEP]),
    ]);
    records.extend(quad(-1., -1., 1., 1., -0.5, YELLOW));
    records.push(call(STENCIL_FUNC, &[ALWAYS, 0, 0xff]));
    records.extend(quad(-1., -1., 1., 1., 0.9, WHITE));
    records.extend([call(DISABLE, &[STENCIL_TEST]), call(DISABLE, &[DEPTH_TEST])]);
    d.submit(&records);
    assert_eq!(d.error(), 0);
    // ReadPixels from the framebuffer object, including beyond the window and
    // beyond its 16x16 images (zero there).
    let row = d.rect(0, 9, 16, 1);
    assert_eq!(row, [vec![GREEN; 8], vec![YELLOW; 8]].concat());
    let edge = d.rect(14, 15, 4, 2);
    assert_eq!(edge, [YELLOW, YELLOW, ZERO, ZERO, ZERO, ZERO, ZERO, ZERO]);
    assert_eq!(d.rect(100, 100, 2, 1), [ZERO, ZERO]);
    assert_eq!(d.stencil_at(3, 3), 1);
    assert_eq!(d.stencil_at(12, 3), 0);
    assert!((d.depth_at(3, 3) - 0.5).abs() < 1e-6);
    assert!((d.depth_at(12, 3) - 0.25).abs() < 1e-6);
    // Binding zero is the window again; sample the texture into it.
    let mut records = vec![
        call(BIND_FRAMEBUFFER, &[FRAMEBUFFER, 0]),
        call(VIEWPORT, &[0, 0, 8, 8]),
        call(BIND_TEXTURE, &[TEXTURE_2D, 10]),
        call(TEX_ENV_I, &[TEXTURE_ENV, TEXTURE_ENV_MODE, REPLACE_ENV]),
        call(ENABLE, &[TEXTURE_2D]),
    ];
    records.extend(textured());
    records.push(call(DISABLE, &[TEXTURE_2D]));
    d.submit(&records);
    assert_eq!(d.integer(FRAMEBUFFER_BINDING), 0);
    assert_eq!(d.integer(DRAW_BUFFER_NAME), BACK);
    assert_eq!(
        d.rect(0, 0, 8, 1),
        [vec![GREEN; 4], vec![YELLOW; 4]].concat()
    );
    // Present with a framebuffer object bound: the window's back buffer is
    // exported and becomes its front buffer; the binding survives.
    d.submit(&[call(BIND_FRAMEBUFFER, &[FRAMEBUFFER, 1]), (7, vec![])]);
    assert_gpu_image(&device, &queue, &receive(&server, &ready, 1, 1), |x, _| {
        if x < 4 {
            [0, 255, 0, 255]
        } else {
            [0, 255, 255, 255]
        }
    });
    assert_eq!(d.integer(FRAMEBUFFER_BINDING), 1);
    assert_eq!(d.at(0, 15), GREEN, "the object kept its contents");
    d.submit(&[
        call(BIND_FRAMEBUFFER, &[READ_FRAMEBUFFER, 0]),
        call(READ_BUFFER, &[FRONT]),
    ]);
    assert_eq!(d.at(7, 7), YELLOW);
    // Drawing into the object while the window is the read framebuffer.
    d.submit(&clear(MAGENTA));
    assert_eq!(d.at(0, 0), GREEN, "front untouched");
    d.submit(&[call(READ_BUFFER, &[BACK])]);
    assert_eq!(d.at(0, 0), ZERO, "back (the old front) untouched");
    d.submit(&[call(BIND_FRAMEBUFFER, &[READ_FRAMEBUFFER, 1])]);
    assert_eq!(d.at(15, 15), MAGENTA);
    assert_eq!(d.error(), 0);
    // A feedback loop: texture 10 sampled while it is the colour attachment.
    // The pixels are undefined; the batch and the context must survive.
    let mut records = vec![
        call(VIEWPORT, &[0, 0, 16, 16]),
        call(BIND_TEXTURE, &[TEXTURE_2D, 10]),
        call(ENABLE, &[TEXTURE_2D]),
    ];
    records.extend(textured());
    records.push(call(DISABLE, &[TEXTURE_2D]));
    records.extend(clear(CYAN));
    d.submit(&records);
    assert_eq!(d.error(), 0);
    assert_eq!(d.at(3, 3), CYAN, "the context keeps drawing");
    drop(server);
    drop(d);
    let _ = std::fs::remove_dir_all(directory);
}

#[test]
#[ignore = "requires native host GPU and framebuffer object QEMU producer; diskless acceptance"]
fn qemu_framebuffer_objects_attach_faces_slices_1d_and_report_status() {
    let (mut d, _server, directory) = start("fbo-attach");
    // A cube map with six zero-defined 4x4 faces.
    let mut records = vec![
        call(BIND_TEXTURE, &[TEXTURE_CUBE_MAP, 30]),
        call(TEX_PARAMETER_I, &[TEXTURE_CUBE_MAP, MIN_FILTER, NEAREST]),
    ];
    for face in 0..6 {
        records.push(data_call(
            TEX_IMAGE_2D,
            &[POSITIVE_X + face, 0, RGBA8, 4, 4, 0, RGBA, UNSIGNED_BYTE],
            &[],
        ));
    }
    // A zero-defined 4x4x4 volume and an 8-wide 1D texture.
    records.extend([
        call(BIND_TEXTURE, &[TEXTURE_3D, 31]),
        data_call(
            TEX_IMAGE_3D,
            &[TEXTURE_3D, 0, RGBA8, 4, 4, 4, 0, RGBA, UNSIGNED_BYTE],
            &[],
        ),
        call(BIND_TEXTURE, &[TEXTURE_1D, 32]),
        data_call(
            TEX_IMAGE_1D,
            &[TEXTURE_1D, 0, RGBA8, 8, 1, 0, RGBA, UNSIGNED_BYTE],
            &[],
        ),
        call(BIND_FRAMEBUFFER, &[FRAMEBUFFER, 2]),
        call(VIEWPORT, &[0, 0, 4, 4]),
    ]);
    d.submit(&records);
    // An object without attachments, then the faces one after another.
    assert_eq!(d.status(FRAMEBUFFER), MISSING_ATTACHMENT);
    let mut records = vec![attach_2d(FRAMEBUFFER, COLOR0, NEGATIVE_Y, 30)];
    records.extend(clear(MAGENTA));
    records.push(attach_2d(FRAMEBUFFER, COLOR0, POSITIVE_X, 30));
    records.extend(clear(CYAN));
    d.submit(&records);
    assert_eq!(d.status(FRAMEBUFFER), COMPLETE);
    assert_eq!(d.attachment(FRAMEBUFFER, COLOR0, OBJECT_TYPE), TEXTURE);
    assert_eq!(d.attachment(FRAMEBUFFER, COLOR0, OBJECT_NAME), 30);
    assert_eq!(d.attachment(FRAMEBUFFER, COLOR0, TEXTURE_LEVEL), 0);
    assert_eq!(d.attachment(FRAMEBUFFER, COLOR0, CUBE_MAP_FACE), POSITIVE_X);
    assert_eq!(d.attachment(FRAMEBUFFER, COLOR0, ZOFFSET), 0);
    assert_eq!(d.texels(POSITIVE_X, 0, 16), vec![CYAN; 16]);
    assert_eq!(d.texels(NEGATIVE_Y, 0, 16), vec![MAGENTA; 16]);
    assert_eq!(d.texels(POSITIVE_X + 2, 0, 16), vec![ZERO; 16]);
    // Slice 2 of the volume.
    let mut records = vec![call(
        FRAMEBUFFER_TEXTURE_3D,
        &[FRAMEBUFFER, COLOR0, TEXTURE_3D, 31, 0, 2],
    )];
    records.extend(clear(GREEN));
    d.submit(&records);
    assert_eq!(d.attachment(FRAMEBUFFER, COLOR0, ZOFFSET), 2);
    assert_eq!(d.attachment(FRAMEBUFFER, COLOR0, CUBE_MAP_FACE), 0);
    let volume = d.texels(TEXTURE_3D, 0, 64);
    for (i, texel) in volume.iter().enumerate() {
        assert_eq!(*texel, if i / 16 == 2 { GREEN } else { ZERO }, "texel {i}");
    }
    // The 1D texture, rendered and read back after a GetTexImage of the same
    // level was cached: the read cache never serves the older image.
    d.submit(&[call(
        FRAMEBUFFER_TEXTURE_1D,
        &[FRAMEBUFFER, COLOR0, TEXTURE_1D, 32, 0],
    )]);
    assert_eq!(d.texels(TEXTURE_1D, 0, 8), vec![ZERO; 8]);
    d.submit(&[call(VIEWPORT, &[0, 0, 8, 1])]);
    d.submit(&clear(RED));
    assert_eq!(d.texels(TEXTURE_1D, 0, 8), vec![RED; 8]);
    // Incomplete: a texture without an image, a renderbuffer without
    // storage, a depth image at a colour attachment.
    d.submit(&[
        call(BIND_TEXTURE, &[TEXTURE_2D, 33]),
        attach_2d(FRAMEBUFFER, COLOR0, TEXTURE_2D, 33),
    ]);
    assert_eq!(d.status(FRAMEBUFFER), INCOMPLETE_ATTACHMENT);
    // Drawing, clearing and reading an incomplete framebuffer are GL errors.
    d.submit(&clear(RED));
    d.expect_error(
        INVALID_FRAMEBUFFER_OPERATION,
        "clear of an incomplete framebuffer",
    );
    assert_eq!(d.rect(0, 0, 2, 1), [ZERO, ZERO]);
    d.expect_error(
        INVALID_FRAMEBUFFER_OPERATION,
        "read of an incomplete framebuffer",
    );
    let mut records = vec![call(BIND_RENDERBUFFER, &[RENDERBUFFER, 40])];
    records.push(attach_rb(COLOR0, 40));
    d.submit(&records);
    assert_eq!(d.status(FRAMEBUFFER), INCOMPLETE_ATTACHMENT);
    let mut records = storage(41, DEPTH_COMPONENT24, 4, 4);
    records.push(attach_rb(COLOR0, 41));
    d.submit(&records);
    assert_eq!(d.status(FRAMEBUFFER), INCOMPLETE_ATTACHMENT);
    // Separate depth and stencil renderbuffers: not a supported combination
    // of the native framebuffer (a GL error-free status, never a fault).
    let mut records = texture_2d(34, RGBA8, 4, 4, &[]);
    records.extend(storage(42, STENCIL_INDEX8, 4, 4));
    records.extend([
        attach_2d(FRAMEBUFFER, COLOR0, TEXTURE_2D, 34),
        attach_rb(DEPTH, 41),
        attach_rb(STENCIL, 42),
    ]);
    d.submit(&records);
    let separate = d.status(FRAMEBUFFER);
    assert!(
        separate == UNSUPPORTED || separate == COMPLETE,
        "status {separate:#x}"
    );
    assert_eq!(d.error(), 0);
    // State-dependent attachment problems are GL errors.
    d.submit(&[attach_2d(FRAMEBUFFER, COLOR0, TEXTURE_2D, 30)]);
    d.expect_error(INVALID_OPERATION, "a cube map through TEXTURE_2D");
    d.submit(&[attach_2d(FRAMEBUFFER, COLOR0, TEXTURE_2D, 99)]);
    d.expect_error(INVALID_OPERATION, "not a texture");
    d.submit(&[call(
        FRAMEBUFFER_TEXTURE_3D,
        &[FRAMEBUFFER, COLOR0, TEXTURE_3D, 31, 9, 0],
    )]);
    d.expect_error(INVALID_VALUE, "a 3D level beyond 256");
    d.submit(&[attach_rb(COLOR1, 77)]);
    d.expect_error(INVALID_OPERATION, "not a renderbuffer");
    d.submit(&[call(DRAW_BUFFER, &[BACK])]);
    d.expect_error(INVALID_OPERATION, "a window buffer of an object");
    d.submit(&[call(READ_BUFFER, &[NONE])]);
    assert_eq!(d.error(), 0);
    assert_eq!(d.integer(READ_BUFFER_NAME), NONE);
    d.submit(&[call(BIND_FRAMEBUFFER, &[FRAMEBUFFER, 0])]);
    d.submit(&[call(DRAW_BUFFER, &[COLOR0])]);
    d.expect_error(INVALID_OPERATION, "an attachment of the window");
    d.submit(&[call(READ_BUFFER, &[NONE])]);
    d.expect_error(INVALID_ENUM, "NONE as the window's read buffer");
    d.submit(&[attach_rb(COLOR0, 41)]);
    d.expect_error(INVALID_OPERATION, "attachments of the window");
    assert_eq!(d.attachment(FRAMEBUFFER, COLOR0, OBJECT_TYPE), 0);
    d.expect_error(INVALID_OPERATION, "attachment queries of the window");
    // The largest storage the protocol allows, unless the native limit is
    // smaller (then INVALID_VALUE).
    d.submit(&[call(
        RENDERBUFFER_STORAGE,
        &[RENDERBUFFER, RGBA8, 4096, 4096],
    )]);
    let limit = d.integer(MAX_RENDERBUFFER_SIZE);
    assert_eq!(d.error(), if limit < 4096 { INVALID_VALUE } else { 0 });
    d.submit(&[call(BIND_RENDERBUFFER, &[RENDERBUFFER, 0])]);
    d.submit(&[call(RENDERBUFFER_STORAGE, &[RENDERBUFFER, RGBA8, 4, 4])]);
    d.expect_error(INVALID_OPERATION, "storage without a renderbuffer");
    drop(d);
    let _ = std::fs::remove_dir_all(directory);
}

#[test]
#[ignore = "requires native host GPU and framebuffer object QEMU producer; diskless acceptance"]
fn qemu_framebuffer_blits_scale_flip_copy_depth_and_clip() {
    let (mut d, _server, directory) = start("fbo-blit");
    // Source: an 8x8 texture with four coloured quadrants, and a packed
    // depth/stencil renderbuffer whose left half is nearer.
    let mut texels = Vec::new();
    for y in 0..8 {
        for x in 0..8 {
            texels.extend(match (x < 4, y < 4) {
                (true, true) => RED,
                (false, true) => GREEN,
                (true, false) => BLUE,
                (false, false) => WHITE,
            });
        }
    }
    let mut records = texture_2d(10, RGBA8, 8, 8, &texels);
    records.extend(texture_2d(11, RGBA8, 16, 16, &[]));
    records.extend(texture_2d(12, RGBA8, 8, 8, &[]));
    records.extend(texture_2d(13, RGBA8, 8, 8, &[]));
    records.extend(storage(20, DEPTH24_STENCIL8, 8, 8));
    records.extend(storage(21, DEPTH24_STENCIL8, 8, 8));
    records.extend(storage(22, DEPTH_COMPONENT16, 8, 8));
    records.extend([
        call(BIND_FRAMEBUFFER, &[FRAMEBUFFER, 1]),
        attach_2d(FRAMEBUFFER, COLOR0, TEXTURE_2D, 10),
        attach_rb(DEPTH, 20),
        attach_rb(STENCIL, 20),
        call(VIEWPORT, &[0, 0, 8, 8]),
        call(ENABLE, &[SCISSOR_TEST]),
        call(SCISSOR, &[0, 0, 4, 8]),
        call(CLEAR_DEPTH, &[0, 0x3fd0_0000]), // 0.25
        call(CLEAR, &[DEPTH_BIT]),
        call(SCISSOR, &[4, 0, 4, 8]),
        call(CLEAR_DEPTH, &[0, 0x3fe8_0000]), // 0.75
        call(CLEAR, &[DEPTH_BIT]),
        call(DISABLE, &[SCISSOR_TEST]),
        call(BIND_FRAMEBUFFER, &[FRAMEBUFFER, 2]),
        attach_2d(FRAMEBUFFER, COLOR0, TEXTURE_2D, 11),
        call(BIND_FRAMEBUFFER, &[FRAMEBUFFER, 3]),
        attach_2d(FRAMEBUFFER, COLOR0, TEXTURE_2D, 12),
        attach_rb(DEPTH, 21),
        attach_rb(STENCIL, 21),
        call(BIND_FRAMEBUFFER, &[FRAMEBUFFER, 4]),
        attach_2d(FRAMEBUFFER, COLOR0, TEXTURE_2D, 13),
        attach_rb(DEPTH, 22),
    ]);
    d.submit(&records);
    // A 2x scaled NEAREST blit into 2's 16x16 texture, flipped vertically.
    let mut records = vec![
        call(BIND_FRAMEBUFFER, &[READ_FRAMEBUFFER, 1]),
        call(BIND_FRAMEBUFFER, &[DRAW_FRAMEBUFFER, 2]),
    ];
    records.extend(clear(BLACK));
    records.push(blit([0, 0, 8, 8], [0, 16, 16, 0], COLOR_BIT, NEAREST));
    d.submit(&records);
    assert_eq!(d.error(), 0);
    d.submit(&[call(BIND_FRAMEBUFFER, &[READ_FRAMEBUFFER, 2])]);
    let image = d.rect(0, 0, 16, 16);
    for y in 0..16 {
        for x in 0..16 {
            // Source row 7 - y/2 (flipped), column x/2.
            let (sx, sy) = (x / 2, 7 - y / 2);
            let expected = match (sx < 4, sy < 4) {
                (true, true) => RED,
                (false, true) => GREEN,
                (true, false) => BLUE,
                (false, false) => WHITE,
            };
            assert_eq!(image[y * 16 + x], expected, "scaled flipped {x},{y}");
        }
    }
    // Horizontally flipped LINEAR 1:1 blit into the window: exact.
    d.submit(&[
        call(BIND_FRAMEBUFFER, &[READ_FRAMEBUFFER, 1]),
        call(BIND_FRAMEBUFFER, &[DRAW_FRAMEBUFFER, 0]),
        blit([0, 0, 8, 8], [8, 0, 0, 8], COLOR_BIT, LINEAR),
        call(BIND_FRAMEBUFFER, &[READ_FRAMEBUFFER, 0]),
    ]);
    assert_eq!(d.error(), 0);
    assert_eq!(d.rect(0, 0, 8, 1), [vec![GREEN; 4], vec![RED; 4]].concat());
    // Out of bounds: the source rectangle extends 4 pixels beyond the left of
    // the 8x8 source. The part with a source is copied exactly; the rest of
    // the destination keeps its contents.
    let mut records = vec![
        call(BIND_FRAMEBUFFER, &[READ_FRAMEBUFFER, 1]),
        call(BIND_FRAMEBUFFER, &[DRAW_FRAMEBUFFER, 2]),
    ];
    records.extend(clear(MAGENTA));
    records.push(blit([-4, 0, 4, 4], [0, 0, 8, 4], COLOR_BIT, NEAREST));
    records.push(call(BIND_FRAMEBUFFER, &[READ_FRAMEBUFFER, 2]));
    d.submit(&records);
    assert_eq!(d.error(), 0);
    assert_eq!(
        d.rect(0, 0, 8, 1),
        [vec![MAGENTA; 4], vec![RED; 4]].concat()
    );
    // Depth: copy 1's depth into 3's, then draw at depth 0.5 through 3.
    let mut records = vec![
        call(BIND_FRAMEBUFFER, &[READ_FRAMEBUFFER, 1]),
        call(BIND_FRAMEBUFFER, &[DRAW_FRAMEBUFFER, 3]),
        blit([0, 0, 8, 8], [0, 0, 8, 8], DEPTH_BIT | STENCIL_BIT, NEAREST),
        call(BIND_FRAMEBUFFER, &[FRAMEBUFFER, 3]),
    ];
    records.extend(clear(BLACK));
    records.extend([call(ENABLE, &[DEPTH_TEST]), call(DEPTH_FUNC, &[LESS])]);
    records.extend(quad(-1., -1., 1., 1., 0., YELLOW));
    records.push(call(DISABLE, &[DEPTH_TEST]));
    d.submit(&records);
    assert_eq!(d.error(), 0);
    assert_eq!(
        d.rect(0, 3, 8, 1),
        [vec![BLACK; 4], vec![YELLOW; 4]].concat()
    );
    // A depth blit between different depth formats is a native GL error,
    // never a fault; one into a framebuffer without depth is ignored.
    d.submit(&[
        call(BIND_FRAMEBUFFER, &[READ_FRAMEBUFFER, 1]),
        call(BIND_FRAMEBUFFER, &[DRAW_FRAMEBUFFER, 4]),
        blit([0, 0, 8, 8], [0, 0, 8, 8], DEPTH_BIT, NEAREST),
    ]);
    d.expect_error(INVALID_OPERATION, "a depth blit between formats");
    d.submit(&[
        call(BIND_FRAMEBUFFER, &[DRAW_FRAMEBUFFER, 2]),
        blit([0, 0, 8, 8], [0, 0, 8, 8], DEPTH_BIT, NEAREST),
    ]);
    assert_eq!(
        d.error(),
        0,
        "a depth blit without a destination depth buffer"
    );
    // A blit in a display list executes when called; binding commands
    // inside a list execute at once.
    let list = words(&d.query(GEN_LISTS, &[3]).1)[1];
    assert_ne!(list, 0);
    let mut records = vec![call(BIND_FRAMEBUFFER, &[FRAMEBUFFER, 2])];
    records.extend(clear(BLACK));
    d.submit(&records);
    assert_eq!(d.word(NEW_LIST, &[list, COMPILE]), 0);
    d.submit(&[
        call(BIND_FRAMEBUFFER, &[READ_FRAMEBUFFER, 1]),
        blit([0, 0, 8, 8], [0, 0, 8, 8], COLOR_BIT, NEAREST),
    ]);
    assert_eq!(d.word(END_LIST, &[]), 0);
    assert_eq!(
        d.integer(READ_FRAMEBUFFER_BINDING),
        1,
        "bound while compiling"
    );
    d.submit(&[call(BIND_FRAMEBUFFER, &[READ_FRAMEBUFFER, 2])]);
    assert_eq!(d.at(0, 0), BLACK, "the compiled blit has not run");
    d.submit(&[
        call(BIND_FRAMEBUFFER, &[READ_FRAMEBUFFER, 1]),
        call(CALL_LIST, &[list]),
        call(BIND_FRAMEBUFFER, &[READ_FRAMEBUFFER, 2]),
    ]);
    assert_eq!(d.at(0, 0), RED);
    assert_eq!(d.at(7, 7), WHITE);
    assert_eq!(d.error(), 0);
    drop(d);
    let _ = std::fs::remove_dir_all(directory);
}

#[test]
#[ignore = "requires native host GPU and framebuffer object QEMU producer; diskless acceptance"]
fn qemu_framebuffer_deletion_sharing_mipmaps_and_depth_textures() {
    let (mut d, _server, directory) = start("fbo-share");
    // A shared context 2 and an unshared context 3, each with a drawable.
    d.submit_for(2, &[(1, vec![1]), (3, vec![8, 8]), (5, vec![])]);
    d.submit_for(3, &[(1, vec![0]), (3, vec![8, 8]), (5, vec![])]);
    // Context 1: renderbuffer 40 (4x4 RGBA8) in framebuffer 5, red.
    let mut records = storage(40, RGBA8, 4, 4);
    records.extend([
        call(BIND_FRAMEBUFFER, &[FRAMEBUFFER, 5]),
        attach_rb(COLOR0, 40),
        call(VIEWPORT, &[0, 0, 4, 4]),
    ]);
    records.extend(clear(RED));
    d.submit(&records);
    // Context 2: framebuffer 5 is its own (empty) object; renderbuffer 40 is
    // the shared one, with context 1's pixels.
    d.submit_for(2, &[call(BIND_FRAMEBUFFER, &[FRAMEBUFFER, 5])]);
    assert_eq!(d.status_for(2, FRAMEBUFFER), MISSING_ATTACHMENT);
    d.submit_for(2, &[attach_rb(COLOR0, 40)]);
    assert_eq!(d.status_for(2, FRAMEBUFFER), COMPLETE);
    assert_eq!(d.rect_for(2, 0, 0, 4, 1), vec![RED; 4]);
    let mut records = vec![call(VIEWPORT, &[0, 0, 4, 4])];
    records.extend(clear(GREEN));
    d.submit_for(2, &records);
    assert_eq!(
        d.rect(3, 3, 1, 1),
        [GREEN],
        "context 1 sees context 2's write"
    );
    // Context 3 shares nothing.
    d.submit_for(
        3,
        &[
            call(BIND_FRAMEBUFFER, &[FRAMEBUFFER, 5]),
            attach_rb(COLOR0, 40),
        ],
    );
    assert_eq!(d.error_for(3), INVALID_OPERATION);
    // Deleting renderbuffer 40 in context 1 detaches it from context 1's bound
    // framebuffer only; context 2's attachment keeps the image alive.
    d.submit(&[delete(DELETE_RENDERBUFFERS, &[40])]);
    assert_eq!(d.integer(RENDERBUFFER_BINDING), 0);
    assert_eq!(d.status(FRAMEBUFFER), MISSING_ATTACHMENT);
    assert_eq!(d.status_for(2, FRAMEBUFFER), COMPLETE);
    assert_eq!(d.rect_for(2, 1, 1, 1, 1), [GREEN]);
    assert_eq!(
        d.word_for(2, GET_ATTACHMENT, &[FRAMEBUFFER, COLOR0, OBJECT_NAME]),
        40
    );
    // A deleted texture: detached from the bound framebuffer, kept by another.
    let mut records = texture_2d(50, RGBA8, 4, 4, &[]);
    records.extend([
        call(BIND_FRAMEBUFFER, &[FRAMEBUFFER, 6]),
        attach_2d(FRAMEBUFFER, COLOR0, TEXTURE_2D, 50),
        call(BIND_FRAMEBUFFER, &[FRAMEBUFFER, 7]),
        attach_2d(FRAMEBUFFER, COLOR0, TEXTURE_2D, 50),
    ]);
    records.extend(clear(BLUE));
    records.push(delete(DELETE_TEXTURES, &[50]));
    d.submit(&records);
    assert_eq!(d.status(FRAMEBUFFER), MISSING_ATTACHMENT);
    d.submit(&[call(BIND_FRAMEBUFFER, &[FRAMEBUFFER, 6])]);
    assert_eq!(d.status(FRAMEBUFFER), COMPLETE);
    assert_eq!(d.attachment(FRAMEBUFFER, COLOR0, OBJECT_NAME), 50);
    assert_eq!(d.rect(0, 0, 1, 1), [BLUE], "the deleted image lives on");
    // Deleting the bound draw framebuffer rebinds the window: the clear lands
    // there.
    let mut records = vec![
        call(BIND_FRAMEBUFFER, &[READ_FRAMEBUFFER, 0]),
        delete(DELETE_FRAMEBUFFERS, &[6, 6, 0, 1234]),
        call(VIEWPORT, &[0, 0, 8, 8]),
    ];
    records.extend(clear(YELLOW));
    d.submit(&records);
    assert_eq!(d.integer(FRAMEBUFFER_BINDING), 0);
    assert_eq!(d.rect(0, 0, 8, 8), vec![YELLOW; 64]);
    // GenerateMipmap after rendering into the base level.
    let mut records = texture_2d(60, RGBA8, 4, 4, &RED.repeat(16));
    records.extend([
        call(BIND_FRAMEBUFFER, &[FRAMEBUFFER, 8]),
        attach_2d(FRAMEBUFFER, COLOR0, TEXTURE_2D, 60),
        call(VIEWPORT, &[0, 0, 4, 4]),
    ]);
    records.extend(clear(CYAN));
    records.push(call(GENERATE_MIPMAP, &[TEXTURE_2D]));
    d.submit(&records);
    assert_eq!(d.error(), 0);
    assert_eq!(d.texels(TEXTURE_2D, 1, 4), vec![CYAN; 4]);
    assert_eq!(d.texels(TEXTURE_2D, 2, 1), vec![CYAN; 1]);
    // 2D depth textures: zero-defined packed depth/stencil storage as an
    // attachment, and the window's depth copied into a sampled depth texture.
    let mut records = vec![
        call(BIND_TEXTURE, &[TEXTURE_2D, 70]),
        call(TEX_PARAMETER_I, &[TEXTURE_2D, MIN_FILTER, NEAREST]),
        call(TEX_PARAMETER_I, &[TEXTURE_2D, MAG_FILTER, NEAREST]),
        data_call(
            TEX_IMAGE_2D,
            &[
                TEXTURE_2D,
                0,
                DEPTH24_STENCIL8,
                4,
                4,
                0,
                DEPTH_STENCIL,
                UNSIGNED_INT_24_8,
            ],
            &[],
        ),
        attach_2d(FRAMEBUFFER, DEPTH, TEXTURE_2D, 70),
        attach_2d(FRAMEBUFFER, STENCIL, TEXTURE_2D, 70),
    ];
    d.submit(&records);
    assert_eq!(d.status(FRAMEBUFFER), COMPLETE);
    assert_eq!(d.depth_at(1, 1), 0., "zero-defined depth");
    assert_eq!(d.stencil_at(1, 1), 0, "zero-defined stencil");
    records = vec![call(CLEAR_STENCIL, &[7]), call(CLEAR, &[STENCIL_BIT])];
    d.submit(&records);
    assert_eq!(d.stencil_at(2, 2), 7);
    // Depth 16 uploads with data, sampled as luminance through REPLACE.
    let mut records = vec![
        call(BIND_FRAMEBUFFER, &[FRAMEBUFFER, 0]),
        call(VIEWPORT, &[0, 0, 8, 8]),
        call(BIND_TEXTURE, &[TEXTURE_2D, 71]),
        call(TEX_PARAMETER_I, &[TEXTURE_2D, MIN_FILTER, NEAREST]),
        call(TEX_PARAMETER_I, &[TEXTURE_2D, MAG_FILTER, NEAREST]),
        data_call(
            TEX_IMAGE_2D,
            &[
                TEXTURE_2D,
                0,
                DEPTH_COMPONENT16,
                2,
                1,
                0,
                DEPTH_COMPONENT,
                UNSIGNED_SHORT,
            ],
            &[0, 0, 0xff, 0xff],
        ),
        call(TEX_ENV_I, &[TEXTURE_ENV, TEXTURE_ENV_MODE, REPLACE_ENV]),
        call(ENABLE, &[TEXTURE_2D]),
    ];
    records.extend(textured());
    records.push(call(DISABLE, &[TEXTURE_2D]));
    d.submit(&records);
    assert_eq!(d.error(), 0);
    assert_eq!(
        d.rect(0, 0, 8, 1),
        [vec![BLACK; 4], vec![WHITE; 4]].concat()
    );
    // CopyTexImage2D of the window's depth: left half 0, right half 1.
    let one = 1f64.to_bits();
    let mut records = vec![
        call(ENABLE, &[SCISSOR_TEST]),
        call(SCISSOR, &[0, 0, 4, 8]),
        call(CLEAR_DEPTH, &[0, 0]),
        call(CLEAR, &[DEPTH_BIT]),
        call(SCISSOR, &[4, 0, 4, 8]),
        call(CLEAR_DEPTH, &[one as u32, (one >> 32) as u32]),
        call(CLEAR, &[DEPTH_BIT]),
        call(DISABLE, &[SCISSOR_TEST]),
        call(BIND_TEXTURE, &[TEXTURE_2D, 72]),
        call(TEX_PARAMETER_I, &[TEXTURE_2D, MIN_FILTER, NEAREST]),
        call(TEX_PARAMETER_I, &[TEXTURE_2D, MAG_FILTER, NEAREST]),
        call(
            COPY_TEX_IMAGE_2D,
            &[TEXTURE_2D, 0, DEPTH_COMPONENT, 0, 0, 8, 8, 0],
        ),
        call(ENABLE, &[TEXTURE_2D]),
    ];
    records.extend(textured());
    records.push(call(DISABLE, &[TEXTURE_2D]));
    d.submit(&records);
    assert_eq!(d.error(), 0);
    assert_eq!(
        d.rect(0, 5, 8, 1),
        [vec![BLACK; 4], vec![WHITE; 4]].concat()
    );
    // PushAttrib/PopAttrib across framebuffers: a saved draw buffer returns
    // only into the framebuffer it was saved from.
    d.submit(&[
        call(DRAW_BUFFER, &[FRONT]),
        call(PUSH_ATTRIB, &[COLOR_BUFFER_BIT]),
        call(BIND_FRAMEBUFFER, &[FRAMEBUFFER, 8]),
        call(DRAW_BUFFER, &[COLOR1]),
        call(POP_ATTRIB, &[]),
    ]);
    assert_eq!(d.integer(DRAW_BUFFER_NAME), COLOR1);
    d.submit(&[call(BIND_FRAMEBUFFER, &[FRAMEBUFFER, 0])]);
    assert_eq!(d.integer(DRAW_BUFFER_NAME), FRONT);
    d.submit(&[call(DRAW_BUFFER, &[BACK])]);
    for context in [1, 2, 3] {
        assert_eq!(d.error_for(context), 0, "context {context}");
    }
    drop(d);
    let _ = std::fs::remove_dir_all(directory);
}

#[test]
#[ignore = "requires native host GPU and framebuffer object QEMU producer; diskless acceptance"]
fn qemu_point_sprite_origin_flips_sprites_for_framebuffer_objects() {
    // WineD3D renders every D3D target into a framebuffer object upside down
    // and flips point sprites back with POINT_SPRITE_COORD_ORIGIN (GL 2.0),
    // which the scalar PointParameterf record passes to native GL.
    const POINT_PARAMETER_F: u32 = 1687;
    const POINT_SIZE: u32 = 1700;
    const POINT_SPRITE: u32 = 0x8861;
    const COORD_REPLACE: u32 = 0x8862;
    const COORD_ORIGIN: u32 = 0x8ca0;
    const LOWER_LEFT: u32 = 0x8ca1;
    const UPPER_LEFT: u32 = 0x8ca2;
    const POINTS: u32 = 0;
    let (mut d, _server, directory) = start("fbo-sprite");
    // A 1x2 texture: bottom texel red, top texel green.
    let mut records = texture_2d(5, RGBA8, 1, 2, &[RED, GREEN].concat());
    records.extend(texture_2d(6, RGBA8, 8, 8, &[]));
    records.extend([
        call(BIND_FRAMEBUFFER, &[FRAMEBUFFER, 1]),
        attach_2d(FRAMEBUFFER, COLOR0, TEXTURE_2D, 6),
        call(VIEWPORT, &[0, 0, 8, 8]),
        call(BIND_TEXTURE, &[TEXTURE_2D, 5]),
        call(TEX_ENV_I, &[TEXTURE_ENV, TEXTURE_ENV_MODE, REPLACE_ENV]),
        call(TEX_ENV_I, &[POINT_SPRITE, COORD_REPLACE, 1]),
        call(ENABLE, &[TEXTURE_2D]),
        call(ENABLE, &[POINT_SPRITE]),
        call(POINT_SIZE, &[f(8.)]),
    ]);
    d.submit(&records);
    let sprite = |origin: u32| {
        let mut out = clear(BLACK);
        out.extend([
            call(POINT_PARAMETER_F, &[COORD_ORIGIN, f(origin as f32)]),
            call(BEGIN, &[POINTS]),
            call(VERTEX2F, &[f(0.), f(0.)]),
            call(END, &[]),
        ]);
        out
    };
    // UPPER_LEFT (the default): t runs down the sprite, so its top half shows
    // the bottom texel.
    // UPPER_LEFT (the default) runs t from the top of the sprite, so its top
    // shows texel row 0 (red); LOWER_LEFT flips it. Objects and the window
    // agree (natively both are framebuffer objects), which WineD3D's
    // offscreen LOWER_LEFT / onscreen UPPER_LEFT choice relies on.
    for framebuffer in [1, 0] {
        d.submit(&[call(BIND_FRAMEBUFFER, &[FRAMEBUFFER, framebuffer])]);
        for (origin, top, bottom) in [(UPPER_LEFT, RED, GREEN), (LOWER_LEFT, GREEN, RED)] {
            d.submit(&sprite(origin));
            assert_eq!(d.error(), 0);
            assert_eq!(
                (d.at(3, 6), d.at(3, 1)),
                (top, bottom),
                "framebuffer {framebuffer} origin {origin:#x}"
            );
        }
    }
    drop(d);
    let _ = std::fs::remove_dir_all(directory);
}
