#pragma once

// Heuristic eval: feature weights and win-proxy scoring.

#include "catan/board.hpp"
#include "catan/rules.hpp"
#include "catan/state.hpp"

#include <array>
#include <string>

namespace catan {

inline constexpr int kEvalDim = 6;

struct EvalWeights {
  std::array<double, kEvalDim> w{{6.0, 2.8, 0.5, 1.4, 1.5, 1.0}};
  double scale = 5.0;

  static EvalWeights defaults() { return {}; }
};

void sanitize_race_weights(EvalWeights& w);

EvalWeights& active_weights();
void set_active_weights(const EvalWeights& w);

bool save_weights(const EvalWeights& w, const std::string& path);
bool load_weights(EvalWeights& w, const std::string& path);

std::array<double, kEvalDim> player_features(const RuleCtx& ctx, const GameState& s, int player);

double evaluate(const RuleCtx& ctx, const GameState& s, int perspective);

double resource_utility(const RuleCtx& ctx, const GameState& s, int player, Resource r);

}
