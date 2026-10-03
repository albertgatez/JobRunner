# JobRunner Copilot Instructions

Apply these instructions to any implementation, refactor, or documentation change in this repository.

## Goal
Keep the code aligned with the approved requirements in `Indicaciones/`, the ADRs in `docs/decisions/`, and the verification flow in `verif/verification-plan/`.

## General Working Rules
- Prefer concise, high-signal work that saves tokens: inspect the minimum context needed, avoid repeating known information, and do not expand scope without a reason.
- Use readable code that a human can maintain. Favor clarity over cleverness, and follow good C++ practices for naming, ownership, const-correctness, and separation of concerns.
- Preserve the variable naming style and formatting already used in the file or module being edited.
- Ask the user before implementing when the goal, scope, behavior, or constraints are ambiguous, or when the change could affect surrounding code.
- If something is unclear or implementation details are ambiguous, say so explicitly and do not execute it blindly.
- Use the repository documents to resolve requirements, architecture, tests, and operational questions whenever the code is not explicit.
- If you detect a missing rule, gap, or contradiction in the documentation, mention it before proceeding.
- Before starting a new session or task, ask for the objective and keep the work scoped to that objective. If the scope expands, stop and confirm whether to continue in the same thread or split the work and save the change of scope in memory when useful.
- Treat these rules as general guidance for tech projects, not only for this repository, and adapt them to the local project documentation and conventions.

## New File Structure
When creating a new C++ class or header, prefer this order unless the existing project style requires otherwise:
- Base constructors.
- Copy constructor.
- Getters.
- Setters.
- Methods.
- Operator overloads.

Keep the ordering consistent inside the file and avoid mixing sections unless the current file already uses a different stable format.

## Current code map
- `src/client/main.cpp` and `src/server/main.cpp` are the entry points.
- `src/server/request_handler.cpp` translates protocol messages into domain operations.
- `src/domain/job_manager.cpp` owns submission, duplicate detection, dispatch, cancellation, and state transitions.
- `src/domain/in_memory_job_store.cpp` is the current persistence boundary and is still in-memory only.
- `src/process/posix_process_launcher.cpp` manages `fork`, `exec`, signals, and child reaping.
- `src/io/reactor.cpp`, `src/network/connection.cpp`, and `src/network/unix_socket_listener.cpp` implement the current event loop and local socket transport.

## Non-negotiable repository rules
- Preserve requirement IDs exactly: `RF-XX`, `RNF-XX`, `TC-XXX`, `ADR-XXX`, `CR-XX`, `INC-XX`.
- Every functional change must be traceable to an Issue, a test case, and evidence in `verif/results/<run-id>/`.
- Do not introduce behavior that is not documented in `docs/technical-guide/` or backed by a verification case.
- Keep business rules in the domain layer, transport concerns in the server/network layer, and persistence concerns in the store layer.
- Avoid broad rewrites; change the nearest controlling module first.

## Current gaps to close first
1. Add a reproducible build system. The repo currently has no checked-in build file, so the first step should be a documented, repeatable build entrypoint.
2. Implement queueing and concurrency limits. `JobManager` currently dispatches immediately, so RF-03, RF-05, RF-25, RNF-04, and RNF-07 remain open.
3. Add persistence and recovery. The store is still in-memory, so RF-12, RF-13, RNF-10, and RNF-11 need a real persistence boundary.
4. Add remote/private-network operation. The current transport is Unix-socket only, so RF-18 to RF-22 and RNF-12, RNF-13, RNF-26 still need implementation.
5. Expand status payloads with received, started, and finished timestamps so RF-07 is observable.
6. Complete cancellation policy with timeout and escalation so RF-30 and RNF-30 are verifiable.
7. Add configuration and health reporting so RF-16 and RF-24 can be demonstrated.

## Implementation order
When asked to implement requirements, prefer this order unless an Issue says otherwise:
1. Build and run reproducibility.
2. Persistence and recovery.
3. Queue, concurrency, and saturation handling.
4. Network protocol and remote access control.
5. Cancellation escalation and orphan cleanup.
6. Configuration, health, and operational reporting.
7. Documentation, matrix updates, and evidence collection.

## Editing guidance
- Prefer small edits in the owner module of the behavior.
- Keep serialization in `src/server/request_handler.cpp` and business rules in `src/domain/job_manager.cpp`.
- If a change affects transport or framing, update the network layer and protocol documentation together.
- If a change affects lifecycle or persistence, update the store and the verification artifacts together.
- Do not merge a feature without updating the matching `TC-XXX` case, the traceability matrix, and the relevant docs.

## Verification expectations
- Every requirement change needs a reproducible test or inspection result.
- Store evidence in `verif/results/<run-id>/` with the command, environment, commit, and outcome.
- Update `verif/verification-plan/traceability-matrix.md` whenever RF/RNF coverage changes.
- If a change affects architecture, recovery, network security, or persistence, add or update an ADR in `docs/decisions/`.
- If a change alters user workflow, update `docs/user-guide/USER_MANUAL.md`.

## Requirement-specific reminders
- RF-01 to RF-05: submission, validation, queueing, concurrency, and capacity control.
- RF-06 to RF-10: terminal states, timestamps, status, listing, and cancellation.
- RF-11 to RF-14: stdout/stderr, persistence, recovery, and operational logging.
- RF-15 to RF-17: controlled shutdown, configuration, and client usability.
- RF-18 to RF-22: remote/private operation, framing, and disconnect handling.
- RF-23 to RF-30: limits, health summary, duplicate handling, cancel races, crash handling, and escalation.
- RNF-01 to RNF-34: reproducible Linux build, robustness, security, maintainability, observability, and recovery.

## What to do before editing
- Read the nearest controller file first.
- State one local hypothesis about what controls the behavior.
- Make the smallest change that can test or implement that hypothesis.
- Run the narrowest validation that can confirm or reject the change.

## What to avoid
- Do not silently change request semantics.
- Do not blend remote transport with domain rules.
- Do not mark a requirement as done without test evidence.
- Do not add new behavior without documenting its impact on RF/RNF, ADRs, and tests.