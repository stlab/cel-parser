"""Verifies fail-closed comparison and the two permitted IEEE normalizations."""
import unittest
from harness import compare


class ComparisonTests(unittest.TestCase):
    """Checks observable fixture decisions, independent of either evaluator."""

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

    def test_only_explicit_zero_choice_normalizes(self):
        """Accepts unspecified extrema zero signs without hiding other differences."""
        positive = ["ok", "D", "0000000000000000", ""]
        negative = ["ok", "D", "8000000000000000", ""]
        compare({"id": "min", "zero_choice": True}, positive, negative)
        with self.assertRaises(AssertionError):
            compare({"id": "negation"}, positive, negative)
        with self.assertRaises(AssertionError):
            compare({"id": "min", "zero_choice": True}, positive,
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
