#pragma once

#include "catan/board.hpp"
#include "catan/rules.hpp"
#include "catan/state.hpp"

#include <array>
#include <string>

namespace catan {

inline constexpr int kEvalDim = 6;

// Feature layout: vp, income, diversity, army, road, seven_penalty
struct EvalWeights {
  // Race-to-10 priors: VP first, then income / army / road.
  std::array<double, kEvalDim> w{{3.8, 2.2, 0.2, 0.55, 0.35, 1.1}};
  double scale = 5.0;  // tanh(diff / scale)

  static EvalWeights defaults() { return {}; }
};

// Global active weights used by evaluate() (loaded by advisor/train).
EvalWeights& active_weights();
void set_active_weights(const EvalWeights& w);

bool save_weights(const EvalWeights& w, const std::string& path);
bool load_weights(EvalWeights& w, const std::string& path);

std::array<double, kEvalDim> player_features(const RuleCtx& ctx, const GameState& s, int player);

// Heuristic win-proxy in [-1, 1] from `perspective` player's view (zero-sum soft).
double evaluate(const RuleCtx& ctx, const GameState& s, int perspective);

// Marginal utility of holding one more of resource r for player.
double resource_utility(const RuleCtx& ctx, const GameState& s, int player, Resource r);

}  // namespace catan
