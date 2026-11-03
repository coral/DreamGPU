#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Patch 0034 on actual pinned Wine source. DreamGPU's GL vendor is unknown to
WineD3D, so the card comes from the NVIDIA fallback table by D3D level. With
vertex programs (VS 2.0) that is a GeForce FX 5800 instead of a GeForce 256,
which matches the "Geforce 5 NP2 disable" quirk. The quirk used to enable
GL_ARB_texture_rectangle unconditionally, and DreamGPU has no rectangles, so
every NPOT surface (front buffer, backbuffer) used a texture target the
frontend rejects, and DirectDraw/D3D6/D3D7 presented black. The prepared
card guess, NPOT bookkeeping and quirk run under ASan/UBSan: rectangles stay
off without NPOT or rectangles, and a real FX driver (NPOT through GL 2.0,
rectangles advertised) still trades NPOT for rectangles."""
import importlib.util
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[3]
HERE = ROOT / 'tests/guest/support'
spec = importlib.util.spec_from_file_location('dreamgpu_wine_prepare', HERE / 'wine_prepare.py')
prepare = importlib.util.module_from_spec(spec)
spec.loader.exec_module(prepare)


def block(text, start):
    """text[start:] up to the brace closing its first opening brace."""
    depth, at = 0, text.index('{', start)
    while True:
        if text[at] == '{':
            depth += 1
        elif text[at] == '}':
            depth -= 1
            if not depth:
                return text[start:at + 1]
        at += 1


def function(text, signature):
    start = text.index(signature)
    assert ';' not in text[start:text.index('\n{', start)], signature
    return block(text, start)


def definition(text, head):
    """An enum or struct definition starting with head, with its semicolon."""
    return block(text, text.index(head + '\n{')) + ';\n'


TEST = r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
typedef int BOOL;
typedef unsigned int UINT, DWORD;
#define TRUE 1
#define FALSE 0
#define TRACE(...) ((void)0)
#define FIXME(...) ((void)0)
#define debugstr_a(s) (s)
#define MAKEDWORD_VERSION(maj, min) (((maj & 0xffffu) << 16) | (min & 0xffffu))
#define WINED3DTEXOPCAPS_DOTPRODUCT3 %(dot3)s
%(types)s
/* The fields the functions below use, under their prepared names. */
struct wined3d_gl_info
{
    BOOL supported[WINED3D_GL_EXT_COUNT];
    BOOL texture_rectangle_hidden;
};
static const enum wined3d_pci_device card_fallback_nvidia[] =
%(fallback)s;
%(functions)s
/* The NPOT bookkeeping of wined3d_adapter_init_gl_caps. */
static void hide_rectangle(struct wined3d_gl_info *gl_info)
{
%(hide)s
}

/* The NVIDIA fallback card for these caps and whether the FX quirk applies. */
static enum wined3d_pci_device card(UINT vs_version, UINT ps_version)
{
    struct shader_caps shader = {0};
    struct fragment_caps fragment = {0};
    shader.vs_version = vs_version;
    shader.ps_version = ps_version;
    fragment.TextureOpCaps = WINED3DTEXOPCAPS_DOTPRODUCT3;
    fragment.MaxSimultaneousTextures = 8;
    return card_fallback_nvidia[d3d_level_from_caps(&shader, &fragment, 0)];
}

/* Extension detection then the quirk table entry for a GeForce FX. */
static struct wined3d_gl_info detect(BOOL npot, BOOL rectangle)
{
    struct wined3d_gl_info gl_info;
    memset(&gl_info, 0, sizeof(gl_info));
    gl_info.supported[ARB_TEXTURE_NON_POWER_OF_TWO] = npot;
    gl_info.supported[ARB_TEXTURE_RECTANGLE] = rectangle;
    hide_rectangle(&gl_info);
    assert(match_geforce5(&gl_info, "", GL_VENDOR_UNKNOWN, HW_VENDOR_NVIDIA,
                          CARD_NVIDIA_GEFORCEFX_5800));
    quirk_no_np2(&gl_info);
    return gl_info;
}

int main(void)
{
    const char vendor[] = "DreamGPU", renderer[] = "DreamGPU (native host OpenGL)";
    struct wined3d_gl_info gl_info;
    memset(&gl_info, 0, sizeof(gl_info));
    /* An unknown GL vendor and an NVIDIA card vendor: the fallback table. */
    assert(wined3d_guess_gl_vendor(&gl_info, vendor, renderer) == GL_VENDOR_UNKNOWN);
    assert(wined3d_guess_card_vendor(vendor, renderer) == HW_VENDOR_NVIDIA);
    /* Fragment programs alone: a GeForce 256, outside the quirk. */
    assert(card(0, 2) == CARD_NVIDIA_GEFORCE);
    assert(!match_geforce5(&gl_info, renderer, GL_VENDOR_UNKNOWN, HW_VENDOR_NVIDIA,
                           CARD_NVIDIA_GEFORCE));
    /* Vertex programs as well: VS 2.0 is the D3D9 SM2 level, a GeForce FX 5800. */
    assert(card(2, 2) == CARD_NVIDIA_GEFORCEFX_5800);
    assert(card(1, 2) == CARD_NVIDIA_GEFORCE3);
    printf("PASS prepared card guess: GeForce 256 (%%#06x) with fragment programs, "
           "GeForce FX 5800 (%%#06x) with VS 2.0\n", CARD_NVIDIA_GEFORCE, CARD_NVIDIA_GEFORCEFX_5800);

    /* DreamGPU: neither extension, and the quirk adds none. */
    gl_info = detect(FALSE, FALSE);
    assert(!gl_info.supported[ARB_TEXTURE_RECTANGLE]);
    assert(!gl_info.supported[ARB_TEXTURE_NON_POWER_OF_TWO]);
    /* A real FX driver: NPOT through GL 2.0, rectangles advertised and
     * hidden behind it; the quirk trades NPOT for rectangles as before. */
    gl_info = detect(TRUE, TRUE);
    assert(gl_info.supported[ARB_TEXTURE_RECTANGLE]);
    assert(!gl_info.supported[ARB_TEXTURE_NON_POWER_OF_TWO]);
    /* NPOT without rectangles: NPOT goes, no rectangles appear. */
    gl_info = detect(TRUE, FALSE);
    assert(!gl_info.supported[ARB_TEXTURE_RECTANGLE]);
    assert(!gl_info.supported[ARB_TEXTURE_NON_POWER_OF_TWO]);
    /* Rectangles without NPOT were never hidden and stay. */
    gl_info = detect(FALSE, TRUE);
    assert(gl_info.supported[ARB_TEXTURE_RECTANGLE]);
    assert(!gl_info.supported[ARB_TEXTURE_NON_POWER_OF_TWO]);
    puts("PASS prepared GeForce FX quirk: rectangles only when the driver reported them");
    return 0;
}
'''

with tempfile.TemporaryDirectory(prefix='dreamgpu-fx-quirk-') as temporary:
    work = Path(temporary) / 'wine'
    output = Path(temporary) / 'output'
    shutil.copytree(ROOT / 'vendor/wine9x', work, ignore=shutil.ignore_patterns('.git'))
    output.mkdir()
    prepare.prepare(work, output, False)
    directx = (work / 'wined3d/directx.c').read_text()
    private = (work / 'wined3d/wined3d_private.h').read_text()
    gl = (work / 'wined3d/wined3d_gl.h').read_text()
    public = (work / 'include/wine/wined3d.h').read_text()

    # The flag is part of the real gl_info.
    gl_info = definition(private, 'struct wined3d_gl_info')
    assert re.search(r'BOOL supported\[WINED3D_GL_EXT_COUNT\];\n\s+/\*[^*]*\*/\n\s+BOOL texture_rectangle_hidden;',
                     gl_info), gl_info
    # The FX entry still sits in the quirk table and fixup_extensions applies it.
    table = directx[directx.index('static const struct driver_quirk quirk_table[] ='):]
    assert re.search(r'match_geforce5,\s+quirk_no_np2,\s+"Geforce 5 NP2 disable"', table[:table.index('};')])
    assert 'quirk_table[i].apply(gl_info);' in function(directx, 'static void fixup_extensions(')
    # No NVIDIA card selector exists for an unknown GL vendor, so the card is
    # card_fallback_nvidia[d3d_level] (wined3d_guess_card).
    nvidia = block(directx, directx.index('nvidia_gl_vendor_table[] ='))
    assert 'GL_VENDOR_UNKNOWN' not in nvidia
    guess = function(directx, 'static enum wined3d_pci_device wined3d_guess_card(')
    assert 'return card_vendor_table[i].card_fallback[d3d_level];' in guess
    assert re.search(r'\{HW_VENDOR_NVIDIA,\s+"Nvidia",\s+nvidia_gl_vendor_table,[^}]*card_fallback_nvidia\}',
                     directx)
    fallback = directx.index('card_fallback_nvidia[] =')
    fallback = block(directx, directx.index('{', fallback))
    # The bookkeeping where full NPOT support hides rectangles.
    init = function(directx, 'static BOOL wined3d_adapter_init_gl_caps(')
    hide_at = init.index('    gl_info->texture_rectangle_hidden = FALSE;\n')
    hide = init[hide_at:]
    hide = hide[:hide.index('\n    }\n') + len('\n    }')]
    assert hide.count('gl_info->supported[ARB_TEXTURE_RECTANGLE] = FALSE;') == 1
    assert init.index('wined3d_adapter_init_limits(gl_info);') > hide_at
    assert init.index('fixup_extensions(gl_info,') > hide_at

    types = ''.join([
        definition(gl, 'enum wined3d_gl_extension'),
        definition(private, 'enum wined3d_pci_vendor'),
        definition(private, 'enum wined3d_pci_device'),
        definition(directx, 'enum wined3d_gl_vendor'),
        definition(directx, 'enum wined3d_d3d_level'),
        definition(private, 'struct shader_caps'),
        definition(private, 'struct fragment_caps'),
    ])
    dot3 = re.search(r'#define WINED3DTEXOPCAPS_DOTPRODUCT3\s+(\S+)', public)[1]
    functions = '\n'.join('static ' + function(directx, 'static ' + name)[len('static '):]
                          for name in ('enum wined3d_gl_vendor wined3d_guess_gl_vendor(',
                                       'enum wined3d_pci_vendor wined3d_guess_card_vendor(',
                                       'enum wined3d_d3d_level d3d_level_from_caps(',
                                       'BOOL match_geforce5(', 'void quirk_no_np2('))
    (work / 'test.c').write_text(TEST % {'dot3': dot3, 'types': types, 'fallback': fallback,
                                         'functions': functions, 'hide': hide})
    subprocess.run([os.environ.get('CC', 'cc'), '-std=c99', '-O1', '-Wall', '-Wextra', '-Werror',
                    '-Wno-unused-parameter', '-fsanitize=address,undefined', '-fno-omit-frame-pointer',
                    str(work / 'test.c'), '-o', str(work / 'test')], check=True)
    subprocess.run([str(work / 'test')], check=True)
print('PASS GeForce FX quirk: prepared card guess, NPOT bookkeeping and rectangle-free quirk')
