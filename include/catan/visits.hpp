#pragma once

#include "catan/board.hpp"
#include "catan/rules.hpp"
#include "catan/state.hpp"

#include <cstdint>
#include <string>
#include <unordered_map>

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
  uint64_t total_position_hits() const;
  size_t unique_positions() const { return by_hash.size(); }
};

}  // namespace catan
