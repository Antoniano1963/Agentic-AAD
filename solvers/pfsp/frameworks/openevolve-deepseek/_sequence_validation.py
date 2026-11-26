from __future__ import annotations

from collections import Counter
from operator import index
from typing import Any, Iterable


class FSPSequenceValidationError(ValueError):
    """Base class for invalid FSP destroy outputs and solution sequences."""


class InvalidDestroyOutputError(FSPSequenceValidationError):
    """Raised when a destroy operator does not partition its input exactly."""


class InvalidSolutionSequenceError(FSPSequenceValidationError):
    """Raised when a solution is not a permutation of every job exactly once."""


def validate_destroy_result(
    original_sequence: Iterable[Any],
    result: Any,
    *,
    context: str = "destroy operator",
) -> tuple[list[int], list[int]]:
    """Validate and normalize an LLM destroy-operator result.

    The two returned sequences may use any order, but together they must be an
    exact multiset partition of the input sequence. This rejects lost jobs,
    duplicated jobs, out-of-range replacements, malformed return values, and
    non-integral job identifiers before objective evaluation.
    """
    original = _normalize_sequence(original_sequence, context="original sequence")
    if not isinstance(result, (tuple, list)) or len(result) != 2:
        raise InvalidDestroyOutputError(
            f"{context} must return (remaining_sequence, removed_jobs)"
        )

    remaining = _normalize_sequence(result[0], context=f"{context} remaining_sequence")
    removed = _normalize_sequence(result[1], context=f"{context} removed_jobs")
    expected = Counter(original)
    observed = Counter(remaining + removed)
    if observed != expected:
        raise InvalidDestroyOutputError(
            f"{context} must partition the input sequence exactly; "
            f"original_len={len(original)}, remaining_len={len(remaining)}, "
            f"removed_len={len(removed)}, "
            f"{_counter_delta_message(expected=expected, observed=observed)}"
        )
    return remaining, removed


def validate_solution_sequence(
    sequence: Iterable[Any],
    n_jobs: int,
    *,
    context: str = "solution",
) -> list[int]:
    """Require a complete permutation of integer job IDs ``0..n_jobs-1``."""
    normalized = _normalize_sequence(sequence, context=context)
    expected = Counter(range(int(n_jobs)))
    observed = Counter(normalized)
    if observed != expected:
        raise InvalidSolutionSequenceError(
            f"{context} must contain every job exactly once; "
            f"n_jobs={int(n_jobs)}, sequence_len={len(normalized)}, "
            f"{_counter_delta_message(expected=expected, observed=observed)}"
        )
    return normalized


def _normalize_sequence(sequence: Iterable[Any], *, context: str) -> list[int]:
    if isinstance(sequence, (str, bytes)):
        raise FSPSequenceValidationError(f"{context} must be a job-ID sequence, not text")
    try:
        values = list(sequence)
    except TypeError as exc:
        raise FSPSequenceValidationError(f"{context} must be iterable") from exc

    normalized: list[int] = []
    for position, value in enumerate(values):
        if isinstance(value, bool):
            raise FSPSequenceValidationError(
                f"{context}[{position}] must be an integer job ID, got bool"
            )
        try:
            normalized.append(int(index(value)))
        except TypeError as exc:
            raise FSPSequenceValidationError(
                f"{context}[{position}] must be an integer job ID, got {type(value).__name__}"
            ) from exc
    return normalized


def _counter_delta_message(*, expected: Counter, observed: Counter, limit: int = 8) -> str:
    missing = list((expected - observed).elements())
    extra = list((observed - expected).elements())
    return f"missing={missing[:limit]}, extra={extra[:limit]}"
