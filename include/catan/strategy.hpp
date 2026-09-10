#pragma once

#include "catan/rules.hpp"

namespace catan {

// Community meta (r/Catan + tournament guides):
//  - OWS: ore/wheat/sheep → cities, dev cards, Largest Army
//  - WoodBrick: wood/brick → expand, settlements, Longest Road
//  - Balanced: flex between both
enum class StrategyStyle : uint8_t { OwsCities = 0, WoodBrickRoad = 1, Balanced = 2 };

const char* strategy_name(StrategyStyle s);

// Infer style from a player's production profile on the board.
StrategyStyle infer_strategy(const RuleCtx& ctx, const GameState& s, int player);

// Heuristic bonus added to action scores (win-race aligned with community meta).
double strategy_action_bonus(const RuleCtx& ctx, const GameState& s, const Action& a, int player,
                             StrategyStyle style);

}  // namespace catan
