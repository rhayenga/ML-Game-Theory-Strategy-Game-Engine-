#pragma once

#include "catan/board.hpp"
#include "catan/state.hpp"
#include "catan/topology.hpp"

#include <array>
#include <string>
#include <vector>

namespace catan {

enum class ActionType : uint8_t {
  Roll,
  EndTurn,
  Discard,          // bitmask-ish via res counts in payload
  PlaceRobber,      // hex + steal player
  BuildRoad,
  BuildSettlement,
  BuildCity,
  BuyDev,
  PlayKnight,       // hex + steal
  PlayMonopoly,     // resource
  PlayYearOfPlenty, // r0, r1
  PlayRoadBuilding, // e0, e1 (-1 if only one)
  MaritimeTrade,    // give resource, recv resource, rate implied
};

struct Action {
  ActionType type = ActionType::EndTurn;
  int a = 0;
  int b = 0;
  int c = 0;
  // Discard: encode 5 nibbles in a 32-bit via a as packed counts? Use side buffer.
  std::array<uint8_t, 5> discard{};
};

std::string action_to_string(const Action& act);

struct RuleCtx {
  const Topology* topo = nullptr;
  const BoardSpec* board = nullptr;
};

std::vector<Action> legal_actions(const RuleCtx& ctx, const GameState& s);
void apply_action(const RuleCtx& ctx, GameState& s, const Action& act);

// After production / robber etc.
void recompute_awards(const RuleCtx& ctx, GameState& s);
int longest_road_len(const GameState& s, const Topology& topo, int player);

bool can_afford(const PlayerState& p, int brick, int lumber, int ore, int grain, int wool);
void pay(GameState& s, int player, int brick, int lumber, int ore, int grain, int wool);

}  // namespace catan
