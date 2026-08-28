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

## The reaper a destructor builds

The POSIX backend reaps its child through a singleton whose thread is created the first time a
terminal is terminated, and that first call is reached from `~PosixPtyBackend`. A destructor is
implicitly noexcept, so a thread the system refuses to create ends the process rather than being
reported.

Moving the construction to `start()` only moves the throw, because this project has no try and no
catch anywhere and its whole error model is `utils::Result`. Handling it would mean the one
exception handler in the codebase, for a condition where the system is already refusing threads.

The boundary is named here rather than hidden. If it has to be answered, the step is to reap the
child from the event loop the backend already runs on instead of from a thread of its own.

## The thread sanitizer

Running the registered suite under `-fsanitize=thread` on 2026-08-28 reported 298 warnings and
every one of them has the same shape: an object this project constructs on one thread and hands to
Qt, which uses it on another. The write is our constructor or our lambda, the read is
`QThreadPool::run`, `QObject::event` or a future continuation, and the happens-before edge between
them lives inside QtCore.

Qt is consumed as a shared build that was not compiled with the thread sanitizer, so the tool cannot
see the mutexes and atomics that publish those objects and reports the handoff as a race. Reading
one report of each shape found no access this project makes without the synchronization Qt provides.

Using the tool here would need a thread-sanitized Qt, which is not what the product ships against,
so the sanitizer configuration stays AddressSanitizer with UndefinedBehaviorSanitizer. If the
question returns, the step is to instrument Qt rather than to read these reports again.

## Unexplained intermittent failure

- The case `CodeWorkspaceViewTest.SurvivesManyDocumentsOpenedEditedSavedAndClosedInOneWorkspace`
  ended with SIGTRAP once on 2026-08-28, on the local machine, inside a run of the whole suite at ten parallel
  jobs. It has not returned in twelve runs of that case alone, eighteen concurrent runs of it beside
  the other stress cases, or the full suite since, in Debug and under the sanitizers.
  Nothing points at a cause yet. The next step, if it returns, is to capture the output of the run
  that traps rather than only its status, because SIGTRAP on this platform is a fatal from Qt or an
  assertion rather than a memory fault the sanitizers would already have named.

- The case `LanguageServerClientTest.KeepsEveryRequestInsideTheCapabilitiesTheServerDeclares`
  segfaulted on the macOS release runner on 2026-08-20 and again on 2026-08-21, passing on the same runner in the
  pushes around both. The case builds a client for a server that does not exist and never starts it,
  so the transport thread of that client was still finishing while the process exited.
  It never reproduced locally, in more than sixty runs including the sanitizers, so the fix is the
  one this file recorded as the next step rather than a rewrite on a guess: the client registers its
  transport thread while it runs, the plugin drains them at teardown and the suite drains them when
  it ends, and the destructor no longer reaches across threads to disconnect an object it does not
  own, because its own destruction already disconnects everything it receives.
  If it appears again, the next step is to make the transport own its process end to end instead of
  releasing it, so no thread is left with work after the client is gone.
