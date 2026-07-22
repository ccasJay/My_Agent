# TUI Slash Command Registry — Design

**Date:** 2026-07-22  
**Status:** Approved for implementation  
**Branch:** `feature/tui-slash-command-registry`

## Context

My_Agent’s TUI already supports three hard-coded session slash commands (`/new`, `/sessions`, `/resume`) via `parse_session_command` and a `switch` in `src/tui.cpp`. Every new meta-command requires editing an enum, a parser if-chain, and the dispatch switch.

This design introduces a **modular, in-process C++ slash-command registry** so handlers can be added without growing that switch. Model tools (`RUN:`) and ACP protocol methods stay out of scope: slash remains a **TUI UI-boundary** concern.

## Goals

- Replace hard-coded session command parsing/dispatch with `SlashRegistry` + `SlashCommand` handlers.
- Migrate `/new`, `/sessions`, `/resume` with **behavior-compatible** error strings.
- Add `/help` that lists registered commands.
- Keep slash work on the **UI thread only**; never send slash lines into the Agent Loop or User history.

## Non-goals (v1)

- Console or ACP slash handling
- Dynamic plugins (`.so` / `.dylib`)
- YAML-driven handlers
- Quoted arguments, flags, aliases, autocomplete
- Mid-run slash (input remains idle-only)
- `/clear`, `/model`, `/config`, and other commands that need new core APIs

## Decisions

| Topic | Choice |
| --- | --- |
| Surface | TUI only |
| Extension model | In-process C++ registry |
| Architecture | Handler interface + registry + `SlashContext` |
| Registration | Explicit `register_builtin_slash_commands` (no static init) |
| Namespace | `swe_agent::tui` |
| Notice titles | Keep `Session command` / `Session error` for compatibility |
| Old API | Delete `session_command.*` (no long-lived shim) |

## Architecture

```text
Idle Enter (tui.cpp start_task)
  → SlashRegistry::dispatch(ctx, line)
       NotACommand → host starts normal agent task
       Unknown / UsageError → notice("Session command", …)
       Ok → handler.execute(ctx, args)
            → SessionManager ops + reload / notices
            → never Agent Loop
```

**Placement:** `swe_agent_tui_support` (and host wiring in `src/tui.cpp`). Session operations continue to use `SessionManager`. Core agent loop and ACP are unchanged.

**Threading:** Construct the registry once on the UI thread; treat it as immutable afterward. No locks. Handlers must not spawn workers.

## API

### Types

- `SlashParseStatus`: `NotACommand`, `Ok`, `UsageError`, `Unknown`
- `SlashParseResult`: status, name, args, error, non-owning `command*`
- `SlashDispatchStatus`: `NotACommand`, `Handled`
- `SlashCommand` (abstract):
  - `name()` without leading `/`
  - optional `aliases()` (empty in v1)
  - `summary()`, `usage()`
  - `validate(args) → optional<string>` error
  - `execute(SlashContext&, args)`
- `SlashContext`:
  - `SessionManager& sessions`
  - `TuiSession& ui`
  - `on_session_view_changed` callback (cache bust after session switch)
  - helpers: `notice`, `notice_error`, `reload_active_session()`
- `SlashRegistry`:
  - `register_command(unique_ptr)`
  - `find`, `list` (registration order for help)
  - `parse`, `dispatch`
- `register_builtin_slash_commands(registry)`

### Parse rules (preserve current behavior)

1. Trim outer whitespace.
2. Empty or first character not `/` → `NotACommand`.
3. Command token is `/` plus a run of non-space characters (`/resumeabcdef12` is one unknown token).
4. Remainder after the first whitespace run is `args` (trimmed).
5. Lookup name without `/` (exact, case-sensitive).
6. Missing command → `Unknown` with `Unknown command: /…` (keep leading `/`).
7. Found → `validate(args)`; failure → `UsageError` with exact current strings for `/resume`.
8. Else → `Ok`.

### Built-in commands

| Command | Behavior |
| --- | --- |
| `/new` | `sessions.new_session()` then `reload_active_session()` |
| `/sessions` | List workspace sessions; notice title `Sessions`; empty → `No sessions in this workspace.` |
| `/resume <prefix>` | Validate args; `sessions.resume`; reload |
| `/help` | List `usage` + `summary` for all commands; notice title `Help` |

`HelpCommand` holds a `const SlashRegistry&` and reads `list()` at execute time (register help last).

### Error handling

| Situation | Notice heading | Body |
| --- | --- | --- |
| Unknown / usage | `Session command` | Existing / usage strings |
| Exception in execute | `Session error` | `e.what()` |
| List / help success | `Sessions` / `Help` | Formatted body |

`dispatch` returns `Handled` whenever the line started as slash-related work (including errors already noticed). Only `NotACommand` lets the host submit a normal task.

## File layout

```text
include/tui/slash_types.hpp
include/tui/slash_command.hpp
include/tui/slash_context.hpp
include/tui/slash_registry.hpp
src/slash_registry.cpp
src/slash_context.cpp
src/slash_builtins.cpp
tests/test_slash_registry.cpp
```

Delete after migration:

- `include/tui/session_command.hpp`
- `src/session_command.cpp`
- `tests/test_session_command.cpp`

## Host integration

In `tui::run`:

1. Build `SlashRegistry` and call `register_builtin_slash_commands`.
2. In `start_task`, build `SlashContext` with `session_manager`, `session`, and a lambda that invalidates `cached_log_revision`.
3. If `dispatch != NotACommand`, clear input and return.
4. Otherwise keep the existing `session.start(task)` path.

## Testing

- Port every case from `tests/test_session_command.cpp` with **identical** error strings.
- Registry unit tests: register, find, list, duplicate name rejection.
- `/help` lists all four built-ins after registration.
- Prefer pure parse/dispatch tests; optional SessionManager integration when cheap.

## How to add a command later

1. Implement a `SlashCommand` subclass (in `slash_builtins.cpp` or a new TU linked into `swe_agent_tui_support`).
2. Register it in `register_builtin_slash_commands`.
3. Add tests and one line in `docs/session-and-tui.md`.
4. Do not modify `agent::run`, ACP, or reintroduce a host-side switch.

## Implementation commits (planned)

1. `docs(slash): design TUI slash command registry`
2. `feat(tui): add SlashRegistry parse and dispatch core`
3. `feat(tui): register builtin slash commands and /help`
4. `refactor(tui): dispatch slash via registry; drop session_command`
5. `test(tui): cover slash registry and migrate session command cases`
6. `docs(tui): document slash registry and /help`

## Acceptance criteria

- [ ] Former session-command parse behaviors and strings preserved
- [ ] `/help` lists `/new`, `/sessions`, `/resume`, `/help`
- [ ] Non-slash tasks still reach `TuiSession::start`
- [ ] Unknown `/` never hits the model
- [ ] Idle-only submit gate unchanged
- [ ] Docs updated; `[tui]` tests green
