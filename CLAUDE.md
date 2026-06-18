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
