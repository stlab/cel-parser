# Continue after parser P4 {#parser_p4_handoff}

P4 adds public differential tooling, a cached decoded-expression client,
installed clients, separate execution measurements, and explicit provisioned
CI inputs. It does not publish packages or establish overall port completion.
The controller owns independent task and final whole-port reviews.

## Feature closure

The maintained census remains the authoritative signature inventory.
Every retained row has public emission/contract evidence; the fixture families
below add comparisons without claiming exhaustive enumeration of infinite types.
`tools/parity/fixtures.py` gives each exact source and expected adapter.

| Criterion | Status | Strongest exact evidence | Confidence / gap |
| --- | --- | --- | --- |
| All 34 eager retained signatures | Differential plus public contracts | `scalar`, `numeric-functions`, `ranges`; census rows link `lookup_test.cpp` operator/range/cast/standard cases | High; no removed scalar surrogate |
| Eleven numeric names; ordinary/member selectors; IEEE edges | Differential plus public contracts | `numeric-functions`, `numeric-errors`; `standard numeric names require explicit installation and use source call selectors` | High; NaN payload/equal-zero choices only |
| Unit/group/singleton/multi tuples; exact selectors | Differential | `structural`, `raw-selector-errors`; `tuple selectors use retained raw digits rather than double conversion` | High; selector conversion defect corrected in Rust |
| Recursive homogeneous arrays and complete annotations | Differential plus general runtime contracts | `structural`, `recursive-types-and-headers`, `exact-diagnostics`; tuple/array/type tests | High; explicit safe Rust witness set |
| Six range forms and endpoint order | Differential | `ranges`, `recursive-types-and-headers`; `all six range forms preserve complete type and endpoint order` | High; no iteration claim |
| Records, permutation, projection, schema errors | Differential plus owner contracts | `records`, `custom-precedence`, `type-lookup-errors`; `nominal records and arbitrary nested registration use public runtime emitters` | High; native host layout intentionally adapted |
| Registered values/functions/operations; first marker; transient/persistent LIFO | Differential plus public selectors | `lookup-scopes`, `custom-precedence`, `opt-in-library`; lookup scope/marker cases | High; library-only selectors independently covered |
| Independent closure scopes and explicit complete parameters | Differential plus public contracts | `lookup-scopes`, `closures-complete-results`, `type-lookup-errors`; Fn/type/expression cases | High; excluded parameter types explicit |
| Lazy AND/OR, every conditional form, chosen effects/errors | Differential | `lazy-conditional`, `selected-exceptions`; runtime dependent-body contracts | High; unselected callbacks do not run |
| Any-valid complete results: leaves, owning/empty composites, Fn fields/higher-order Fn | Differential plus recursive public contracts | `closures-complete-results`, `recursive-types-and-headers`; Rust callable-array test, runtime callable/structural tests | High; representatives and recursive laws, not all possible types |
| Compiler lifetime, cached repeats, prefix/exception cleanup | Differential plus independent prefix tests | Both drivers repeat after destruction; `cached_test.cpp`; runtime rollback/reentry tests | High; Rust safe API lacks identical caller-prefix access |
| Casts, strict separators, reserved alternatives, exact projection/annotation spans | Differential | `malformed-syntax`, `exact-diagnostics`, `raw-selector-errors`, `type-lookup-errors` | High; explicit group/EOF adaptations only |
| C++ category enum; selected runtime failure kinds | Differential boundary plus C++ own contract | Harness `compare`; per-row categories; typed callback adapter and source-defined clamp error | High; no matching Rust enum exists |
| Text/Unicode/doc/numeric spelling and resource bounds | Independent public C++ adaptation tests | `tests.cpp`, recursive-expression resource case, compile-fail cursor/parser fixtures | High locally; no false proc-macro text-lexer equivalence |
| Bracket indexing and finite integral bounds | Explicit C++ adaptation | `cpp-adaptations`; `explicit C++ array indexing validates before structural access`; runtime index/prefix cases | High; Rust has no bracket-postfix syntax |
| Removed integers/f32/strings, bitwise/casts/helpers, wrong nominal types | Excluded with public rejection tests | Census removed rows; lexer/type/lookup/expression tests; `type-lookup-errors` | High; no invented conversions/names |
| Ownership binding, resource overflow, allocation rollback, callable exclusion/reentry | Equivalent public runtime contract paths | Runtime parity ledger and callable/composite/resource tests; typed empty Fn fixtures | High; not relabeled as differential raw-storage tests |
| Installed headers/artifacts and decoded cache/composites/callables | Package-only clients | `installed_consumer` and its five nested clients | High locally; no remote package publication |
| Allocation/timing boundaries | Separate Release passes | `measurement` CSV and `measurement_contracts` | Local only; not portable speed or leak claims |
| Required CI inputs and fail-closed local parity | Configuration plus negative checks | `provisioned-validation.yml`, CMake parity requirements, harness tests | Local only; runner/hosting and remote execution pending |

## Corrections and remaining boundaries

P4 corrected C++ record field-type/unknown-constructor spans with focused
regressions. It also corrected Rust malformed tuple selector conversion spans
at `1516e45f0799927043352ffeeb2b13d58ac66526`; it did not normalize the defect
away. See [validation](validation.md) for exact adapters and driver contracts.

P4 used runtime prerequisite
`bf2d2ac9fe420139b85d1adec9d57c1565356e32` and changed only its documentation.
The final integrated wave subsequently corrects executable retirement and
higher-order result ownership. Validation must consume the corrected normal
and instrumented runtime installations; the session's `final-fix-wave-report.md`
identifies their revisions and inputs. Ordinary named scalar calls allocate
owning marker/result intermediates; direct scalar dispatch measurements remain
separate. Native owning composites and dynamic invocation retain their costs.

The session's `parser-task-4-report.md` records final revisions, exact local
commands/results, RED/GREEN evidence, installed prefix checks, and measurements.
Actual GCC, MSVC, macOS LSan, remote CI, hosting, and independent reviews are
not established by AppleClang/local evidence. Repeat the validation commands
when changing a contract or reference revision.

## Local P4 evidence

AppleClang 21 on arm64 macOS passes all 18 parser CTests in Debug, Release,
matching ASan+UBSan, and clang-tidy configurations. Each run includes six
negative-compilation gates, actual differential execution, and installed
consumption. The differential run passes 275 shared fixtures and 14 explicit
C++ adaptations across 19 fixture families. A separate real combined
installation passes all five consumer tests. The Rust driver passes three
tests, formatting, and all-target clippy with warnings denied. Both strict
Doxygen targets pass with `WARN_AS_ERROR=YES`.

Release uses `-O3 -DNDEBUG`, C++20, and no fast-math. One representative warmed
snapshot records these separate passes; it is not a statistical benchmark:

| Workload | Iterations per pass | Total timing ns | Allocation calls |
| --- | --- | --- | --- |
| Direct prepared scalar | 100000 | 23137541 | 0 |
| Named scalar with owning intermediate | 100000 | 49010167 | 400000 |
| Prepared native nested array | 100000 | 63364791 | 1300000 |
| Erased owning nested array | 100000 | 61291250 | 1100000 |
| Dynamic callable | 100000 | 27485916 | 400000 |

See [measurement boundaries](validation.md) for warmup, reservation, result
checks, and the bounded measurement-target analyzer exclusion. Missing source,
disabled tests, absent drivers/Cargo, and disabled Python assertions all fail
explicitly in local negative checks. Generated and manual remote workflows
remain unexecuted; GCC/MSVC and macOS LSan remain unavailable.

## Final integrated corrections

The oracle now checks every successful cached trace, restricts execution-error
normalization to selected source witnesses, and validates complete response
schemas before comparison or adaptations. `parity_driver_test.cpp`, Rust
reference tests, and comparator negatives cover previously hidden failures.
This wave adds one parser CTest and two Rust driver tests; the historical P4
measurements and evidence remain unchanged. Correction-scoped approval remains
the controller's responsibility.
