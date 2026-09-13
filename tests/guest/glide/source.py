"""Load the same exact patched upstream sources compiled by the guest builder."""
from functools import cache
import importlib.util
import json
from pathlib import Path
import shutil
import tempfile

ROOT = Path(__file__).resolve().parents[3]
_spec = importlib.util.spec_from_file_location('dreamgpu_patches', ROOT / 'tests/guest/support/patches.py')
patches = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(patches)


@cache
def sources():
    manifest = ROOT / 'support/guest/glide/patches/manifest.json'
    names = json.loads(manifest.read_text())['files']
    with tempfile.TemporaryDirectory(prefix='dreamgpu-glide-source-') as temporary:
        work = Path(temporary)
        for name in names:
            original = (ROOT / 'vendor/wine-glu/dlls/glu32/mipmap.c' if name == 'dg-mipmap.c'
                        else ROOT / 'vendor/qemu-xtra/openglide' / name)
            (work / name).parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(original, work / name)
        patches.apply(work, manifest)
        return {name: (work / name).read_text() for name in names}


def patched_source(name):
    return sources()[name]
