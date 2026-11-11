"""Maintains authored exact-source fixtures without evaluating or rewriting CEL."""
import struct

from core_corpus import core_corpus


def double(value):
    """Encodes independently specified binary64 values, preserving zero signs."""
    return struct.pack(">d", value).hex()


def fixtures():
    """Returns core grammar cases and explicitly corresponding client extension fixtures.

    Complexity: O(corpus size) time and result space.
    """
    rows = []

    def add(family, source, expected=None, setup="client", **options):
        """Appends one uniquely keyed authored source and its observation contract."""
        row = {"id": f"{family}-{len(rows)}", "family": family,
               "source": source, "setup": setup, **options}
        if expected is not None:
            row["expected"] = expected
        rows.append(row)

    for source, expected in [
        ("1.0f64+2.0f64*3.0f64", 7), ("10.0f64-3.0f64-2.0f64", 5),
        ("20.0f64/2.0f64/2.0f64", 5), ("-2.0f64 as f64*3.0f64", -6),
        ("1.0f64 as f64 as f64+2.0f64", 3), ("-5.0f64%2.0f64", -1),
        ("-0.0f64", -0.0), ("1.0f64/0.0f64", float("inf")),
        ("-1.0f64/0.0f64", -float("inf")), ("1e-9999f64", 0),
        ("5e-324f64", 5e-324), ("1e9999f64", float("inf")),
        ("5.0f64%(1.0f64/0.0f64)", 5), ("-0.0f64%2.0f64", -0.0),
        ("9007199254740993.0f64", 9007199254740992),
        ("1.7976931348623157e308f64", float.fromhex("0x1.fffffffffffffp+1023")),
    ]:
        add("scalar", source, ["D", double(expected), ""], setup="core")
    for source in ["0.0f64/0.0f64", "1.0f64%0.0f64", "(1.0f64/0.0f64)%2.0f64"]:
        add("scalar", source, ["D", "nan", ""], setup="core")
    add("trace-scalar-boundaries", "tap(0.0f64/0.0f64)", ["D", "nan", "nan"])
    add("trace-scalar-boundaries", "tap(1.0f64/0.0f64)",
        ["D", double(float("inf")), double(float("inf"))])
    for source in ["!false", "true as bool==!false", "1.0f64<2.0f64", "2.0f64>1.0f64",
                   "1.0f64<=1.0f64", "1.0f64>=1.0f64", "1.0f64!=2.0f64", "true!=false",
                   "0.0f64==-0.0f64", "!(0.0f64/0.0f64==0.0f64/0.0f64)"]:
        add("scalar", source, ["B", "true", ""], setup="core")
    for operator in ["<", "<=", ">", ">="]:
        add("scalar", f"(0.0f64/0.0f64){operator}1.0f64", ["B", "false", ""], setup="core")
    for source in ["()", "(1.0f64)", "(1.0f64,)", "(true,2.0f64,())",
                   "((),(true,),[[1.0f64,2.0f64],[3.0f64]])", "[]:[f64]",
                   "[1.0f64,2.0f64]:[f64]", "[[]:[bool]]", "[true,false]:[bool]",
                   "((1.0f64,2.0f64),).0.1", "(1.0f64,2.0f64).0x1",
                   "(1.0f64,2.0f64).0b1", "(1.0f64,2.0f64).0o1",
                   "(1.0f64,2.0f64).0_1", "((1.0f64,2.0f64),).0_0.0_1",
                   "[(),()]", "[]:[()]", "[(1.0f64,true),(2.0f64,false)]"]:
        add("structural", source, setup="core")
    for source in ["1.0f64+2.0f64..4.0f64*2.0f64", "1.0f64..=3.0f64", "1.0f64..",
                   "..3.0f64", "..=3.0f64", "..", "(|r:RangeFull|r)(..)"]:
        add("ranges", source, setup="core")
    for source in ["Pair{y:tap(2.0f64),x:tap(1.0f64)}", "Pair{x:1.0f64,y:2.0f64}.x",
                   "Pair{y:2.0f64,x:1.0f64}.y", "Empty{}", "Pair{x:1.0f64,y:2.0f64}.x.plus(3.0f64)",
                   "(|p:Pair|p.x)(Pair{x:1.0f64,y:2.0f64})",
                   "[Empty{},Empty{}]", "[]:[Empty]"]:
        add("records", source)
    for source in ["Pair{x:1.0f64,y:2.0f64}.x", "(2.0f64)(3.0f64)", "1.0f64|2.0f64*3.0f64",
                   "10.0f64^3.0f64|2.0f64", "2.0f64&3.0f64+4.0f64", "1.0f64<<2.0f64*3.0f64",
                   "10.0f64>>2.0f64+3.0f64", "1.0f64|2.0f64^3.0f64&4.0f64"]:
        add("custom-precedence", source, setup="custom")
    for source in ["named(2.0f64)", "1.0f64.named()", "1.0f64.plus(2.0f64)",
                   "ambient", "shadow", "(|| library)()", "(|| shadow)()",
                   "(|ambient:f64|ambient)(9.0f64)", "if true {ambient}else{0.0f64}"]:
        add("lookup-scopes", source)
    add("lookup-scopes", "named", ["Marker:named", "marker", ""])
    add("lookup-scopes", "othermarker", ["Marker:secondary", "marker", ""])
    for source in ["hit(1.0f64)||explode()", "false&&explode()",
                   "hit(1.0f64)&&hit(2.0f64)", "hit(-1.0f64)||hit(2.0f64)",
                   "if true {hit(1.0f64)}else{explode()}",
                   "if false {explode()}else if true {hit(2.0f64)}else{false}",
                   "if true {()}", "if false {()}", "if false {()}else{()}"]:
        add("lazy-conditional", source)
    for source in ["true&&explode()", "false||explode()",
                   "((),if true {explode()}else{false})"]:
        add("selected-exceptions", source, setup="callback_error", status="runtime")
    for source in ["||7.0f64", "|x:f64,y:bool|if y{x+1.0f64}else{0.0f64}",
                   "(|x:f64,y:bool|if y{x+1.0f64}else{0.0f64})(4.0f64,true)",
                   "(|x:((f64,bool),[f64])|x.0.0)(((1.0f64,true),[2.0f64]))",
                   "(|x:()|x)(())", "(||leaf())()", "(||[leaf()])()",
                   "[leaf()]:[Client]", "[]:[Client]", "(|| ||7.0f64)()()",
                   "(||[||7.0f64])()", "(if true {||7.0f64}else{||8.0f64})()",
                   "[||7.0f64]:[Fn((),f64)]", "(Box{f:||9.0f64}.f)()",
                   "Box{f:||9.0f64}.f()()", "[]:[Fn((),f64)]",
                   "[]:[([Fn((),f64)],bool)]", "[]:[Box]",
                   "(||[]:[Fn((),f64)])()", "[]:[Fn((),Fn((),f64))]",
                   "||leaf()", "||[leaf()]", "|| ||7.0f64"]:
        add("closures-complete-results", source)
    for source, category, span in [
        ("missing", "lookup_error", [0,7]), ("(true).0", "type_error", [0,8]),
        ("(1.0f64,).2", "type_error", [0,11]), ("((1.0f64,),).0.2", "type_error", [0,16]),
        ("[]:(f64)", "type_error", [3,8]), ("[true]:[f64]", "type_error", [0,12]),
        ("[true]:([f64])   ", "type_error", [0,14]), ("[]:((f64,[bool]))   ", "type_error", [3,17]),
        ("[[]:[bool]]:([[f64]])   ", "type_error", [0,21]),
    ]:
        add("exact-diagnostics", source, category=category, span=span)
    for source in ["[]", "[1.0f64,]", "(1.0f64,2.0f64,)", "(,)", "[,]", "[1.0f64 2.0f64]",
                   "(1.0f64 2.0f64)", "else", "as", "1.0f64+", "1.0f64 2.0f64",
                   "..=", "1.0f64..=", "..else", "1.0f64..else", "..as", "1.0f64..as",
                   "| |1.0f64", "|x:f64,|1.0f64", "||", "|x f64|x",
                   "|x:f64,x:f64|x", "|if:f64|1.0f64", "|else:f64|1.0f64",
                   "|as:f64|1.0f64", "|true:f64|1.0f64", "if true {}",
                   "if true {1.0f64}else{}", "if true {1.0f64}else 2.0f64",
                   "if true 1.0f64", "if true {1.0f64;2.0f64}",
                   "Pair{x:1.0f64,y:2.0f64,}", "named(1.0f64,)", "1.0f64.plus(2.0f64,)",
                   "1.0f64<2.0f64<3.0f64", "+1.0f64", "1.0f64 as (f64)"]:
        add("malformed-syntax", source, category="malformed_input")
    for source in ["true as f64", "1.0f64 as bool", "false&&1.0f64",
                   "if true {1.0f64}", "if true {1.0f64}else{false}", "if 1.0f64 {()}",
                   "[true]:[f64]", "[1.0f64,true]", "Pair{x:1.0f64}", "Pair{x:1.0f64,x:2.0f64}",
                   "Pair{z:1.0f64,y:2.0f64}", "Pair{x:true,y:2.0f64}",
                   "(|x:f64|x)(true)", "(||1.0f64)(2.0f64)", "[||1.0f64]:[Fn((),bool)]",
                   "||ambient", "|x:f64| ||x", "(1.0f64,).2", "true..false"]:
        add("type-lookup-errors", source, category="type_error")
    for source in ["Unknown{}", "plus(1.0f64,2.0f64)", "othermarker(1.0f64)",
                   "Pair{x:1.0f64,y:2.0f64}.missing", "round(2.5f64)",
                   "1.0f64|2.0f64", "1.0f64&2.0f64", "1.0f64<<2.0f64", "1.0f64^2.0f64"]:
        add("type-lookup-errors", source, category="lookup_error")
    for source in ["sqrt(4.0f64)", "abs(-2.0f64)", "tap(1.0f64)", "Pair{x:1.0f64,y:2.0f64}"]:
        add("core-no-extensions", source, setup="core", cpp_reject="unregistered client names")
    # [knowledge] Rust reports whole group tokens and a call-site EOF sentinel.
    spans = {
        "[1.0f64,]": ([8,9], [0,9], "group-delimiter"),
        "(1.0f64,2.0f64,)": ([15,16], [0,16], "group-delimiter"),
        "1.0f64+": ([7,7], [0,0], "eof-sentinel"), "..=": ([3,3], [0,0], "eof-sentinel"),
        "1.0f64..=": ([9,9], [0,0], "eof-sentinel"), "||": ([2,2], [0,0], "eof-sentinel"),
        "if true {}": ([9,10], [8,10], "group-delimiter"),
        "if true {1.0f64}else{}": ([21,22], [20,22], "group-delimiter"),
        "Pair{x:1.0f64,y:2.0f64,}": ([23,24], [4,24], "group-delimiter"),
        "named(1.0f64,)": ([13,14], [5,14], "group-delimiter"),
        "1.0f64.plus(2.0f64,)": ([19,20], [11,20], "group-delimiter"),
        "1.0f64 as (f64)": ([10,11], [10,15], "group-delimiter"),
    }
    categories = {
        "[]": "type_error", "[1.0f64 2.0f64]": "delimiter_error", "(1.0f64 2.0f64)": "delimiter_error",
        "if true {1.0f64}else 2.0f64": "delimiter_error", "if true 1.0f64": "delimiter_error",
        "if true {1.0f64;2.0f64}": "delimiter_error", "(|x:f64|x)(true)": "lookup_error",
        "(||1.0f64)(2.0f64)": "lookup_error", "||ambient": "lookup_error",
        "|x:f64| ||x": "lookup_error", "true..false": "lookup_error", "Unknown{}": "type_error",
    }
    for row in rows:
        source = row["source"]
        if source in spans:
            row["span"], row["rust_span"], row["adaptation"] = spans[source]
        if source in categories:
            row["category"] = categories[source]
    for source, span in [
        ("|x:Client|1.0f64", [0,10]), ("|x:[Client]|1.0f64", [0,12]),
        ("|f:Fn((),f64)|1.0f64", [0,14]),
    ]:
        add("excluded-parameters", source, cpp_reject="unsupported parameter type",
            expected_cpp=["parse", *map(str, span), "type_error"])
    for source in ["2+3*4", "2f64+3", "[[1,2],[3,4]][1][0]", "[4][-0]",
                   "[1][-1]", "[1][0.5]", "[1][1]", "[1][1e9999]",
                   "[1][0/0]", "[1][18446744073709551616]", "([]:[f64])[0]",
                   "[1.0f64][0]", "[1.0f64][0.0f64]"]:
        add("excluded-integer-index", source, setup="core",
            cpp_reject="integer values and postfix indexing are outside core")
    for source in [
        "if (Pair{x:1.0f64,y:2.0f64}.x==1.0f64){3.0f64}else{4.0f64}",
        "(|x:Range(f64)|x)(1.0f64..2.0f64)", "(|x:RangeInclusive(f64)|x)(1.0f64..=2.0f64)",
        "(|x:RangeFrom(f64)|x)(1.0f64..)", "(|x:RangeTo(f64)|x)(..2.0f64)",
        "(|x:RangeToInclusive(f64)|x)(..=2.0f64)",
        "[1.0f64]:[Alias]", "(||Pair{x:1.0f64,y:2.0f64})()", "(||Box{f:||9.0f64})()",
        "[Pair{x:1.0f64,y:2.0f64}]", "[Box{f:||9.0f64}]",
        "([||7.0f64],true)", "[([||7.0f64],true)]",
    ]:
        add("recursive-types-and-headers", source)
    for source in ["(1.0f64,).0f64", "(1.0f64,).0e0", "(1.0f64,).0.1e0",
                   "(1.0f64,).184467440737095516160"]:
        add("raw-selector-errors", source, category="malformed_input", setup="core")
    add("malformed-syntax", "", category="malformed_input", span=[0,0], setup="core")
    add("malformed-syntax", "   ", category="malformed_input", span=[3,3],
        rust_span=[0,0], adaptation="eof-sentinel", setup="core")
    rows.extend(core_corpus())
    return rows
