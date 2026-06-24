# Conformance (golden / characterization tests)

Freezes the **transpiler's generated C++ output** for a set of sample `.hx` apps,
so refactors can be proven *behavior-preserving*: if `hf_core transpile` produces
anything different from the recorded golden, CI fails.

This is the safety net required before refactoring the code generator / extracting
the runtime — it locks current behavior so changes that are meant to be
output-identical can be verified mechanically.

## Layout

- `apps/*.hx` — sample inputs covering the language + runtime surface
  (slices, views, APIs, dynamic routes, validation, relations, jobs, HTTP client,
  `requires`, sqlite backend, middleware/websocket, auth).
- `golden/*.cpp` — recorded transpiler output, one per app.
- `run.sh` — the harness.

## Usage

```bash
make hf_core          # build the compiler core
make golden-verify    # CI: diff current output vs golden (fails on drift)
make golden           # intentionally re-record goldens after a deliberate change
```

Each app is transpiled in an isolated temp dir so stray files in the repo
(`.hexagen_modules/`, `style.css`, `db_*.jsonl`) cannot influence the output.

## When output *should* change

For refactors expected to be output-identical (e.g. moving runtime code into a
header and inlining it), `golden-verify` must stay green untouched.

For changes that legitimately alter the generated code, re-record with
`make golden` **in the same PR** and review the `git diff` of `golden/` as part
of code review — that diff is the behavioral change, made explicit.
