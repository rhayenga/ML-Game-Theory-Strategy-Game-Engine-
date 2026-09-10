#include "catan/strategy.hpp"

#include "catan/eval.hpp"

#include <algorithm>

namespace catan {
namespace {

double pip_of(const RuleCtx& ctx, const GameState& s, int player, Resource res) {
  double acc = 0;
  for (int h = 0; h < kNumHexes; ++h) {
    if (h == s.robber) continue;
    if (resource_of(ctx.board->hexes[h].terrain) != res) continue;
    int n = ctx.board->hexes[h].number;
    if (n < 2 || n > 12) continue;
    for (int c = 0; c < 6; ++c) {
      int v = ctx.topo->hex_vertices[h][c];
      uint8_t b = s.building[v];
      if (b == player + 1) acc += kPips[n];
      else if (b == player + 5) acc += 2.0 * kPips[n];
    }
  }
  return acc;
}

}  // namespace

const char* strategy_name(StrategyStyle s) {
  switch (s) {
    case StrategyStyle::OwsCities: return "OWS (cities + army)";
    case StrategyStyle::WoodBrickRoad: return "Wood/Brick (expand + road)";
    default: return "Balanced";
  }
}

StrategyStyle infer_strategy(const RuleCtx& ctx, const GameState& s, int player) {
  double ore = pip_of(ctx, s, player, Resource::Ore);
  double grain = pip_of(ctx, s, player, Resource::Grain);
  double wool = pip_of(ctx, s, player, Resource::Wool);
  double wood = pip_of(ctx, s, player, Resource::Lumber);
  double brick = pip_of(ctx, s, player, Resource::Brick);
  double ows = ore + grain + 0.6 * wool;
  double wb = wood + brick;
  if (ows >= wb + 3.0 && ore >= 2.0 && grain >= 2.0) return StrategyStyle::OwsCities;
  if (wb >= ows + 2.0) return StrategyStyle::WoodBrickRoad;
  return StrategyStyle::Balanced;
}

double strategy_action_bonus(const RuleCtx& ctx, const GameState& s, const Action& a, int player,
                             StrategyStyle style) {
  double b = 0;
  int vp = total_vp(s, *ctx.topo, player);

  // Universal community tips: don't pass if you can grow; cities before early random roads.
  if (a.type == ActionType::EndTurn) b -= 0.25;
  if (a.type == ActionType::BuildCity) b += 0.55;          // Reddit meta: cities scale fastest
  if (a.type == ActionType::BuildSettlement) b += 0.35;   // still need 4+ buildings to win
  if (a.type == ActionType::BuyDev) {
    // Guides: cities first; after ~7 VP, devs are high EV (army / VP cards).
    b += (vp >= 7) ? 0.45 : 0.12;
  }
  if (a.type == ActionType::PlayKnight) b += 0.2;  // robber denial + army
  if (a.type == ActionType::PlayMonopoly) b += 0.35;
  if (a.type == ActionType::MaritimeTrade) {
    // Prefer trading into ore/wheat (city fuel) or wood/brick if expanding.
    if (a.b == static_cast<int>(Resource::Ore) || a.b == static_cast<int>(Resource::Grain)) b += 0.12;
  }

  switch (style) {
    case StrategyStyle::OwsCities:
      if (a.type == ActionType::BuildCity) b += 0.35;
      if (a.type == ActionType::BuyDev) b += 0.3;
      if (a.type == ActionType::PlayKnight) b += 0.25;
      if (a.type == ActionType::BuildRoad && vp < 6) b -= 0.08;  // don't sprawl too early
      break;
    case StrategyStyle::WoodBrickRoad:
      if (a.type == ActionType::BuildRoad) b += 0.28;
      if (a.type == ActionType::BuildSettlement) b += 0.4;
      if (a.type == ActionType::PlayRoadBuilding) b += 0.45;
      if (a.type == ActionType::BuildCity) b += 0.15;  // still upgrade when possible
      break;
    case StrategyStyle::Balanced:
      if (a.type == ActionType::BuildSettlement || a.type == ActionType::BuildCity) b += 0.2;
      if (a.type == ActionType::BuyDev && vp >= 6) b += 0.15;
      break;
  }

  // Never waste first productive turns: Roll is required, not a "pass".
  if (a.type == ActionType::Roll) b += 0.05;
  (void)ctx;
  return b;
}

}  // namespace catan
