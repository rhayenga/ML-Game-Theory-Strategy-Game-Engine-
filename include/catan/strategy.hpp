#pragma once

#include "catan/rules.hpp"

namespace catan {

// Main play levers (rulebook): roads → settlements (1 VP), maritime trade to
// complete builds, development cards; cities/LR/LA as strong finishers.
// Styles bias which lever a seat prefers from its production profile.
enum class StrategyStyle : uint8_t { OwsCities = 0, WoodBrickRoad = 1, Balanced = 2 };

const char* strategy_name(StrategyStyle s);

StrategyStyle infer_strategy(const RuleCtx& ctx, const GameState& s, int player);

double strategy_action_bonus(const RuleCtx& ctx, const GameState& s, const Action& a, int player,
                             StrategyStyle style);

// Higher = better hex to park the robber (hurts advanced opponents on hot numbers).
// Hexes touching your own buildings score as unusable (-1e9).
bool robber_hits_self(const RuleCtx& ctx, const GameState& s, int me, int hex);
double robber_hex_score(const RuleCtx& ctx, const GameState& s, int me, int hex);

// Maritime bank/harbor trade score. >0 only if it completes settle/city/road/dev kit.
double maritime_trade_score(const GameState& s, int player, int give, int recv, int rate);
inline bool maritime_is_purposeful(const GameState& s, int player, const Action& a) {
  return a.type == ActionType::MaritimeTrade &&
         maritime_trade_score(s, player, a.a, a.b, a.c) > 0.0;
}

}  // namespace catan
