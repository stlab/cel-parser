"""Maintains contract-derived decoded expressions; it does not evaluate CEL."""
import struct
import re


def double(value):
    """Encodes independently specified expected binary64 values, preserving zero signs."""
    return struct.pack(">d", value).hex()


def fixtures():
    """Returns ordered fixtures covering each retained production and census family."""
    rows = []

    def add(family, source, expected=None, setup="std", **options):
        """Appends one uniquely keyed source-bound comparison or diagnostic contract."""
        # Shared sources use decimal f64 literals; Rust rejects an integer token's f64 suffix.
        if not options.pop("verbatim", False) and not options.get("cpp_only"):
            source = re.sub(
                r"(?<!\w)([0-9]+(?:\.[0-9]+)?(?:e[+-]?[0-9]+)?)f64\b",
                lambda match: (match[1] if "." in match[1] or "e" in match[1]
                               else match[1] + ".0") + "f64", source)
        row = {"id": f"{family}-{len(rows)}", "family": family,
               "source": source, "setup": setup, **options}
        if expected is not None:
            row["expected"] = expected
        rows.append(row)

    for source, expected in [
        ("1f64+2f64*3f64", 7), ("10f64-3f64-2f64", 5),
        ("20f64/2f64/2f64", 5), ("-2f64 as f64*3f64", -6),
        ("1f64 as f64 as f64+2f64", 3), ("-5f64%2f64", -1),
        ("-0f64", -0.0), ("1f64/0f64", float("inf")),
        ("-1f64/0f64", -float("inf")), ("1e-9999f64", 0),
        ("5e-324f64", 5e-324), ("1e9999f64", float("inf")),
    ]:
        add("scalar", source, ["D", double(expected), ""])
    add("scalar", "0f64/0f64", ["D", "nan", ""])
    add("trace-scalar-boundaries", "tap(0f64/0f64)", ["D", "nan", "nan"])
    add("trace-scalar-boundaries", "tap(1f64/0f64)",
        ["D", double(float("inf")), double(float("inf"))])
    for source in ["!false", "true as bool==!false", "1f64<2f64", "2f64>1f64",
                   "1f64<=1f64", "1f64>=1f64", "1f64!=2f64", "true!=false",
                   "0f64==-0f64", "!(0f64/0f64==0f64/0f64)"]:
        add("scalar", source, ["B", "true", ""])
    for source, expected in [
        ("min(1f64,2f64)", 1), ("max(1f64,2f64)", 2),
        ("clamp(4f64,1f64,3f64)", 3), ("abs(-2f64)", 2),
        ("signum(-2f64)", -1), ("sqrt(4f64)", 2), ("floor(2.5f64)", 2),
        ("ceil(2.5f64)", 3), ("trunc(-2.5f64)", -2), ("fract(2.5f64)", .5),
        ("round_ties_even(2.5f64)", 2), ("round_ties_even(3.5f64)", 4),
        ("round_ties_even(-0.5f64)", -0.0), ("(-2f64).abs()", 2),
        ("abs(-0f64)", 0), ("signum(-0f64)", -1), ("signum(0f64)", 1),
        ("sqrt(-0f64)", -0.0), ("floor(-0f64)", -0.0), ("ceil(-0f64)", -0.0),
        ("trunc(-0f64)", -0.0), ("fract(-2.5f64)", -.5),
        ("min(0f64/0f64,2f64)", 2), ("max(2f64,0f64/0f64)", 2),
    ]:
        add("numeric-functions", source, ["D", double(expected), ""])
    for source in ["sqrt(-1f64)", "signum(0f64/0f64)", "fract(1f64/0f64)",
                   "clamp(0f64/0f64,1f64,3f64)", "abs(0f64/0f64)",
                   "min(0f64/0f64,0f64/0f64)", "max(0f64/0f64,0f64/0f64)",
                   "floor(0f64/0f64)", "ceil(0f64/0f64)", "trunc(0f64/0f64)",
                   "round_ties_even(0f64/0f64)"]:
        add("numeric-functions", source, ["D", "nan", ""])
    for name in ["min", "max"]:
        add("numeric-functions", f"{name}(0f64,-0f64)", zero_choice=True)
    for source in ["clamp(1f64,3f64,2f64)", "clamp(1f64,0f64/0f64,2f64)",
                   "clamp(1f64,0f64,0f64/0f64)"]:
        add("numeric-errors", source, setup="clamp_error", status="runtime")
    for source in ["()", "(1f64)", "(1f64,)", "(true,2f64,())",
                   "((),(true,),[[1f64,2f64],[3f64]])", "[]:[f64]",
                   "[1f64,2f64]:[f64]", "[[]:[bool]]", "[true,false]:[bool]",
                   "((1f64,2f64),).0.1", "(1f64,2f64).0x1",
                   "(1f64,2f64).0b1", "(1f64,2f64).0o1",
                   "(1f64,2f64).0_1", "((1f64,2f64),).0_0.0_1"]:
        add("structural", source)
    for source in ["1f64+2f64..4f64*2f64", "1f64..=3f64", "1f64..",
                   "..3f64", "..=3f64", "..", "(|r:RangeFull|r)(..)"]:
        add("ranges", source)
    for source in ["Pair{y:tap(2f64),x:tap(1f64)}", "Pair{x:1f64,y:2f64}.x",
                   "Pair{y:2f64,x:1f64}.y", "Empty{}", "Pair{x:1f64,y:2f64}.x.plus(3f64)",
                   "(|p:Pair|p.x)(Pair{x:1f64,y:2f64})"]:
        add("records", source)
    for source in ["Pair{x:1f64,y:2f64}.x", "(2f64)(3f64)", "1f64|2f64*3f64"]:
        add("custom-precedence", source, setup="custom")
    for source in ["named(2f64)", "1f64.named()", "1f64.plus(2f64)",
                   "ambient", "shadow", "(|| library)()", "(|| shadow)()",
                   "(|ambient:f64|ambient)(9f64)", "if true {ambient}else{0f64}"]:
        add("lookup-scopes", source)
    for source in ["hit(1f64)||explode()", "false&&explode()",
                   "hit(1f64)&&hit(2f64)", "hit(-1f64)||hit(2f64)",
                   "if true {hit(1f64)}else{explode()}",
                   "if false {explode()}else if true {hit(2f64)}else{false}",
                   "if true {()}", "if false {()}", "if false {()}else{()}"]:
        add("lazy-conditional", source)
    for source in ["true&&explode()", "false||explode()",
                   "((),if true {explode()}else{false})"]:
        add("selected-exceptions", source, setup="callback_error", status="runtime")
    for source in ["||7f64", "|x:f64,y:bool|if y{x+1f64}else{0f64}",
                   "(|x:f64,y:bool|if y{x+1f64}else{0f64})(4f64,true)",
                   "(|x:((f64,bool),[f64])|x.0.0)(((1f64,true),[2f64]))",
                   "(|x:()|x)(())", "(||leaf())()", "(||[leaf()])()",
                   "[leaf()]:[Client]", "[]:[Client]", "(|| ||7f64)()()",
                   "(||[||7f64])()", "(if true {||7f64}else{||8f64})()",
                   "[||7f64]:[Fn((),f64)]", "(Box{f:||9f64}.f)()",
                   "Box{f:||9f64}.f()()", "[]:[Fn((),f64)]",
                   "[]:[([Fn((),f64)],bool)]", "[]:[Box]",
                   "(||[]:[Fn((),f64)])()", "[]:[Fn((),Fn((),f64))]",
                   "||leaf()", "||[leaf()]", "|| ||7f64"]:
        add("closures-complete-results", source)
    # These are valid token streams: diagnostics originate in CEL rather than a host lexer.
    for source, category, span in [
        ("missing", "lookup_error", [0,7]),
        ("(true).0", "type_error", [0,8]),
        ("(1f64,).2", "type_error", [0,11]),
        ("((1f64,),).0.2", "type_error", [0,16]),
        ("[]:(f64)", "type_error", [3,8]),
        ("[true]:[f64]", "type_error", [0,12]),
        ("[true]:([f64])   ", "type_error", [0,14]),
        ("[]:((f64,[bool]))   ", "type_error", [3,17]),
        ("[[]:[bool]]:([[f64]])   ", "type_error", [0,21]),
    ]:
        add("exact-diagnostics", source, category=category, span=span)
    for source in ["[]", "[1f64,]", "(1f64,2f64,)", "(,)", "[,]", "[1f64 2f64]",
                   "(1f64 2f64)", "else", "as", "1f64+", "1f64 2f64",
                   "..=", "1f64..=", "..else", "1f64..else", "..as", "1f64..as",
                   "| |1f64", "|x:f64,|1f64", "||", "|x f64|x",
                   "|x:f64,x:f64|x", "|if:f64|1f64", "|else:f64|1f64",
                   "|as:f64|1f64", "|true:f64|1f64", "if true {}",
                   "if true {1f64}else{}", "if true {1f64}else 2f64",
                   "if true 1f64", "if true {1f64;2f64}",
                   "Pair{x:1f64,y:2f64,}", "named(1f64,)", "1f64.plus(2f64,)"]:
        add("malformed-syntax", source, category="malformed_input")
    for source in ["1f64<2f64<3f64", "+1f64", "1f64 as (f64)"]:
        add("malformed-syntax", source, category="malformed_input")
    for source in ["true as f64", "1f64 as bool", "false&&1f64",
                   "if true {1f64}", "if true {1f64}else{false}", "if 1f64 {()}",
                   "[true]:[f64]", "[1f64,true]", "Pair{x:1f64}", "Pair{x:1f64,x:2f64}",
                   "Pair{z:1f64,y:2f64}", "Pair{x:true,y:2f64}",
                   "|x:Client|1f64", "|x:[Client]|1f64", "|f:Fn((),f64)|1f64",
                   "(|x:f64|x)(true)", "(||1f64)(2f64)", "[||1f64]:[Fn((),bool)]",
                   "||ambient", "|x:f64| ||x", "(1f64,).2", "true..false"]:
        add("type-lookup-errors", source, category="type_error")
    for source in ["Unknown{}", "plus(1f64,2f64)", "othermarker(1f64)",
                   "Pair{x:1f64,y:2f64}.missing", "round(2.5f64)",
                   "1f64|2f64", "1f64&2f64", "1f64<<2f64", "1f64^2f64"]:
        add("type-lookup-errors", source, category="lookup_error")
    add("opt-in-library", "sqrt(4f64)", category="lookup_error", setup="default")
    # These exact intervals come from the retained producer contracts: Rust group tokens
    # carry the entire group; its absent EOF token carries call_site, not a text offset.
    spans = {
        "[1.0f64,]": ([8,9], [0,9], "group-delimiter"),
        "(1.0f64,2.0f64,)": ([15,16], [0,16], "group-delimiter"),
        "1.0f64+": ([7,7], [0,0], "eof-sentinel"),
        "..=": ([3,3], [0,0], "eof-sentinel"),
        "1.0f64..=": ([9,9], [0,0], "eof-sentinel"),
        "||": ([2,2], [0,0], "eof-sentinel"),
        "if true {}": ([9,10], [8,10], "group-delimiter"),
        "if true {1.0f64}else{}": ([21,22], [20,22], "group-delimiter"),
        "Pair{x:1.0f64,y:2.0f64,}": ([23,24], [4,24], "group-delimiter"),
        "named(1.0f64,)": ([13,14], [5,14], "group-delimiter"),
        "1.0f64.plus(2.0f64,)": ([19,20], [11,20], "group-delimiter"),
        "1.0f64 as (f64)": ([10,11], [10,15], "group-delimiter"),
    }
    categories = {
        "[]": "type_error",
        "[1.0f64 2.0f64]": "delimiter_error", "(1.0f64 2.0f64)": "delimiter_error",
        "if true {1.0f64}else 2.0f64": "delimiter_error",
        "if true 1.0f64": "delimiter_error", "if true {1.0f64;2.0f64}": "delimiter_error",
        "(|x:f64|x)(true)": "lookup_error", "(||1.0f64)(2.0f64)": "lookup_error",
        "||ambient": "lookup_error", "|x:f64| ||x": "lookup_error",
        "true..false": "lookup_error", "Unknown{}": "type_error",
    }
    excluded_parameters = {
        "|x:Client|1.0f64": [0,10], "|x:[Client]|1.0f64": [0,12],
        "|f:Fn((),f64)|1.0f64": [0,14],
    }
    for row in rows:
        source = row["source"]
        if source in spans:
            row["span"], row["rust_span"], row["adaptation"] = spans[source]
        if source in categories:
            row["category"] = categories[source]
        if source in excluded_parameters:
            row["cpp_only"] = "excluded-parameter-type"
            row["expected_cpp"] = ["parse", *map(str, excluded_parameters[source]), "type_error"]
    for source, expected in [("2+3*4", 14), ("2f64+3", 5),
                             ("[[1,2],[3,4]][1][0]", 3), ("[4][-0]", 4)]:
        add("cpp-adaptations", source, cpp_only="literal-or-index",
            expected_cpp=["ok", "D", double(expected), ""])
    for source in ["[1][-1]", "[1][0.5]", "[1][1]", "[1][1e9999]",
                   "[1][0/0]", "[1][18446744073709551616]", "([]:[f64])[0]"]:
        add("cpp-adaptations", source, setup="index_error", cpp_only="checked-index",
            expected_cpp=["runtime", "index", ""])
    for source in ["10f64^3f64|2f64", "2f64&3f64+4f64", "1f64<<2f64*3f64",
                   "10f64>>2f64+3f64", "1f64|2f64^3f64&4f64"]:
        add("custom-precedence", source, setup="custom")
    for source in [
        "if (Pair{x:1f64,y:2f64}.x==1f64){3f64}else{4f64}",
        "(|x:Range(f64)|x)(1f64..2f64)",
        "(|x:RangeInclusive(f64)|x)(1f64..=2f64)",
        "(|x:RangeFrom(f64)|x)(1f64..)",
        "(|x:RangeTo(f64)|x)(..2f64)",
        "(|x:RangeToInclusive(f64)|x)(..=2f64)",
        "[1f64]:[Alias]", "(||Pair{x:1f64,y:2f64})()", "(||Box{f:||9f64})()",
        "[Pair{x:1f64,y:2f64}]", "[Box{f:||9f64}]",
        "([||7f64],true)", "[([||7f64],true)]",
    ]:
        add("recursive-types-and-headers", source)
    for name in ["floor", "ceil", "trunc", "round_ties_even"]:
        for sign in ["", "-"]:
            add("numeric-functions", f"{name}({sign}1f64/0f64)",
                ["D", double(-float("inf") if sign else float("inf")), ""])
    for source in ["(1.0f64,).0f64", "(1.0f64,).0e0", "(1.0f64,).0.1e0",
                   "(1.0f64,).184467440737095516160"]:
        add("raw-selector-errors", source, category="malformed_input", verbatim=True)
    for source, expected in [
        ("abs(1f64/0f64)", float("inf")), ("abs(-1f64/0f64)", float("inf")),
        ("sqrt(1f64/0f64)", float("inf")), ("signum(1f64/0f64)", 1),
        ("signum(-1f64/0f64)", -1), ("min(-1f64/0f64,1f64/0f64)", -float("inf")),
        ("max(-1f64/0f64,1f64/0f64)", float("inf")),
        ("clamp(1f64/0f64,1f64,3f64)", 3), ("clamp(-1f64/0f64,1f64,3f64)", 1),
        ("clamp(2f64,-1f64/0f64,1f64/0f64)", 2), ("fract(-0f64)", 0),
        ("5f64%(1f64/0f64)", 5), ("-0f64%2f64", -0.0),
        ("9007199254740993.0f64", 9007199254740992),
        ("1.7976931348623157e308f64", float.fromhex("0x1.fffffffffffffp+1023")),
    ]:
        add("numeric-functions", source, ["D", double(expected), ""])
    for source in ["1f64%0f64", "(1f64/0f64)%2f64"]:
        add("scalar", source, ["D", "nan", ""])
    for operator in ["<", "<=", ">", ">="]:
        add("scalar", f"(0f64/0f64){operator}1f64", ["B", "false", ""])
    add("lookup-scopes", "named", ["Marker:named", "marker", ""])
    add("lookup-scopes", "othermarker", ["Marker:secondary", "marker", ""])
    for source in ["[(),()]", "[Empty{},Empty{}]", "[]:[()]", "[]:[Empty]",
                   "[(1f64,true),(2f64,false)]"]:
        add("structural", source)
    add("malformed-syntax", "", category="malformed_input", span=[0,0])
    add("malformed-syntax", "   ", category="malformed_input", span=[3,3],
        rust_span=[0,0], adaptation="eof-sentinel")
    return rows
