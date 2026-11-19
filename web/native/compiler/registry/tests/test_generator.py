from pathlib import Path
import os
import sys
import tempfile
import unittest

HERE = Path(__file__).resolve().parent.parent
tools = os.environ.get("KHDAYS_STATIC_REGISTRY_TOOLS")
if not tools:
    raise RuntimeError("Run tests/run_tests.py --patched <applied source checkout> first.")
sys.path.insert(0, tools)
from static_registry import records_in, write_registry


class RegistryTest(unittest.TestCase):
    def test_all_record_kinds_and_decoys_across_tus(self):
        sources = [HERE / "tests/tu_alpha.c", HERE / "tests/tu_beta.c"]
        self.assertEqual(len(records_in(sources[0])), 4)
        with tempfile.TemporaryDirectory() as temp:
            result = write_registry(sources, Path(temp) / "registry.c").read_text()
        self.assertNotIn("fake_comment", result)
        self.assertNotIn("fake_string", result)
        self.assertIn("khdays_static_function_count = 3u", result)
        self.assertIn("&khdays_function_beta_again", result)

    def test_data_source_and_declaration_order_retained(self):
        sources = [HERE / "tests/tu_alpha.c", HERE / "tests/tu_beta.c"]
        with tempfile.TemporaryDirectory() as temp:
            out = Path(temp) / "registry.c"
            first = write_registry(sources, out).read_text()
            timestamp = out.stat().st_mtime_ns
            self.assertEqual(write_registry(sources, out).read_text(), first)
            self.assertEqual(timestamp, out.stat().st_mtime_ns)
            entries = first.split("khdays_static_data[] = {", 1)[1].split("};", 1)[0]
            self.assertLess(entries.index("&khdays_place_z_first"), entries.index("&khdays_place_a_second"))
            reverse = write_registry(reversed(sources), out).read_text()
            entries = reverse.split("khdays_static_data[] = {", 1)[1].split("};", 1)[0]
            self.assertLess(entries.index("&khdays_place_a_second"), entries.index("&khdays_place_z_first"))

    def test_zero_records_are_legal_arrays_with_explicit_counts(self):
        with tempfile.TemporaryDirectory() as temp:
            result = write_registry([], Path(temp) / "empty.c").read_text()
        self.assertEqual(result.count("    NULL,"), 3)
        self.assertEqual(result.count("_count = 0u"), 3)

    def test_duplicate_symbols_refused_but_duplicate_source_list_entries_are_deduplicated(self):
        source = HERE / "tests/tu_alpha.c"
        with tempfile.TemporaryDirectory() as temp:
            copy = Path(temp) / "copy.c"
            copy.write_text(source.read_text())
            with self.assertRaisesRegex(ValueError, "duplicate metadata"):
                write_registry([source, copy], Path(temp) / "bad.c")
            result = write_registry([source, source], Path(temp) / "one.c").read_text()
            self.assertIn("khdays_static_function_count = 2u", result)

    def test_nonexternal_or_wrongly_qualified_metadata_refused(self):
        cases = [
            "static const struct khdays_function_record local = {0};",
            "void f(void) { const struct khdays_function_record local = {0}; }",
            "struct khdays_function_record mutable = {0};",
        ]
        with tempfile.TemporaryDirectory() as temp:
            source = Path(temp) / "bad.c"
            for code in cases:
                source.write_text(code)
                with self.subTest(code=code), self.assertRaises(ValueError):
                    records_in(source)

    def test_generated_output_cannot_be_its_own_input(self):
        with tempfile.TemporaryDirectory() as temp:
            source = Path(temp) / "registry.c"
            source.write_text("")
            with self.assertRaisesRegex(ValueError, "own input"):
                write_registry([source], source)


if __name__ == "__main__":
    unittest.main()
