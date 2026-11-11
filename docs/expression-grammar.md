# Expression grammar and emission {#expression_grammar}

This reference lets C++ clients compile the retained CEL language and lets
maintainers compare its productions with executable Rust consumers. The C++ core
must accept only expressions accepted by Rust with corresponding bindings.
See [exact-source validation](validation.md) for the differential gate.

The source baseline is Rust commit
`03d658f8b12acabe1d6a2503281e7c86cf00b6a3`. All Rust paths in the table are
relative to `cel-parser/src/`; line numbers identify executable source, not
stale index-outline offsets. The P2 [built-in census](builtin-census.md)
defines the retained bool/f64 signatures.

## Production inventory

Every row below is implemented in `src/compiler.cpp` through checked runtime
builders and private lookup dispatch. Token and prefix compilation entry points
used by the contract tests are internal, not installed client interfaces.
The contract-test map identifies its evidence.
The following productions choose alternatives from the current token:

| Production, loose to tight | Syntax and selector | Rust consumer | C++ emission |
| --- | --- | --- | --- |
| `expression` | Leading `\|\|` or `\|` selects closure; otherwise range | `lib.rs:1308–1319` | Independent body, `push_closure` |
| `range_expression` | `or [ (".." [or]) \| ("..=" or) ]`, or leading `.. [or]`, or `..= or`; one range only | `lib.rs:1420–1516` | Lookup `range`, `range_inclusive`, `range_from`, `range_to`, `range_to_inclusive`, `range_full` |
| `or_expression` | `and { "\|\|" and }`, left-associated | `lib.rs:1322–1348` | Dependent RHS at `before_top()`, `logical_or` |
| `and_expression` | `comparison { "&&" comparison }`, left-associated | `lib.rs:1351–1377` | Dependent RHS, `logical_and` |
| `comparison_expression` | `bitwise_or [ ("==" \| "!=" \| "<" \| ">" \| "<=" \| ">=") bitwise_or ]`; no chains | `lib.rs:1380–1417` | Exact lookup, two operands |
| Bitwise precedence | OR, XOR, AND, shifts, each iterative and left-associated | `lib.rs:1518–1615` | Exact lookup; no built-in bool/f64 signature, clients may register exact operations |
| `additive_expression` | `multiplicative { ("+" \| "-") multiplicative }` | `lib.rs:1618–1650` | Exact lookup |
| `multiplicative_expression` | `cast { ("*" \| "/" \| "%") cast }` | `lib.rs:1652–1687` | Exact lookup |
| `cast_expression` | `unary { "as" identifier }`, left-associated; literal target spelling | `lib.rs:1688–1717` | `apply_cast`, not type parsing |
| `unary_expression` | `("-" \| "!") unary \| postfix`; no unary plus | `lib.rs:1719–1744` | Reverse prefix emission after iterative token collection |
| `postfix_expression` | `primary { arguments \| "." (identifier [arguments] \| selector) }` | `lib.rs:1794–1876` | Ordinary `"()"` counts callee plus arguments; member counts receiver plus arguments |
| Tuple selector | Unsuffixed integer with radix/underscores, or decimal token split on dot for `.0.1`; exponent text/suffixes cannot become indices | `lib.rs:1828–1870` | Checked raw-spelling integer conversion, `project_tuple`; never convert the f64 value |
| `primary_expression` | Literal, identifier, `if`, `(`, `[`; `else`/`as` are not value names | `lib.rs:1954–2003` | `just`, zero-argument lookup, conditional/aggregate productions |
| `record_fields` | `Name "{" [ identifier ":" expression { "," identifier ":" expression } ] "}"` | `lib.rs:2006–2071` | Complete nominal schema, source-order evaluation, schema permutation, `make_record` |
| `tuple_or_group` | `()` unit; `(expression)` grouping; `(expression,)` singleton; multi-item tuple forbids trailing comma | `lib.rs:2075–2160` | `make_tuple`, except grouping passes through |
| `array_expression` | `"[" [expression { "," expression }] "]" [ ":" type_expression ]`; annotation names the whole array type; empty needs annotation | `lib.rs:2182–2247`, `type_expr.rs:643–657` | `make_array`, actual nonempty witness or bound empty annotation |
| `closure_expression` | `\|\| expression` or `\| name ":" type { "," name ":" type } \| expression` | `lib.rs:2416–2491` | Explicit positional reads, isolated transients, complete independent body, `push_closure` |
| `if_expression` | `if expression { expression } [else ({ expression } \| if_expression)]` | `lib.rs:2507–2582` | Dependent `if_else`; missing else emits unit |
| `ordinary_expression` | Explicit delimiter/argument/field context restores record literals in condition headers | `lib.rs:917–938` | Temporary header restriction |
| `type_expression` | Existing P2 complete prefix type production | `lib.rs:2252–2259` | `parse_type`; semantic declarations are not owners |

Every consumed list comma requires another item, except the required comma of a
singleton tuple. Numeric values require floating syntax; integer literals are
not converted to f64. Postfix brackets are not a core expression alternative.
The executable closure consumer requires a parameter after a
single opening `|`; use `||` for zero parameters. It does not accept a separated
empty `| |` even though an older prose production displays an optional list.
An identifier after `..` commits to endpoint parsing; reserved `else`/`as`
tokens are malformed endpoints, not an absent endpoint followed by a host token.

## Scope and ownership laws

Rust ordinary and member expression dispatch select all eligible scopes
(`parser_context.rs:827–857`). Ordinary `"()"` includes the callee/marker;
members are direct receiver operations. `library_only` is an internal lookup
selector, not invented expression syntax. An independent closure temporarily
isolates every enclosing transient scope and installs only its own positional
parameters (`lib.rs:2470–2488`). Persistent libraries/registrations remain
visible. Parameter names cannot be reserved or duplicated
(`ast.rs:395–415`); parameters obey the runtime's bool/f64 composite admission,
while results admit any valid complete type.

Arrays and records preserve complete nominal types and actual callable owners.
An empty typed aggregate binds its declaration through
`bind_runtime_ownership`; binding creates capability metadata, not callable
bodies or initialized bytes. A populated aggregate retains its actual operand
witness after validating a semantic annotation. Both branches type-check, but
only the selected branch executes (`parser_context.rs:874–918`).

An `if` condition excludes unparenthesized record literals so its next `{`
selects the body. Explicit groups, arguments, array elements, and record fields
lift that restriction. Bodies contain exactly one expression, not statements,
implicit ambient captures, or general block expressions.

## Verification disposition

The following cases in `tests/expression_test.cpp` cover the implementation:

| Case-name prefix | Production and boundary coverage |
| --- | --- |
| `retained precedence` | Arithmetic, all comparisons, casts, unary operators, IEEE numbers, unsupported signatures |
| `numeric values require` | Integer/suffix/radix value rejection, valid floating values, retained integer tuple selectors |
| `core lookup does not` | Standard-library function names are absent |
| `tuple group unit` | All group/tuple/unit alternatives, nested arrays, whole-array annotations, strict separators |
| `tuple selectors` | Integer radices/underscores, decimal-token splitting, suffix/exponent/overflow rejection |
| `postfix brackets remain` | Full-method rejection and unconsumed host brackets in prefix parsing |
| `complete expressions reject` | Nested and typed-array bracket suffix rejection |
| `all six range forms` | Complete form identity, endpoint order, low precedence, malformed inclusive forms |
| `lazy branches` | Both lazy operators, conditionals/else-if/implicit unit, no compile-time callback execution, selected exceptions |
| `registered names` | First marker, receiver operations, source-order nominal fields and canonical permutation |
| `closures admit complete` | Full parameters, any valid results, higher-order/callable arrays, bound empty typed composites |
| `independent closure scope` | Persistent libraries, hidden ambient/enclosing parameters, success/failure restoration |
| `semantic record parameters` | Nominal formals, custom-member precedence, registered operators, closed cast targets |
| `client emitter exceptions` | Original emitter exceptions, recursive parser exclusion, reusable parser after failure |
| `compiled values own` | Parser-destruction survival, owned move-only captures, repeated callable results, caller prefixes |
| `text and token methods` | Both full methods, inline host preservation, producer diagnostics, visible/invisible malformed groups, exact spans |
| `recursive input` | Bounded nesting, long arithmetic/unary/cast/member/lazy/list chains |

`tests/type_test.cpp` additionally verifies exact span-output prefix parsing,
and `tests/installed_consumer/compiler.cpp` exercises the public installed
compiler and runtime together.

The differential fixtures cover retained production families with identical
sources. Narrower scalar/parameter availability, canonical record carriers,
C++ exceptions, and byte-based text spans do not authorize additional accepted
syntax. The parser supplies no standard-library functions, implicit numeric
coercions, floating range iteration, or universal aggregate equality.

## Compilation and lifetime contract

`parser` owns its lookup and requires exclusive compilation access. Recursive
entry from a selected emitter throws `logic_error` before changing parser state.
Temporary closure/header/nesting state restores on successful and exceptional
exits. Failed parsing may advance a borrowed host cursor; it never rewinds.
Compiled code retains required values, code, descriptors, and callback captures;
referenced client objects and capture synchronization remain client obligations.

Compilation traverses tokens once, plus emitted operations and existing
lookup/type/structural metadata work. Hash maps perform expected linear
parameter/field-name work; bound ownership costs O(distinct descriptor nodes,
edges, and copied name bytes). The compiler does not promise linear total work
when client emitters or repeated complete-type comparisons do more.
Temporary nesting is bounded; code/captures, flat unary prefixes, and field/list
metadata use storage proportional to their actual input.

Source diagnostics preserve producer categories/messages/spans. Member lookup
errors identify the member name. Malformed raw tuple selectors identify their
selector token, including failed conversions of a maximally munched dotted
token. Record field type mismatches identify the field name; unknown constructors
and incomplete schemas identify the complete record expression. Other structural
validation identifies the complete expression interval, and non-array annotation
errors identify the complete parsed type interval. P4 corrects the original Rust
selector conversion span loss at `1516e45f0799927043352ffeeb2b13d58ac66526`.
The internal span-output `parse_type` overloads provide exact consumed intervals,
excluding following whitespace/host tokens. Selected client-emitter failures
are not caught by structural diagnostic translation. Evaluation uses frozen
runtime code, never a retained parser or an expression AST interpreter.
