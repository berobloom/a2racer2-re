# A2 Racer II Reverse Engineering

## Project goal

Reverse engineer the 1998 Windows version of A2 Racer II.

## Evidence rules

Never present an inference as fact.

Use these confidence levels:

- CONFIRMED
- STRONGLY SUPPORTED
- LIKELY
- SPECULATIVE

Prefer evidence from binaries and runtime experiments over external documentation.

## Important files

- `research/discoveries.md` — confirmed discoveries
- `research/hypotheses.md` — current hypotheses
- `research/open-questions.md` — unresolved questions
- `reverse-engineering/` — technical documentation

## Workflow

Before investigating a subsystem:

1. Search existing research.
2. Check open questions.
3. Reuse existing discoveries.
4. Only then perform new analysis.

When discovering something important, update the appropriate research file.

Do not duplicate existing findings.

## Reverse engineering

Preserve original binaries.

Record executable addresses/offsets when possible.

Record evidence for important conclusions.

Prefer small experiments that distinguish competing hypotheses.
