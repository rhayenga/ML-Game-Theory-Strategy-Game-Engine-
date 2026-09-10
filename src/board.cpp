// Board generation, ports, openings, and seed pool.

#include "catan/board.hpp"

#include <array>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <stdexcept>
#include <unordered_set>
#include <vector>

namespace catan {
namespace {

void set_port_edge(BoardSpec& b, const Topology& topo, int v0, int v1, PortType pt) {
  b.port_at[v0] = pt;
  b.port_at[v1] = pt;
  (void)topo;
}

void attach_port_near(BoardSpec& b, const Topology& topo, int land_hex, PortType pt,
                      int prefer_other_hex) {
  int best_e = -1;
  for (int c = 0; c < 6; ++c) {
    int e = topo.hex_edges[land_hex][c];
    if (topo.edge_hex_count[e] != 1) continue;
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

}

BoardSpec beginner_board(const Topology& topo) {
  BoardSpec b{};
  b.desert_hex = 9;

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

  attach_port_near(b, topo, 0, PortType::Lumber2, 3);
  attach_port_near(b, topo, 1, PortType::Wool2, -1);
  attach_port_near(b, topo, 2, PortType::Grain2, -1);
  attach_port_near(b, topo, 6, PortType::Generic3, 11);
  attach_port_near(b, topo, 11, PortType::Brick2, 15);
  attach_port_near(b, topo, 17, PortType::Generic3, 18);
  attach_port_near(b, topo, 16, PortType::Ore2, -1);
  attach_port_near(b, topo, 7, PortType::Generic3, -1);
  attach_port_near(b, topo, 3, PortType::Generic3, 7);

  b.placements = {{
      Placement{Player::Red, {1, 4, 5}, {1, 2, 5}, false, false},
      Placement{Player::Red, {7, 8, 12}, {8, 12, 13}, false, true},
      Placement{Player::White, {3, 4, 8}, {0, 3, 4}, false, false},
      Placement{Player::White, {10, 11, 15}, {10, 14, 15}, false, true},
      Placement{Player::Orange, {2, 5, 6}, {1, 2, 5}, false, false},
      Placement{Player::Orange, {13, 14, 17}, {14, 17, 18}, false, true},
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

}

BoardSpec random_board(const Topology& topo, uint32_t& rng) {
  BoardSpec b{};
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
  auto hex_adjacent = [&](int h0, int h1) {
    const Cube& a = topo.hex_cube[h0];
    const Cube& c = topo.hex_cube[h1];
    return (std::abs(a.x - c.x) + std::abs(a.y - c.y) + std::abs(a.z - c.z)) / 2 == 1;
  };
  auto reds_ok = [&]() {
    std::vector<int> reds;
    for (int h = 0; h < kNumHexes; ++h) {
      uint8_t n = b.hexes[h].number;
      if (n == 6 || n == 8) reds.push_back(h);
    }
    for (size_t i = 0; i < reds.size(); ++i) {
      for (size_t j = i + 1; j < reds.size(); ++j) {
        if (hex_adjacent(reds[i], reds[j])) return false;
      }
    }
    return true;
  };
  for (int attempt = 0; attempt < 400; ++attempt) {
    shuffle(nums, rng);
    int ni = 0;
    for (int i = 0; i < kNumHexes; ++i) {
      if (b.hexes[i].terrain == Terrain::Desert) {
        b.hexes[i].number = 0;
      } else {
        b.hexes[i].number = nums[ni++];
      }
    }
    if (reds_ok()) break;
  }

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
    b.port_at[v0] = ports[pi];
    b.port_at[v1] = ports[pi];
    port_used[v0] = port_used[v1] = 1;
    ++pi;
  }

  std::array<Player, 8> order = {Player::Red,    Player::White,  Player::Orange, Player::Blue,
                                 Player::Blue,   Player::Orange, Player::White,  Player::Red};
  std::array<uint8_t, kNumVertices> building{};
  std::array<uint8_t, kNumEdges> road{};
  std::array<int, 4> settle_count{};

  auto vertex_pips = [&](int v) {
    double acc = 0;
    for (int i = 0; i < topo.vertex_hex_count[v]; ++i) {
      int h = topo.vertex_hexes[v][i];
      int n = b.hexes[h].number;
      if (n >= 2 && n <= 12) acc += kPips[n];
    }
    if (b.port_at[v] != PortType::None) acc += 1.2;
    return acc;
  };

  for (int i = 0; i < 8; ++i) {
    Player pl = order[i];
    int p = static_cast<int>(pl);
    std::vector<int> legal;
    for (int v = 0; v < kNumVertices; ++v) {
      if (settle_ok(topo, building, v)) legal.push_back(v);
    }
    if (legal.empty()) throw std::runtime_error("random_board: no legal settlement");

    double temp = 1.1 + 0.55 * ((rnd(rng) % 100) / 100.0);
    if (settle_count[p] == 1) temp += 0.35;
    std::vector<double> w(legal.size());
    double wsum = 0;
    for (size_t j = 0; j < legal.size(); ++j) {
      w[j] = std::exp(vertex_pips(legal[j]) / temp);
      wsum += w[j];
    }
    double pick = (rnd(rng) % 100000) / 100000.0 * wsum;
    int v = legal.back();
    for (size_t j = 0; j < legal.size(); ++j) {
      pick -= w[j];
      if (pick <= 0) {
        v = legal[j];
        break;
      }
    }
    building[v] = static_cast<uint8_t>(p + 1);

    std::vector<int> edges;
    for (int j = 0; j < topo.vertex_edge_count[v]; ++j) {
      int e = topo.vertex_edges[v][j];
      if (road[e] == 0) edges.push_back(e);
    }
    if (edges.empty()) throw std::runtime_error("random_board: no legal road");
    std::vector<double> ew(edges.size());
    double esum = 0;
    for (size_t j = 0; j < edges.size(); ++j) {
      int e = edges[j];
      int a0 = topo.edge_vertices[e][0], a1 = topo.edge_vertices[e][1];
      int other = (a0 == v) ? a1 : a0;
      double score = 0.4;
      if (building[other] == 0) score += 0.5 * vertex_pips(other);
      if (b.port_at[other] != PortType::None) score += 0.8;
      ew[j] = std::exp(score / 1.2);
      esum += ew[j];
    }
    double ep = (rnd(rng) % 100000) / 100000.0 * esum;
    int e = edges.back();
    for (size_t j = 0; j < edges.size(); ++j) {
      ep -= ew[j];
      if (ep <= 0) {
        e = edges[j];
        break;
      }
    }
    road[e] = static_cast<uint8_t>(p + 1);

    settle_count[p]++;
    bool star = settle_count[p] == 2;
    b.placements[i] = Placement{pl, {0, 0, 0}, {0, 0, 0}, false, star, v, e};
  }

  return b;
}

void append_board_seed(const std::string& path, uint32_t seed) {
  std::ofstream out(path, std::ios::app);
  if (!out) return;
  out << seed << "\n";
}

std::vector<uint32_t> load_board_seeds(const std::string& path) {
  std::vector<uint32_t> out;
  std::ifstream in(path);
  if (!in) return out;
  uint32_t s = 0;
  std::unordered_set<uint32_t> seen;
  while (in >> s) {
    if (seen.insert(s).second) out.push_back(s);
  }
  return out;
}

void write_board_seeds(const std::string& path, const std::vector<uint32_t>& seeds) {
  std::ofstream out(path, std::ios::trunc);
  if (!out) return;
  for (uint32_t s : seeds) out << s << "\n";
}

uint32_t pick_play_board_seed(uint32_t prefer, uint32_t& last, const std::string& path) {
  auto seeds = load_board_seeds(path);
  if (seeds.empty()) {
    uint32_t s = prefer ^ 0xC47A11u;
    if (s == last) s ^= 0xA5A5A5A5u;
    last = s;
    append_board_seed(path, s);
    return s;
  }

  if (seeds.size() < 48 && ((prefer ^ last) % 6u) == 0u) {
    uint32_t fresh = prefer ^ (0x9E3779B9u * static_cast<uint32_t>(seeds.size() + 3));
    if (fresh == 0) fresh = 0xC47A11u;
    bool known = false;
    for (uint32_t s : seeds) {
      if (s == fresh) {
        known = true;
        break;
      }
    }
    if (!known) {
      append_board_seed(path, fresh);
      last = fresh;
      return fresh;
    }
  }

  uint32_t mix = prefer * 2654435761u ^ (last * 1597334677u) ^ 0xA5A5A5A5u;
  size_t start = mix % seeds.size();
  for (size_t k = 0; k < seeds.size(); ++k) {
    uint32_t s = seeds[(start + k) % seeds.size()];
    if (s != last) {
      last = s;
      return s;
    }
  }
  last = seeds[start];
  return last;
}

}
