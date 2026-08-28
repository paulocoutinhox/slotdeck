# SlotDeck backlog

This file records work that is deliberately not implemented yet. It is a list of decisions, not a
standard. `CLAUDE.md` remains the authoritative engineering standard and describes only what the
product already guarantees.

## Code Editor language analysis

Every item below is read-only analysis. None of them changes a file.

### Fidelity of what already exists

- Semantic token modifiers are discarded, so `readonly`, `static`, `deprecated` and `declaration`
  paint like any other token of the same type.
- Diagnostics keep only range, severity and message. The reported `code`, `source`, `tags` and
  `relatedInformation` are dropped, so the Problems surface has no code column, no origin column,
  no dimmed unused code and no jump to the related location.
- Completion filters by label instead of by the `filterText` the server declares, and ignores
  `isIncomplete`, so a truncated list is never asked for again while the user types.
- Dynamic capability registration is acknowledged and not honored, so a server that announces a
  capability through `client/registerCapability` instead of the initialize result is treated as if
  it did not have it.

### Analysis features that do not exist

- Call hierarchy and type hierarchy.
- Text search across the whole workspace, which needs no language server at all.
- Folding ranges, selection ranges, document links and the color provider.
- Semantic tokens delta and range requests, so a large file is always requested in full.
- Workspace-wide pull diagnostics through `workspace/diagnostic`.
- Server configuration, both the settings surface and the `workspace/didChangeConfiguration`
  notification that would carry it.

### Excluded by decision

- Inlay hints, because a plain text editor cannot place a glyph that is not in the document without
  changing the file it would then save.

## Code Editor editing

- Rename, formatting, code actions and quick fixes. All four apply a `WorkspaceEdit`, which is one
  shared implementation that must edit several files atomically, including files that are not open,
  with a single undo step and without breaking the language server synchronization.

## Unexplained intermittent failure

- `LanguageServerClientTest.KeepsEveryRequestInsideTheCapabilitiesTheServerDeclares` segfaulted on
  the macOS release runner on 2026-08-20 and again on 2026-08-21, passing on the same runner in the
  pushes around both. The case builds a client for a server that does not exist and never starts it,
  so the transport thread of that client was still finishing while the process exited.
  It never reproduced locally, in more than sixty runs including the sanitizers, so the fix is the
  one this file recorded as the next step rather than a rewrite on a guess: the client registers its
  transport thread while it runs, the plugin drains them at teardown and the suite drains them when
  it ends, and the destructor no longer reaches across threads to disconnect an object it does not
  own, because its own destruction already disconnects everything it receives.
  If it appears again, the next step is to make the transport own its process end to end instead of
  releasing it, so no thread is left with work after the client is gone.
