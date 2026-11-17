# Scheme 3 byte grammar compiler

This branch publishes the XGrammar source used by the approved centralized T04 demo. The native development base is **`97787376faee5ed8466cfad57c99855e4ce2f6aa`** from [mlc-ai/xgrammar](https://github.com/mlc-ai/xgrammar). It does not track the latest upstream main for compatibility. The companion patch filenames retain the historical `0.2.8` label; use the exact base SHA and source checksums to identify this version rather than the automatically derived package version.

The branch contains the cumulative Scheme 3 implementation: optimized byte-grammar compilation, exported/translated compilation plans, four byte-domain adaptations, the existing native worker pool with the added state-safety changes, and an optional native cross-rule cache. It also includes explicit experimental compilation of previously unproved features and the `max_chars` byte-alphabet fix described below. It is independent from Scheme 3.1.

## Source scope

| Files | Purpose |
| --- | --- |
| `cpp/byte_compilation_plan.cc`, `cpp/byte_compilation_plan.h` | Plan, observer, candidate intervals, experimental structural validation and optional character-start predicate |
| `cpp/grammar_compiler.cc` | Byte compiler entry, four adaptations, worker safety, rule-cache integration and binding of character semantics |
| `cpp/compiled_grammar.cc`, `cpp/compiled_grammar_impl.h` | Immutable character-budget view, optional predicate serialization, reconstruction and memory accounting |
| `cpp/earley_parser.cc`, `cpp/earley_parser.h`, `cpp/grammar_matcher.cc` | Object-local character counting in parsing, budget-boundary masks, token acceptance and state restoration |
| `cpp/grammar_functor.cc`, `cpp/grammar_functor.h` | Translated-domain rule-cache context and transactional publication |
| `cpp/support/container.h` | No-throw move enqueue for publication rollback |
| `cpp/tvm_ffi/tvm_ffi.cc` | FFI registrations |
| `include/xgrammar/compiler.h`, `python/xgrammar/compiler.py` | C++/Python entry points and argument validation |

## Experimental admission and frontend options

The specialized `prepare_byte_grammar`, `compile_optimized_byte_grammar` and `inspect_compilation` entry points accept `experimental_unproven=True`; the default remains `False`. The explicit flag relaxes capability-scope rejections so the actual compiler and Matcher can be compared with the native implementation. It does not remove structural validation, prove every newly admitted feature correct, or change the ordinary native compile APIs. Experimental compilation requires `cache_enabled=False`; cross-rule caching remains available only under its separately validated contract.

The companion runtime enables this route with `MIXED_GRAMMAR_EXPERIMENTAL_UNPROVEN=1` and `MIXED_GRAMMAR_RULE_CACHE=0`, set before constructing the service/compiler. Experimental artifacts are explicitly marked and isolated from strict-mode registrations. See the companion [experimental compilation design](https://github.com/Catherine-Chu/vllm-mixed-tokenizer-demo/blob/demo/scheme3/docs/task04_unproven_compile.md) for the producer and service commands.

The whitespace-option fix belongs to the companion prototype, not to XGrammar's JSON frontend. Native vLLM's default `disable_any_whitespace=False` selects `any_whitespace=True, separators=None`; disabling arbitrary whitespace selects `any_whitespace=False, separators=(",", ":")`. The producer now records effective frontend options in the content-addressed artifact. The producer CLI follows the native default unless `--disable-any-whitespace` is supplied. For backward compatibility only, the Python producer API's `frontend_options=None` retains its historical compact configuration; explicit `{}` selects native defaults. Use identical effective options for native/candidate comparisons.

## Character-budget repair and artifact migration

An arbitrary byte permutation does not preserve the native UTF-8 continuation-byte test. For example, under the tested P6 permutation the six-character token `return` was counted as five characters, allowing `return!` under `max_chars=5`. Equal serialized caches alone did not expose this failure because native derived token-character counts and online parser counts were still computed from the permuted bytes.

For a grammar containing `max_chars`, preparation now adds the native 192-byte character-start set to `utf8_start_bytes`. The companion producer maps each element through the same byte permutation, preserving preparation order. Import validates 192 distinct bytes and requires its first 128 entries to equal the mapped ASCII predicate. A `CompiledGrammar` then owns a shared immutable `CharacterBudgetInfo`: a 256-bit predicate, counts in the associated TokenizerInfo's sorted-vocabulary order, and the maximum token count. It shares the existing vocabulary instead of copying or mutating TokenizerInfo. Compiler and runtime parsers use the same predicate; Matcher budget filtering uses the corresponding counts. No process-global or thread-local mapping is installed.

The optional `CompiledGrammar.utf8_start_bytes` JSON field preserves that classification across serialization. Deserialization rebuilds the derived view against the supplied TokenizerInfo. The view is included in `memory_size_bytes`; matchers share it. Native compiled grammars without the field retain native semantics. Non-budget mixed plans omit the new field and retain their previous representation.

**Rebuild old mixed Grammar artifacts that contain character budgets.** A mixed budget plan missing `utf8_start_bytes` is now rejected with a rebuild instruction; accepting it with a native predicate would restore the known error. Existing mixed artifacts without character budgets remain compatible. Updating a binary does not repair a previously exported character-budget Grammar by itself.

The fixed upstream base and serializer label remain `97787376faee5ed8466cfad57c99855e4ce2f6aa` and `v16`. The optional extension is specific to this paired Scheme 3 build; the version label alone is not evidence of compatibility with another upstream or fork. Deploy matching source checksums, companion adapters and artifacts. The repair preserves native behavior, including native best-effort character-budget handling and native limitations; it does not redefine `max_chars`. The experimental `qwen_xml` case also still lacks the native TagDispatch compilation fast path after AST sanitization, even where its final cache and tested masks agree.

## Build and paired verification

Initialize submodules with `git submodule update --init --recursive` and build/install XGrammar for the target environment using its existing build system. The source already includes the cumulative patch; do not apply the companion cumulative or incremental patches again. Build once when installing or updating this package, then import the built package when starting the service. Do not copy the locally tested macOS binary to Linux or Ascend.

The companion [vllm-mixed-tokenizer-demo](https://github.com/Catherine-Chu/vllm-mixed-tokenizer-demo/tree/demo/scheme3) repository is private. It provides offline byte translation, the runtime adapter, full-vocabulary fixtures, tests and design documents. The matching vLLM branch is [demo/scheme3-structured-output](https://github.com/Catherine-Chu/vllm/tree/demo/scheme3-structured-output).

- [Pinned versions and handoff](https://github.com/Catherine-Chu/vllm-mixed-tokenizer-demo/blob/demo/scheme3/docs/CHAR_BUDGET_HANDOFF.md)
- [Compiler design and build steps](https://github.com/Catherine-Chu/vllm-mixed-tokenizer-demo/blob/demo/scheme3/docs/task03_xgrammar_patch.md)
- [Multithreading proof and tests](https://github.com/Catherine-Chu/vllm-mixed-tokenizer-demo/blob/demo/scheme3/docs/task03_multithreading.md)
- [Cross-rule cache proof and tests](https://github.com/Catherine-Chu/vllm-mixed-tokenizer-demo/blob/demo/scheme3/docs/task03_rule_cache.md)
- [Character-budget repair and verification](https://github.com/Catherine-Chu/vllm-mixed-tokenizer-demo/blob/demo/scheme3/docs/task04_char_budget_fix.md)
- [Experimental coverage and observed limits](https://github.com/Catherine-Chu/vllm-mixed-tokenizer-demo/blob/demo/scheme3/docs/task04_unproven_results.md)
- [Regression source and result manifest](https://github.com/Catherine-Chu/vllm-mixed-tokenizer-demo/blob/demo/scheme3/docs/results/task04-char-budget/review-manifest.json)

Relevant companion tests are `test_task03_grammar.py`, `test_task03_thread_native.py`, `test_task03_rule_cache.py`, `test_task03_rule_cache_native.py`, `test_task03_gate.py`, `test_task03_experimental_native.py`, `test_task04_unproven_compile.py` and `test_task04_char_budget.py`. The earlier T04 performance regression entry is `scripts/run_task04opt_regression.py`. Keeping these tests with the companion project preserves their fixtures and cross-repository contracts. The native `tests/python/test_max_chars.py` suite covers native character-budget behavior.

All 14 cumulative implementation files match the frozen development checkout and the companion `patches/xgrammar-0.2.8-scheme3-byte-grammar.manifest.json` byte for byte. The matching cumulative patch SHA256 is `54d5e4d73b2b878e2dbe2854703691f72916747f89f40f823dd1aef358ec8ea3` (80,458 bytes). This branch already contains these changes; the patch is an alternative installation route, not another patch to apply here.

The current repair passed the companion 247-test unit regression; the native character-budget suite passed 18 tests. The earlier approved T04 regression passed 219 unit tests, 180 model HTTP functional requests and 82 lifecycle calls using a local CPU random one-block model and full target vocabulary. Those historical HTTP counts are not measurements of this repair. Use the companion repair report for its final grammar-matrix and HTTP evidence. This publication is not a fresh fork-package build or a 910B3, MTP, multimodal or arbitrary-grammar validation. Existing provenance-gated E2E drivers expect the original base HEAD plus the exact patch scope; follow the companion handoff for that reproduction route rather than removing the gate.
