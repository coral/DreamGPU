// SPDX-License-Identifier: GPL-2.0-or-later
//! Shared CPU frame, native cursor and low-level input transport.
//! Application input routing and audio playback remain responsibilities of the consumer.
mod cursor;
mod display;
mod input;
mod server;
pub use display::ShmemDisplay;
pub use server::ShmemServer;
