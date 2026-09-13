// SPDX-License-Identifier: GPL-2.0-or-later
//! Optional, bounded pipeline tracing. Hot paths never format or write files.
use std::cell::Cell;
use std::sync::atomic::{AtomicBool, AtomicU64, Ordering};
use std::sync::{Arc, Mutex, OnceLock};
use std::thread::JoinHandle;

use crossbeam_queue::ArrayQueue;
use serde::{Deserialize, Serialize};

const CAPACITY: usize = 65_536;
const MAX_SAMPLES: usize = 2_000_000;
static ACTIVE: AtomicBool = AtomicBool::new(false);
static SESSION: AtomicU64 = AtomicU64::new(0);
static DROPPED: AtomicU64 = AtomicU64::new(0);
static NEXT_INPUT: AtomicU64 = AtomicU64::new(1);
static PRODUCERS: AtomicU64 = AtomicU64::new(0);
static NEXT_THREAD: AtomicU64 = AtomicU64::new(1);
static ANIMATION_GRAY: AtomicU64 = AtomicU64::new(0);
static CAPTURE: Mutex<Option<Recording>> = Mutex::new(None);
thread_local! {
    static THREAD: u64 = NEXT_THREAD.fetch_add(1, Ordering::Relaxed);
    static INPUT: Cell<u64> = const { Cell::new(0) };
}

/// CLOCK_MONOTONIC microseconds, shared with native producers on the same host.
pub fn now_us() -> u64 {
    let mut time = libc::timespec {
        tv_sec: 0,
        tv_nsec: 0,
    };
    // CLOCK_MONOTONIC is available on both supported host platforms.
    unsafe {
        libc::clock_gettime(libc::CLOCK_MONOTONIC, &mut time);
    }
    time.tv_sec as u64 * 1_000_000 + time.tv_nsec as u64 / 1_000
}

#[derive(Clone, Debug, Serialize, Deserialize)]
pub struct Sample {
    pub name: String,
    pub ts: u64,
    pub dur: u64,
    pub id: u64,
    pub value: u64,
    pub tid: u64,
}

// Static names are retained in the queue; allocate Strings only on the collector.
#[derive(Clone, Copy)]
struct Event {
    name: &'static str,
    ts: u64,
    dur: u64,
    id: u64,
    value: u64,
    tid: u64,
}
static EVENTS: OnceLock<ArrayQueue<(u64, Event)>> = OnceLock::new();

#[derive(Clone, Debug, Serialize, Deserialize)]
pub struct Capture {
    pub start_us: u64,
    pub end_us: u64,
    pub dropped: u64,
    pub samples: Vec<Sample>,
}
struct Recording {
    start: u64,
    done: Arc<AtomicBool>,
    thread: JoinHandle<Vec<Sample>>,
}

pub fn enabled() -> bool {
    ACTIVE.load(Ordering::Relaxed)
}
/// Token for asynchronous work; callbacks must not spill into a later capture.
pub fn session_id() -> u64 {
    SESSION.load(Ordering::Acquire)
}
pub fn event_for_session(session: u64, name: &'static str, id: u64, value: u64) {
    record_epoch(session, name, id, value, now_us(), 0);
}
pub fn input_id() -> u64 {
    INPUT.with(Cell::get)
}
pub fn next_input_id() -> u64 {
    NEXT_INPUT.fetch_add(1, Ordering::Relaxed)
}

pub fn with_input<T>(id: u64, f: impl FnOnce() -> T) -> T {
    struct Restore(u64);
    impl Drop for Restore {
        fn drop(&mut self) {
            INPUT.with(|v| v.set(self.0));
        }
    }
    let _restore = Restore(INPUT.with(|v| v.replace(id)));
    f()
}

pub fn event(name: &'static str, id: u64, value: u64) {
    if enabled() {
        record_at(name, id, value, now_us(), 0);
    }
}

/// Decode the benchmark guest's RGB sync + 16 LSB-first black/white blocks.
/// This inspects 19 barcode pixels and one animation pixel, before CRT
/// effects. It never reads the GPU back or claims to measure scanout.
pub fn probe_ack(frame: &crate::FrameLease) -> Option<u16> {
    if !enabled() || !frame.is_valid() || frame.width < 152 || frame.height < 8 {
        return None;
    }
    let bpp = frame.format.bytes_per_pixel();
    let rgb = |block: usize| {
        let offset = 4 * frame.stride as usize + (block * 8 + 4) * bpp;
        let p = &frame.bytes()[offset..offset + bpp];
        match frame.format {
            crate::PixelFormat::Bgra8 | crate::PixelFormat::Bgr8 => [p[2], p[1], p[0]],
            _ => [p[0], p[1], p[2]],
        }
    };
    for color in 0..3 {
        let pixel = rgb(color);
        for (channel, &value) in pixel.iter().enumerate() {
            if (channel == color && value < 200) || (channel != color && value > 55) {
                return None;
            }
        }
    }
    let mut ack = 0u16;
    for bit in 0..16 {
        let pixel = rgb(bit + 3);
        if pixel.iter().all(|&v| v > 200) {
            ack |= 1 << bit;
        } else if !pixel.iter().all(|&v| v < 55) {
            return None;
        }
    }
    event("probe.ack", ack as u64, frame.generation);
    // The probe's F11 animation alternates the body between two gray values.
    // Count actual observed changes, not repeated uploads of the same image.
    if frame.height > 32 {
        let offset = 32 * frame.stride as usize + 4 * bpp;
        let pixel = &frame.bytes()[offset..offset + 3];
        // Guest 15/16-bit color modes quantize the requested GDI colors before
        // the backend expands them to RGB8 (e.g. 96 becomes 88/92/88).
        if let Some(gray) = [96u8, 160]
            .into_iter()
            .find(|&gray| pixel.iter().all(|&v| v.abs_diff(gray) <= 12))
        {
            let epoch = session_id();
            let previous = ANIMATION_GRAY.swap((epoch << 8) | gray as u64, Ordering::Relaxed);
            if previous >> 8 == epoch && previous as u8 != gray {
                event("animation.gray_transition", frame.generation, gray as u64);
            }
        }
    }
    Some(ack)
}
pub fn record_at(name: &'static str, id: u64, value: u64, ts: u64, dur: u64) {
    if !enabled() {
        return;
    }
    record_epoch(session_id(), name, id, value, ts, dur);
}
fn record_epoch(epoch: u64, name: &'static str, id: u64, value: u64, ts: u64, dur: u64) {
    if !enabled() {
        return;
    }
    struct Producer;
    impl Drop for Producer {
        fn drop(&mut self) {
            PRODUCERS.fetch_sub(1, Ordering::Release);
        }
    }
    PRODUCERS.fetch_add(1, Ordering::AcqRel);
    let _producer = Producer;
    if !enabled() || epoch != session_id() {
        return;
    }
    let event = Event {
        name,
        ts,
        dur,
        id,
        value,
        tid: THREAD.with(|id| *id),
    };
    if EVENTS
        .get()
        .is_some_and(|queue| queue.push((epoch, event)).is_err())
    {
        DROPPED.fetch_add(1, Ordering::Relaxed);
    }
}

pub struct Span {
    name: &'static str,
    id: u64,
    start: u64,
    session: u64,
}
pub fn span(name: &'static str, id: u64) -> Span {
    Span {
        name,
        id,
        start: if enabled() { now_us() } else { 0 },
        session: SESSION.load(Ordering::Relaxed),
    }
}
impl Drop for Span {
    fn drop(&mut self) {
        if self.start != 0 && self.session == SESSION.load(Ordering::Relaxed) {
            record_epoch(
                self.session,
                self.name,
                self.id,
                0,
                self.start,
                now_us().saturating_sub(self.start),
            );
        }
    }
}

pub fn start() -> Result<(), &'static str> {
    let mut capture = CAPTURE.lock().map_err(|_| "capture lock poisoned")?;
    if capture.is_some() {
        return Err("a capture is already running");
    }
    let queue = EVENTS.get_or_init(|| ArrayQueue::new(CAPACITY));
    while queue.pop().is_some() {}
    DROPPED.store(0, Ordering::Relaxed);
    let epoch = SESSION.fetch_add(1, Ordering::AcqRel) + 1;
    let start = now_us();
    let done = Arc::new(AtomicBool::new(false));
    let stop = done.clone();
    let thread = std::thread::Builder::new()
        .name("dreamgpu-trace".into())
        .spawn(move || {
            let mut samples = Vec::with_capacity(CAPACITY);
            loop {
                while let Some((session, e)) = queue.pop() {
                    if session != epoch || e.ts < start {
                        continue;
                    }
                    if samples.len() == MAX_SAMPLES {
                        DROPPED.fetch_add(1, Ordering::Relaxed);
                        continue;
                    }
                    samples.push(Sample {
                        name: e.name.into(),
                        ts: e.ts,
                        dur: e.dur,
                        id: e.id,
                        value: e.value,
                        tid: e.tid,
                    });
                }
                if stop.load(Ordering::Acquire)
                    && PRODUCERS.load(Ordering::Acquire) == 0
                    && queue.is_empty()
                {
                    break;
                }
                std::thread::park_timeout(std::time::Duration::from_millis(10));
            }
            samples
        })
        .map_err(|_| "cannot start trace collector")?;
    *capture = Some(Recording {
        start,
        done,
        thread,
    });
    ACTIVE.store(true, Ordering::Release);
    Ok(())
}

pub fn stop() -> Result<Capture, &'static str> {
    let mut capture = CAPTURE.lock().map_err(|_| "capture lock poisoned")?;
    let recording = capture.take().ok_or("no capture running")?;
    ACTIVE.store(false, Ordering::Release);
    let end = now_us();
    recording.done.store(true, Ordering::Release);
    recording.thread.thread().unpark();
    let mut samples = recording
        .thread
        .join()
        .map_err(|_| "trace collector panicked")?;
    samples.retain(|e| e.ts >= recording.start && e.ts <= end);
    Ok(Capture {
        start_us: recording.start,
        end_us: end,
        dropped: DROPPED.load(Ordering::Relaxed),
        samples,
    })
}

#[cfg(test)]
mod tests {
    use super::*;
    static CAPTURE_TEST: Mutex<()> = Mutex::new(());
    #[test]
    fn captures_cross_thread_flows_and_resets_sessions() {
        let _capture = CAPTURE_TEST.lock().unwrap();
        start().unwrap();
        assert!(start().is_err());
        with_input(42, || {
            assert_eq!(input_id(), 42);
            event("input.received", input_id(), 0);
        });
        assert_eq!(input_id(), 0);
        std::thread::spawn(|| event("input.consumed", 42, 0))
            .join()
            .unwrap();
        let capture = stop().unwrap();
        assert_eq!(capture.samples.len(), 2);
        assert_ne!(capture.samples[0].tid, capture.samples[1].tid);
        assert!(capture.samples.iter().all(|s| s.id == 42));
        let old_session = session_id();
        start().unwrap();
        event_for_session(old_session, "late.gpu.callback", 42, 0);
        assert!(stop().unwrap().samples.is_empty());
    }

    #[test]
    fn padded_probe_frames_count_visual_changes_within_each_capture() {
        let _capture = CAPTURE_TEST.lock().unwrap();
        let frame = |gray: [u8; 3], generation| {
            let stride = 160 * 4 + 32;
            let mut pixels = vec![0; stride * 40];
            for color in 0..3 {
                pixels[4 * stride + (color * 8 + 4) * 4 + 2 - color] = 255;
            }
            pixels[32 * stride + 16..32 * stride + 19].copy_from_slice(&gray);
            crate::FrameLease {
                pixels: Arc::new(pixels),
                width: 160,
                height: 40,
                stride: stride as u32,
                format: crate::PixelFormat::Bgra8,
                generation,
                damage: None,
            }
        };
        start().unwrap();
        for (gray, generation) in [([96; 3], 1), ([160; 3], 2), ([160; 3], 3)] {
            assert_eq!(probe_ack(&frame(gray, generation)), Some(0));
        }
        let capture = stop().unwrap();
        let changes: Vec<_> = capture
            .samples
            .iter()
            .filter(|s| s.name == "animation.gray_transition")
            .collect();
        assert_eq!(changes.len(), 1);
        assert_eq!((changes[0].id, changes[0].value), (2, 160));
        start().unwrap();
        assert_eq!(probe_ack(&frame([96; 3], 4)), Some(0));
        assert!(stop()
            .unwrap()
            .samples
            .iter()
            .all(|s| s.name != "animation.gray_transition"));
        start().unwrap();
        for (gray, generation) in [([88, 92, 88], 5), ([96; 3], 6), ([152, 156, 152], 7)] {
            assert_eq!(probe_ack(&frame(gray, generation)), Some(0));
        }
        let capture = stop().unwrap();
        let changes: Vec<_> = capture
            .samples
            .iter()
            .filter(|s| s.name == "animation.gray_transition")
            .collect();
        assert_eq!(
            changes.len(),
            1,
            "color quantization must not create false transitions"
        );
        assert_eq!((changes[0].id, changes[0].value), (7, 160));
    }
}
