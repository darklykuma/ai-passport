#!/usr/bin/env python3
"""Regenerate FOG MARCH assets into a temp tree and compare bytes-for-bytes.

The committed sprites under main/assets/ and the previews under
assets/fog-march/preview/ must be exactly reproducible from
tools/gen_fog_march_assets.py. If this test fails after editing the generator,
re-run the generator and commit the refreshed outputs together with the script.
"""

from __future__ import annotations

import filecmp
import importlib.util
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location(
    "gen_fog_march_assets", ROOT / "tools" / "gen_fog_march_assets.py"
)
assert SPEC and SPEC.loader
GEN = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(GEN)


class FogAssetsReproducible(unittest.TestCase):
    def test_regeneration_matches_committed_files(self) -> None:
        committed_raw = sorted((ROOT / "main" / "assets").glob("fog_*"))
        committed_png = sorted((ROOT / "assets" / "fog-march" / "preview").glob("*.png"))
        self.assertGreaterEqual(len(committed_raw), 18, "missing generated sprite blobs")
        self.assertGreaterEqual(len(committed_png), 19, "missing generated previews")

        with tempfile.TemporaryDirectory() as tmp:
            GEN.RAW_DIR = Path(tmp) / "main" / "assets"
            GEN.PREVIEW_DIR = Path(tmp) / "assets" / "fog-march" / "preview"
            GEN.main()

            for fresh in sorted(GEN.RAW_DIR.glob("fog_*")):
                committed = ROOT / "main" / "assets" / fresh.name
                self.assertTrue(
                    committed.exists(), f"committed blob missing: {fresh.name}"
                )
                self.assertTrue(
                    filecmp.cmp(fresh, committed, shallow=False),
                    f"stale blob {fresh.name}: rerun tools/gen_fog_march_assets.py",
                )
            for fresh in sorted(GEN.PREVIEW_DIR.glob("*.png")):
                committed = ROOT / "assets" / "fog-march" / "preview" / fresh.name
                self.assertTrue(
                    committed.exists(), f"committed preview missing: {fresh.name}"
                )
                self.assertTrue(
                    filecmp.cmp(fresh, committed, shallow=False),
                    f"stale preview {fresh.name}: rerun tools/gen_fog_march_assets.py",
                )

    def test_blob_layout_matches_lvgl_formats(self) -> None:
        """RGB565 tiles are w*h*2 bytes; RGB565A8 units are w*h*3."""
        size = GEN.SIZE * GEN.SIZE
        for path in sorted((ROOT / "main" / "assets").glob("fog_*")):
            data = path.read_bytes()
            if path.suffix == ".rgb565":
                self.assertEqual(len(data), size * 2, path.name)
            else:
                self.assertEqual(len(data), size * 3, path.name)


if __name__ == "__main__":
    unittest.main()
