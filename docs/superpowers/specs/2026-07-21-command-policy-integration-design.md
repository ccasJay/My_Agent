# Command Policy Integration Design

## Goal

Complete the command-policy integration across TUI and Console so every model-generated shell command receives the same policy evaluation, commands that cannot be analyzed safely require review, and every rejection is visible to the user and to the model.

## Scope

This design covers:

- conservative command classification in `CommandPolicy`;
- shared mapping from policy results to command decisions;
- TUI Auto/Review behavior;
- interactive and non-interactive Console behavior;
- structured command-rejection events and user-visible feedback;
- unit and integration tests for all decision paths;
- correction of outdated command-approval documentation.

It does not attempt to implement a complete POSIX shell parser, persist approval choices, add configurable policy files, or terminate commands that are already running.

## Decision Matrix

| Policy result | TUI Auto | TUI Review | Console TTY | Console non-TTY |
| --- | --- | --- | --- | --- |
| `Allow` | Approve | Ask user | Approve | Approve |
| `RequireReview` | Ask user | Ask user | Ask user | Reject |
| `Deny` | Reject | Reject | Reject | Reject |

`Deny` cannot be overridden. `RequireReview` can be approved only when an interactive reviewer is available. Console mode is considered interactive only when standard input is a TTY; EOF while prompting is a rejection.

## Architecture

The integration keeps policy, authorization orchestration, and user interaction separate:

```text
model command
    |
    v
CommandPolicy ----> Allow / RequireReview / Deny
    |
    v
shared command authorization
    |                 |
    |                 +----> immediate Approve or Reject
    v
frontend reviewer callback
    |                 |
    +----> TUI Y/N    +----> Console TTY Y/N
    |
    v
CommandDecision
    |
    v
Agent Loop ----> CommandRejected event / Observation / run_shell
```

### Command policy

`command_policy` remains a synchronous, non-interactive classifier. It receives the command and `PolicyContext`; it never reads terminal input, blocks on UI state, or executes a command.

Classification is deliberately conservative:

- A directly invoked denied executable such as `rm`, `shutdown`, or `reboot`, including an absolute executable path, returns `Deny`.
- A single clearly identified executable not covered by a dangerous rule returns `Allow`.
- Commands containing shell composition or indirection return `RequireReview`. This includes command separators, logical operators, pipelines, redirections, command substitution, embedded newlines, and wrapper programs such as `sudo`, `sh -c`, `bash -c`, or `zsh -c`.
- Empty or otherwise unrecognizable commands return `RequireReview`.

The classifier is not presented as a security-complete shell parser. Conservative false positives are acceptable: ambiguous input must be reviewed instead of being automatically approved.

### Shared command authorization

A focused agent-layer component owns the mapping between `PolicyResult` and `CommandDecision`. It accepts:

- a `CommandRequest`;
- a `PolicyContext`;
- whether otherwise-allowed commands must also be reviewed;
- an optional reviewer callback.

Its behavior is deterministic:

1. Evaluate the policy.
2. Convert `Deny` directly to `Reject`.
3. Convert `Allow` directly to `Approve` unless review-all mode is active.
4. For `RequireReview` or review-all mode, invoke the reviewer when available.
5. If review is required but no reviewer is available, return `Reject` with an explicit reason.

The returned decision retains the policy rule identifier and reason. A user rejection receives a stable user-rejection rule identifier. This metadata supports consistent events and logs without requiring frontends to repeat policy logic.

### TUI adapter

`TuiSession` supplies the reviewer callback. The callback reuses the existing condition-variable workflow:

- set the pending command in `TuiState`;
- notify the UI thread;
- wait for Y, N, or a stop request;
- return `Approve`, `Reject`, or `Stop`.

TUI Review mode sets review-all behavior. TUI Auto mode reviews only `RequireReview`; it cannot bypass `Deny`.

`PolicyContext` remains immutable for a `TuiSession`. Its working directory and workspace root both use the normalized workspace currently supplied by `SessionManager`, because each shell invocation starts from the agent process working directory and shell `cd` state is not persistent between invocations.

### Console adapter

Console mode uses the same shared authorization component in Auto semantics:

- `Allow` is approved without prompting.
- `Deny` is rejected without prompting.
- `RequireReview` prompts only when standard input is a TTY.
- Non-TTY execution and EOF reject with a reason explaining that interactive review is unavailable.

Prompts are written to standard error so command output on standard output remains usable by scripts. The prompt accepts case-insensitive `y`/`yes` and `n`/`no`; invalid input is prompted again while the TTY remains available.

TTY detection stays in `main.cpp`. The prompt loop itself lives in a small
`app_cli` helper that accepts input/output streams plus the detected
interactive flag. This keeps terminal interaction deterministic in unit tests
without opening a real pseudo-terminal.

## Rejection Data Flow

Every rejected command, whether rejected by policy, by a user, or because no interactive reviewer is available, follows one path:

1. The authorizer returns `CommandDecision::Reject` with command-policy metadata.
2. The Agent Loop emits a `CommandRejected` event before continuing.
3. The Agent Loop appends an Observation containing the rejection reason to history so the model can choose a safer alternative.
4. TUI renders the event as a system log with the command, rule identifier, and reason.
5. Console renders the same fields to standard error.

The existing TUI approval-resolution method only clears pending approval state. It must not append a second rejection log; the structured event becomes the single display source for both frontends.

A rejection is not itself a terminal Agent status. The loop may continue until the model completes, a stop is requested, the response is empty, or the step limit is reached. Console exit behavior therefore remains tied to the final task status rather than to an individual rejected command.

## Stop and Error Handling

- A stop request takes precedence before policy evaluation, after policy evaluation, and while waiting for a reviewer.
- Policy classification must not throw for arbitrary command text. Ambiguous input returns `RequireReview`.
- A missing reviewer, Console EOF, or non-TTY Console is a normal rejection, not an exception.
- TUI shutdown wakes any reviewer wait through the existing stop source and condition variable.
- Exceptions from a frontend reviewer propagate through the existing task-runner error boundary; they are not silently converted into approval.

## Files and Responsibilities

- `include/agent/command_policy.hpp`, `src/command_policy.cpp`: classify command text only.
- `include/agent/command_authorization.hpp`, `src/command_authorization.cpp`: shared policy-to-decision orchestration.
- `include/agent/agent_event.hpp`: decision metadata and `CommandRejected` event type.
- `include/agent/agent_loop.hpp`: emit rejection event and append rejection Observation.
- `include/tui/tui_session.hpp`, `src/tui_session.cpp`: TUI reviewer adapter and immutable policy context.
- `src/tui_state.cpp`: render rejection events and clear approval state without duplicate logs.
- `src/tui.cpp`: construct TUI authorization context from the active workspace.
- `include/app_cli/command_review.hpp`, `src/command_review.cpp`: stream-based Console reviewer for interactive approval, EOF, and non-interactive rejection.
- `src/main.cpp`: construct Console authorization, detect TTY, connect the Console reviewer, and render rejection events.
- `include/agent/session_manager.hpp`, `src/session_manager.cpp`: expose the normalized workspace already owned by `SessionManager`.
- `CMakeLists.txt`: compile the shared authorization component.
- `tests/test_command_policy.cpp`: policy classification matrix.
- `tests/test_command_authorization.cpp`: shared decision matrix.
- `tests/test_agent_loop.cpp`: rejection event, Observation, and no-shell-execution behavior.
- `tests/test_tui_session.cpp`, `tests/test_tui_state.cpp`: TUI review and visible rejection behavior.
- `tests/test_command_review.cpp`: TTY/non-TTY decision behavior using injected streams rather than a real terminal.
- `docs/tui-integration-changes.md`: replace outdated approval limitations with the implemented behavior and remaining constraints.

## Test Strategy

Testing follows the component boundaries:

1. Policy tests cover direct denied executables, absolute paths, simple allowed commands, compound operators, redirections, command substitution, wrappers, empty commands, and conservative false positives.
2. Shared authorization tests cover every row and frontend mode in the decision matrix, including unavailable reviewers and reviewer Stop results.
3. Agent Loop tests prove that rejection emits exactly one structured event, adds an Observation, and never calls `run_shell` for the rejected command.
4. TUI tests prove that policy denial never opens the approval panel, `RequireReview` opens it even in Auto mode, stop releases the wait, and rejection appears exactly once in logs.
5. `command_review` tests inject TTY state and input streams to cover approve, reject, invalid-input retry, EOF, and non-TTY behavior deterministically.
6. Final verification runs the full build, `ctest --output-on-failure`, and `git diff --check`.

## Success Criteria

- No TUI or Console command reaches `run_shell` without passing shared authorization.
- Auto mode cannot automatically approve ambiguous compound or indirect shell commands.
- `Deny` cannot be overridden in any frontend or mode.
- Console scripts never block waiting for approval when standard input is not a TTY.
- Every rejection is visible exactly once to the user and is returned to the model as an Observation.
- All existing and new tests pass, and documentation matches runtime behavior.
