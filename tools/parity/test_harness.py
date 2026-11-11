"""Verifies fail-closed exact-source comparison and complete observation records."""
import unittest
from unittest.mock import patch
from harness import compare, main
import sys


class ComparisonTests(unittest.TestCase):
    """Checks observable fixture decisions, independent of either evaluator."""

    def test_successful_compilation_never_bypasses_rust_parsing(self):
        """Rejects Rust parse failures even for legacy exemptions and runtime records."""
        for cpp in [["ok", "D", "4008000000000000", ""],
                    ["runtime", "callback", ""]]:
            with self.subTest(cpp=cpp):
                with self.assertRaises(AssertionError):
                    compare({"id": "legacy", "cpp_only": "legacy",
                             "expected_cpp": cpp},
                            cpp, ["parse", "0", "4", "parse"])

    def test_declared_narrowing_requires_cpp_rejection(self):
        """Allows wider Rust acceptance without accepting an excluded C++ feature."""
        row = {"id": "integer", "cpp_reject": "integer values"}
        compare(row, ["parse", "0", "1", "unsupported_literal"],
                ["ok", "i32", "2", ""])
        with self.assertRaises(AssertionError):
            compare(row, ["ok", "D", "4000000000000000", ""],
                    ["ok", "D", "4000000000000000", ""])

    def test_all_authored_sources_reach_both_processes(self):
        """Rejects a legacy bypass and sends identical authored requests to both drivers."""
        rows = [{"id": "legacy", "family": "witness", "setup": "default",
                 "source": "2f64+3", "cpp_only": "legacy",
                 "expected_cpp": ["ok", "D", "4014000000000000", ""]},
                {"id": "runtime", "family": "witness", "setup": "index_error",
                 "source": "[1.0f64][2]", "cpp_only": "legacy",
                 "expected_cpp": ["runtime", "index", ""]}]
        inputs = []

        def process(command, *, input, **kwargs):
            """Records the actual transport boundary and returns complete driver records."""
            inputs.append(input)
            from subprocess import CompletedProcess
            output = ("ok\tD\t4014000000000000\t\nruntime\tindex\t\n"
                      if command == ["cpp"] else "parse\t0\t4\tparse\nparse\t8\t11\tparse\n")
            return CompletedProcess(command, 0, output if input else "", "")

        with patch.object(sys, "argv", ["harness", "--cpp", "cpp", "--rust", "rust"]), \
             patch("harness.fixtures", return_value=rows), \
             patch("harness.subprocess.run", side_effect=process):
            with self.assertRaises(AssertionError):
                main()
        self.assertEqual(inputs, [
            "default\t2f64+3\nindex_error\t[1.0f64][2]\n",
            "default\t2f64+3\nindex_error\t[1.0f64][2]\n"])

    def test_narrowing_still_validates_the_rust_record(self):
        """Rejects incomplete oracle records even when C++ deliberately rejects the feature."""
        with self.assertRaises(AssertionError):
            compare({"id": "narrow", "cpp_reject": "integer values"},
                    ["parse", "0", "1", "unsupported_literal"], ["ok"])

    def test_grammar_cases_preserve_value_and_trace_comparison(self):
        """Keeps successful corpus observations exact rather than checking status alone."""
        row = {"id": "grammar", "acceptance_only": True}
        with self.assertRaises(AssertionError):
            compare(row, ["ok", "D", "4000000000000000", "1"],
                    ["ok", "D", "4000000000000000", "2"])

    def test_positive_grammar_cases_require_success(self):
        """Rejects jointly failed positive grammar cases rather than hiding lost coverage."""
        with self.assertRaises(AssertionError):
            compare({"id": "positive", "acceptance_only": True},
                    ["parse", "0", "1", "type_error"], ["parse", "0", "1", "parse"])

    def test_oracle_receives_all_sources_before_cpp_process_failure(self):
        """Runs complete Rust requests even when the C++ process later fails."""
        rows = [{"id": "probe", "family": "probe", "source": "[1.0f64][2]",
                 "setup": "default", "cpp_reject": "postfix indexing"}]
        observed = []

        def process(command, *, input, **kwargs):
            """Records transport and emulates a process-level C++ execution failure."""
            from subprocess import CompletedProcess
            observed.append((command, input))
            if command == ["cpp"]:
                return CompletedProcess(command, 1, "", "out of bounds")
            return CompletedProcess(command, 0, "parse\t8\t11\tparse\n", "")

        with patch.object(sys, "argv", ["harness", "--cpp", "cpp", "--rust", "rust"]), \
             patch("harness.fixtures", return_value=rows), \
             patch("harness.subprocess.run", side_effect=process):
            with self.assertRaises(RuntimeError):
                main()
        self.assertEqual(observed, [
            (["rust"], "default\t[1.0f64][2]\n"),
            (["cpp"], "default\t[1.0f64][2]\n")])

    def test_mismatch_is_not_accepted(self):
        """Rejects changed finite values, types, traces, spans, and exit categories."""
        row = {"id": "probe"}
        good = ["ok", "D", "4008000000000000", "1,2"]
        for bad in [
            ["ok", "D", "4000000000000000", "1,2"],
            ["ok", "B", "true", "1,2"],
            ["ok", "D", "4008000000000000", "2,1"],
            ["parse", "0", "1", "malformed_input"],
        ]:
            with self.assertRaises(AssertionError):
                compare(row, good, bad)
        with self.assertRaises(AssertionError):
            compare({"id": "span", "category": "type_error"},
                    ["parse", "0", "8", "type_error"],
                    ["parse", "3", "8", "parse"])
        with self.assertRaises(AssertionError):
            compare(row, ["parse", "0", "1", "type_error"],
                    ["parse", "0", "1", "parse"])

    def test_signed_zero_never_normalizes_for_core_operations(self):
        """Rejects changed zero signs even when obsolete library metadata requests it."""
        positive = ["ok", "D", "0000000000000000", ""]
        negative = ["ok", "D", "8000000000000000", ""]
        with self.assertRaises(AssertionError):
            compare({"id": "legacy-zero", "zero_choice": True}, positive, negative)
        with self.assertRaises(AssertionError):
            compare({"id": "negation"}, positive, negative)
        with self.assertRaises(AssertionError):
            compare({"id": "legacy-zero", "zero_choice": True}, positive,
                    ["ok", "D", "3ff0000000000000", ""])

    def test_incomplete_and_malformed_records_are_rejected_before_equality(self):
        """Rejects equal incomplete records, including the reviewed corpus witnesses."""
        for row, record in [
            ({"id": "structural-65"}, ["ok"]),
            ({"id": "selected-exceptions-114", "status": "runtime"}, ["runtime", "callback"]),
            ({"id": "empty"}, []),
            ({"id": "unknown", "status": "other"}, ["other", "D", "value", ""]),
            ({"id": "empty-type"}, ["ok", "", "value", ""]),
            ({"id": "empty-value"}, ["ok", "D", "", ""]),
            ({"id": "empty-error", "status": "runtime"}, ["runtime", "", ""]),
            ({"id": "excess"}, ["ok", "D", "value", "", "extra"]),
            ({"id": "parse", "category": "type_error"}, ["parse", "", "1", "type_error"]),
            ({"id": "parse", "category": "type_error"}, ["parse", "x", "1", "type_error"]),
            ({"id": "parse", "category": "type_error"}, ["parse", "2", "1", "type_error"]),
            ({"id": "adapted", "zero_choice": True}, ["ok", "D"]),
        ]:
            with self.subTest(record=record):
                with self.assertRaises(AssertionError):
                    compare(row, record, record)

    def test_empty_traces_remain_valid(self):
        """Accepts a complete success or execution error with no side effects."""
        compare({"id": "success"}, ["ok", "D", "0000000000000000", ""],
                ["ok", "D", "0000000000000000", ""])
        compare({"id": "error", "status": "runtime"}, ["runtime", "callback", ""],
                ["runtime", "callback", ""])


if __name__ == "__main__":
    unittest.main()
