#pragma once

// MCTS search for best / top-K moves.

#include "catan/eval.hpp"
#include "catan/rules.hpp"
#include "catan/visits.hpp"

#include <string>
#include <vector>

namespace catan {

struct MCTSConfig {
  int simulations = 2000;
  double c_puct = 1.6;
  int rollout_depth = 40;
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

MCTSResult search_best_action(const RuleCtx& ctx, const GameState& root, int perspective,
                              const MCTSConfig& cfg);

std::vector<RankedAction> search_top_actions(const RuleCtx& ctx, const GameState& root,
                                             int perspective, const MCTSConfig& cfg, int k = 3);

}
