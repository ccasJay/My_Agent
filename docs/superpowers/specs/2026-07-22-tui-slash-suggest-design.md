# TUI Slash Command Suggest Dropdown — Design

**Date:** 2026-07-22  
**Status:** Approved for implementation planning  
**Branch:** `feature/tui-slash-command-registry`  
**Depends on:** [2026-07-22-tui-slash-command-registry-design.md](./2026-07-22-tui-slash-command-registry-design.md)

## Context

The TUI already has a modular `SlashRegistry` with built-ins (`/new`, `/sessions`, `/resume`, `/help`). Users must remember command names; typing `/` does not surface discovery UI.

This design adds an **inline suggest dropdown** when the user is typing a slash token: list registered commands, filter by **command-name prefix**, and **complete into the input buffer** (do not execute on select).

## Goals

- Show a candidate list when the **token under the cursor** starts with `/`.
- Filter candidates with **case-sensitive prefix match** on `SlashCommand::name()`.
- Selecting a candidate **only rewrites** `task_input` (completion); execution still requires a later Enter with the popup closed (existing `dispatch` / agent path).
- Keep filtering pure and unit-testable; keep FTXUI wiring thin in `tui.cpp` / `tui_view`.

## Non-goals

- Argument completion (session id prefixes, flags)
- Substring / fuzzy / summary text search
- Alias matching (aliases remain empty in registry v1)
- Mouse click selection
- Suggest while a task is running or while the approval pane is shown
- FTXUI `Menu` / `Dropdown` focus takeover
- Console or ACP surfaces

## Decisions

| Topic | Choice |
| --- | --- |
| UI approach | Pure filter module + custom DOM list (no FTXUI Menu) |
| When open | Cursor token starts with `/` and filtered matches non-empty |
| Filter | Prefix on canonical `name()` (no leading `/`) |
| Select action | Complete into input only |
| Enter while open | Complete selected item and close (do **not** `start_task`) |
| Tab while open | Same as Enter complete (do **not** switch Prompt/Scrollback) |
| Esc while open | Close list only; keep buffer (second Esc keeps existing clear behavior) |
| Branching | Implement on existing `feature/tui-slash-command-registry` |

## Architecture

```text
task_input + input_cursor
    → evaluate_slash_suggest(input, cursor, registry.list())
         → open, prefix, token range, matches[], selected
    → render suggest panel above / under prompt row
    → CatchEvent (when open): ↑↓ Tab Enter Esc
    → apply_slash_completion → task_input + cursor
    → later Enter (closed) → existing SlashRegistry::dispatch / agent start
```

### Components

| Unit | Responsibility | Dependencies |
| --- | --- | --- |
| `evaluate_slash_suggest` | Token extract + prefix filter | `const SlashCommand*` list only |
| `apply_slash_completion` | Replace token with `/name` or `/name ` | Suggest result |
| `SlashSuggestUiState` (in `tui.cpp` or small struct) | `open`, `selected`, last result | UI thread |
| `render_slash_suggest` | Draw rows with highlight | Pure FTXUI Element |
| `tui.cpp` | `on_change`, key routing, write-back | Registry + Input |

Registry APIs stay read-only for suggest; no change to `parse` / `dispatch` semantics.

## Token model

1. Treat `input` as a byte string; `cursor` is the FTXUI `input_cursor` byte index, clamped to `[0, input.size()]`.
2. Find the token containing the cursor by scanning left/right for ASCII whitespace (`' '`, `'\t'`, etc.). If the cursor sits on whitespace, use the token **to the left** if any; if none, inactive.
3. **Active** only when that token starts with `'/'`.
4. `prefix` = token without the leading `'/'` (may be empty when the user typed only `/`).
5. `token_begin` / `token_end` are byte offsets of the full token including `/`.

Examples:

| Input (│ = cursor) | Active | prefix | Notes |
| --- | --- | --- | --- |
| `│` | no | | |
| `/│` | yes | `""` | all commands |
| `/he│` | yes | `"he"` | `help` |
| `foo /se│` | yes | `"se"` | `sessions` |
| `/resume │x` | no* | | token under cursor is `x` or space—see rule 2 |
| `/resume│` | yes | `"resume"` | still a single token |
| `hello│` | no | | |

\* After `/resume ` with cursor after the space, the current token is empty/non-`/` → popup **closes** so the user can type the id without the list.

## Filtering

- For each command from `list()` (registration order stable):
  - Match if `name().starts_with(prefix)` (C++ `string_view` prefix; case-sensitive).
- Do not match on `summary` or `usage`.
- If matches empty → `open = false`.
- If matches non-empty → `open = true`; keep `selected` in range after refilter (prefer keep same `name` if still present, else clamp to `0`).

## Completion write-back

Replace `[token_begin, token_end)` with:

- `"/{name} "` if `usage()` indicates arguments after the command (v1 rule: `usage` contains a space after the command word, e.g. `/resume <session-id-prefix>`), else
- `"/{name}"` for zero-arg commands (`/new`, `/sessions`, `/help`).

Set `input_cursor` to the end of the inserted text. Close the popup (`open = false`).

Do **not** call `dispatch` or `SessionManager` from completion.

## Keyboard (only when `open`)

| Key | Behavior |
| --- | --- |
| `↓` | `selected = min(selected + 1, n - 1)` (clamp, no wrap) |
| `↑` | `selected = selected == 0 ? 0 : selected - 1` |
| `Tab` | Apply completion for `selected`; consume event (no pane switch) |
| `Enter` | Apply completion for `selected`; consume event (no `start_task`) |
| `Esc` | `open = false`; keep `task_input`; consume event |

When `open` is false, existing bindings apply (history arrows, Tab pane switch, Enter submit, Esc clear).

Force `open = false` when:

- Task starts running or approval UI shows
- Active pane leaves Prompt
- Input is cleared (Ctrl+C / post-submit)
- `on_change` evaluation returns inactive / no matches

## Rendering

- Place the list in the **action/prompt region** (not a full-screen modal): e.g. `vbox({ suggest_box, prompt_row })` or suggest immediately above the prompt border.
- Each row: `/{name}` bold/ink + dim `summary`; selected row inverted or `blueprint` background on paper theme.
- Cap visible rows (e.g. 6); if more matches than cap, show a trailing `…` line (v1 may simply show first 6 given tiny registry).
- Truncate with existing width helpers on Compact/Minimal.
- While open, shortcut strip may mention `↑↓` navigate, `Tab/Enter` complete, `Esc` dismiss (optional, Full density only).

## API sketch

```cpp
// include/tui/slash_suggest.hpp
namespace swe_agent::tui {

struct SlashSuggestItem {
    std::string name;
    std::string usage;
    std::string summary;
};

struct SlashSuggestResult {
    bool open{false};
    std::string prefix;
    std::size_t token_begin{0};
    std::size_t token_end{0};
    std::vector<SlashSuggestItem> matches;
};

[[nodiscard]] SlashSuggestResult evaluate_slash_suggest(
    std::string_view input,
    int cursor,
    const std::vector<const SlashCommand*>& commands);

/** Replace the active token with the selected match. selected must be in range. */
[[nodiscard]] std::string apply_slash_completion(
    std::string_view input,
    const SlashSuggestResult& result,
    std::size_t selected);

[[nodiscard]] int completion_cursor_after(
    std::string_view completed_input) noexcept;

}  // namespace swe_agent::tui
```

`completion_cursor_after` returns `completed_input.size()` for v1 (cursor at end).

## File layout

| Path | Action |
| --- | --- |
| `include/tui/slash_suggest.hpp` | Add |
| `src/slash_suggest.cpp` | Add |
| `tests/test_slash_suggest.cpp` | Add |
| `src/tui.cpp` | Wire `on_change`, keys, state |
| `include/tui/tui_view.hpp` / `src/tui_view.cpp` | Render helper |
| `CMakeLists.txt` / `tests/CMakeLists.txt` | Sources |
| `docs/session-and-tui.md` | Document suggest UX |

## Testing

| Case | Expect |
| --- | --- |
| empty / no slash | `open == false` |
| `/` only | all registered names, order = `list()` |
| `/he` | only `help` |
| `/H` | no match (case-sensitive) → closed |
| `task /se` cursor in `/se` | `sessions` |
| `/resume ` cursor after space | closed |
| apply on `/he` + help | `/help` |
| apply on `/re` + resume | `/resume ` (trailing space) |
| refilter shrinks list | selected clamped |

No required FTXUI automated test for the popup; manual smoke:

1. Type `/` → see four commands  
2. Type `s` → `sessions`  
3. Tab/Enter → buffer becomes `/sessions`  
4. Enter again → command runs  
5. `/re` → complete to `/resume ` → type id → Enter  
6. Esc closes list without clearing `/re`  
7. Second Esc (existing) clears when list already closed  

## Implementation commits (suggested)

1. `feat(tui): add slash suggest evaluate/complete pure helpers`  
2. `feat(tui): render slash suggest panel in prompt region`  
3. `feat(tui): wire slash suggest popup keys and on_change`  
4. `test(tui): cover slash suggest token and prefix cases`  
5. `docs(tui): document slash command dropdown`  

## Acceptance criteria

- [ ] Typing `/` in idle Prompt shows registered commands  
- [ ] Prefix filter updates as the user types  
- [ ] Tab/Enter completes; does not execute until a subsequent Enter with popup closed  
- [ ] Esc dismisses without clearing the buffer  
- [ ] History ↑↓ and pane Tab work when popup is closed  
- [ ] Running/approval never shows the popup  
- [ ] Pure unit tests green; existing `[tui]` / `[slash]` still pass  

## Risks

| Risk | Mitigation |
| --- | --- |
| ↑↓ conflict with prompt history | Gate history on `!open` |
| Tab conflict with pane switch | Gate pane switch on `!open` |
| Enter conflict with submit | Gate `start_task` on `!open` |
| Esc muscle memory | First Esc only closes list; document in shortcuts |
| UTF-8 cursor vs bytes | Commands are ASCII; document byte cursor; clamp carefully |
| `tui.cpp` size | Keep logic in `slash_suggest.cpp` |

## YAGNI

- No scrolling window beyond small lists  
- No ranking beyond registration order  
- No “insert completion and execute” single key for zero-arg commands  
