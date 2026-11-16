/*!
 * Copyright (c) 2026 by Contributors
 * \file byte_compilation_plan.cc
 * \brief Preserve native compiler observations across an arbitrary byte bijection.
 */
#include "byte_compilation_plan.h"

#include <algorithm>
#include <functional>
#include <limits>
#include <map>
#include <set>
#include <stdexcept>
#include <unordered_set>

#include "earley_parser.h"
#include "support/json_parse.h"

namespace xgrammar {

// Reuse the actual native interval implementation, including its empty ranges and ordering.
int GetPossibleTokenIntervals(
    const std::vector<std::pair<int32_t, std::string>>& vocab,
    const std::bitset<256>& first_chars,
    std::vector<std::pair<int32_t, int32_t>>& intervals
);

namespace {
constexpr int64_t kMaxPlanBytes = 64 * 1024 * 1024;
constexpr int64_t kMaxObserverNodes = 10000000;

void Require(bool condition, const char* message) {
  if (!condition) throw std::invalid_argument(std::string("mixed byte plan: ") + message);
}

uint64_t StateKey(int64_t rule, int64_t state) {
  return (static_cast<uint64_t>(rule) << 32) | static_cast<uint32_t>(state);
}

std::set<std::pair<int32_t, int32_t>> ScanableStates(const Grammar& grammar) {
  std::set<std::pair<int32_t, int32_t>> states;
  for (int32_t rule = 0; rule < grammar->NumRules(); ++rule) {
    const auto& fsm = grammar->per_rule_fsms[rule]->GetFsm();
    std::unordered_set<int> reachable;
    fsm.GetReachableStates(&reachable);
    for (int state : reachable) {
      if (fsm.IsScanableState(state)) states.emplace(rule, state);
    }
  }
  return states;
}

std::set<int32_t> LookaheadIds(const Grammar& grammar) {
  std::set<int32_t> result;
  for (int32_t rule = 0; rule < grammar->NumRules(); ++rule) {
    int32_t id = grammar->GetRule(rule).lookahead_assertion_id;
    if (id != -1) result.insert(id);
  }
  return result;
}

struct QueryNode {
  std::map<uint8_t, int32_t> edges;
};

std::vector<QueryNode> BuildSuffixDomain(const TokenizerInfo& tokenizer, int64_t limit) {
  std::vector<QueryNode> domain(1);
  for (const auto& entry : tokenizer.GetSortedDecodedVocab()) {
    const auto& token = entry.second;
    // Every native lookahead query is a suffix of a fixed vocabulary token.
    Require(token.size() <= 4096, "token exceeds the observer recursion depth limit");
    for (size_t start = 0; start < token.size(); ++start) {
      int32_t node = 0;
      for (size_t i = start; i < token.size(); ++i) {
        uint8_t byte = static_cast<uint8_t>(token[i]);
        auto found = domain[node].edges.find(byte);
        if (found == domain[node].edges.end()) {
          Require(static_cast<int64_t>(domain.size()) < limit, "suffix domain node limit exceeded");
          int32_t child = static_cast<int32_t>(domain.size());
          domain[node].edges.emplace(byte, child);
          domain.emplace_back();
          node = child;
        } else {
          node = found->second;
        }
      }
    }
  }
  return domain;
}

ByteLookaheadObserver BuildObserver(
    const Grammar& grammar,
    int32_t lookahead_id,
    const std::vector<QueryNode>& domain,
    int64_t limit,
    int64_t* total_nodes
) {
  // This is the native AST helper, including its observed UTF-8 behaviour. Do not
  // replace it with a mathematically cleaner Unicode DFA under the strict contract.
  EarleyParser parser(grammar, ParserState(-1, lookahead_id, 0, ParserState::kNoPrevInputPos, 0));
  ByteLookaheadObserver result;
  result.lookahead_id = lookahead_id;
  result.initial_completed = parser.IsCompleted();
  Require(++*total_nodes <= limit, "observer node limit exceeded");
  result.nodes.push_back({result.initial_completed, {}});
  std::function<void(int32_t, int32_t)> visit = [&](int32_t query_node, int32_t output_node) {
    // The native probe returns at the first completion; later bytes are never queried.
    if (result.nodes[output_node].completed) return;
    for (const auto& [byte, query_child] : domain[query_node].edges) {
      if (!parser.Advance(byte)) {
        result.nodes[output_node].edges.emplace_back(byte, -1);
        continue;
      }
      Require(++*total_nodes <= limit, "observer node limit exceeded");
      int32_t output_child = static_cast<int32_t>(result.nodes.size());
      result.nodes[output_node].edges.emplace_back(byte, output_child);
      result.nodes.push_back({parser.IsCompleted(), {}});
      visit(query_child, output_child);
      parser.PopLastStates(1);
    }
  };
  visit(0, 0);
  return result;
}
}  // namespace

void ValidateOptimizedByteGrammar(const Grammar& grammar) {
  Require(!grammar.IsNull(), "missing grammar");
  if (auto error = grammar->Validate()) throw std::invalid_argument(*error);
  Require(grammar->optimized, "an already optimized grammar is required");
  for (int32_t rule_id = 0; rule_id < grammar->NumRules(); ++rule_id) {
    const auto& rule = grammar->GetRule(rule_id);
    Require(
        grammar->GetSuffixStopInfo(rule_id) == nullptr, "suffix/capture metadata is unsupported"
    );
    Require(rule.max_tokens == -1 && rule.max_chars == -1, "rule budgets are unsupported");
    Require(
        rule.capture_name.empty() && !rule.is_lazy && !rule.temperature.has_value(),
        "capture, lazy, and temperature rules are unsupported"
    );
    auto type = grammar->GetGrammarExpr(rule.body_expr_id).type;
    Require(
        type != Grammar::Impl::GrammarExprType::kTagDispatch &&
            type != Grammar::Impl::GrammarExprType::kTokenTagDispatch,
        "tag dispatch is unsupported"
    );
  }
  auto check_edges = [&](const CompactFSM& fsm) {
    for (int32_t state = 0; state < fsm.NumStates(); ++state) {
      for (const auto& edge : fsm.GetEdges(state)) {
        Require(
            !edge.IsToken() && !edge.IsExcludeToken(),
            "explicit Token/ExcludeToken edges are unsupported"
        );
        Require(
            edge.IsCharRange() || edge.IsEpsilon() || edge.IsRuleRef() || edge.IsRepeatRef() ||
                edge.IsEOS(),
            "unknown FSM edge type"
        );
        Require(
            !edge.IsCharRange() || (edge.min <= edge.max && edge.max <= 255),
            "invalid byte FSM edge"
        );
      }
    }
  };
  check_edges(grammar->complete_fsm);
  for (const auto& fsm : grammar->per_rule_fsms) check_edges(fsm->GetFsm().GetFsm());
}

ByteCompilationPlan BuildByteCompilationPlan(
    const Grammar& grammar, const TokenizerInfo& tokenizer, int64_t max_observer_nodes
) {
  ValidateOptimizedByteGrammar(grammar);
  Require(
      max_observer_nodes > 0 && max_observer_nodes <= kMaxObserverNodes,
      "observer node budget must be in [1, 10000000]"
  );
  ByteCompilationPlan plan;
  plan.vocab_size = tokenizer.GetVocabSize();
  for (const auto& entry : tokenizer.GetSortedDecodedVocab())
    plan.sorted_token_ids.push_back(entry.first);
  const auto& trie = tokenizer.GetTrieSubtreeNodesRange();
  plan.trie_subtree_nodes_range.assign(trie.begin(), trie.end());
  for (int64_t i = 0; i < 128; ++i) plan.ascii_bytes.push_back(i);
  for (const auto& [rule, state] : ScanableStates(grammar)) {
    std::bitset<256> first_chars;
    const auto& fsm = grammar->per_rule_fsms[rule]->GetFsm().GetFsm();
    for (const auto& edge : fsm.GetEdges(state)) {
      if (edge.IsCharRange())
        for (int byte = edge.min; byte <= edge.max; ++byte) first_chars.set(byte);
    }
    std::vector<std::pair<int32_t, int32_t>> ranges;
    int count = GetPossibleTokenIntervals(tokenizer.GetSortedDecodedVocab(), first_chars, ranges);
    ByteIntervalPlan interval;
    interval.rule_id = rule;
    interval.state_id = state;
    interval.possible_token_num = count;
    interval.ranges.assign(ranges.begin(), ranges.end());
    plan.intervals.push_back(std::move(interval));
  }
  auto ids = LookaheadIds(grammar);
  if (!ids.empty()) {
    auto domain = BuildSuffixDomain(tokenizer, max_observer_nodes);
    int64_t total_nodes = 0;
    for (int32_t id : ids) {
      plan.observers.push_back(
          BuildObserver(grammar, id, domain, max_observer_nodes, &total_nodes)
      );
    }
  }
  plan.ValidateAndIndex(grammar, tokenizer);
  return plan;
}

void ByteCompilationPlan::ValidateAndIndex(const Grammar& grammar, const TokenizerInfo& tokenizer) {
  ValidateOptimizedByteGrammar(grammar);
  Require(format == "xgrammar-mixed-byte-plan-v1", "unsupported plan ABI");
  Require(vocab_size == tokenizer.GetVocabSize(), "vocabulary width mismatch");
  const auto& vocab = tokenizer.GetSortedDecodedVocab();
  Require(sorted_token_ids.size() == vocab.size(), "reference index size mismatch");
  Require(trie_subtree_nodes_range.size() == vocab.size(), "reference trie size mismatch");
  const auto& trie = tokenizer.GetTrieSubtreeNodesRange();
  for (size_t i = 0; i < vocab.size(); ++i) {
    Require(sorted_token_ids[i] == vocab[i].first, "reference token index order mismatch");
    Require(
        trie_subtree_nodes_range[i] == trie[i] && trie[i] > static_cast<int64_t>(i) &&
            trie[i] <= static_cast<int64_t>(vocab.size()),
        "reference trie range mismatch"
    );
  }
  Require(ascii_bytes.size() == 128, "mapped ASCII predicate must have 128 bytes");
  ascii_mask.reset();
  for (int64_t byte : ascii_bytes) {
    Require(byte >= 0 && byte <= 255 && !ascii_mask[byte], "invalid or duplicate ASCII byte");
    ascii_mask.set(byte);
  }
  auto expected_states = ScanableStates(grammar);
  Require(intervals.size() == expected_states.size(), "interval state coverage mismatch");
  interval_index.clear();
  for (size_t index = 0; index < intervals.size(); ++index) {
    const auto& interval = intervals[index];
    Require(
        interval.rule_id >= 0 && interval.rule_id < grammar->NumRules() && interval.state_id >= 0 &&
            interval.state_id <= std::numeric_limits<int32_t>::max(),
        "interval state out of range"
    );
    Require(
        expected_states.count(
            {static_cast<int32_t>(interval.rule_id), static_cast<int32_t>(interval.state_id)}
        ) != 0,
        "interval refers to an uncompiled state"
    );
    Require(
        interval_index.emplace(StateKey(interval.rule_id, interval.state_id), index).second,
        "duplicate interval state"
    );
    Require(!interval.ranges.empty(), "missing byte candidate intervals");
    int64_t count = 0;
    int64_t previous_end = 0;
    for (const auto& range : interval.ranges) {
      Require(
          range.first >= previous_end && range.second >= range.first &&
              range.second <= static_cast<int64_t>(vocab.size()),
          "invalid candidate interval"
      );
      count += range.second - range.first;
      previous_end = range.second;
    }
    Require(count == interval.possible_token_num, "possible token count mismatch");
  }
  auto ids = LookaheadIds(grammar);
  Require(observers.size() == ids.size(), "lookahead observer coverage mismatch");
  observer_index.clear();
  int64_t total_nodes = 0;
  for (size_t index = 0; index < observers.size(); ++index) {
    const auto& observer = observers[index];
    Require(
        observer.lookahead_id >= 0 && observer.lookahead_id < grammar->NumGrammarExprs() &&
            ids.count(static_cast<int32_t>(observer.lookahead_id)) != 0,
        "unknown observer id"
    );
    Require(observer_index.emplace(observer.lookahead_id, index).second, "duplicate observer id");
    Require(
        !observer.nodes.empty() && observer.nodes[0].completed == observer.initial_completed,
        "invalid observer initial state"
    );
    total_nodes += observer.nodes.size();
    Require(total_nodes <= kMaxObserverNodes, "observer node limit exceeded");
    std::vector<int32_t> parents(observer.nodes.size(), 0);
    for (size_t node_id = 0; node_id < observer.nodes.size(); ++node_id) {
      const auto& node = observer.nodes[node_id];
      Require(!node.completed || node.edges.empty(), "observer continues after native completion");
      std::bitset<256> labels;
      for (const auto& [byte, child] : node.edges) {
        Require(byte >= 0 && byte <= 255 && !labels[byte], "invalid or duplicate observer byte");
        labels.set(byte);
        Require(
            child == -1 || (child > static_cast<int64_t>(node_id) &&
                            child < static_cast<int64_t>(observer.nodes.size())),
            "observer target must be a later node or failure"
        );
        if (child >= 0) Require(++parents[child] == 1, "observer node has multiple parents");
      }
    }
    for (size_t i = 1; i < parents.size(); ++i)
      Require(parents[i] == 1, "unreachable observer node");
  }
}

const ByteIntervalPlan& ByteCompilationPlan::GetIntervals(int32_t rule_id, int32_t state_id) const {
  auto found = interval_index.find(StateKey(rule_id, state_id));
  Require(found != interval_index.end(), "missing candidate plan for current state");
  return intervals[found->second];
}

std::pair<bool, bool> ByteCompilationPlan::Probe(
    int32_t lookahead_id, const std::string& token, const std::vector<bool>& end_positions
) const {
  auto found = observer_index.find(lookahead_id);
  Require(found != observer_index.end(), "missing native-helper observer");
  const auto& observer = observers[found->second];
  if (observer.initial_completed) return {true, true};
  Require(end_positions.size() <= token.size() + 1, "lookahead suffix position is outside token");
  for (int i = static_cast<int>(end_positions.size()) - 1; i >= 0; --i) {
    if (!end_positions[i]) continue;
    int64_t node = 0;
    int last_accepted = i - 1;
    for (int pos = i; pos < static_cast<int>(token.size()); ++pos) {
      uint8_t byte = static_cast<uint8_t>(token[pos]);
      const auto& edges = observer.nodes[node].edges;
      auto edge = std::find_if(edges.begin(), edges.end(), [&](const auto& item) {
        return item.first == byte;
      });
      Require(edge != edges.end(), "lookahead query is outside the exported suffix domain");
      if (edge->second == -1) break;
      node = edge->second;
      last_accepted = pos;
      if (observer.nodes[node].completed) return {true, true};
    }
    if (last_accepted == static_cast<int>(token.size()) - 1) return {true, false};
  }
  return {false, false};
}

namespace {
// Fixed-width encoding is unambiguous even for embedded NUL/permuted bytes.
void CacheInteger(std::string* output, int64_t value) {
  uint64_t bits = static_cast<uint64_t>(value);
  for (int i = 0; i < 8; ++i) output->push_back(static_cast<char>(bits >> (i * 8)));
}
}  // namespace

std::vector<ByteRuleCacheContext> BuildByteRuleCacheContexts(
    const Grammar& grammar, const ByteCompilationPlan& plan
) {
  std::unordered_set<int32_t> nullable(
      grammar->allow_empty_rule_ids.begin(), grammar->allow_empty_rule_ids.end()
  );
  std::vector<ByteRuleCacheContext> result(grammar->NumRules());
  for (int32_t entry = 0; entry < grammar->NumRules(); ++entry) {
    std::string context = "scheme3-final-state-cache-v1";
    for (int byte = 0; byte < 256; ++byte) context.push_back(plan.ascii_mask[byte] ? 1 : 0);
    std::vector<int32_t> rules{entry};
    std::unordered_map<int32_t, int32_t> rule_ids{{entry, 0}};
    auto local_rule = [&](int32_t id) {
      auto [found, added] = rule_ids.emplace(id, static_cast<int32_t>(rules.size()));
      if (added) rules.push_back(id);
      return found->second;
    };
    for (size_t rule_index = 0; rule_index < rules.size(); ++rule_index) {
      int32_t rule_id = rules[rule_index];
      const auto& rule = grammar->GetRule(rule_id);
      const auto& fsm = grammar->per_rule_fsms[rule_id]->GetFsm();
      std::vector<int32_t> states{fsm.GetStart()};
      std::unordered_map<int32_t, int32_t> state_ids{{fsm.GetStart(), 0}};
      CacheInteger(&context, nullable.count(rule_id));
      CacheInteger(&context, rule.is_exact_lookahead);
      // A missing lookahead differs from an initially completed observer.
      CacheInteger(&context, rule.lookahead_assertion_id != -1);
      if (rule.lookahead_assertion_id != -1) {
        const auto& observer =
            plan.observers.at(plan.observer_index.at(rule.lookahead_assertion_id));
        CacheInteger(&context, observer.initial_completed);
        CacheInteger(&context, observer.nodes.size());
        for (const auto& node : observer.nodes) {
          CacheInteger(&context, node.completed);
          CacheInteger(&context, node.edges.size());
          for (const auto& [byte, child] : node.edges) {
            CacheInteger(&context, byte);
            CacheInteger(&context, child);
          }
        }
      }
      std::string graph;
      // Ordered BFS renumbers states without sorting edges: native ordering
      // and known native observations are part of the consistency contract.
      for (size_t state_index = 0; state_index < states.size(); ++state_index) {
        int32_t state = states[state_index];
        CacheInteger(&graph, fsm.IsEndState(state));
        const auto edges = fsm.GetFsm().GetEdges(state);
        CacheInteger(&graph, edges.size());
        for (const auto& edge : edges) {
          auto [target, added] =
              state_ids.emplace(edge.target, static_cast<int32_t>(states.size()));
          if (added) states.push_back(edge.target);
          CacheInteger(&graph, edge.min);
          if (edge.IsRuleRef()) {
            CacheInteger(&graph, local_rule(edge.GetRefRuleId()));
          } else if (edge.IsRepeatRef()) {
            auto repeat = fsm.GetFsm().GetRepeatEdgeInfo(edge.GetAuxIndex());
            CacheInteger(&graph, local_rule(repeat.RuleId()));
            CacheInteger(&graph, repeat.Lower());
            CacheInteger(&graph, repeat.Upper());
          } else {
            CacheInteger(&graph, edge.max);
          }
          CacheInteger(&graph, target->second);
        }
      }
      CacheInteger(&context, states.size());
      context += graph;
      if (rule_id == entry) result[entry].state_ids = std::move(state_ids);
    }
    // Contexts are immutable and shared by states. Neither a complete Grammar
    // digest nor unrelated plan intervals enter the key, allowing real reuse
    // across different schemas and arbitrary global rule/state numbering.
    result[entry].hash = std::hash<std::string>{}(context);
    result[entry].bytes = std::make_shared<const std::string>(std::move(context));
  }
  return result;
}

RuleLevelCache::ByteKey MakeByteRuleCacheKey(
    const ByteRuleCacheContext& context,
    const ByteCompilationPlan& plan,
    int32_t rule_id,
    int32_t state_id,
    bool is_root
) {
  std::string state;
  CacheInteger(&state, context.state_ids.at(state_id));
  CacheInteger(&state, is_root);
  // Remaining initial ParserState fields are fixed by the compilation entry
  // (no parent, zero token/string/repeat positions, and no budgets).
  const auto& interval = plan.GetIntervals(rule_id, state_id);
  CacheInteger(&state, interval.possible_token_num);
  CacheInteger(&state, interval.ranges.size());
  for (const auto& [begin, end] : interval.ranges) {
    CacheInteger(&state, begin);
    CacheInteger(&state, end);
  }
  return {context.bytes, context.hash, std::move(state)};
}

std::string ByteCompilationPlan::SerializeJSON() const { return AutoSerializeJSON(*this); }

std::shared_ptr<ByteCompilationPlan> ByteCompilationPlan::DeserializeJSON(const std::string& json) {
  Require(json.size() <= kMaxPlanBytes, "serialized plan exceeds 64 MiB");
  auto result = std::make_shared<ByteCompilationPlan>();
  if (auto error = AutoDeserializeJSON(result.get(), json, false, "ByteCompilationPlan")) {
    std::visit([](const auto& value) { throw value; }, *error);
  }
  return result;
}
}  // namespace xgrammar
