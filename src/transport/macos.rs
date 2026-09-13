// SPDX-License-Identifier: GPL-2.0-or-later
//! Mach send-right reception for IOSurfaces; file descriptor passing cannot carry these rights.

use super::wire::{invalid, Packet, BYTES};
use std::{
    ffi::{c_char, c_void, CString},
    io,
    mem::size_of,
};

#[repr(C)]
#[derive(Default)]
struct Header {
    bits: u32,
    size: u32,
    remote: u32,
    local: u32,
    voucher: u32,
    id: i32,
}
#[repr(C)]
#[derive(Default)]
struct PortDescriptor {
    name: u32,
    pad: u32,
    pad2: u16,
    disposition: u8,
    kind: u8,
}
#[repr(C)]
struct Message {
    header: Header,
    count: u32,
    port: PortDescriptor,
    packet: [u8; BYTES],
    trailer: [u8; 64],
}

unsafe extern "C" {
    static mach_task_self_: u32;
    static bootstrap_port: u32;
    fn mach_port_allocate(task: u32, right: u32, name: *mut u32) -> i32;
    fn mach_port_insert_right(task: u32, name: u32, right: u32, kind: u32) -> i32;
    fn mach_port_destroy(task: u32, name: u32) -> i32;
    fn mach_port_deallocate(task: u32, name: u32) -> i32;
    fn bootstrap_register(bootstrap: u32, name: *const c_char, port: u32) -> i32;
    fn mach_msg(
        message: *mut Header,
        options: u32,
        send_size: u32,
        receive_size: u32,
        receive_name: u32,
        timeout: u32,
        notify: u32,
    ) -> i32;
    fn mach_msg_destroy(message: *mut Header);
}

#[link(name = "IOSurface", kind = "framework")]
unsafe extern "C" {
    fn IOSurfaceLookupFromMachPort(port: u32) -> *mut c_void;
    fn IOSurfaceGetWidth(surface: *mut c_void) -> usize;
    fn IOSurfaceGetHeight(surface: *mut c_void) -> usize;
    fn IOSurfaceGetBytesPerRow(surface: *mut c_void) -> usize;
    fn IOSurfaceGetBytesPerElement(surface: *mut c_void) -> usize;
    fn IOSurfaceGetPixelFormat(surface: *mut c_void) -> u32;
}
#[link(name = "CoreFoundation", kind = "framework")]
unsafe extern "C" {
    fn CFRelease(value: *const c_void);
}

pub fn validate_surface(port: u32, width: u32, height: u32, stride: u32) -> io::Result<()> {
    unsafe {
        let surface = IOSurfaceLookupFromMachPort(port);
        if surface.is_null() {
            return Err(invalid("Received right is not an IOSurface"));
        }
        let valid = IOSurfaceGetWidth(surface) == width as usize
            && IOSurfaceGetHeight(surface) == height as usize
            && IOSurfaceGetBytesPerRow(surface) == stride as usize
            && IOSurfaceGetBytesPerElement(surface) == 4
            && IOSurfaceGetPixelFormat(surface) == u32::from_be_bytes(*b"BGRA");
        CFRelease(surface);
        if valid {
            Ok(())
        } else {
            Err(invalid(
                "GPU wire geometry differs from the received IOSurface",
            ))
        }
    }
}

fn check(result: i32, operation: &str) -> io::Result<()> {
    if result == 0 {
        Ok(())
    } else {
        Err(io::Error::other(format!(
            "{operation}: Mach error {result:#x}"
        )))
    }
}

pub struct SendRight(pub u32);
impl Drop for SendRight {
    fn drop(&mut self) {
        unsafe {
            mach_port_deallocate(mach_task_self_, self.0);
        }
    }
}

pub struct Endpoint {
    port: u32,
    name: CString,
}
impl Endpoint {
    pub fn new() -> io::Result<Self> {
        let mut port = 0;
        unsafe {
            check(
                mach_port_allocate(mach_task_self_, 1, &mut port),
                "allocate GPU receive right",
            )?;
        }
        let endpoint = Self {
            port,
            name: CString::new(format!(
                "org.dreamgpu.gpu.{}",
                uuid::Uuid::new_v4().simple()
            ))
            .unwrap(),
        };
        unsafe {
            check(
                mach_port_insert_right(mach_task_self_, port, port, 20),
                "make GPU service send right",
            )?;
            check(
                bootstrap_register(bootstrap_port, endpoint.name.as_ptr(), port),
                "register GPU Mach service",
            )?;
        }
        Ok(endpoint)
    }
    pub fn name(&self) -> &str {
        self.name.to_str().unwrap()
    }
    pub fn port(&self) -> u32 {
        self.port
    }
}
impl Drop for Endpoint {
    fn drop(&mut self) {
        unsafe {
            bootstrap_register(bootstrap_port, self.name.as_ptr(), 0);
            mach_port_destroy(mach_task_self_, self.port);
        }
    }
}

/// Blocks without a timeout. Destroying the endpoint interrupts the receive.
pub fn receive(port: u32) -> io::Result<(Packet, SendRight)> {
    let mut message: Message = unsafe { std::mem::zeroed() };
    let result = unsafe {
        mach_msg(
            &mut message.header,
            2,
            0,
            size_of::<Message>() as u32,
            port,
            0,
            0,
        )
    };
    check(result, "receive GPU IOSurface")?;
    let valid = message.header.size as usize
        == size_of::<Header>() + 4 + size_of::<PortDescriptor>() + BYTES
        && message.header.bits & 0x80000000 != 0
        && message.count == 1
        && message.port.kind == 0
        && message.port.disposition == 17
        && message.port.name != 0;
    if !valid {
        unsafe {
            mach_msg_destroy(&mut message.header);
        }
        return Err(invalid("GPU Mach message has invalid layout or rights"));
    }
    let right = SendRight(message.port.name);
    message.port.name = 0; // Move the received send right into its RAII owner.
    unsafe {
        mach_msg_destroy(&mut message.header);
    }
    let packet = Packet(message.packet);
    packet.validate()?;
    Ok((packet, right))
}

#[cfg(test)]
mod tests {
    use super::*;
    unsafe extern "C" {
        fn bootstrap_look_up(bootstrap: u32, name: *const c_char, port: *mut u32) -> i32;
    }
    #[test]
    fn mach_right_transfer_and_blocked_receive_shutdown() {
        assert_eq!(size_of::<Header>(), 24);
        assert_eq!(size_of::<PortDescriptor>(), 12);
        let endpoint = Endpoint::new().unwrap();
        let mut destination = 0;
        unsafe {
            check(
                bootstrap_look_up(bootstrap_port, endpoint.name.as_ptr(), &mut destination),
                "lookup test endpoint",
            )
            .unwrap();
        }
        let destination = SendRight(destination);
        let receive_port = endpoint.port();
        let receiver = std::thread::spawn(move || receive(receive_port));
        let mut message: Message = unsafe { std::mem::zeroed() };
        message.header = Header {
            bits: 0x80000000 | 19,
            size: 168,
            remote: destination.0,
            ..Header::default()
        };
        message.count = 1;
        message.port = PortDescriptor {
            name: destination.0,
            disposition: 19,
            ..PortDescriptor::default()
        };
        message.packet = Packet::new(super::super::wire::FRAME).0;
        unsafe {
            check(
                mach_msg(&mut message.header, 1, 168, 0, 0, 0, 0),
                "send test right",
            )
            .unwrap();
        }
        let (packet, received) = receiver.join().unwrap().unwrap();
        assert_eq!(packet.kind(), super::super::wire::FRAME);
        assert_ne!(received.0, 0);
        drop(received);
        let receiver = std::thread::spawn(move || receive(receive_port));
        drop(endpoint);
        assert!(receiver.join().unwrap().is_err());
    }
}
