#pragma once

#include "catan/topology.hpp"
#include "catan/types.hpp"

#include <array>
#include <cstdint>

namespace catan {

enum class PortType : uint8_t { None = 0, Generic3, Brick2, Lumber2, Ore2, Grain2, Wool2 };

struct HexSpec {
  Terrain terrain = Terrain::Desert;
  uint8_t number = 0;  // 0 for desert
};

struct Placement {
  Player player = Player::Red;
  int settle_hex[3]{};
  int road_to_hex[3]{};  // second vertex as hex triple (use -1 for coastal pair in [0],[1])
  bool road_coastal = false;
  bool gives_starting_resources = false;
  // If >= 0, used instead of hex triples (for random setups).
  int vertex = -1;
  int road_edge = -1;
};

struct BoardSpec {
  std::array<HexSpec, kNumHexes> hexes{};
  // Per-vertex port (harbors touch 2 vertices; both get the same PortType).
  std::array<PortType, kNumVertices> port_at{};
  std::array<Placement, 8> placements{};  // 4 players × 2
  int desert_hex = 9;
};

// Official Starting Map for Beginners (Illustration A) + fixed placements.
BoardSpec beginner_board(const Topology& topo);

// Variable board: shuffled terrain/numbers/ports + random legal opening placements.
// Every call with a different rng stream yields a different game.
BoardSpec random_board(const Topology& topo, uint32_t& rng);

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

}  // namespace catan
