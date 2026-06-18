# Claude Working Rules

## Roméo knowledge retrieval

When the user asks how Roméo implements a method, semantic rule, scheduling behavior, or source-level mechanism:

1. Check `docs/romeo/*.md` first.
2. If the knowledge docs already answer the question clearly, answer directly without scanning the whole `tools/romeo-2t` tree.
3. If the latest source fact must be confirmed, only do narrow verification:
   - one file;
   - one class;
   - one function body or function-local context;
   - one short call-chain fragment.
4. Prefer answers in the form: document conclusion first, then only the minimal source anchor needed to verify it.
5. Do not perform full-repository rescans for ordinary Roméo implementation questions.

## PTPN / PToPNer knowledge retrieval

When the user asks about this repository's PTPN semantics, state-class construction, time advancement, scheduling / suspension / restore behavior, TDG → PTPN lowering, or PToPNer export limits:

1. Check `docs/ptopner/*.md` first.
2. If those knowledge docs already answer the question clearly, answer directly without scanning the whole repository.
3. If more formal background is needed, next consult only the relevant authority doc:
   - `docs/ptpn-formal-semantics.md`
   - `docs/rule.md`
   - `docs/ptpn-language-spec.md`
   - `docs/json_format.md`
   - `tools/PToPNer/README.md`
4. If the latest implementation fact must be confirmed, only do narrow verification:
   - one file;
   - one class;
   - one function body or function-local context;
   - one short call-chain fragment.
5. Prefer answers in the form: document conclusion first, then only the minimal source anchor needed to verify it.
6. Do not perform full-repository rescans for ordinary PTPN / PToPNer implementation questions.
7. If a knowledge doc and current source disagree, trust the source and then update `docs/ptopner/*.md` so future answers stay docs-first.
8. Scope this policy to the PTPN / PToPNer path only, especially `src/analysis/*`, `src/petri/*`, `src/tdg2pn/*`, `src/tdg2ptopner/*`, and the related docs.
