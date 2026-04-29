# ADR-0001: Record architecture decisions

- **Status**: Accepted
- **Date**: 2026-04-29
- **Deciders**: project maintainers

## Context

flux has design constraints that are easy to lose when documentation is rewritten: low-level API scope, explicit ownership, backend boundaries, and Vulkan execution trade-offs. These choices need stable historical records separate from mutable explanatory docs.

## Decision

The project will record significant architecture decisions as ADRs in `docs/adr/`. ADRs are numbered sequentially, kept short, and treated as append-only after acceptance. If a decision changes, a new ADR supersedes the old one.

## Alternatives considered

- **Keep rationale only in explanation docs**: Rejected because explanation docs are mutable and can erase decision history during rewrites.
- **Use commit messages only**: Rejected because readers should not need git archaeology to understand current architecture constraints.

## Consequences

- Positive: Design history has a durable home.
- Trade-off: Contributors must add an ADR for significant architecture changes.
- Negative (accepted): Some rationale is duplicated between ADRs and explanation docs.
