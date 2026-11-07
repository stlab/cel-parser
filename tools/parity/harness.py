"""Runs public C++ and Rust drivers against deterministic decoded-expression fixtures."""
import argparse
import json
import subprocess
from fixtures import fixtures

if not __debug__:
    raise RuntimeError("The parity gate requires Python assertion checking")


def validate_response(row, response):
    """Requires a complete recognized phase with nonempty discriminators; traces may be empty."""
    lengths = {"ok": 4, "parse": 4, "runtime": 3}
    assert response and response[0] in lengths, (row["id"], "unknown phase", response)
    phase = response[0]
    assert len(response) == lengths[phase], (row["id"], "incomplete record", response)
    required = response if phase == "parse" else response[:-1]
    assert all(isinstance(field, str) and field for field in required), (
        row["id"], "empty discriminator", response)
    assert isinstance(response[-1], str), (row["id"], "invalid field", response)
    if phase == "parse":
        assert all(span.isdecimal() for span in response[1:3]), (
            row["id"], "invalid span", response)
        assert int(response[1]) <= int(response[2]), (row["id"], "reversed span", response)


def compare(row, cpp, rust):
    """Rejects differing types, bits, structures, traces, or normalized error intervals."""
    validate_response(row, cpp)
    validate_response(row, rust)
    if cpp[0] == rust[0] == "parse":
        assert "category" in row, (row["id"], "unexpected parse failure", cpp, rust)
        assert len(cpp) == len(rust) == 4, (row["id"], cpp, rust)
        if "rust_span" in row:
            assert row["adaptation"] in {"group-delimiter", "eof-sentinel"}, row
            assert list(map(int, rust[1:3])) == row["rust_span"], (row["id"], rust, row)
        else:
            assert cpp[1:3] == rust[1:3], (row["id"], cpp, rust)
        assert cpp[3] == row["category"], (row["id"], cpp, row)
        assert rust[3] == "parse", (row["id"], rust)
        if "span" in row:
            assert list(map(int, cpp[1:3])) == row["span"], (row["id"], cpp, row)
        return
    if row.get("zero_choice"):
        assert cpp[0:2] == rust[0:2] == ["ok", "D"], (row["id"], cpp, rust)
        zeros = {"0000000000000000", "8000000000000000"}
        if cpp[2] in zeros and rust[2] in zeros:
            cpp = [*cpp[:2], "zero", *cpp[3:]]
            rust = [*rust[:2], "zero", *rust[3:]]
    assert cpp == rust, (row["id"], cpp, rust)
    assert cpp[0] == row.get("status", "ok"), (row["id"], cpp, row)
    if "expected" in row:
        assert cpp[1:] == row["expected"], (row["id"], cpp, row)


def run_driver(path, rows):
    """Runs one process and requires exactly one complete response per fixture."""
    assert all(
        not any(c in r["source"] or c in r.get("setup", "std") for c in "\t\r\n")
        for r in rows), "line-protocol fixtures must contain no tabs or line breaks"
    requests = "".join(
        f'{r.get("setup", "std")}\t{r["source"]}\n' for r in rows)
    result = subprocess.run([path], input=requests, text=True,
                            capture_output=True)
    if result.returncode:
        raise RuntimeError(f"{path} failed with exit {result.returncode}: {result.stderr}")
    responses = [line.split("\t") for line in result.stdout.splitlines()]
    assert len(responses) == len(rows), (path, len(responses), len(rows), result.stderr)
    return responses


def main():
    """Compares all fixtures and reports failure rather than skipping missing drivers."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cpp", required=True)
    parser.add_argument("--rust", required=True)
    parser.add_argument("--fixtures", help="optional external JSON fixture array")
    args = parser.parse_args()
    if args.fixtures:
        with open(args.fixtures, encoding="utf-8") as source:
            rows = json.load(source)
    else:
        rows = fixtures()
    assert rows and len({r["id"] for r in rows}) == len(rows), "empty/duplicate fixtures"
    shared = [r for r in rows if not r.get("cpp_only")]
    cpp = run_driver(args.cpp, rows)
    rust = dict(zip((r["id"] for r in shared), run_driver(args.rust, shared)))
    failures = []
    for row, left in zip(rows, cpp):
        try:
            if row.get("cpp_only"):
                validate_response(row, left)
                assert left == row["expected_cpp"], (row["id"], left, row)
            else:
                compare(row, left, rust[row["id"]])
        except AssertionError as error:
            failures.append(str(error))
    if failures:
        raise AssertionError("\n".join(failures))
    print(f"PASS: {len(shared)} differential fixtures; "
          f'{len(rows)-len(shared)} explicit C++ contract adaptations; '
          f'{len({r["family"] for r in rows})} fixture families')


if __name__ == "__main__":
    main()
