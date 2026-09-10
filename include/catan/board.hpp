#pragma once

// Board setups: beginner map, random boards, and seed helpers.

#include "catan/topology.hpp"
#include "catan/types.hpp"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace catan {

enum class PortType : uint8_t { None = 0, Generic3, Brick2, Lumber2, Ore2, Grain2, Wool2 };

struct HexSpec {
  Terrain terrain = Terrain::Desert;
  uint8_t number = 0;
};

struct Placement {
  Player player = Player::Red;
  int settle_hex[3]{};
  int road_to_hex[3]{};
  bool road_coastal = false;
  bool gives_starting_resources = false;
  int vertex = -1;
  int road_edge = -1;
};

struct BoardSpec {
  std::array<HexSpec, kNumHexes> hexes{};
  std::array<PortType, kNumVertices> port_at{};
  std::array<Placement, 8> placements{};
  int desert_hex = 9;
};

BoardSpec beginner_board(const Topology& topo);

BoardSpec random_board(const Topology& topo, uint32_t& rng);

void append_board_seed(const std::string& path, uint32_t seed);
std::vector<uint32_t> load_board_seeds(const std::string& path);
void write_board_seeds(const std::string& path, const std::vector<uint32_t>& seeds);
uint32_t pick_play_board_seed(uint32_t prefer, uint32_t& last,
                              const std::string& path = "build/board_seeds.txt");

inline int port_rate(PortType p) {
  if (p == PortType::None) return 4;
  if (p == PortType::Generic3) return 3;
  return 2;
}

inline Resource port_resource(PortType p) {
  switch (p) {
    case PortType::Brick2: return Resource::Brick;
    case PortType::Lumber2: return Resource::Lumber;
    case PortType::Ore2: return Resource::Ore;
    case PortType::Grain2: return Resource::Grain;
    case PortType::Wool2: return Resource::Wool;
    default: return Resource::None;
  }
}

}
