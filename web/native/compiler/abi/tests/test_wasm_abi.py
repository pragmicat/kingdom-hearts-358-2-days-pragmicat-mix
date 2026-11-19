import unittest
import inspect

from references.layout import Layout
from references.parse import parse
from references.types import TypePass
from references.wasm_abi import UnsupportedWasmABI, block_abi, hidden_result, mismatched_returns, prototype_shape, return_signature


def function(source, name="f"):
    ast = parse(source, "abi-test.c")
    types = TypePass().run(ast)
    return Layout(types), types.file.ordinary[name].type


class WasmABITest(unittest.TestCase):
    def test_explicit_profile_wrapper_preserves_windows_default(self):
        from references.build import _hidden_result, _shape
        if "abi_profile" not in inspect.signature(_hidden_result).parameters:
            self.skipTest("ABI profile wrapper is not installed in the supplied upstream source")
        layout, fn = function("struct R {int a,b;}; struct R f(struct R value);")
        self.assertFalse(_hidden_result(layout, fn))
        self.assertTrue(_hidden_result(layout, fn, "wasm32"))
        self.assertEqual(_shape(layout, fn), [["s", 2]])
        self.assertEqual(_shape(layout, fn, "wasm32"), prototype_shape(layout, fn))

    def test_returns_depend_on_members_not_size_only(self):
        cases = [
            ("unsigned char a;", "i32", False), ("unsigned short a;", "i32", False),
            ("int a;", "i32", False), ("char a,b,c,d;", "sret:4:1", True),
            ("int a,b;", "sret:8:4", True), ("long long a;", "i64", False),
            ("int a,b,c;", "sret:12:4", True), ("float a;", "f32", False),
            ("double a;", "f64", False), ("int a[1];", "i32", False),
            ("int a[2];", "sret:8:4", True),
        ]
        for fields, signature, hidden in cases:
            with self.subTest(fields=fields):
                layout, fn = function(f"struct R {{{fields}}}; struct R f(int x);")
                self.assertEqual(return_signature(layout, fn), signature)
                self.assertEqual(hidden_result(layout, fn), hidden)

    def test_nested_single_member_and_empty_fields(self):
        layout, fn = function("struct E {}; struct I {int x;}; struct R {struct E e; struct I v[1];}; struct R f(void);")
        self.assertEqual(return_signature(layout, fn), "i32")
        layout, fn = function("struct R {}; struct R f(void);")
        self.assertEqual(return_signature(layout, fn), "void")
        self.assertFalse(hidden_result(layout, fn))

    def test_float_and_integer_aggregate_parameters_differ(self):
        layout, fn = function("struct F {float x;}; struct I {int x;}; void f(struct F x, struct I y);")
        shape = prototype_shape(layout, fn)
        self.assertIn(":f32:", shape[0][0])
        self.assertIn(":i32:", shape[1][0])
        self.assertNotEqual(shape[0], shape[1])

    def test_exact_size_survives_word_rounding(self):
        layout, fn = function("struct A {char a[5];}; struct B {int a,b;}; void f(struct A x, struct B y);")
        a, b = prototype_shape(layout, fn)
        self.assertEqual(a[1], b[1])
        self.assertNotEqual(a[0], b[0])

    def test_refuses_unaudited_values(self):
        cases = [
            "union R {int x; float y;}; union R f(void);",
            "struct R {unsigned int x:3;}; struct R f(void);",
            "struct R {int n; char bytes[];}; struct R f(void);",
            "struct R; struct R f(void);", "long double f(void);",
            "struct R {long double x;}; void f(struct R x);",
            "enum E {large=0x100000000ll}; enum E f(void);",
        ]
        for source in cases:
            with self.subTest(source=source), self.assertRaises(UnsupportedWasmABI):
                layout, fn = function(source)
                prototype_shape(layout, fn)

    def test_pointers_to_unaudited_records_remain_references(self):
        layout, fn = function("union R {int x; float y;}; void f(union R *p);")
        self.assertEqual(prototype_shape(layout, fn)[0][1], 1)

    def test_void_and_unprototyped_functions(self):
        layout, fn = function("void f(void);")
        self.assertEqual(prototype_shape(layout, fn), [])
        self.assertEqual(return_signature(layout, fn), "void")
        layout, fn = function("int f();")
        self.assertIsNone(prototype_shape(layout, fn))

    def test_variadic_marker_preserved(self):
        layout, fn = function("int f(int x, ...);")
        self.assertEqual(prototype_shape(layout, fn)[-1], ["...", 0])

    def test_exact_arity_and_prototype_for_block_normalization(self):
        shapes = {
            "shorter": [["def", [["w",1],["w",1]], False], ["decl", [["w",1]], False]],
            "longer": [["def", [["w",1]], False], ["decl", [["w",1],["w",1]], False]],
            "unknown": [["def", [["w",1]], False], ["decl", None, False]],
            "same": [["def", [["w",1]], False], ["decl", [["w",1]], False]],
        }
        found = [("one.c", [], [], shapes, "")]
        self.assertEqual(block_abi(found, set(shapes)), {"shorter","longer","unknown"})
        self.assertEqual(block_abi(found, {"same"}), set())

    def test_return_mismatches_and_missing_metadata(self):
        shapes = {
            "float_int": [["def", [], False, "f32"], ["decl", [], False, "i32"]],
            "different_out": [["def", [], True, "sret:8:4"], ["decl", [], True, "sret:12:4"]],
            "same": [["def", [], False, "i64"], ["decl", [], False, "i64"]],
        }
        self.assertEqual(mismatched_returns([("one.c",[],[],shapes,"")], set(shapes)),
                         ["different_out (defined sret:8:4)", "float_int (defined f32)"])
        with self.assertRaises(UnsupportedWasmABI):
            mismatched_returns([("old.c",[],[],{"f":[["def",[],False]]},"")], {"f"})


if __name__ == "__main__":
    unittest.main()
