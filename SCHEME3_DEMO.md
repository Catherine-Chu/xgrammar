# Scheme 3 byte grammar compiler

This branch publishes the XGrammar source used by the approved centralized T04 demo. The native development base is **`97787376faee5ed8466cfad57c99855e4ce2f6aa`** from [mlc-ai/xgrammar](https://github.com/mlc-ai/xgrammar). It does not track the latest upstream main for compatibility. The companion patch filenames retain the historical `0.2.8` label; use the exact base SHA and source checksums to identify this version rather than the automatically derived package version.

The branch contains the cumulative Scheme 3 implementation: optimized byte-grammar compilation, exported/translated compilation plans, four byte-domain adaptations, the existing native worker pool with the added state-safety changes, and an optional native cross-rule cache. It is independent from Scheme 3.1. The latest T04 input/cold-import performance optimization is in the companion Python package; it did not add new XGrammar source changes.

## Source scope

| Files | Purpose |
| --- | --- |
| `cpp/byte_compilation_plan.cc`, `cpp/byte_compilation_plan.h` | Plan, observer, candidate intervals and serialization/validation |
| `cpp/grammar_compiler.cc` | Byte compiler entry, four adaptations, worker safety and rule-cache integration |
| `cpp/grammar_functor.cc`, `cpp/grammar_functor.h` | Translated-domain rule-cache context and transactional publication |
| `cpp/support/container.h` | No-throw move enqueue for publication rollback |
| `cpp/tvm_ffi/tvm_ffi.cc` | FFI registrations |
| `include/xgrammar/compiler.h`, `python/xgrammar/compiler.py` | C++/Python entry points and argument validation |

## Build and paired verification

Initialize submodules with `git submodule update --init --recursive` and build/install XGrammar for the target environment using its existing build system. The source already includes the cumulative patch; do not apply the companion cumulative or incremental patches again. Build once when installing or updating this package, then import the built package when starting the service. Do not copy the locally tested macOS binary to Linux or Ascend.

The companion [vllm-mixed-tokenizer-demo](https://github.com/Catherine-Chu/vllm-mixed-tokenizer-demo/tree/demo/scheme3) repository is private. It provides offline byte translation, the runtime adapter, full-vocabulary fixtures, tests and design documents. The matching vLLM branch is [demo/scheme3-structured-output](https://github.com/Catherine-Chu/vllm/tree/demo/scheme3-structured-output).

- [Pinned versions and handoff](https://github.com/Catherine-Chu/vllm-mixed-tokenizer-demo/blob/demo/scheme3/docs/T04_HANDOFF.md)
- [Compiler design and build steps](https://github.com/Catherine-Chu/vllm-mixed-tokenizer-demo/blob/demo/scheme3/docs/task03_xgrammar_patch.md)
- [Multithreading proof and tests](https://github.com/Catherine-Chu/vllm-mixed-tokenizer-demo/blob/demo/scheme3/docs/task03_multithreading.md)
- [Cross-rule cache proof and tests](https://github.com/Catherine-Chu/vllm-mixed-tokenizer-demo/blob/demo/scheme3/docs/task03_rule_cache.md)
- [Regression source and result manifest](https://github.com/Catherine-Chu/vllm-mixed-tokenizer-demo/blob/demo/scheme3/docs/results/task04opt/review-manifest.json)

Relevant companion tests are `test_task03_grammar.py`, `test_task03_thread_native.py`, `test_task03_rule_cache.py`, `test_task03_rule_cache_native.py` and `test_task03_gate.py`; the complete T04 regression entry is `scripts/run_task04opt_regression.py`. Keeping these tests with the companion project preserves their fixtures and cross-repository contracts.

All nine published implementation files match the approved patched checkout byte for byte. The latest approved regression passed 219 unit tests, 180 real-model HTTP functional requests and 82 lifecycle calls under their expected result contracts, using a local CPU random one-block model and full target vocabulary. This publication is not a fresh fork-package build or a 910B3, MTP, multimodal or arbitrary-grammar validation. Existing provenance-gated E2E drivers expect the original base HEAD plus the exact patch scope; follow the companion handoff for that reproduction route rather than removing the gate.
