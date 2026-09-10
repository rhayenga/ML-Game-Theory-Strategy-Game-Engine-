#pragma once

// Position/move visit store for self-play priors.

#include "catan/board.hpp"
#include "catan/rules.hpp"
#include "catan/state.hpp"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace catan {

struct MoveStat {
  int visits = 0;
};

struct PositionStat {
  uint64_t visits = 0;
  uint64_t game_seen = 0;
  std::unordered_map<std::string, MoveStat> moves;
};

uint64_t hash_position(const GameState& s, const BoardSpec& board);
std::string action_key(const Action& a);

struct VisitStore {
  std::unordered_map<std::string, PositionStat> by_hash;
  std::string path = "build/position_visits.json";

  bool load(const std::string& p);
  bool save(const std::string& p) const;

  PositionStat& touch(uint64_t h);
  const PositionStat* find(uint64_t h) const;
  void record_move(uint64_t h, const Action& a);
  void prune(size_t max_positions = 250000);
  uint64_t total_position_hits() const;
  size_t unique_positions() const { return by_hash.size(); }
};

double move_prior(const VisitStore* visits, uint64_t h, const Action& a,
                  const std::vector<Action>& legal);

}
