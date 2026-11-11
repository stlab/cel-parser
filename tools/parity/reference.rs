//! Test-only public Rust reference driver for decoded CEL expression fixtures.
//!
//! Reads `setup<TAB>expression` records and emits exact scalar bits, public structural
//! identities, values, traces, or source byte intervals. It never inspects raw carriers.
//! Configure the source checkout through `prepare_reference.py`, outside installed packages.

use std::any::TypeId;
use std::io::{self, BufRead};
use std::sync::{Arc, Mutex};

use anyhow::{Result, bail};
use cel_parser::{
    CELParser, OpLookup, OperationSignature, RecordField, RecordSchema, ResolvedLeafType,
    ResolvedType, SourceSpan,
};
use cel_runtime::{ArrayOf, Cons, FnOf, Leaf, Nil, OwnedValue, ValueKind, ValueRepr, ValueType};

/// Identifies the first marker for a traced scalar function.
#[derive(Clone)]
struct Tap;
/// Identifies only the intentionally selected client callback failure.
#[derive(Debug)]
struct CallbackError;
impl std::fmt::Display for CallbackError {
    /// Formats the client-owned error without claiming a library diagnostic category.
    fn fmt(&self, output: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        output.write_str("fixture selected callback")
    }
}
impl std::error::Error for CallbackError {}
/// Identifies the first marker for a traced boolean function.
#[derive(Clone)]
struct Hit;
/// Identifies a deliberate selected callback failure.
#[derive(Clone)]
struct Explode;
/// Identifies the first marker for a client-leaf function.
#[derive(Clone)]
struct MakeLeaf;
/// Identifies the first marker for the named unary operation.
#[derive(Clone)]
struct Named;
/// Identifies a rejected secondary marker for the same operation.
#[derive(Clone)]
struct Secondary;
/// Stores an independently copied admitted client leaf.
#[derive(Clone)]
struct Client(f64);
/// Stores one complete nominal schema in its explicit Rust host representation.
#[derive(Clone)]
struct Pair {
    x: f64,
    y: f64,
}
/// Stores an empty nominal record.
#[derive(Clone)]
struct Empty;
/// Stores an actual callable field, never a manufactured ownership witness.
#[derive(Clone)]
struct CallbackBox(<FnOf<Nil, Leaf<f64>> as ValueRepr>::Value);
/// Returns exact binary64 bits, normalizing only unspecified NaN payloads.
fn bits(number: f64) -> String {
    if number.is_nan() {
        "nan".into()
    } else {
        format!("{:016x}", number.to_bits())
    }
}
/// Returns the public semantic identity of each admitted fixture leaf and composite.
///
/// - Complexity: O(descriptor nodes and name bytes).
fn type_name(ty: &ValueType) -> Result<String> {
    Ok(match ty.kind() {
        ValueKind::Leaf(id) => {
            if *id == TypeId::of::<f64>() {
                "D".into()
            } else if *id == TypeId::of::<bool>() {
                "B".into()
            } else if *id == TypeId::of::<Client>() {
                "Client".into()
            } else if *id == TypeId::of::<Named>() {
                "Marker:named".into()
            } else if *id == TypeId::of::<Secondary>() {
                "Marker:secondary".into()
            } else if *id == TypeId::of::<Pair>() {
                "R:Pair(x:D;y:D;)".into()
            } else if *id == TypeId::of::<Empty>() {
                "R:Empty()".into()
            } else if *id == TypeId::of::<CallbackBox>() {
                "R:Box(f:F(->D);)".into()
            } else if *id == TypeId::of::<std::ops::Range<f64>>() {
                "Range(D)".into()
            } else if *id == TypeId::of::<std::ops::RangeFrom<f64>>() {
                "RangeFrom(D)".into()
            } else if *id == TypeId::of::<std::ops::RangeTo<f64>>() {
                "RangeTo(D)".into()
            } else if *id == TypeId::of::<std::ops::RangeInclusive<f64>>() {
                "RangeInclusive(D)".into()
            } else if *id == TypeId::of::<std::ops::RangeToInclusive<f64>>() {
                "RangeToInclusive(D)".into()
            } else if *id == TypeId::of::<std::ops::RangeFull>() {
                "RangeFull()".into()
            } else {
                // Wider Rust scalars still need complete records for checked C++ rejections.
                macro_rules! wider {
                    ($($t:ty),*) => {$(
                        if *id == TypeId::of::<$t>() {
                            return Ok(stringify!($t).into());
                        }
                    )*};
                }
                wider!(
                    i8, i16, i32, i64, i128, isize, u8, u16, u32, u64, u128, usize, f32
                );
                bail!(
                    "fixture uses an unregistered public leaf: {}",
                    ty.type_name()
                )
            }
        }
        ValueKind::Tuple(fields) => {
            let mut out = String::from("T(");
            for field in fields {
                out.push_str(&type_name(&field.value_type)?);
                out.push(';');
            }
            out.push(')');
            out
        }
        ValueKind::Array(element) => format!("A({})", type_name(element)?),
        ValueKind::Callable { parameters, result } => {
            let mut out = String::from("F(");
            for parameter in parameters {
                out.push_str(&type_name(parameter)?);
                out.push(';');
            }
            out.push_str("->");
            out.push_str(&type_name(result)?);
            out.push(')');
            out
        }
    })
}
/// Consumes a known whole-array witness using checked public typed conversion.
///
/// - Complexity: O(elements and public serialization work); transfers, rather than clones, elements.
fn array<R: ValueRepr>(owner: OwnedValue) -> Result<String> {
    let mut out = String::from("[");
    for element in owner.try_into_typed::<ArrayOf<R>>()?.into_vec() {
        out.push_str(&value(OwnedValue::new::<R>(element))?);
        out.push(';');
    }
    out.push(']');
    Ok(out)
}
/// Consumes a complete fixture value through public checked access, never raw storage.
///
/// # Errors
/// Rejects any fixture outside the explicitly enumerated host witness set.
/// - Complexity: O(values and checked conversion/serialization work).
fn value(owner: OwnedValue) -> Result<String> {
    let ty = owner.value_type().clone();
    match ty.kind() {
        ValueKind::Callable { .. } => Ok("fn".into()),
        ValueKind::Tuple(_) => {
            let mut out = String::from("[");
            for field in owner.into_fields()? {
                out.push_str(&value(field)?);
                out.push(';');
            }
            out.push(']');
            Ok(out)
        }
        ValueKind::Array(element) => {
            macro_rules! candidate {
                ($r:ty) => {
                    if element.same_shape(&<$r>::value_type()) {
                        return array::<$r>(owner);
                    }
                };
            }
            candidate!(Leaf<f64>);
            candidate!(Leaf<bool>);
            candidate!(Leaf<Client>);
            candidate!(Leaf<Pair>);
            candidate!(Leaf<CallbackBox>);
            candidate!(Leaf<Empty>);
            candidate!(Nil);
            candidate!(Cons<Leaf<f64>, Cons<Leaf<bool>, Nil>>);
            candidate!(ArrayOf<Leaf<f64>>);
            candidate!(ArrayOf<Leaf<bool>>);
            candidate!(FnOf<Nil, Leaf<f64>>);
            candidate!(FnOf<Nil, FnOf<Nil, Leaf<f64>>>);
            candidate!(FnOf<Cons<Leaf<f64>, Nil>, Leaf<bool>>);
            candidate!(Cons<ArrayOf<FnOf<Nil, Leaf<f64>>>, Cons<Leaf<bool>, Nil>>);
            bail!("no public test witness for {}", type_name(&ty)?)
        }
        ValueKind::Leaf(id) => {
            if *id == TypeId::of::<f64>() {
                return Ok(bits(owner.try_into_typed::<Leaf<f64>>()?));
            }
            if *id == TypeId::of::<bool>() {
                return Ok(owner.try_into_typed::<Leaf<bool>>()?.to_string());
            }
            if *id == TypeId::of::<Client>() {
                return Ok(bits(owner.try_into_typed::<Leaf<Client>>()?.0));
            }
            if *id == TypeId::of::<Named>() {
                owner.try_into_typed::<Leaf<Named>>()?;
                return Ok("marker".into());
            }
            if *id == TypeId::of::<Secondary>() {
                owner.try_into_typed::<Leaf<Secondary>>()?;
                return Ok("marker".into());
            }
            if *id == TypeId::of::<Pair>() {
                let pair = owner.try_into_typed::<Leaf<Pair>>()?;
                return Ok(format!("[{};{};]", bits(pair.x), bits(pair.y)));
            }
            if *id == TypeId::of::<Empty>() {
                return Ok("[]".into());
            }
            if *id == TypeId::of::<CallbackBox>() {
                return Ok("[fn;]".into());
            }
            if *id == TypeId::of::<std::ops::Range<f64>>() {
                let range = owner.try_into_typed::<Leaf<std::ops::Range<f64>>>()?;
                return Ok(format!("[{};{};]", bits(range.start), bits(range.end)));
            }
            if *id == TypeId::of::<std::ops::RangeInclusive<f64>>() {
                let range = owner.try_into_typed::<Leaf<std::ops::RangeInclusive<f64>>>()?;
                return Ok(format!(
                    "[{};{};]",
                    bits(*range.start()),
                    bits(*range.end())
                ));
            }
            if *id == TypeId::of::<std::ops::RangeFrom<f64>>() {
                return Ok(format!(
                    "[{};]",
                    bits(
                        owner
                            .try_into_typed::<Leaf<std::ops::RangeFrom<f64>>>()?
                            .start
                    )
                ));
            }
            if *id == TypeId::of::<std::ops::RangeTo<f64>>() {
                return Ok(format!(
                    "[{};]",
                    bits(owner.try_into_typed::<Leaf<std::ops::RangeTo<f64>>>()?.end)
                ));
            }
            if *id == TypeId::of::<std::ops::RangeToInclusive<f64>>() {
                return Ok(format!(
                    "[{};]",
                    bits(
                        owner
                            .try_into_typed::<Leaf<std::ops::RangeToInclusive<f64>>>()?
                            .end
                    )
                ));
            }
            if *id == TypeId::of::<std::ops::RangeFull>() {
                return Ok("[]".into());
            }
            // Serialize through checked public conversions, never raw carrier storage.
            macro_rules! wider {
                ($($t:ty),*) => {$(
                    if *id == TypeId::of::<$t>() {
                        return Ok(owner.try_into_typed::<Leaf<$t>>()?.to_string());
                    }
                )*};
            }
            wider!(
                i8, i16, i32, i64, i128, isize, u8, u16, u32, u64, u128, usize, f32
            );
            bail!("unsupported fixture value")
        }
    }
}
/// Constructs a complete scalar registration from its public concrete descriptor.
fn scalar<T: Clone + 'static>(name: &'static str) -> ResolvedType {
    ResolvedLeafType::new(name, ValueType::cloneable_leaf::<T>()).into()
}
/// Registers a named signature using the public selected-emitter interface.
fn operation(
    lookup: &mut OpLookup,
    name: &'static str,
    operands: Vec<ResolvedType>,
    result: ResolvedType,
    emit: cel_parser::OperationEmitter,
) -> Result<()> {
    lookup.register_operation(OperationSignature {
        name,
        operands,
        result,
        emit,
    })
}
/// Returns fresh intrinsics for core/default or matching client records, callbacks, and scopes.
///
/// - Complexity: O(registration and descriptor work).
fn lookup(setup: &str, trace: &Arc<Mutex<Vec<f64>>>) -> Result<OpLookup> {
    let mut result = OpLookup::new();
    if setup == "core" || setup == "default" {
        return Ok(result);
    }
    let d = scalar::<f64>("f64");
    let b = scalar::<bool>("bool");
    let pair = ResolvedLeafType::new("Pair", ValueType::cloneable_leaf::<Pair>());
    result.register_record(RecordSchema {
        ty: pair.clone(),
        fields: vec![
            RecordField {
                name: "x",
                ty: d.clone(),
            },
            RecordField {
                name: "y",
                ty: d.clone(),
            },
        ],
        emit: Box::new(|s, permutation, _, _| {
            let swapped = permutation[0] == 1;
            s.op2(move |a: f64, b: f64| {
                if swapped {
                    Pair { x: b, y: a }
                } else {
                    Pair { x: a, y: b }
                }
            })
        }),
    })?;
    result.register_record(RecordSchema {
        ty: ResolvedLeafType::new("Empty", ValueType::cloneable_leaf::<Empty>()),
        fields: vec![],
        emit: Box::new(|s, _, _, _| {
            s.op0(|| Empty);
            Ok(())
        }),
    })?;
    let fn_type = CELParser::new(OpLookup::new())
        .parse_type_expr_str("Fn((),f64)")
        .map_err(|error| anyhow::anyhow!(error.to_string()))?
        .resolve(cel_parser::builtin_type_resolver().as_ref())
        .map_err(|error| anyhow::anyhow!(error.to_string()))?;
    result.register_record(RecordSchema {
        ty: ResolvedLeafType::new("Box", ValueType::cloneable_leaf::<CallbackBox>()),
        fields: vec![RecordField {
            name: "f",
            ty: fn_type.clone(),
        }],
        emit: Box::new(|s, _, _, _| {
            s.op1_repr::<FnOf<Nil, Leaf<f64>>, Leaf<CallbackBox>, _>(CallbackBox)
        }),
    })?;
    let custom = setup == "custom";
    operation(
        &mut result,
        "x",
        vec![pair.clone().into()],
        d.clone(),
        Box::new(move |s, _| s.op1(move |p: Pair| if custom { p.y } else { p.x })),
    )?;
    operation(
        &mut result,
        "y",
        vec![pair.into()],
        d.clone(),
        Box::new(|s, _| s.op1(|p: Pair| p.y)),
    )?;
    operation(
        &mut result,
        "f",
        vec![scalar::<CallbackBox>("Box")],
        fn_type,
        Box::new(|s, _| s.op1_repr::<Leaf<CallbackBox>, FnOf<Nil, Leaf<f64>>, _>(|p| p.0)),
    )?;
    result.register_function("tap", Tap)?;
    let log = Arc::clone(trace);
    operation(
        &mut result,
        "tap",
        vec![d.clone()],
        d.clone(),
        Box::new(move |s, _| {
            let log = Arc::clone(&log);
            s.op1(move |n: f64| {
                log.lock().unwrap().push(n);
                n
            })
        }),
    )?;
    result.register_function("hit", Hit)?;
    let log = Arc::clone(trace);
    operation(
        &mut result,
        "hit",
        vec![d.clone()],
        b.clone(),
        Box::new(move |s, _| {
            let log = Arc::clone(&log);
            s.op1(move |n: f64| {
                log.lock().unwrap().push(n);
                n > 0.0
            })
        }),
    )?;
    result.register_function("explode", Explode)?;
    operation(
        &mut result,
        "explode",
        vec![],
        b,
        Box::new(|s, _| {
            s.op0r(|| -> Result<bool> { Err(CallbackError.into()) });
            Ok(())
        }),
    )?;
    result.register_function("leaf", MakeLeaf)?;
    operation(
        &mut result,
        "leaf",
        vec![],
        scalar::<Client>("Client"),
        Box::new(|s, _| {
            s.op0(|| Client(8.0));
            Ok(())
        }),
    )?;
    result.register_function("named", Named)?;
    result.register_function("named", Secondary)?;
    operation(
        &mut result,
        "named",
        vec![d.clone()],
        d.clone(),
        Box::new(|s, _| s.op1(|n: f64| n * 10.0)),
    )?;
    operation(
        &mut result,
        "plus",
        vec![d.clone(), d.clone()],
        d.clone(),
        Box::new(|s, _| s.op2(|a: f64, b: f64| a + b)),
    )?;
    result.push_library_scope(|name, s, n, _| {
        if n != 0 || (name != "library" && name != "shadow") {
            return Ok(false);
        }
        s.just(if name == "library" { 7.0f64 } else { 1.0f64 });
        Ok(true)
    });
    for number in [4.0f64, 5.0f64] {
        result.push_scope(move |name, s, n, _| {
            if n != 0 {
                return Ok(false);
            }
            if name == "othermarker" {
                s.just(Secondary);
                return Ok(true);
            }
            if name != "ambient" && name != "shadow" {
                return Ok(false);
            }
            s.just(if name == "ambient" { 3.0f64 } else { number });
            Ok(true)
        });
    }
    if setup == "custom" {
        for name in ["|", "()"] {
            operation(
                &mut result,
                name,
                vec![d.clone(), d.clone()],
                d.clone(),
                Box::new(|s, _| s.op2(|a: f64, b: f64| a + b)),
            )?;
        }
        operation(
            &mut result,
            "^",
            vec![d.clone(), d.clone()],
            d.clone(),
            Box::new(|s, _| s.op2(|a: f64, b: f64| a - b)),
        )?;
        operation(
            &mut result,
            "&",
            vec![d.clone(), d.clone()],
            d.clone(),
            Box::new(|s, _| s.op2(|a: f64, b: f64| a * b)),
        )?;
        operation(
            &mut result,
            "<<",
            vec![d.clone(), d.clone()],
            d.clone(),
            Box::new(|s, _| s.op2(|a: f64, b: f64| a + b)),
        )?;
        operation(
            &mut result,
            ">>",
            vec![d.clone(), d.clone()],
            d,
            Box::new(|s, _| s.op2(|a: f64, b: f64| a - b)),
        )?;
    }
    if setup == "unstable_trace_first" || setup == "unstable_trace_second" {
        let first = setup == "unstable_trace_first";
        let calls = Arc::new(Mutex::new(0usize));
        let trace = Arc::clone(trace);
        result.push_scope(move |name, segment, arity, _| {
            if name != "traceprobe" || arity != 0 {
                return Ok(false);
            }
            let calls = Arc::clone(&calls);
            let trace = Arc::clone(&trace);
            segment.op0(move || {
                let mut call = calls.lock().unwrap();
                trace
                    .lock()
                    .unwrap()
                    .push(if *call == if first { 0 } else { 1 } {
                        2.0
                    } else {
                        1.0
                    });
                *call += 1;
                1.0f64
            });
            Ok(true)
        });
    }
    Ok(result)
}
/// Serializes a borrowed trace with the same explicit IEEE normalization as values.
///
/// - Complexity: O(trace length) time and response space.
fn trace_text(trace: &[f64]) -> String {
    let mut result = String::new();
    for n in trace {
        if !result.is_empty() {
            result.push(',');
        }
        result.push_str(&bits(*n));
    }
    result
}
/// Evaluates each cached fixture three times after parser destruction.
///
/// Every successful execution preserves its normalized type, value, and trace.
///
/// # Errors
///
/// Returns unrelated execution errors, observation errors, or changed cached observations.
///
/// - Complexity: O(compilation, three evaluations, and serialization).
fn run(setup: &str, source: &str) -> Result<()> {
    let trace = Arc::new(Mutex::new(Vec::new()));
    let mut parser = CELParser::new(lookup(setup, &trace)?);
    parser.set_type_resolver(|name: &str, args: &[ResolvedType]| {
        if args.is_empty() {
            match name {
                "Client" => {
                    return Some(ResolvedLeafType::new(
                        "Client",
                        ValueType::cloneable_leaf::<Client>(),
                    ));
                }
                "Empty" => {
                    return Some(ResolvedLeafType::new(
                        "Empty",
                        ValueType::cloneable_leaf::<Empty>(),
                    ));
                }
                "Pair" => {
                    return Some(ResolvedLeafType::new(
                        "Pair",
                        ValueType::cloneable_leaf::<Pair>(),
                    ));
                }
                "Box" => {
                    return Some(ResolvedLeafType::new(
                        "Box",
                        ValueType::cloneable_leaf::<CallbackBox>(),
                    ));
                }
                "Alias" => {
                    return Some(ResolvedLeafType::new(
                        "Alias",
                        ValueType::cloneable_leaf::<f64>(),
                    ));
                }
                _ => {}
            }
        }
        cel_parser::resolve_builtin_named_type(name, args)
    });
    if setup == "core" || setup == "default" {
        parser.set_type_resolver(cel_parser::resolve_builtin_named_type);
    }
    let mut program = match parser.parse_str(source) {
        Ok(program) => program,
        Err(error) => {
            let span = SourceSpan::from_proc_macro2_range(
                error.span(),
                error.end_span().unwrap_or(error.span()),
            )
            .to_byte_range(source);
            println!("parse\t{}\t{}\tparse", span.start, span.end);
            return Ok(());
        }
    };
    drop(parser);
    anyhow::ensure!(
        trace.lock().unwrap().is_empty(),
        "callback executed during compilation"
    );
    let mut response = String::new();
    for repeat in 0..3 {
        trace.lock().unwrap().clear();
        match program.call_dyn_value(&[]) {
            Ok(owner) => {
                let next = format!(
                    "ok\t{}\t{}\t{}",
                    type_name(owner.value_type())?,
                    value(owner)?,
                    trace_text(&trace.lock().unwrap()),
                );
                anyhow::ensure!(
                    repeat == 0 || response == next,
                    "cached result or trace changed"
                );
                response = next;
            }
            Err(error) => {
                if setup == "callback_error" && error.downcast_ref::<CallbackError>().is_some() {
                    response = "runtime\tcallback".into();
                } else {
                    return Err(error);
                }
                response.push('\t');
                response.push_str(&trace_text(&trace.lock().unwrap()));
                break;
            }
        }
    }
    println!("{response}");
    Ok(())
}
/// Runs a fail-closed line protocol using standard I/O, separate from product execution.
fn main() -> Result<()> {
    for line in io::stdin().lock().lines() {
        let line = line?;
        let (setup, source) = line
            .split_once('\t')
            .ok_or_else(|| anyhow::anyhow!("missing setup"))?;
        run(setup, source)?;
    }
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;
    use cel_runtime::IntoCStackList;
    /// Observes wider Rust scalar values when C++ deliberately rejects their syntax.
    #[test]
    fn wider_scalar_rejections_still_have_complete_oracle_records() {
        let integer = OwnedValue::new::<Leaf<i32>>(2);
        assert_eq!(type_name(integer.value_type()).unwrap(), "i32");
        assert_eq!(value(integer).unwrap(), "2");
        let float = OwnedValue::new::<Leaf<f32>>(1.5);
        assert_eq!(type_name(float.value_type()).unwrap(), "f32");
        assert_eq!(value(float).unwrap(), "1.5");
    }
    /// Leaves core lookup free of client callbacks and registered record names.
    #[test]
    fn core_lookup_has_no_client_or_standard_bindings() {
        let trace = Arc::new(Mutex::new(Vec::new()));
        let mut core = CELParser::new(lookup("core", &trace).unwrap());
        assert!(core.parse_str("1.0f64+2.0f64").is_ok());
        for source in ["sqrt(4.0f64)", "tap(1.0f64)", "Pair{x:1.0f64,y:2.0f64}"] {
            assert!(core.parse_str(source).is_err());
        }
        let mut client = CELParser::new(lookup("client", &trace).unwrap());
        assert!(client.parse_str("tap(1.0f64)").is_ok());
    }
    /// Rejects a stable result whose first successful execution changes its trace.
    #[test]
    fn cached_execution_checks_every_trace() {
        assert!(run("unstable_trace_first", "traceprobe").is_err());
        assert!(run("client", "tap(1.0f64)").is_ok());
    }
    /// Rejects a stable result whose second successful execution changes its trace.
    #[test]
    fn cached_execution_checks_second_trace() {
        assert!(run("unstable_trace_second", "traceprobe").is_err());
    }
    /// Preserves signed zero and finite bits and normalizes unspecified NaN payloads.
    #[test]
    fn scalar_observation_preserves_ieee_contracts() {
        assert_eq!(bits(-0.0), "8000000000000000");
        assert_eq!(bits(3.0), "4008000000000000");
        assert_eq!(bits(f64::NAN), "nan");
    }
    /// Distinguishes recursive shapes and transfers structural values through safe witnesses.
    #[test]
    fn structural_observation_uses_complete_public_types() {
        let a = OwnedValue::new::<ArrayOf<Leaf<f64>>>(cel_runtime::Array::from_vec(vec![1.0]));
        assert_eq!(type_name(a.value_type()).unwrap(), "A(D)");
        assert_eq!(value(a).unwrap(), "[3ff0000000000000;]");
        assert_eq!(
            type_name(&FnOf::<Nil, Leaf<f64>>::value_type()).unwrap(),
            "F(->D)"
        );
    }
    /// Retains real callable array elements after the parser has been destroyed.
    #[test]
    fn cached_callable_array_uses_the_public_typed_interface() {
        let mut parser = CELParser::new(OpLookup::new());
        let body = parser.parse_str("[||7.0f64]:[Fn((),f64)]").unwrap();
        let mut prepared = body
            .prepare::<Nil, ArrayOf<FnOf<Nil, Leaf<f64>>>>()
            .unwrap();
        drop(parser);
        for _ in 0..3 {
            let result = prepared.call(&().into_c_stack_list()).unwrap();
            assert_eq!(
                result.as_slice()[0].call(&().into_c_stack_list()).unwrap(),
                7.0
            );
        }
    }
}
