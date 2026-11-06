//! Queued GL transport through the device: negotiation, many small and large
//! pipelined batches published without waiting, a later synchronous query, a
//! present whose identity arrives in the queue page, faults inside pipelined
//! batches (validation and execution) that skip only their own fault domain,
//! interrupts only while a wait is armed, a legacy SUBMIT refused while the
//! queue owns ordering, and an engine reset with batches in flight followed by
//! re-enabling and by the unchanged register protocol. MMIO accesses are
//! counted on the driver side: one doorbell write per pipelined batch.
use super::textures::{call, words};
use super::*;
use std::fmt::Write as _;
use std::sync::{Arc, mpsc};

const PAGE: u64 = 0x300000;
const RESULT: u64 = 0x380000;
const BUFFERS: u64 = 0x400000;
const BUFFER_BYTES: u64 = 0x40000;
const BUFFER_COUNT: u32 = 8;

const QUEUE_VERSION: u32 = 0x1180;
const QUEUE_ADDR_LO: u32 = 0x1184;
const QUEUE_ADDR_HI: u32 = 0x1188;
const QUEUE_CONTROL: u32 = 0x118c;
const QUEUE_DOORBELL: u32 = 0x1190;
const STATE: u64 = 0;
const GENERATION: u64 = 4;
const CONSUMED: u64 = 8;
const COMPLETED: u64 = 12;
const PRESENT_SLOT: u64 = 16;
const PRESENT_FRAME_LO: u64 = 28;
const PRODUCED: u64 = 64;
const WAIT: u64 = 68;
const WAIT_COMPLETED: u64 = 72;
const RING: u64 = 256;
const ENTRY: u64 = 64;
const STATUS: u64 = 48;
const ERROR: u64 = 52;
const RESULT_BYTES: u64 = 56;
const RESULT_TYPE: u64 = 60;

const IRQ_STATUS: u32 = 0x1034;
const IRQ_GL: u32 = 2;
const DONE: u32 = 2;
const FAILED: u32 = 6;
const ERROR_UNSUPPORTED: u32 = 3;
const ERROR_CONTEXT: u32 = 4;
const ERROR_BATCH: u32 = 1;
const ERROR_SKIPPED: u32 = 12;

const GET_ERROR: u32 = 764;
const CLEAR_COLOR: u32 = 0xa3;
const CLEAR: u32 = 0x9a;
const BEGIN: u32 = 0x01a;
const END: u32 = 0x216;
const VERTEX3F: u32 = 0x9ea;

struct Driver {
    qemu: Qemu,
    generation: u32,
    produced: u32,
    sequence: u32,
    mmio_reads: u32,
    mmio_writes: u32,
}
impl Driver {
    fn mmio_read(&mut self, register: u32) -> u32 {
        self.mmio_reads += 1;
        self.qemu.read(register)
    }
    fn mmio_write(&mut self, register: u32, value: u32) {
        self.mmio_writes += 1;
        self.qemu.write(register, value);
    }
    fn ram(&mut self, address: u64) -> u32 {
        let reply = self.qemu.command(&format!("readl {address:#x}"));
        u32::from_str_radix(
            reply
                .split_whitespace()
                .nth(1)
                .unwrap()
                .trim_start_matches("0x"),
            16,
        )
        .unwrap()
    }
    fn set(&mut self, address: u64, value: u32) {
        self.qemu
            .command(&format!("writel {address:#x} {value:#x}"));
    }
    fn bytes(&mut self, address: u64, length: usize) -> Vec<u8> {
        let reply = self.qemu.command(&format!("read {address:#x} {length}"));
        let hex = reply
            .split_whitespace()
            .nth(1)
            .unwrap()
            .trim_start_matches("0x");
        (0..length)
            .map(|i| u8::from_str_radix(&hex[i * 2..i * 2 + 2], 16).unwrap())
            .collect()
    }
    fn enable(&mut self) {
        self.generation = self.mmio_read(0x103c);
        assert_eq!(self.mmio_read(QUEUE_VERSION), 1);
        self.qemu.command(&format!("memset {PAGE:#x} 4096 0"));
        self.produced = 0;
        self.mmio_write(QUEUE_ADDR_LO, PAGE as u32);
        self.mmio_write(QUEUE_ADDR_HI, 0);
        self.mmio_write(QUEUE_CONTROL, self.generation);
        assert_eq!(self.mmio_read(QUEUE_CONTROL), 1);
        assert_eq!(self.ram(PAGE + STATE), 1);
        assert_eq!(self.ram(PAGE + GENERATION), self.generation);
    }
    /// Records of `stream`'s context (context 1 outside any stream).
    fn encode(&self, client: u32, stream: u32, records: &[(u32, Vec<u32>)]) -> Vec<u8> {
        let context = stream.max(1);
        let mut bytes = Vec::new();
        for (opcode, arguments) in records {
            let header = [
                *opcode,
                32 + arguments.len() as u32 * 4,
                client,
                context,
                context,
                0,
                0,
                self.generation,
            ];
            for word in header.iter().chain(arguments) {
                bytes.extend_from_slice(&word.to_le_bytes());
            }
        }
        bytes
    }
    fn completed(&mut self) -> u32 {
        self.ram(PAGE + COMPLETED)
    }
    /// Poll the page (never MMIO) until COMPLETED reaches `target`.
    fn await_completed(&mut self, target: u32) {
        let deadline = Instant::now() + Duration::from_secs(10);
        while (self.completed().wrapping_sub(target) as i32) < 0 {
            assert!(Instant::now() < deadline, "entry {target} did not complete");
            std::thread::sleep(Duration::from_millis(2));
        }
    }
    /// Publish one entry without waiting (except for a free entry slot).
    fn publish(
        &mut self,
        client: u32,
        stream: u32,
        records: &[(u32, Vec<u32>)],
        result: u32,
    ) -> u32 {
        let index = self.produced;
        if index.wrapping_sub(self.completed()) >= 16 {
            self.await_completed(index.wrapping_sub(15));
        }
        // Buffer index % 8 is free once entry index - 8 was consumed.
        let deadline = Instant::now() + Duration::from_secs(10);
        while (self
            .ram(PAGE + CONSUMED)
            .wrapping_sub(index.wrapping_sub(BUFFER_COUNT - 1)) as i32)
            < 0
        {
            assert!(
                Instant::now() < deadline,
                "buffer for {index} never consumed"
            );
            std::thread::sleep(Duration::from_millis(1));
        }
        let buffer = BUFFERS + u64::from(index % BUFFER_COUNT) * BUFFER_BYTES;
        let bytes = self.encode(client, stream, records);
        assert!(bytes.len() as u64 <= BUFFER_BYTES);
        let mut hex = String::from("0x");
        for byte in &bytes {
            write!(hex, "{byte:02x}").unwrap();
        }
        self.qemu
            .command(&format!("write {buffer:#x} {} {hex}", bytes.len()));
        self.sequence += 1;
        let entry = PAGE + RING + u64::from(index % 16) * ENTRY;
        let generation = self.generation;
        let sequence = self.sequence;
        for (offset, value) in [
            (0, buffer as u32),
            (4, 0),
            (8, bytes.len() as u32),
            (12, sequence),
            (16, generation),
            (20, client),
            (24, stream),
            (28, if result != 0 { RESULT as u32 } else { 0 }),
            (32, 0),
            (36, result),
            (40, 0),
            (44, 0),
            (STATUS as u32, 0),
            (ERROR as u32, 0),
        ] {
            self.set(entry + u64::from(offset), value);
        }
        self.produced = index.wrapping_add(1);
        let produced = self.produced;
        self.set(PAGE + PRODUCED, produced);
        self.mmio_write(QUEUE_DOORBELL, produced);
        index
    }
    fn outcome(&mut self, index: u32) -> (u32, u32) {
        let entry = PAGE + RING + u64::from(index % 16) * ENTRY;
        (self.ram(entry + STATUS), self.ram(entry + ERROR))
    }
    fn wait(&mut self, index: u32) -> (u32, u32) {
        self.await_completed(index.wrapping_add(1));
        self.outcome(index)
    }
}

fn start(label: &str) -> (Driver, GpuServer, mpsc::Receiver<()>, PathBuf) {
    let (_device, _queue, host) = native_gpu();
    let directory = std::env::temp_dir().join(format!(
        "dg-queue-{label}-{}",
        uuid::Uuid::new_v4().simple()
    ));
    std::fs::create_dir(&directory).unwrap();
    let socket = directory.join("g.sock");
    let server = GpuServer::new(&socket, host).unwrap();
    let (wake, ready) = mpsc::sync_channel(64);
    server.set_callback(Arc::new(move |_| {
        let _ = wake.try_send(());
    }));
    let qemu = Qemu::start(&directory, &socket);
    let mut driver = Driver {
        qemu,
        generation: 0,
        produced: 0,
        sequence: 0,
        mmio_reads: 0,
        mmio_writes: 0,
    };
    driver.enable();
    (driver, server, ready, directory)
}

fn f(v: f32) -> u32 {
    v.to_bits()
}

#[test]
#[ignore = "requires native host GPU and queued QEMU producer; diskless acceptance"]
fn qemu_queue_pipelines_batches_queries_presents_and_contains_faults() {
    let (mut d, server, ready, directory) = start("pipeline");
    // Client 1, context and drawable 1 (stream 1). Streams 2 and 3 below name
    // contexts that do not exist, which only their own batches notice.
    let setup = d.publish(1, 0, &[(1, vec![0]), (3, vec![8, 8]), (5, vec![])], 0);
    assert_eq!(d.wait(setup), (DONE, 0));

    // Many small batches, then large ones, all published back to back: one
    // doorbell write and no MMIO read per batch, however far the host lags.
    let (reads, writes) = (d.mmio_reads, d.mmio_writes);
    let first = d.produced;
    for i in 0..48 {
        let shade = i as f32 / 48.0;
        d.publish(
            1,
            1,
            &[
                call(CLEAR_COLOR, &[f(shade), 0, 0, f(1.)]),
                call(CLEAR, &[0x4000]),
            ],
            0,
        );
    }
    for _ in 0..3 {
        let mut records = vec![call(BEGIN, &[0])];
        for v in 0..4000 {
            let x = (v % 64) as f32 / 32.0 - 1.0;
            records.push(call(VERTEX3F, &[f(x), f(0.), f(0.)]));
        }
        records.push(call(END, &[]));
        d.publish(1, 1, &records, 0);
    }
    let batches = d.produced - first;
    assert_eq!(d.mmio_writes - writes, batches, "one doorbell per batch");
    assert_eq!(d.mmio_reads - reads, 0, "no MMIO reads while pipelining");
    println!(
        "queued transport: {batches} batches, {:.2} MMIO accesses per batch",
        f64::from(d.mmio_writes - writes + d.mmio_reads - reads) / f64::from(batches)
    );

    // A synchronous query after the pipelined work observes all of it: no
    // GL error, and its result arrives in the entry and its result page.
    d.qemu.command(&format!("memset {RESULT:#x} 64 0xcc"));
    let query = d.publish(1, 1, &[(11, vec![GET_ERROR, 0, 0, 0])], 64);
    assert_eq!(d.wait(query), (DONE, 0));
    for index in first..query {
        assert_eq!(d.outcome(index), (DONE, 0), "entry {index}");
    }
    let entry = PAGE + RING + u64::from(query % 16) * ENTRY;
    assert_eq!(d.ram(entry + RESULT_BYTES), 4);
    assert_eq!(d.ram(entry + RESULT_TYPE), 2);
    let result = d.bytes(RESULT, 8);
    assert_eq!(words(&result[..4])[0], 0);
    assert_eq!(&result[4..], &[0xcc; 4]);

    // PRESENT through the queue: its export identity is in the page.
    let present = d.publish(1, 1, &[call(CLEAR, &[0x4000]), (7, vec![])], 0);
    assert_eq!(d.wait(present), (DONE, 0));
    ready.recv_timeout(Duration::from_secs(10)).unwrap();
    let frames = server.take_drawables();
    assert_eq!(frames.len(), 1);
    drop(frames);
    // The page mirrors the PRESENT_* registers without an MMIO read.
    let slot = d.mmio_read(0x1140);
    let frame = d.mmio_read(0x114c);
    assert_ne!(frame, 0);
    assert_eq!(
        (d.ram(PAGE + PRESENT_SLOT), d.ram(PAGE + PRESENT_FRAME_LO)),
        (slot, frame)
    );

    // Faults inside pipelined work. Stream 2: rejected at validation (unknown
    // function), then a valid batch of the same stream is skipped. Stream 3:
    // fails at execution (no such context), its follower is skipped too.
    // Stream 1 work between and after still executes in order.
    let rejected = d.publish(1, 2, &[call(0xffff, &[])], 0);
    let skipped = d.publish(1, 2, &[(5, vec![])], 0);
    let mid = d.publish(1, 1, &[call(CLEAR, &[0x4000])], 0);
    let failed = d.publish(1, 3, &[(5, vec![])], 0);
    let skipped2 = d.publish(1, 3, &[call(CLEAR, &[0x4000])], 0);
    let last = d.publish(1, 1, &[(11, vec![GET_ERROR, 0, 0, 0])], 64);
    assert_eq!(d.wait(last), (DONE, 0));
    assert_eq!(d.outcome(rejected), (FAILED, ERROR_UNSUPPORTED));
    assert_eq!(d.outcome(skipped), (FAILED, ERROR_SKIPPED));
    assert_eq!(d.outcome(mid), (DONE, 0));
    assert_eq!(d.outcome(failed), (FAILED, ERROR_CONTEXT));
    assert_eq!(d.outcome(skipped2), (FAILED, ERROR_SKIPPED));

    // Interrupts: none while unarmed, one once an armed wait holds.
    d.mmio_write(IRQ_STATUS, IRQ_GL);
    let quiet = d.publish(1, 1, &[call(CLEAR, &[0x4000])], 0);
    d.wait(quiet);
    assert_eq!(
        d.mmio_read(IRQ_STATUS) & IRQ_GL,
        0,
        "unarmed completion raised an IRQ"
    );
    let armed = d.publish(1, 1, &[call(CLEAR, &[0x4000])], 0);
    d.set(PAGE + WAIT_COMPLETED, armed + 1);
    d.set(PAGE + WAIT, 1);
    d.wait(armed);
    let deadline = Instant::now() + Duration::from_secs(5);
    while d.mmio_read(IRQ_STATUS) & IRQ_GL == 0 {
        // The device may finish before the arm; the next doorbell rechecks.
        let kick = d.produced;
        d.mmio_write(QUEUE_DOORBELL, kick);
        assert!(Instant::now() < deadline, "armed wait raised no IRQ");
        std::thread::sleep(Duration::from_millis(2));
    }
    d.set(PAGE + WAIT, 0);
    d.mmio_write(IRQ_STATUS, IRQ_GL);

    // The register protocol cannot interleave with queued entries.
    d.qemu.batch(900, &[call(CLEAR, &[0x4000])]);
    assert_eq!(d.mmio_read(0x111c), FAILED);
    assert_eq!(d.mmio_read(0x1124), ERROR_BATCH);
    drop(d);
    drop(server);
    std::fs::remove_dir_all(directory).unwrap();
}

#[test]
#[ignore = "requires native host GPU and queued QEMU producer; diskless acceptance"]
fn qemu_queue_reset_discards_in_flight_batches_and_old_protocol_still_works() {
    // Engine reset publishes a CPU coherence anchor: give QEMU a CPU display.
    let (_device, _queue, host) = native_gpu();
    let directory =
        std::env::temp_dir().join(format!("dg-queue-reset-{}", uuid::Uuid::new_v4().simple()));
    std::fs::create_dir(&directory).unwrap();
    let socket = directory.join("g.sock");
    let server = GpuServer::new(&socket, host).unwrap();
    let cpu_socket = directory.join("cpu.sock");
    let mut cpu = crate::ShmemServer::new(cpu_socket.to_str().unwrap()).unwrap();
    let qemu = Qemu::start_with_display(&directory, &socket, Some(&cpu_socket));
    let mut d = Driver {
        qemu,
        generation: 0,
        produced: 0,
        sequence: 0,
        mmio_reads: 0,
        mmio_writes: 0,
    };
    for (index, value) in [(1, 64), (2, 32), (3, 32), (4, 0x41)] {
        d.qemu.command(&format!(
            "writew {:#x} {value:#x}",
            0xf0000500u32 + index * 2
        ));
    }
    d.qemu.command("outb 0x3c0 0x20");
    drop(crate::wait_cpu_frame(&mut cpu, None));
    d.enable();
    let setup = d.publish(1, 0, &[(1, vec![0]), (3, vec![8, 8]), (5, vec![])], 0);
    assert_eq!(d.wait(setup), (DONE, 0));
    let mut records = vec![call(BEGIN, &[0])];
    for _ in 0..4000 {
        records.push(call(VERTEX3F, &[0, 0, 0]));
    }
    records.push(call(END, &[]));
    for _ in 0..12 {
        d.publish(1, 1, &records, 0);
    }
    let old_generation = d.generation;
    d.mmio_write(0x1038, 1);
    // Reset is synchronous for the page: STATE is zero before the write returns.
    assert_eq!(d.ram(PAGE + STATE), 0);
    assert_eq!(d.mmio_read(QUEUE_CONTROL), 0);
    let stopped = d.completed();
    assert!((stopped.wrapping_sub(d.produced) as i32) <= 0);
    std::thread::sleep(Duration::from_millis(200));
    assert_eq!(d.completed(), stopped, "discarded entries never complete");
    // A doorbell for the disabled page does nothing.
    d.mmio_write(QUEUE_DOORBELL, 1);
    assert_eq!(d.ram(PAGE + STATE), 0);
    // A stale generation cannot enable; the current one can.
    d.mmio_write(QUEUE_ADDR_LO, PAGE as u32);
    d.mmio_write(QUEUE_CONTROL, old_generation);
    assert_eq!(d.mmio_read(QUEUE_CONTROL), 0);
    d.enable();
    assert_ne!(d.generation, old_generation);
    let setup = d.publish(1, 0, &[(1, vec![0]), (3, vec![8, 8]), (5, vec![])], 0);
    assert_eq!(d.wait(setup), (DONE, 0), "{:?}", server.take_error());
    let query = d.publish(1, 1, &[(11, vec![GET_ERROR, 0, 0, 0])], 64);
    assert_eq!(d.wait(query), (DONE, 0));
    // An idle queue disables; the register protocol of older drivers then
    // runs unchanged on the same device.
    d.mmio_write(QUEUE_CONTROL, 0);
    assert_eq!(d.ram(PAGE + STATE), 0);
    d.qemu.batch(1000, &[call(CLEAR, &[0x4000])]);
    d.qemu.await_completion(1000);
    drop(d);
    std::fs::remove_dir_all(directory).unwrap();
}
