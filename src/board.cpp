#include "catan/board.hpp"

#include <array>
#include <stdexcept>
#include <vector>

namespace catan {
namespace {

void set_port_edge(BoardSpec& b, const Topology& topo, int v0, int v1, PortType pt) {
  b.port_at[v0] = pt;
  b.port_at[v1] = pt;
  (void)topo;
}

// Attach a port to the unique coastal edge that borders `land_hex` and whose
// midpoint faces outward near the given neighbor land hex (or -1 = most coastal).
void attach_port_near(BoardSpec& b, const Topology& topo, int land_hex, PortType pt,
                      int prefer_other_hex) {
  int best_e = -1;
  for (int c = 0; c < 6; ++c) {
    int e = topo.hex_edges[land_hex][c];
    if (topo.edge_hex_count[e] != 1) continue;  // need sea edge
    if (prefer_other_hex >= 0) {
      int v0 = topo.edge_vertices[e][0];
      int v1 = topo.edge_vertices[e][1];
      auto touches = [&](int v) {
        for (int i = 0; i < topo.vertex_hex_count[v]; ++i) {
          if (topo.vertex_hexes[v][i] == prefer_other_hex) return true;
        }
        return false;
      };
      if (touches(v0) || touches(v1)) {
        best_e = e;
        break;
      }
    } else if (best_e < 0) {
      best_e = e;
    }
  }
  if (best_e < 0) {
    // Fallback: any coastal edge of the hex.
    for (int c = 0; c < 6; ++c) {
      int e = topo.hex_edges[land_hex][c];
      if (topo.edge_hex_count[e] == 1) {
        best_e = e;
        break;
      }
    }
  }
  if (best_e < 0) throw std::runtime_error("no coastal edge for port");
  set_port_edge(b, topo, topo.edge_vertices[best_e][0], topo.edge_vertices[best_e][1], pt);
}

}  // namespace

BoardSpec beginner_board(const Topology& topo) {
  BoardSpec b{};
  b.desert_hex = 9;

  // Hex terrain + numbers (Illustration A), row-major.
  const Terrain terr[kNumHexes] = {
      Terrain::Mountains, Terrain::Pasture, Terrain::Forest,
      Terrain::Fields,    Terrain::Hills,   Terrain::Pasture, Terrain::Hills,
      Terrain::Fields,    Terrain::Forest,  Terrain::Desert,  Terrain::Forest, Terrain::Mountains,
      Terrain::Forest,    Terrain::Mountains, Terrain::Fields, Terrain::Pasture,
      Terrain::Hills,     Terrain::Fields,  Terrain::Pasture,
  };
  const uint8_t nums[kNumHexes] = {
      10, 2, 9,
      12, 6, 4, 10,
      9, 11, 0, 3, 8,
      8, 3, 4, 5,
      5, 6, 11,
  };
  for (int i = 0; i < kNumHexes; ++i) {
    b.hexes[i] = HexSpec{terr[i], nums[i]};
  }

  // Harbors: 2:1 lumber, wool, grain, brick, ore + four 3:1 (Illustration A approx).
  attach_port_near(b, topo, 0, PortType::Lumber2, 3);
  attach_port_near(b, topo, 1, PortType::Wool2, -1);
  attach_port_near(b, topo, 2, PortType::Grain2, -1);
  attach_port_near(b, topo, 6, PortType::Generic3, 11);
  attach_port_near(b, topo, 11, PortType::Brick2, 15);
  attach_port_near(b, topo, 17, PortType::Generic3, 18);
  attach_port_near(b, topo, 16, PortType::Ore2, -1);
  attach_port_near(b, topo, 7, PortType::Generic3, -1);
  attach_port_near(b, topo, 3, PortType::Generic3, 7);

  // Fixed beginner settlements / roads (valid hex triples from topology).
  // Road second endpoint = adjacent vertex (3-hex or coastal 2-hex via road_coastal).
  b.placements = {{
      // Red: {pasture2, hills6, pasture4} → road toward forest9
      Placement{Player::Red, {1, 4, 5}, {1, 2, 5}, false, false},
      Placement{Player::Red, {7, 8, 12}, {8, 12, 13}, false, true},
      // White
      Placement{Player::White, {3, 4, 8}, {0, 3, 4}, false, false},
      Placement{Player::White, {10, 11, 15}, {10, 14, 15}, false, true},
      // Orange: {forest9, pasture4, hills10} → road toward forest9/coast
      Placement{Player::Orange, {2, 5, 6}, {1, 2, 5}, false, false},
      Placement{Player::Orange, {13, 14, 17}, {14, 17, 18}, false, true},
      // Blue star = brick+lumber+ore (rulebook); road along wood8 coast
      Placement{Player::Blue, {12, 13, 16}, {12, 16, -1}, true, true},
      Placement{Player::Blue, {14, 15, 18}, {14, 17, 18}, false, false},
  }};

  return b;
}

namespace {

uint32_t rnd(uint32_t& x) {
  x ^= x << 13;
  x ^= x >> 17;
  x ^= x << 5;
  if (!x) x = 0xA5A5A5A5u;
  return x;
}

template <typename T>
void shuffle(std::vector<T>& a, uint32_t& rng) {
  for (int i = static_cast<int>(a.size()) - 1; i > 0; --i) {
    int j = static_cast<int>(rnd(rng) % (i + 1));
    std::swap(a[i], a[j]);
  }
}

bool settle_ok(const Topology& topo, const std::array<uint8_t, kNumVertices>& building, int v) {
  if (building[v] != 0) return false;
  uint64_t occupied = 0;
  for (int u = 0; u < kNumVertices; ++u) {
    if (building[u] != 0) occupied |= (1ULL << u);
  }
  if (occupied & (topo.dist2_block[v] & ~(1ULL << v))) return false;
  return true;
}

}  // namespace

BoardSpec random_board(const Topology& topo, uint32_t& rng) {
  BoardSpec b{};
  // Standard terrain mix
  std::vector<Terrain> terr = {
      Terrain::Hills,    Terrain::Hills,    Terrain::Hills,
      Terrain::Forest,   Terrain::Forest,   Terrain::Forest,   Terrain::Forest,
      Terrain::Mountains,Terrain::Mountains,Terrain::Mountains,
      Terrain::Fields,   Terrain::Fields,   Terrain::Fields,   Terrain::Fields,
      Terrain::Pasture,  Terrain::Pasture,  Terrain::Pasture,  Terrain::Pasture,
      Terrain::Desert,
  };
  shuffle(terr, rng);
  for (int i = 0; i < kNumHexes; ++i) {
    b.hexes[i].terrain = terr[i];
    if (terr[i] == Terrain::Desert) b.desert_hex = i;
  }

  std::vector<uint8_t> nums = {2, 3, 3, 4, 4, 5, 5, 6, 6, 8, 8, 9, 9, 10, 10, 11, 11, 12};
  shuffle(nums, rng);
  int ni = 0;
  for (int i = 0; i < kNumHexes; ++i) {
    if (b.hexes[i].terrain == Terrain::Desert) {
      b.hexes[i].number = 0;
    } else {
      b.hexes[i].number = nums[ni++];
    }
  }

  // Ports on distinct coastal edges
  std::vector<PortType> ports = {PortType::Generic3, PortType::Generic3, PortType::Generic3,
                                 PortType::Generic3, PortType::Brick2,   PortType::Lumber2,
                                 PortType::Ore2,     PortType::Grain2,   PortType::Wool2};
  shuffle(ports, rng);
  std::vector<int> coastal_edges;
  for (int e = 0; e < kNumEdges; ++e) {
    if (topo.edge_hex_count[e] == 1) coastal_edges.push_back(e);
  }
  shuffle(coastal_edges, rng);
  int pi = 0;
  std::array<uint8_t, kNumVertices> port_used{};
  for (int e : coastal_edges) {
    if (pi >= static_cast<int>(ports.size())) break;
    int v0 = topo.edge_vertices[e][0];
    int v1 = topo.edge_vertices[e][1];
    if (port_used[v0] || port_used[v1]) continue;
    // Prefer vertices that aren't already marked
    b.port_at[v0] = ports[pi];
    b.port_at[v1] = ports[pi];
    port_used[v0] = port_used[v1] = 1;
    ++pi;
  }

  // Random legal placements: order P0..P3 then P3..P0; second settle gets resources.
  std::array<Player, 8> order = {Player::Red,    Player::White,  Player::Orange, Player::Blue,
                                 Player::Blue,   Player::Orange, Player::White,  Player::Red};
  std::array<uint8_t, kNumVertices> building{};
  std::array<uint8_t, kNumEdges> road{};
  std::array<int, 4> settle_count{};
  for (int i = 0; i < 8; ++i) {
    Player pl = order[i];
    int p = static_cast<int>(pl);
    std::vector<int> legal;
    for (int v = 0; v < kNumVertices; ++v) {
      if (settle_ok(topo, building, v)) legal.push_back(v);
    }
    if (legal.empty()) throw std::runtime_error("random_board: no legal settlement");
    int v = legal[rnd(rng) % legal.size()];
    building[v] = static_cast<uint8_t>(p + 1);

    std::vector<int> edges;
    for (int j = 0; j < topo.vertex_edge_count[v]; ++j) {
      int e = topo.vertex_edges[v][j];
      if (road[e] == 0) edges.push_back(e);
    }
    if (edges.empty()) throw std::runtime_error("random_board: no legal road");
    int e = edges[rnd(rng) % edges.size()];
    road[e] = static_cast<uint8_t>(p + 1);

    settle_count[p]++;
    bool star = settle_count[p] == 2;  // second settlement gets resources
    b.placements[i] = Placement{pl, {0, 0, 0}, {0, 0, 0}, false, star, v, e};
  }

  return b;
}

}  // namespace catan
