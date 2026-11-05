@AGENTS.md

## Claude-specific: how to run a session

- **Act as the planner and orchestrator.** Keep the main session's context small:
  delegate implementation, benchmarking, research and long reads to subagents, run
  independent ones in parallel, and keep only their conclusions. Do small checks
  (a few tool calls) inline.
- Brief each subagent fully. It knows only its prompt, so include the goal, the relevant
  files, which slice to use, what to measure, and the rules above (round-trip verification,
  ledger row, attic on failure). Tell implementation agents whether they may commit.
- Long benchmarks run in the background. Don't poll; wait for the notification.
- Before you end a session: the build log entry is written, the ledger is updated, docs match
  the code, failed ideas are in the attic, and everything is committed.
