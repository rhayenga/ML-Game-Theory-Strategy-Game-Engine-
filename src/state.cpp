#include "catan/state.hpp"

#include <stdexcept>
#include <utility>

namespace catan {

uint32_t rng_next(GameState& s) {
  // xorshift32
  uint32_t x = s.rng;
  x ^= x << 13;
  x ^= x >> 17;
  x ^= x << 5;
  s.rng = x ? x : 0xA5A5A5A5u;
  return s.rng;
}

int roll_dice(GameState& s) {
  int a = 1 + static_cast<int>(rng_next(s) % 6);
  int b = 1 + static_cast<int>(rng_next(s) % 6);
  s.last_roll = static_cast<uint8_t>(a + b);
  return s.last_roll;
}

int hand_size(const PlayerState& p) {
  int n = 0;
  for (int r = 0; r < 5; ++r) n += p.res[r];
  return n;
}

int visible_vp(const GameState& s, const Topology& topo, int player) {
  int vp = 0;
  for (int v = 0; v < kNumVertices; ++v) {
    uint8_t b = s.building[v];
    if (b == player + 1) vp += 1;
    if (b == player + 5) vp += 2;
  }
  if (s.longest_road == player) vp += 2;
  if (s.largest_army == player) vp += 2;
  (void)topo;
  return vp;
}

int total_vp(const GameState& s, const Topology& topo, int player) {
  return visible_vp(s, topo, player) + s.players[player].vp_cards;
}

GameState make_initial_state(const Topology& topo, const BoardSpec& board, uint32_t seed) {
  GameState s{};
  s.rng = seed ? seed : 0xCA7A12u;
  s.robber = static_cast<uint8_t>(board.desert_hex);
  s.phase = Phase::PreRoll;
  s.current = 0;  // Red starts (first player)

  // Official shuffled development deck.
  {
    int i = 0;
    for (int k = 0; k < 14; ++k) s.dev_deck[i++] = static_cast<uint8_t>(DevType::Knight);
    for (int k = 0; k < 5; ++k) s.dev_deck[i++] = static_cast<uint8_t>(DevType::VictoryPoint);
    for (int k = 0; k < 2; ++k) s.dev_deck[i++] = static_cast<uint8_t>(DevType::Monopoly);
    for (int k = 0; k < 2; ++k) s.dev_deck[i++] = static_cast<uint8_t>(DevType::YearOfPlenty);
    for (int k = 0; k < 2; ++k) s.dev_deck[i++] = static_cast<uint8_t>(DevType::RoadBuilding);
    for (int j = GameState::kDevDeckSize - 1; j > 0; --j) {
      int k = static_cast<int>(rng_next(s) % static_cast<uint32_t>(j + 1));
      std::swap(s.dev_deck[j], s.dev_deck[k]);
    }
    s.dev_next = 0;
    s.dev_bank = {14, 5, 2, 2, 2};
  }

  for (const auto& pl : board.placements) {
    int p = static_cast<int>(pl.player);
    int v = pl.vertex;
    if (v < 0) {
      v = topo.find_vertex(pl.settle_hex[0], pl.settle_hex[1], pl.settle_hex[2]);
    }
    if (s.building[v] != 0) throw std::runtime_error("placement collision");
    s.building[v] = static_cast<uint8_t>(p + 1);
    s.players[p].settles_left--;

    int e = pl.road_edge;
    if (e < 0) {
      int v2;
      if (pl.road_coastal) {
        v2 = topo.find_vertex2(pl.road_to_hex[0], pl.road_to_hex[1]);
      } else {
        v2 = topo.find_vertex(pl.road_to_hex[0], pl.road_to_hex[1], pl.road_to_hex[2]);
      }
      e = topo.find_edge(v, v2);
    }
    if (s.road[e] != 0) throw std::runtime_error("road collision");
    s.road[e] = static_cast<uint8_t>(p + 1);
    s.players[p].roads_left--;

    // Rulebook: only the second settlement grants starting resources (1 per adjacent land hex).
    if (pl.gives_starting_resources) {
      for (int i = 0; i < topo.vertex_hex_count[v]; ++i) {
        int h = topo.vertex_hexes[v][i];
        Resource r = resource_of(board.hexes[h].terrain);
        if (r == Resource::None) continue;
        int ri = static_cast<int>(r);
        if (s.bank[ri] > 0) {
          s.bank[ri]--;
          s.players[p].res[ri]++;
        }
      }
    }
  }

  return s;
}

}  // namespace catan
