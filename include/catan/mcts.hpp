#pragma once

#include "catan/eval.hpp"
#include "catan/rules.hpp"

#include <string>
#include <vector>

namespace catan {

struct MCTSConfig {
  int simulations = 2000;
  double c_puct = 1.4;
  int rollout_depth = 40;
};

struct MCTSResult {
  Action best{};
  double value = 0;
  int visits = 0;
};

struct RankedAction {
  Action action{};
  double value = 0;
  int visits = 0;
  std::string label;
};

// Expectimax-flavored MCTS: chance outcomes folded into Roll transitions via sampling.
MCTSResult search_best_action(const RuleCtx& ctx, const GameState& root, int perspective,
                              const MCTSConfig& cfg);

// Top-K move ranking. Prefers non-EndTurn when real options exist (Roll still allowed).
std::vector<RankedAction> search_top_actions(const RuleCtx& ctx, const GameState& root,
                                             int perspective, const MCTSConfig& cfg, int k = 3);

}  // namespace catan
