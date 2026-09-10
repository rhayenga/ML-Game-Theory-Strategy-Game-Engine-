#pragma once

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
  // Axial/cube coords for the 19 land hexes (row-major Illustration A order).
  std::array<Cube, kNumHexes> hex_cube{};

  // For each hex: 6 vertex ids clockwise from NE.
  std::array<std::array<int, 6>, kNumHexes> hex_vertices{};
  std::array<std::array<int, 6>, kNumHexes> hex_edges{};

  // Vertex -> up to 3 hexes / 3 neighbor vertices / 3 edges.
  std::array<std::array<int, 3>, kNumVertices> vertex_hexes{};
  std::array<uint8_t, kNumVertices> vertex_hex_count{};
  std::array<std::array<int, 3>, kNumVertices> vertex_neighbors{};
  std::array<uint8_t, kNumVertices> vertex_neighbor_count{};
  std::array<std::array<int, 3>, kNumVertices> vertex_edges{};
  std::array<uint8_t, kNumVertices> vertex_edge_count{};

  std::array<std::array<int, 2>, kNumEdges> edge_vertices{};
  std::array<std::array<int, 2>, kNumEdges> edge_hexes{};  // -1 if sea
  std::array<uint8_t, kNumEdges> edge_hex_count{};

  // For each vertex: bitmask of vertices within distance < 2 (illegal settlement).
  std::array<uint64_t, kNumVertices> dist2_block{};

  int find_vertex(int h0, int h1, int h2) const;
  int find_vertex2(int h0, int h1) const;  // coastal: exactly those two land hexes
  int find_edge(int v0, int v1) const;
};

Topology build_topology();

}  // namespace catan
