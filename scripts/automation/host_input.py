"""Host-boundary injection for benchmark replays (Linux uinput / macOS Quartz).

The Juke window must already have focus. This intentionally does not synthesize
window activation or alter desktop policy. macOS requires Accessibility access.
"""

# Keep this CLI runnable by its file path as well as through the scripts package.
if __package__ in (None, ""):
    import sys as _sys
    from pathlib import Path as _Path
    _sys.path.insert(0, str(_Path(__file__).resolve().parents[2]))

import ctypes
import os
import struct
import sys
import time


class HostInput:
    def __init__(self):
        self.held = set()
        self.held_buttons = set()
        if sys.platform == 'linux':
            import fcntl
            self.fcntl = fcntl
            self.fd = os.open('/dev/uinput', os.O_WRONLY | os.O_NONBLOCK)
            self.keys = {'A': 30, 'F12': 88, 'Escape': 1, 'Enter': 28, 'Space': 57,
                         'Left': 105, 'Right': 106, 'Up': 103, 'Down': 108}
            self.buttons = {'Left': 272, 'Right': 273, 'Middle': 274}
            for event_type in [1, 2]:
                fcntl.ioctl(self.fd, 0x40045564, event_type)  # UI_SET_EVBIT
            for key in [*self.keys.values(), *self.buttons.values()]:
                fcntl.ioctl(self.fd, 0x40045565, key)
            for axis in [0, 1, 6, 8]:
                fcntl.ioctl(self.fd, 0x40045566, axis)
            setup = struct.pack('HHHH80sI', 3, 0x1209, 0x0001, 1, b'Juke benchmark input', 0)
            fcntl.ioctl(self.fd, 0x405c5503, setup)
            fcntl.ioctl(self.fd, 0x5501)
            time.sleep(.5)  # Allow the compositor to discover the virtual device.
        elif sys.platform == 'darwin':
            self.cg = ctypes.CDLL('/System/Library/Frameworks/ApplicationServices.framework/ApplicationServices')
            self.cf = ctypes.CDLL('/System/Library/Frameworks/CoreFoundation.framework/CoreFoundation')
            self.cg.AXIsProcessTrusted.restype = ctypes.c_bool
            if not self.cg.AXIsProcessTrusted():
                raise PermissionError('Host injection requires Accessibility permission for the terminal/Python process')
            self.cg.CGEventCreateKeyboardEvent.argtypes = [ctypes.c_void_p, ctypes.c_uint16, ctypes.c_bool]
            self.cg.CGEventCreateKeyboardEvent.restype = ctypes.c_void_p
            self.cg.CGEventPost.argtypes = [ctypes.c_uint32, ctypes.c_void_p]
            self.cf.CFRelease.argtypes = [ctypes.c_void_p]
            self.keys = {'A': 0, 'F12': 111, 'Escape': 53, 'Enter': 36, 'Space': 49,
                         'Left': 123, 'Right': 124, 'Up': 126, 'Down': 125}
        else:
            raise RuntimeError('Host injection supports Linux and macOS')

    def emit(self, event_type, code, value):
        packet = struct.pack('llHHi', 0, 0, event_type, code, value)
        if os.write(self.fd, packet) != len(packet):
            raise OSError('Incomplete uinput write')

    def send(self, event):
        if event == 'ReleaseAll':
            for key in list(self.held):
                self.send({'KeyUp': key})
            for button in list(self.held_buttons):
                self.send({'MouseUp': button})
            return
        kind, value = next(iter(event.items()))
        if kind in ('KeyDown', 'KeyUp'):
            down = kind == 'KeyDown'
            if down:
                self.held.add(value)
            else:
                self.held.discard(value)
            if sys.platform == 'linux':
                self.emit(1, self.keys[value], int(down))
            else:
                native = self.cg.CGEventCreateKeyboardEvent(None, self.keys[value], down)
                if not native:
                    raise RuntimeError('CGEventCreateKeyboardEvent failed')
                self.cg.CGEventPost(0, native)
                self.cf.CFRelease(native)
        elif sys.platform == 'linux' and kind == 'MouseMoveRel':
            self.emit(2, 0, value['dx']); self.emit(2, 1, value['dy'])
        elif sys.platform == 'linux' and kind in ('MouseDown', 'MouseUp'):
            if kind == 'MouseDown':
                self.held_buttons.add(value)
            else:
                self.held_buttons.discard(value)
            self.emit(1, self.buttons[value], int(kind == 'MouseDown'))
        else:
            raise ValueError(f'Unsupported host event: {event}')
        if sys.platform == 'linux':
            self.emit(0, 0, 0)

    def close(self):
        try:
            self.send('ReleaseAll')
        finally:
            if sys.platform == 'linux':
                self.fcntl.ioctl(self.fd, 0x5502)
                os.close(self.fd)
