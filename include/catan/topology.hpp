#pragma once

// Hex/vertex/edge graph for the standard board.

#include "catan/types.hpp"

#include <array>
#include <cstdint>
#include <vector>

namespace catan {

struct Cube {
  int x = 0, y = 0, z = 0;
  bool operator==(const Cube& o) const { return x == o.x && y == o.y && z == o.z; }
  bool operator<(const Cube& o) const {
    if (x != o.x) return x < o.x;
    if (y != o.y) return y < o.y;
    return z < o.z;
  }
};

struct Topology {
  std::array<Cube, kNumHexes> hex_cube{};

  std::array<std::array<int, 6>, kNumHexes> hex_vertices{};
  std::array<std::array<int, 6>, kNumHexes> hex_edges{};

  std::array<std::array<int, 3>, kNumVertices> vertex_hexes{};
  std::array<uint8_t, kNumVertices> vertex_hex_count{};
  std::array<std::array<int, 3>, kNumVertices> vertex_neighbors{};
  std::array<uint8_t, kNumVertices> vertex_neighbor_count{};
  std::array<std::array<int, 3>, kNumVertices> vertex_edges{};
  std::array<uint8_t, kNumVertices> vertex_edge_count{};

  std::array<std::array<int, 2>, kNumEdges> edge_vertices{};
  std::array<std::array<int, 2>, kNumEdges> edge_hexes{};
  std::array<uint8_t, kNumEdges> edge_hex_count{};

  std::array<uint64_t, kNumVertices> dist2_block{};

  int find_vertex(int h0, int h1, int h2) const;
  int find_vertex2(int h0, int h1) const;
  int find_edge(int v0, int v1) const;
};

Topology build_topology();

}
