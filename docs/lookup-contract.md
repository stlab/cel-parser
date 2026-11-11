# Lookup and complete types {#lookup_contract}

This reference helps library clients register owned operations and parse complete
types through `cel::parser`. See the [expression grammar](expression-grammar.md)
for expression syntax and compilation.

`op_lookup` supplies intrinsic operators, identity casts, and full-range
construction, not a standard library. Separate supplied libraries must match
their Rust counterparts. Corresponding client bindings use the same names,
argument/result types, and semantics.

## Ownership and exact registration

`op_lookup` owns names, record schemas, scope callbacks, and operation emitters.
`register_type<T>` admits trivially-copyable opaque leaves. Standard vectors and
tuples in typed operations use the runtime's trusted concrete adapters; they
aren't registered as opaque native layouts. `register_record` creates canonical
nominal descriptors with complete source-ordered field names and types.
It doesn't infer a native struct layout.

`register_op<R, Args...>` accepts only callbacks that the runtime admits at the
exact declared signature. It retains callback state in an immutable shared
capture so move-only callback objects also work. Use `std::tuple<>`, not `void`,
for source-unit results. Captured references remain your responsibility.

`register_operation` accepts complete descriptors and a public runtime emitter.
Use this interface for nominal records and other runtime-described shapes
without a concrete native adapter. Successful emission must consume the declared
inputs and add one ownership-compatible result. `register_structural_op` provides
the lower-priority structural tier with the same complete-identity contract.
Neither interface inspects native layout or boxes scalars during execution.

Duplicate complete operand signatures are errors, including overloads that
change only the result type. Disjoint arities, nominal leaves, recursive shapes,
and ownership representations remain distinct. Equal layout and display names
never establish compatibility. Core type names, including removed scalar names,
are reserved against client replacement.

`register_op` retains its runtime callback in emitted code after the lookup dies.
Custom scope callbacks and operation emitters must retain the values, descriptors,
and callback captures their emitted code needs. Storing an emitter in the lookup
doesn't automatically retain that emitter's captures in a compiled program.
Captured references must outlive every callback or execution that uses them.
Sharing capture ownership doesn't make mutable callback state thread-safe.
An allocating comparison or a selected emitter's failure propagates; it never
becomes a no-match. An emission failure leaves the builder safely discardable,
not necessarily unchanged.
Installed callable dispatch compares every complete parameter before invoking
the public emitter; an arity or type mismatch reports a source-located no-match
without changing the builder.

## Selectors and scopes

Ordinary dispatch follows this order:

1. Search eligible transient and library scopes in LIFO order.
2. Resolve zero-operand persistent function names or marker-qualified `"()"` calls.
3. Match persistent operation signatures.
4. Match structural signatures, then core built-ins.
5. Invoke an installed callable for `"()"` if its complete signature matches.

`register_function(name, marker)` supplies a bare ordinary function value.
Its trivially-copyable marker must materialize as the same opaque leaf through
the public runtime adapter. Source unit is a structural value, not a leaf alias
or function marker.
Installers retain the first marker for each name. Another marker may register
that same name, but it doesn't replace the first; a marker type cannot name a
different function. Pair the name with `register_op` overloads. A bare operation
registration alone doesn't create an ordinary function value.
Failed marker registration leaves existing bindings and marker reservations
unchanged, including when an allocation fails.

Ordinary `"()"` dispatch counts the marker or callee plus explicit arguments.
Marker calls select direct operations by their bound name, retire the marker,
and preserve any lower prefix. Zero-explicit-argument calls skip direct value
scopes because those scopes cannot distinguish a binding from a zero-argument
operation.

`lookup_selector::member` counts the receiver plus explicit arguments. It uses
operation-only dispatch, not bare function lookup or callable invocation.
Scopes may handle nonzero-arity direct operations before registrations.
Canonical named record projection follows custom operation, structural, and
built-in precedence as a fallback. Projection uses the registered record schema,
not a native field layout.

The matching Rust registration includes both the record schema and explicit
unary field-accessor operations. A Rust schema alone does not install accessors.

<!-- [knowledge]
Preserve schema-based record projection as the C++ carrier adaptation of Rust's
typed member-operation path; do not infer native layouts from member names.
-->

A declined scope callback must leave emitted instructions and operands unchanged.
Scope callbacks and descriptor emitters can query the lookup, but they must not
modify its registrations or scope sets during resolution.
`push_scope` owns a transient scope. `push_library_scope` owns a persistent
scope and requires no active transient scopes. `pop_scope` requires an active
transient scope. `isolate_scopes` transfers transient scopes out;
`restore_scopes` replaces the current transient set with the saved set.
Persistent library scopes and registrations remain available throughout.
`scope_selection::library_only` selects the global library environment without
transferring scopes. Resolve closure parameters separately before this lookup.
Alternatively, isolate enclosing scopes, push closure-local parameter scopes,
and select `all` for the independent body.

## Type grammar

The parser uses the following current-token production:

```text
type_expression = identifier
                    [ "(" [ type_expression { "," type_expression } ] ")" ]
                | "[" type_expression "]"
                | "(" [ type_expression
                    [ "," [ type_expression { "," type_expression } ] ] ] ")".
```

`()` is unit, `(T)` is grouping, `(T,)` is a singleton tuple, and `(T, U)` is a
two-element tuple. Consuming a comma requires another item except for the required
singleton comma. There are no optional trailing commas.

`bool`, `f64`, registered leaves, and registered records resolve as named types.
Arrays and tuples compose these types recursively, including client leaves.
`Range(f64)`, `RangeInclusive(f64)`, `RangeFrom(f64)`, `RangeTo(f64)`,
`RangeToInclusive(f64)`, and `RangeFull()` retain source range forms without
floating iteration rules. Empty generic argument lists have the same resolved
meaning as bare names when the corresponding zero-argument type exists.

`Fn((A, B), R)` requires a tuple of parameter types and one complete result.
`Fn((), R)` has no parameters; `Fn((A,), R)` has one. Parameters admit bool/f64
and recursively built-in-leaf composites, including complete ranges and nominal
records with eligible fields. Client leaves and callables cannot occur inside
parameters. Results admit any valid complete type, including client leaves,
records, arrays containing callables, and further callable signatures.
Parsing a callable signature doesn't install code ownership.
Cast syntax consumes one identifier rather than this complete type production.
`apply_cast` recognizes only `bool` and `f64`, not registered aliases.

The token-cursor and owned-text-lexer overloads consume one prefix and leave
its following host token. The returned descriptor owns its metadata and survives
the cursor, tokens, source, and lookup. Parsing never rewinds; a failed parse may
advance the cursor. The lexer overload doesn't materialize a separate token array.
The `parsed_span` overloads publish the first consumed token's `begin` through the
last consumed token's `end` only on success. They exclude surrounding whitespace
and leave the output span unchanged on failure.
The `std::span<const token>` overload rejects trailing input before terminal EOF.
Producer diagnostics retain their category, span, and message. Malformed syntax,
unsupported names, and parameter eligibility failures produce `parse_error` with
source spans.
At most 128 recursive productions may be active; deeper untrusted input reports
`resource_limit` at the offending current token instead of exhausting the native
stack. Descriptor allocation and accounting failures propagate.

## Compilation costs

Names use transparent borrowed hash lookup. Matching scales with overload count,
arity, and distinct compared metadata pairs; the runtime retains DAG-safe
comparison. Scope storage is an owned vector searched in reverse. Type parsing
uses bounded predictive recursion and allocates complete immutable descriptors.

<!-- [knowledge]
Keep marker-call emission on the runtime's O(1), allocation-free
dyn_segment::origin_below(count) query; reconstructing runtime layout or mirroring
the operand stack would duplicate runtime ownership bookkeeping.
-->

Registration and compilation allocate metadata and heterogeneous callback storage.
Typed scalar execution uses runtime thunks and can reuse a prepared caller stack
without parser-side allocations. Aggregate construction and callable invocation
retain the runtime's documented owned-storage costs.
