"""Generates a deterministic grammar corpus for complete, exact-source core parsing."""
from itertools import product


def core_corpus():
    """Returns authored grammar templates and bounded Cartesian intrinsic combinations.

    Core registers no client records or functions; record grammar remains in client fixtures.
    C++-only rejections require a C++ parse failure. Shared-invalid cases require
    both parsers to reject at the same span; successes compare real observations.
    Complexity: O(corpus size) time and result space.
    """
    rows = []

    def add(family, source, *, reject=False, category=None):
        """Appends an unchanged source with a required success or rejection."""
        row = {"id": f"core-{family}-{len(rows)}", "family": f"core-{family}",
               "source": source, "setup": "core"}
        if reject:
            row["cpp_reject"] = "outside bool/f64 core grammar or exact operand types"
        if category is not None:
            row["category"] = category
            row["status"] = "parse"
        rows.append(row)

    for left, operator, right in product(
            ["true", "false"], ["&&", "||", "==", "!="], ["true", "false"]):
        add("bool-operators", f"{left}{operator}{right}")
    for operand in ["true", "false"]:
        add("bool-operators", f"!{operand}")
        add("identity-casts", f"{operand} as bool")
    for left, operator, right in product(
            ["-2.0f64", "-0.0f64", "0.5f64", "2.0f64"],
            ["+", "-", "*", "/", "%", "==", "!=", "<", "<=", ">", ">="],
            ["-2.0f64", "-0.0f64", "0.5f64", "2.0f64"]):
        add("f64-operators", f"({left}){operator}({right})")
    for operand in ["0.0f64", "1.0f64", "2e0f64", "0.5f64"]:
        add("f64-operators", f"-({operand})")
        add("identity-casts", f"{operand} as f64 as f64")
    for source in [
        "()", "(false)", "(1.0f64,)", "(1.0f64,false,())",
        "((1.0f64,false),).0.1", "[]:[f64]", "[]:[bool]", "[]:[()]",
        "[1.0f64,2.0f64]", "[true,false]", "[[]:[bool]]",
        "[1.0f64,2.0f64]:[f64]", "[(1.0f64,true),(2.0f64,false)]",
        "1.0f64..2.0f64", "1.0f64..=2.0f64", "1.0f64..", "..2.0f64", "..=2.0f64", "..",
        "(|x:Range(f64)|x)(1.0f64..2.0f64)", "(|x:RangeInclusive(f64)|x)(1.0f64..=2.0f64)",
        "(|x:RangeFrom(f64)|x)(1.0f64..)", "(|x:RangeTo(f64)|x)(..2.0f64)",
        "(|x:RangeToInclusive(f64)|x)(..=2.0f64)", "(|x:RangeFull|x)(..)",
        "if true {1.0f64}else{2.0f64}", "if false {()}",
        "if false {()}else if true {()}else{()}",
        "||1.0f64", "(||1.0f64)()", "|x:f64|x+1.0f64", "(|x:f64|x)(2.0f64)",
        "(|x:f64,y:bool|if y{x}else{0.0f64})(2.0f64,true)",
        "(|x:(f64,bool)|x.0)((1.0f64,true))", "(|x:[f64]|x)([1.0f64])",
        "(|x:()|x)(())", "(|x:(f64,)|x.0)((1.0f64,))",
        "[]:[Fn((),f64)]", "[||1.0f64]:[Fn((),f64)]", "(|| ||1.0f64)()()",
        "(if true {||1.0f64}else{||2.0f64})()",
        "(||[||1.0f64])()", "[]:[Fn((f64,),bool)]",
        "(||1.0f64,)", "|x:(f64)|x",
        "1.0f64+2.0f64*3.0f64", "false||true&&false",
        "if 1.0f64<2.0f64 {true}else{false}",
    ]:
        add("grammar", source)
    for source in [
        "1.0", "1.", "1e0", "1.0_f64", "1_0.0f64", "1.0e+0f64",
        "1e-9999f64", "5e-324f64", "1e9999f64", "-0.0f64",
        "1e+_2f64", "1e__2f64", "1e-__2f64", "1.0E+__2f64",
        "1f64", "1_f64", "0f64", "1__f64", "1_000f64", "9007199254740993f64",
        "340_282_366_920_938_463_463_374_607_431_768_211_456f64",
        "-0f64", "1f64+2.0f64", "1f64..2f64", "1f64..=2f64",
        "1f64..", "..2f64", "..=2f64",
        "/* ordinary */true", "true/* nested /* block */ comment */&&false",
        "true// ordinary trailing comment", "/* */1.0f64 /* */+/* */ 2.0f64",
        "(1.0f64,2.0f64).0x1", "(1.0f64,2.0f64).0b1",
        "(1.0f64,2.0f64).0o1", "(1.0f64,2.0f64).0_1",
        "((1.0f64,2.0f64),).0.1", "(|λ:f64|λ)(1.0f64)",
    ]:
        add("lexical-positive", source)
    for source in ["1e_+2f64", "1e__-2f64", "1e_+_2f64", "1.0e_+2f64",
                   "1E__+2f64", "1_e_+_2"]:
        add("exponent-invalid", source, category="malformed_input")
    for source in [
        "", " ", "[]", "[1.0f64,]", "(1.0f64,2.0f64,)", "[,]", "(,)",
        "(1.0f64 2.0f64)", "1.0f64 2.0f64", "true false", "1.0f64;2.0f64",
        "if true {}", "if true {1.0f64}", "if true {()}else{}", "if true {()}else ()",
        "|x:f64,|x", "|x:f64,x:f64|x", "|x f64|x", "||", "| |1.0f64",
        "(||1.0f64)(1.0f64)", "(|x:f64|x)(true)", "(|x:f64|x)(1.0f64,)",
        "1.0f64<2.0f64<3.0f64", "1.0f64..2.0f64..3.0f64", "..=", "1.0f64..=",
        "true as f64", "1.0f64 as bool", "[1.0f64,true]", "true..false",
        "1.0f64 as (f64)", "[]:[Fn(f64,f64)]", "|f:Fn((),f64)|1.0f64",
        "true & & false", "true | | false", "1.0f64 < = 2.0f64", "1.0f64 = = 2.0f64",
        "1.0f64 . . 2.0f64", "true&/*jointness*/&false", "/* unterminated",
        "/// doc comment", "/** doc comment */true", "//! inner doc", "/*! inner doc */true",
        "r#true", "trué", "trueλ", "1.0f64foo", "1.0f32", "1f32",
        "1f64x", "1f64_", "1f640", "0b1f64", "0o7_f64", "0x1f64", "1 as f64",
        "1", "0x1", "0b1", "0o1", "1i32", "1u8", "1.0i32", "2+3*4", "2f64+3",
        "[1.0f64][0]", "[1.0f64][0.0f64]", "([]:[f64])[0]",
        "(1.0f64,).0f64", "(1f64,).0f64", "(1f64,).0_f64",
        "(1.0f64,).0e0", "(1.0f64,).184467440737095516160",
        "let x=1.0f64", "true?1.0f64:2.0f64", "x=1.0f64", "+1.0f64",
        "sqrt(4.0f64)", "tap(1.0f64)", "Empty{}", "1.0f64.abs()",
        "(||1.0f64)()()", "|x:(f64,bool,)|x", "[]:[Fn((f64,bool,),f64)]",
        "true&&||true", "1.0f64+||1.0f64", "[||1.0f64,]",
        "(if true {||1.0f64}else{||2.0f64})(1.0f64)",
        "1.0f64 as f64[]", "[]:[[f64,]]",
        "(1.0f64,).1e0", "(1.0f64,).0.0e0", "1.0f64.. =2.0f64",
        "true! =false", "false& &true", "true/* */|/* */|false", "1.e0",
    ]:
        add("adversarial", source, reject=True)
    return rows
