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
  // Race-to-10 priors: VP first; income/roads/army unlock settle→city→awards.
  std::array<double, kEvalDim> w{{6.0, 2.8, 0.5, 1.4, 1.5, 1.0}};
  double scale = 5.0;  // tanh(diff / scale)

  static EvalWeights defaults() { return {}; }
};

// Keep learned weights from inverting the race (roads/income must stay helpful).
void sanitize_race_weights(EvalWeights& w);

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
