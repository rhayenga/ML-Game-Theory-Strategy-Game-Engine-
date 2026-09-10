#pragma once

#include "catan/board.hpp"
#include "catan/topology.hpp"
#include "catan/types.hpp"

#include <array>
#include <cstdint>

namespace catan {

struct PlayerState {
  std::array<uint8_t, 5> res{};  // brick..wool
  uint8_t knights_played = 0;
  uint8_t vp_cards = 0;
  uint8_t roads_left = 15;
  uint8_t settles_left = 5;
  uint8_t cities_left = 4;
  std::array<uint8_t, 5> devs{};  // by DevType
  uint8_t new_dev = 0;            // bought this turn (type+1), unplayable
};

struct GameState {
  // 0 empty; 1..4 settlement owner (player+1); 5..8 city (player+1)+4
  std::array<uint8_t, kNumVertices> building{};
  std::array<uint8_t, kNumEdges> road{};  // 0 empty; 1..4 owner

  std::array<PlayerState, kNumPlayers> players{};
  std::array<uint8_t, 5> bank{{19, 19, 19, 19, 19}};

  // Remaining development deck counts by type (shuffled order not needed for expectation;
  // we sample proportionally).
  std::array<uint8_t, 5> dev_bank{{14, 5, 2, 2, 2}};

  uint8_t robber = 9;
  uint8_t current = 0;  // player index
  Phase phase = Phase::PreRoll;
  int8_t longest_road = -1;   // player or -1
  int8_t largest_army = -1;
  uint8_t discard_left = 0;   // bitset of players still needing discard
  uint8_t last_roll = 0;
  uint32_t rng = 0xCA7A12u;

  bool game_over = false;
  int8_t winner = -1;
};

GameState make_initial_state(const Topology& topo, const BoardSpec& board, uint32_t seed);

uint32_t rng_next(GameState& s);
int roll_dice(GameState& s);

int hand_size(const PlayerState& p);
int visible_vp(const GameState& s, const Topology& topo, int player);
int total_vp(const GameState& s, const Topology& topo, int player);

}  // namespace catan
