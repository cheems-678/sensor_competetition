import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from backend.asset_manifest import frontend_asset_paths


class AssetManifestTests(unittest.TestCase):
    def test_css_image_is_collected_once_and_data_uri_is_skipped(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            (root / 'assets').mkdir()
            (root / 'index.html').write_text('<link href="./assets/theme.css"><script src="./assets/app.js"></script>', encoding='utf-8')
            (root / 'assets/theme.css').write_text('body{background:url("./bg.png?x=1")}a{background:url(bg.png)}b{background:url(data:image/png;base64,AAAA)}', encoding='utf-8')
            (root / 'assets/app.js').write_text('', encoding='utf-8')
            (root / 'assets/bg.png').write_bytes(b'PNG')
            self.assertEqual({p.relative_to(root).as_posix() for p in frontend_asset_paths(root)}, {'index.html', 'assets/theme.css', 'assets/app.js', 'assets/bg.png'})

    def test_missing_outside_and_remote_assets_fail_the_build(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary) / 'build'
            root.mkdir()
            (root.parent / 'outside.png').write_bytes(b'PNG')
            for reference in ('missing.css', '../outside.png', 'https://example.com/theme.css'):
                with self.subTest(reference=reference):
                    (root / 'index.html').write_text(f'<link href="{reference}">', encoding='utf-8')
                    with self.assertRaises(RuntimeError):
                        frontend_asset_paths(root)
