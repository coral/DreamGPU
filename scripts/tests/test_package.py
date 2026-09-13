"""Recipe identity and atomic shared guest package assembly contracts."""

# Keep this CLI runnable by its file path as well as through the scripts package.
if __package__ in (None, ""):
    import sys as _sys
    from pathlib import Path as _Path
    _sys.path.insert(0, str(_Path(__file__).resolve().parents[2]))

import hashlib
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest

spec=importlib.util.spec_from_file_location('dg_package',(Path(__file__).resolve().parents[2] / 'scripts/fixtures/package.py'))
package=importlib.util.module_from_spec(spec);spec.loader.exec_module(package)

class PackageTests(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory();self.addCleanup(self.temp.cleanup)
        self.root=Path(self.temp.name);self.inputs={};components={}
        for component,names in package.FILES.items():
            directory=self.root/component;directory.mkdir();self.inputs[component]=directory
            for name in names+package.LICENSES.get(component,[]):(directory/name).write_bytes((component+'/'+name).encode())
            hashes={name:package.digest(directory/name) for name in names}
            manifest={'artifacts':hashes}
            if component=='wine':manifest['licenses']={name:package.digest(directory/name) for name in package.LICENSES[component]}
            if component=='opengl':manifest={'outputs':{name:{'sha256':value} for name,value in hashes.items()}}
            if component=='glide':manifest={'dll_sha256':hashes['glide2x.dll']}
            (directory/'manifest.json').write_text(json.dumps(manifest))
            components[component]={'path':str(directory),'manifest_sha256':package.digest(directory/'manifest.json')}
        self.recipe=self.root/'recipe.json';self.recipe.write_text(json.dumps({'schema':1,'components':components}))

    def test_assembles_same_checked_inputs_without_overwriting(self):
        output=self.root/'output';result=package.assemble(package.recipe_inputs(self.recipe),output)
        self.assertEqual(package.digest(output/'application/dgpugl.dll'),package.digest(self.inputs['opengl']/'dgpugl.dll'))
        self.assertTrue((output/'drivers/nt5/dgpumini.sys').is_file())
        self.assertTrue((output/'drivers/win98/dgpumini.vxd').is_file())
        for name in package.LICENSES['wine']:
            self.assertEqual(package.digest(output/'licenses/wine'/name),package.digest(self.inputs['wine']/name))
        expected=hashlib.sha256(json.dumps(result['files'],sort_keys=True).encode()).hexdigest()
        self.assertEqual(result['identity'],expected)
        with self.assertRaisesRegex(ValueError,'immutable'):package.assemble(self.inputs,output)

    def test_recipe_manifest_identity_and_members_are_both_checked(self):
        manifest=self.inputs['wine']/'manifest.json';manifest.write_text(manifest.read_text()+' ')
        with self.assertRaisesRegex(ValueError,'build manifest differs'):package.recipe_inputs(self.recipe)
        with self.assertRaisesRegex(ValueError,'hash mismatch'):
            (self.inputs['opengl']/'dgpugl.dll').write_bytes(b'tampered')
            package.assemble(self.inputs,self.root/'rejected')
        self.assertFalse((self.root/'rejected').exists())

    def test_nested_license_hash_is_required_and_checked(self):
        manifest_path=self.inputs['wine']/'manifest.json'
        manifest=json.loads(manifest_path.read_text())
        manifest['licenses'].pop('LICENSE.nocrt')
        manifest_path.write_text(json.dumps(manifest))
        with self.assertRaisesRegex(ValueError,'notice differs'):
            package.assemble(self.inputs,self.root/'missing-license-hash')
        self.assertFalse((self.root/'missing-license-hash').exists())

    def test_unknown_or_missing_component_is_rejected(self):
        recipe=json.loads(self.recipe.read_text());recipe['components'].pop('nt');self.recipe.write_text(json.dumps(recipe))
        with self.assertRaisesRegex(ValueError,'exactly'):package.recipe_inputs(self.recipe)

    def test_recipe_pins_shared_source_lock(self):
        recipe=json.loads(self.recipe.read_text());recipe['sources_lock_sha256']='0'*64
        self.recipe.write_text(json.dumps(recipe))
        with self.assertRaisesRegex(ValueError,'source lock differs'):package.recipe_inputs(self.recipe)
        recipe['sources_lock_sha256']=package.digest(package.ROOT/'support/guest/sources.lock.json')
        self.recipe.write_text(json.dumps(recipe))
        self.assertEqual(package.recipe_inputs(self.recipe),self.inputs)

    def test_new_source_recipe_validates_payloads_and_never_overwrites(self):
        path=self.root/'new-recipe.json'
        result=package.write_recipe(self.inputs,path)
        self.assertEqual(package.recipe_inputs(path),{key:value.resolve() for key,value in self.inputs.items()})
        self.assertIn('not transferred',result['status'])
        with self.assertRaisesRegex(ValueError,'not overwritten'):package.write_recipe(self.inputs,path)
        (self.inputs['wine']/'wined3d.dll').write_bytes(b'changed source build')
        rejected=self.root/'rejected-recipe.json'
        with self.assertRaisesRegex(ValueError,'hash mismatch'):package.write_recipe(self.inputs,rejected)
        self.assertFalse(rejected.exists())

if __name__=='__main__':unittest.main()
