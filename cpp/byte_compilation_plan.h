/*!
 * Copyright (c) 2026 by Contributors
 * \file byte_compilation_plan.h
 * \brief Version-bound scheme-3 compiler sidecars; no inverse byte map is retained.
 */
#ifndef XGRAMMAR_BYTE_COMPILATION_PLAN_H_
#define XGRAMMAR_BYTE_COMPILATION_PLAN_H_

#include <bitset>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "grammar_functor.h"
#include "grammar_impl.h"
#include "support/json_serializer.h"
#include "xgrammar/tokenizer_info.h"

namespace xgrammar {

struct ByteIntervalPlan {
  int64_t rule_id = -1;
  int64_t state_id = -1;
  int64_t possible_token_num = 0;
  std::vector<std::pair<int64_t, int64_t>> ranges;
};

struct ByteObserverNode {
  bool completed = false;
  // target=-1 means native Advance returned false; a missing edge is a domain error.
  std::vector<std::pair<int64_t, int64_t>> edges;
};

struct ByteLookaheadObserver {
  int64_t lookahead_id = -1;
  bool initial_completed = false;
  std::vector<ByteObserverNode> nodes;
};

struct ByteCompilationPlan {
  std::string format = "xgrammar-mixed-byte-plan-v1";
  int64_t vocab_size = 0;
  std::vector<int64_t> sorted_token_ids;
  std::vector<int64_t> trie_subtree_nodes_range;
  std::vector<int64_t> ascii_bytes;
  // Optional for legacy plans without character budgets. These are the images
  // of the 192 native non-continuation bytes, not an inverse byte permutation.
  std::vector<int64_t> utf8_start_bytes;
  std::vector<ByteIntervalPlan> intervals;
  std::vector<ByteLookaheadObserver> observers;

  // Derived lookup data are deliberately not part of the transport representation.
  std::bitset<256> ascii_mask;
  std::optional<std::bitset<256>> utf8_start_mask;
  std::unordered_map<uint64_t, size_t> interval_index;
  std::unordered_map<int64_t, size_t> observer_index;

  void ValidateAndIndex(
      const Grammar& grammar, const TokenizerInfo& tokenizer, bool experimental_unproven = false
  );
  const ByteIntervalPlan& GetIntervals(int32_t rule_id, int32_t state_id) const;
  std::pair<bool, bool> Probe(
      int32_t lookahead_id, const std::string& token, const std::vector<bool>& end_positions
  ) const;
  std::string SerializeJSON() const;
  static std::shared_ptr<ByteCompilationPlan> DeserializeJSON(const std::string& json);
};

// The explicit diagnostic mode skips only proven-scope gates. Structural safety
// and plan/tokenizer consistency remain mandatory; no semantic metadata is removed.
void ValidateOptimizedByteGrammar(const Grammar& grammar, bool experimental_unproven = false);
ByteCompilationPlan BuildByteCompilationPlan(
    const Grammar& grammar,
    const TokenizerInfo& tokenizer,
    int64_t max_observer_nodes,
    bool experimental_unproven = false
);

// Build each dependency-closed rule context once. Global rule/state numbers,
// source AST text and unrelated rules are excluded so different grammars can
// actually share an identical subrule. A state suffix adds the entry role and
// its native candidate events. Call only after ValidateAndIndex has completed.
struct ByteRuleCacheContext {
  std::shared_ptr<const std::string> bytes;
  uint64_t hash;
  std::unordered_map<int32_t, int32_t> state_ids;
};
std::vector<ByteRuleCacheContext> BuildByteRuleCacheContexts(
    const Grammar& grammar, const ByteCompilationPlan& plan
);
RuleLevelCache::ByteKey MakeByteRuleCacheKey(
    const ByteRuleCacheContext& context,
    const ByteCompilationPlan& plan,
    int32_t rule_id,
    int32_t state_id,
    bool is_root
);

XGRAMMAR_MEMBER_TABLE(
    ByteIntervalPlan,
    "rule_id",
    &ByteIntervalPlan::rule_id,
    "state_id",
    &ByteIntervalPlan::state_id,
    "possible_token_num",
    &ByteIntervalPlan::possible_token_num,
    "ranges",
    &ByteIntervalPlan::ranges
);
XGRAMMAR_MEMBER_TABLE(
    ByteObserverNode, "completed", &ByteObserverNode::completed, "edges", &ByteObserverNode::edges
);
XGRAMMAR_MEMBER_TABLE(
    ByteLookaheadObserver,
    "lookahead_id",
    &ByteLookaheadObserver::lookahead_id,
    "initial_completed",
    &ByteLookaheadObserver::initial_completed,
    "nodes",
    &ByteLookaheadObserver::nodes
);
XGRAMMAR_MEMBER_TABLE(
    ByteCompilationPlan,
    "format",
    &ByteCompilationPlan::format,
    "vocab_size",
    &ByteCompilationPlan::vocab_size,
    "sorted_token_ids",
    &ByteCompilationPlan::sorted_token_ids,
    "trie_subtree_nodes_range",
    &ByteCompilationPlan::trie_subtree_nodes_range,
    "ascii_bytes",
    &ByteCompilationPlan::ascii_bytes,
    "utf8_start_bytes",
    &ByteCompilationPlan::utf8_start_bytes,
    "intervals",
    &ByteCompilationPlan::intervals,
    "observers",
    &ByteCompilationPlan::observers
);

}  // namespace xgrammar
#endif  // XGRAMMAR_BYTE_COMPILATION_PLAN_H_
