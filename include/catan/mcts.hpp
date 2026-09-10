#pragma once

#include "catan/eval.hpp"
#include "catan/rules.hpp"
#include "catan/visits.hpp"

#include <string>
#include <vector>

namespace catan {

struct MCTSConfig {
  int simulations = 2000;
  double c_puct = 1.6;       // PUCT exploration (AlphaZero-style)
  int rollout_depth = 40;
  // Past self-play policy priors (nullptr = uniform).
  const VisitStore* visits = nullptr;
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

// Expectimax-flavored MCTS with optional visit-store policy priors (PUCT).
MCTSResult search_best_action(const RuleCtx& ctx, const GameState& root, int perspective,
                              const MCTSConfig& cfg);

// Top-K move ranking with diversified action families for the coach UI.
std::vector<RankedAction> search_top_actions(const RuleCtx& ctx, const GameState& root,
                                             int perspective, const MCTSConfig& cfg, int k = 3);

}  // namespace catan
