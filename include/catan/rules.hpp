#pragma once

// Legal moves, applying actions, and cost helpers.

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
  Discard,
  PlaceRobber,
  BuildRoad,
  BuildSettlement,
  BuildCity,
  BuyDev,
  PlayKnight,
  PlayMonopoly,
  PlayYearOfPlenty,
  PlayRoadBuilding,
  MaritimeTrade,
};

struct Action {
  ActionType type = ActionType::EndTurn;
  int a = 0;
  int b = 0;
  int c = 0;
  std::array<uint8_t, 5> discard{};
};

std::string action_to_string(const Action& act);

struct RuleCtx {
  const Topology* topo = nullptr;
  const BoardSpec* board = nullptr;
};

std::vector<Action> legal_actions(const RuleCtx& ctx, const GameState& s);
void apply_action(const RuleCtx& ctx, GameState& s, const Action& act);

void recompute_awards(const RuleCtx& ctx, GameState& s);
int longest_road_len(const GameState& s, const Topology& topo, int player);

bool can_afford(const PlayerState& p, int brick, int lumber, int ore, int grain, int wool);
void pay(GameState& s, int player, int brick, int lumber, int ore, int grain, int wool);

}
