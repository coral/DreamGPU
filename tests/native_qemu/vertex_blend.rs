//! Direct3D fixed-function vertex blending as Wine 0052 draws it, through the
//! device. DreamGPU offers no GL_ARB_vertex_blend (neither Mesa nor a GeForce
//! FX's OpenGL had it); WineD3D blends each vertex's position and normal into
//! world space on the CPU (the weighted sum of WORLDi transforms, normals
//! through each WORLDi inverse transpose, the last weight 1 minus the others)
//! and draws them under the view matrix alone, with lights and clip planes
//! specified under that view matrix as Wine always specifies them. These are
//! exactly the frontend's records for such a draw: MatrixMode, PushMatrix,
//! LoadMatrixf(view), a client-array DrawArrays (FLOAT 4 positions, FLOAT 3
//! normals) or the buffer draw without buffer slots for indices, PopMatrix.
//! The pixels must be what ARB_vertex_blend defines in eye space,
//! sum_i w_i (view WORLDi) v, computed here independently in double
//! precision: per-vertex directional lighting of the blended, normalized
//! normal, linear fog of the blended eye depth (FRAGMENT_DEPTH) and a user
//! clip plane of world space, each to within 3 of 255. Four quads use
//! different weights of four distinct affine worlds (rotations, non-uniform
//! scales, depths) under a rotated, translated view. A quad of WORLD alone
//! draws the same pixels as Wine's unblended draw (modelview view * WORLD,
//! model-space arrays), and the modelview survives each blended draw.
use super::fragment_program::{Device, start};
use super::textures::{call, data_call, words};

const DRAW_ARRAYS: u32 = 0x1d5;
const BUFFER_DRAW: u32 = 0x1f5; // DrawRangeElements
const CLEAR_COLOR: u32 = 0x0a3;
const CLEAR: u32 = 0x09a;
const ENABLE: u32 = 0x209;
const DISABLE: u32 = 0x1c5;
const MATRIX_MODE: u32 = 0x549;
const LOAD_MATRIX: u32 = 0x503;
const PUSH_MATRIX: u32 = 0x77a;
const POP_MATRIX: u32 = 0x6b3;
const LIGHT_FV: u32 = 0x4ea;
const LIGHT_MODEL_FV: u32 = 0x4ee;
const MATERIAL_FV: u32 = 0x537;
const FOG_F: u32 = 0x25d;
const FOG_FV: u32 = 0x25f;
const CLIP_PLANE: u32 = 0x0be;
const GET_FLOAT: u32 = 0x305;
const READ_PIXELS: u32 = 0x7a4;

const MODELVIEW: u32 = 0x1700;
const MODELVIEW_MATRIX: u32 = 0x0ba6;
const LIGHTING: u32 = 0x0b50;
const LIGHT0: u32 = 0x4000;
const NORMALIZE: u32 = 0x0ba1;
const FOG: u32 = 0x0b60;
const CLIP_PLANE0: u32 = 0x3000;
const AMBIENT: u32 = 0x1200;
const DIFFUSE: u32 = 0x1201;
const SPECULAR: u32 = 0x1202;
const POSITION: u32 = 0x1203;
const LIGHT_MODEL_AMBIENT: u32 = 0x0b53;
const FRONT_AND_BACK: u32 = 0x0408;
const FOG_MODE: u32 = 0x0b65;
const FOG_START: u32 = 0x0b63;
const FOG_END: u32 = 0x0b64;
const FOG_COLOR: u32 = 0x0b66;
const LINEAR: u32 = 0x2601;
const FLOAT: u32 = 0x1406;
const UNSIGNED_SHORT: u32 = 0x1403;
const ARRAY_RAW: u32 = 0x8000_0000;
const TRIANGLES: u32 = 4;
const QUADS: u32 = 7;

const SIZE: u32 = 64;
const CONTEXT: u32 = 2;
/// The clear colour, distinct from any lit and fogged colour here.
const CLEARED: [f64; 3] = [0.25, 0.75, 0.0];
const FOG_BLUE: [f64; 3] = [0.0, 0.0, 1.0];
/// The world direction towards the light (a GL directional position).
const LIGHT: [f64; 3] = [0.8, 0.3, 0.52];

/// Direct3D row-vector matrices: v' = v * m, m[row][column]; their memory
/// order is GL's column-major order, as Wine loads them.
type Matrix = [[f64; 4]; 4];

fn identity() -> Matrix {
    let mut m = [[0.; 4]; 4];
    for (i, row) in m.iter_mut().enumerate() {
        row[i] = 1.;
    }
    m
}
fn multiply(a: &Matrix, b: &Matrix) -> Matrix {
    let mut out = [[0.; 4]; 4];
    for i in 0..4 {
        for j in 0..4 {
            out[i][j] = (0..4).map(|k| a[i][k] * b[k][j]).sum();
        }
    }
    out
}
fn translation(x: f64, y: f64, z: f64) -> Matrix {
    let mut m = identity();
    m[3] = [x, y, z, 1.];
    m
}
/// Rotation about z by `degrees`, then scale x and y.
fn rotate_scale(degrees: f64, sx: f64, sy: f64) -> Matrix {
    let (s, c) = degrees.to_radians().sin_cos();
    let mut m = identity();
    m[0] = [c * sx, s * sy, 0., 0.];
    m[1] = [-s * sx, c * sy, 0., 0.];
    m
}
fn transform(v: [f64; 4], m: &Matrix) -> [f64; 4] {
    let mut out = [0.; 4];
    for (j, o) in out.iter_mut().enumerate() {
        *o = (0..4).map(|k| v[k] * m[k][j]).sum();
    }
    out
}
/// The inverse of m's upper 3x3.
fn inverse3(m: &Matrix) -> [[f64; 3]; 3] {
    let c =
        |r0: usize, r1: usize, c0: usize, c1: usize| m[r0][c0] * m[r1][c1] - m[r0][c1] * m[r1][c0];
    let det = m[0][0] * c(1, 2, 1, 2) - m[0][1] * c(1, 2, 0, 2) + m[0][2] * c(1, 2, 0, 1);
    assert!(det.abs() > 1e-6);
    [
        [
            c(1, 2, 1, 2) / det,
            -c(0, 2, 1, 2) / det,
            c(0, 1, 1, 2) / det,
        ],
        [
            -c(1, 2, 0, 2) / det,
            c(0, 2, 0, 2) / det,
            -c(0, 1, 0, 2) / det,
        ],
        [
            c(1, 2, 0, 1) / det,
            -c(0, 2, 0, 1) / det,
            c(0, 1, 0, 1) / det,
        ],
    ]
}
/// A row-vector normal through m's inverse transpose: component j is
/// sum_k inverse[j][k] n[k].
fn normal_through(n: [f64; 3], m: &Matrix) -> [f64; 3] {
    let inverse = inverse3(m);
    [0, 1, 2].map(|j| (0..3).map(|k| inverse[j][k] * n[k]).sum())
}
fn normalize(v: [f64; 3]) -> [f64; 3] {
    let length = v.iter().map(|x| x * x).sum::<f64>().sqrt();
    v.map(|x| x / length)
}
fn gl(m: &Matrix) -> Vec<u32> {
    m.iter().flatten().map(|&v| (v as f32).to_bits()).collect()
}
fn f(v: f32) -> u32 {
    v.to_bits()
}
fn vector(function: u32, args: &[u32], values: &[f32]) -> (u32, Vec<u32>) {
    data_call(
        function,
        args,
        &values
            .iter()
            .flat_map(|v| v.to_le_bytes())
            .collect::<Vec<_>>(),
    )
}

/// The view: rotation about z by 30 degrees, then a translation.
fn view() -> Matrix {
    multiply(&rotate_scale(30., 1., 1.), &translation(0.05, -0.05, -0.2))
}

/// One quad of Wine's draw: its model corners around `centre`, the normal
/// (1, 0, 0) at every corner, three weights and four world matrices that
/// rotate and scale about the centre at their own depths.
struct Quad {
    centre: [f64; 2],
    weights: [f64; 3],
    worlds: [Matrix; 4],
}
const HALF: f64 = 0.3;
fn quad(centre: [f64; 2], weights: [f64; 3]) -> Quad {
    let shape = [
        (0., 1., 1.),
        (40., 0.7, 1.1),
        (-30., 1.1, 0.8),
        (70., 0.9, 0.75),
    ];
    let depth = [-0.1, -0.5, -0.3, -0.7];
    let worlds = [0, 1, 2, 3].map(|i| {
        let (degrees, sx, sy) = shape[i];
        let about = multiply(
            &translation(-centre[0], -centre[1], 0.),
            &rotate_scale(degrees, sx, sy),
        );
        multiply(&about, &translation(centre[0], centre[1], depth[i]))
    });
    Quad {
        centre,
        weights,
        worlds,
    }
}
impl Quad {
    fn corners(&self) -> [[f64; 4]; 4] {
        let [x, y] = self.centre;
        [
            [x - HALF, y - HALF, 0., 1.],
            [x + HALF, y - HALF, 0., 1.],
            [x + HALF, y + HALF, 0., 1.],
            [x - HALF, y + HALF, 0., 1.],
        ]
    }
    /// The four weights, the last 1 minus the others (in f32, as Wine).
    fn all_weights(&self) -> [f32; 4] {
        let w = self.weights.map(|w| w as f32);
        [w[0], w[1], w[2], 1. - (w[0] + w[1] + w[2])]
    }
    /// Wine 0052's arrays: blended world positions (FLOAT 4) and normals
    /// (FLOAT 3), in f32 as dg_vertex_blend_vertex computes them.
    fn blended(&self) -> (Vec<f32>, Vec<f32>) {
        let w = self.all_weights();
        let (mut positions, mut normals) = (Vec::new(), Vec::new());
        for v in self.corners() {
            let v = v.map(|x| x as f32);
            let mut p = [0f32; 4];
            let mut n = [0f32; 3];
            for (i, world) in self.worlds.iter().enumerate() {
                let m = world.map(|row| row.map(|x| x as f32));
                for j in 0..4 {
                    p[j] +=
                        w[i] * (v[0] * m[0][j] + v[1] * m[1][j] + v[2] * m[2][j] + v[3] * m[3][j]);
                }
                let inverse = inverse3(world).map(|row| row.map(|x| x as f32));
                for j in 0..3 {
                    n[j] += w[i] * inverse[j][0];
                }
            }
            positions.extend(p);
            normals.extend(n);
        }
        (positions, normals)
    }
    /// ARB_vertex_blend's definition in eye space: the centre's eye position
    /// sum_i w_i v (WORLDi view) and the eye normal sum_i w_i n (WORLDi
    /// view)^-T, in double precision.
    fn eye(&self, view: &Matrix) -> ([f64; 4], [f64; 3]) {
        let w = self.all_weights().map(f64::from);
        let centre = [self.centre[0], self.centre[1], 0., 1.];
        let (mut p, mut n) = ([0.; 4], [0.; 3]);
        for (i, world) in self.worlds.iter().enumerate() {
            let m = multiply(world, view);
            let t = transform(centre, &m);
            let nt = normal_through([1., 0., 0.], &m);
            for j in 0..4 {
                p[j] += w[i] * t[j];
            }
            for j in 0..3 {
                n[j] += w[i] * nt[j];
            }
        }
        (p, n)
    }
    /// The centre in world space (the clip plane's side).
    fn world_centre(&self) -> [f64; 4] {
        let w = self.all_weights().map(f64::from);
        let centre = [self.centre[0], self.centre[1], 0., 1.];
        let mut p = [0.; 4];
        for (i, world) in self.worlds.iter().enumerate() {
            let t = transform(centre, world);
            for j in 0..4 {
                p[j] += w[i] * t[j];
            }
        }
        p
    }
}

/// The 7-slot DrawArrays record of client positions (slot 0, FLOAT 4) and
/// normals (slot 2, FLOAT 3), as the frontend packs them.
fn draw_arrays(mode: u32, positions: &[f32], normals: &[f32]) -> (u32, Vec<u32>) {
    let count = (positions.len() / 4) as u32;
    let mut descriptors = [0u32; 7];
    descriptors[0] = FLOAT | 4 << 16;
    descriptors[2] = FLOAT | 3 << 16;
    let mut bytes: Vec<u8> = descriptors.iter().flat_map(|d| d.to_le_bytes()).collect();
    bytes.extend(positions.iter().flat_map(|v| v.to_le_bytes()));
    bytes.extend(normals.iter().flat_map(|v| v.to_le_bytes()));
    data_call(DRAW_ARRAYS, &[mode, 0, count, ARRAY_RAW | 1 | 4], &bytes)
}
/// The buffer draw without buffer slots (an indexed client draw): the same
/// sections in 32 slots, then UNSIGNED_SHORT indices.
fn ranged(mode: u32, positions: &[f32], normals: &[f32], indices: &[u16]) -> (u32, Vec<u32>) {
    let vertices = (positions.len() / 4) as u32;
    let mut descriptors = [0u32; 32];
    descriptors[0] = FLOAT | 4 << 16;
    descriptors[2] = FLOAT | 3 << 16;
    let mut bytes: Vec<u8> = descriptors.iter().flat_map(|d| d.to_le_bytes()).collect();
    bytes.extend(positions.iter().flat_map(|v| v.to_le_bytes()));
    bytes.extend(normals.iter().flat_map(|v| v.to_le_bytes()));
    bytes.extend(indices.iter().flat_map(|i| i.to_le_bytes()));
    bytes.resize(bytes.len().div_ceil(4) * 4, 0);
    data_call(
        BUFFER_DRAW,
        &[
            mode,
            0,
            indices.len() as u32,
            ARRAY_RAW | 1 | 4,
            0,
            0,
            vertices,
            UNSIGNED_SHORT,
            0,
            0,
        ],
        &bytes,
    )
}
/// Wine 0052's blended draw: the view as modelview around the arrays.
fn blended_draw(view: &Matrix, draw: (u32, Vec<u32>)) -> Vec<(u32, Vec<u32>)> {
    vec![
        call(MATRIX_MODE, &[MODELVIEW]),
        call(PUSH_MATRIX, &[]),
        call(LOAD_MATRIX, &gl(view)),
        draw,
        call(POP_MATRIX, &[]),
    ]
}

/// The window pixel of an eye position (identity projection).
fn window(eye: [f64; 4]) -> (u32, u32) {
    let half = f64::from(SIZE) / 2.;
    (
        ((eye[0] / eye[3] + 1.) * half) as u32,
        ((eye[1] / eye[3] + 1.) * half) as u32,
    )
}
/// The lit, fogged colour of a quad: white diffuse light and material,
/// no ambient, NORMALIZE, fog linear from 0 to 2 towards blue.
fn shade(view: &Matrix, eye: ([f64; 4], [f64; 3])) -> [f64; 3] {
    let light = normalize(
        transform([LIGHT[0], LIGHT[1], LIGHT[2], 0.], view)[..3]
            .try_into()
            .unwrap(),
    );
    let normal = normalize(eye.1);
    let lit = (0..3)
        .map(|k| normal[k] * light[k])
        .sum::<f64>()
        .clamp(0., 1.);
    let fog = (1. - (-eye.0[2] / eye.0[3]) / 2.).clamp(0., 1.);
    [0, 1, 2].map(|c| fog * lit + (1. - fog) * FOG_BLUE[c])
}
fn pixel(d: &mut Device, x: u32, y: u32) -> [u8; 4] {
    let (_, result) = d.query_for(CONTEXT, READ_PIXELS, &[x, y, 1 | (1 << 16)]);
    result[..4].try_into().unwrap()
}
fn close(actual: [u8; 4], expected: [f64; 3], label: &str) {
    for c in 0..3 {
        let e = (expected[c] * 255.).round() as i32;
        assert!(
            (i32::from(actual[c]) - e).abs() <= 3,
            "{label}: {actual:?}, expected {:?}",
            expected.map(|v| (v * 255.).round() as i32)
        );
    }
}

/// Clear, the lighting/fog state with the light specified under the view,
/// optionally the world clip plane x >= 0 (also under the view).
fn scene(view: &Matrix, clip: bool) -> Vec<(u32, Vec<u32>)> {
    let mut records = vec![
        call(CLEAR_COLOR, &[f(0.25), f(0.75), f(0.), f(1.)]),
        call(CLEAR, &[0x4000]),
        vector(LIGHT_MODEL_FV, &[LIGHT_MODEL_AMBIENT], &[0., 0., 0., 1.]),
        vector(LIGHT_FV, &[LIGHT0, AMBIENT], &[0., 0., 0., 1.]),
        vector(LIGHT_FV, &[LIGHT0, DIFFUSE], &[1., 1., 1., 1.]),
        vector(LIGHT_FV, &[LIGHT0, SPECULAR], &[0., 0., 0., 1.]),
        vector(MATERIAL_FV, &[FRONT_AND_BACK, DIFFUSE], &[1., 1., 1., 1.]),
        vector(MATERIAL_FV, &[FRONT_AND_BACK, AMBIENT], &[0., 0., 0., 1.]),
        call(MATRIX_MODE, &[MODELVIEW]),
        call(LOAD_MATRIX, &gl(view)),
        vector(
            LIGHT_FV,
            &[LIGHT0, POSITION],
            &[LIGHT[0] as f32, LIGHT[1] as f32, LIGHT[2] as f32, 0.],
        ),
        data_call(
            CLIP_PLANE,
            &[CLIP_PLANE0],
            &[1f64, 0., 0., 0.]
                .iter()
                .flat_map(|v| v.to_le_bytes())
                .collect::<Vec<_>>(),
        ),
        call(ENABLE, &[LIGHTING]),
        call(ENABLE, &[LIGHT0]),
        call(ENABLE, &[NORMALIZE]),
        call(ENABLE, &[FOG]),
        call(FOG_F, &[FOG_MODE, f(LINEAR as f32)]),
        call(FOG_F, &[FOG_START, f(0.)]),
        call(FOG_F, &[FOG_END, f(2.)]),
        vector(FOG_FV, &[FOG_COLOR], &[0., 0., 1., 1.]),
    ];
    records.push(call(if clip { ENABLE } else { DISABLE }, &[CLIP_PLANE0]));
    records
}

#[test]
#[ignore = "requires native host GPU and the QEMU producer; diskless acceptance"]
fn qemu_vertex_blend_world_space_arrays_under_the_view_match_eye_space_blending() {
    let (mut d, _server, _directory) = start("blend");
    d.submit_for(CONTEXT, &[(1, vec![0]), (3, vec![SIZE, SIZE]), (5, vec![])]);
    assert_eq!(d.error_for(CONTEXT), 0);
    let view = view();
    let quads = [
        quad([-0.5, 0.5], [1., 0., 0.]),        // WORLD alone
        quad([0.5, 0.5], [0., 1., 0.]),         // WORLD1
        quad([-0.5, -0.5], [0.25, 0.25, 0.25]), // all four
        quad([0.5, -0.5], [0., 0., 0.]),        // the implicit WORLD3
    ];

    for clip in [false, true] {
        let mut records = scene(&view, clip);
        for (i, q) in quads.iter().enumerate() {
            let (positions, normals) = q.blended();
            let draw = if i == 3 {
                ranged(TRIANGLES, &positions, &normals, &[0, 1, 2, 0, 2, 3])
            } else {
                draw_arrays(QUADS, &positions, &normals)
            };
            records.extend(blended_draw(&view, draw));
        }
        d.submit_for(CONTEXT, &records);
        assert_eq!(d.error_for(CONTEXT), 0);
        // The modelview survives the blended draws.
        let matrix = words(&d.query_for(CONTEXT, GET_FLOAT, &[MODELVIEW_MATRIX]).1);
        assert_eq!(matrix, gl(&view), "modelview restored");
        for (i, q) in quads.iter().enumerate() {
            let eye = q.eye(&view);
            let (x, y) = window(eye.0);
            let clipped = clip && q.world_centre()[0] < 0.;
            let expected = if clipped { CLEARED } else { shade(&view, eye) };
            let label = format!("quad {i} (clip plane {clip}) at {x},{y}");
            close(pixel(&mut d, x, y), expected, &label);
        }
    }

    // WORLD alone: Wine's unblended draw (modelview view * WORLD, model
    // arrays) gives the blended draw's pixel.
    let q = &quads[0];
    let (blended_positions, blended_normals) = q.blended();
    let model: Vec<f32> = q.corners().iter().flatten().map(|&v| v as f32).collect();
    let normals = [1f32, 0., 0.].repeat(4);
    let (x, y) = window(q.eye(&view).0);
    let mut records = scene(&view, false);
    records.extend(blended_draw(
        &view,
        draw_arrays(QUADS, &blended_positions, &blended_normals),
    ));
    d.submit_for(CONTEXT, &records);
    let blended = pixel(&mut d, x, y);
    let mut records = scene(&view, false);
    records.extend([
        call(LOAD_MATRIX, &gl(&multiply(&q.worlds[0], &view))),
        draw_arrays(QUADS, &model, &normals),
        call(LOAD_MATRIX, &gl(&view)),
    ]);
    d.submit_for(CONTEXT, &records);
    let unblended = pixel(&mut d, x, y);
    for c in 0..3 {
        assert!(
            (i32::from(blended[c]) - i32::from(unblended[c])).abs() <= 1,
            "WORLD alone: blended {blended:?}, unblended {unblended:?}"
        );
    }
    assert_eq!(d.error_for(CONTEXT), 0);
}
