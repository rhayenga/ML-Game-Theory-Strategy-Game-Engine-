// Build the hex/vertex/edge topology for a standard board.

#include "catan/topology.hpp"

#include <algorithm>
#include <map>
#include <stdexcept>
#include <tuple>

namespace catan {
namespace {

constexpr Cube kDir[6] = {
    {1, -1, 0}, {1, 0, -1}, {0, 1, -1}, {-1, 1, 0}, {-1, 0, 1}, {0, -1, 1},
};

Cube add(Cube a, Cube b) { return Cube{a.x + b.x, a.y + b.y, a.z + b.z}; }

using VKey = std::tuple<int, int, int, int, int, int, int, int, int>;

VKey corner_key(Cube h, int c) {
  Cube a = h;
  Cube b = add(h, kDir[c]);
  Cube d = add(h, kDir[(c + 5) % 6]);
  Cube arr[3] = {a, b, d};
  std::sort(arr, arr + 3);
  return VKey{arr[0].x, arr[0].y, arr[0].z, arr[1].x, arr[1].y, arr[1].z,
              arr[2].x, arr[2].y, arr[2].z};
}

}

Topology build_topology() {
  Topology t{};

  const Cube coords[kNumHexes] = {
      {0, -2, 2},  {1, -2, 1},  {2, -2, 0},
      {-1, -1, 2}, {0, -1, 1},  {1, -1, 0},  {2, -1, -1},
      {-2, 0, 2},  {-1, 0, 1},  {0, 0, 0},   {1, 0, -1},  {2, 0, -2},
      {-2, 1, 1},  {-1, 1, 0},  {0, 1, -1},  {1, 1, -2},
      {-2, 2, 0},  {-1, 2, -1}, {0, 2, -2},
  };
  for (int i = 0; i < kNumHexes; ++i) t.hex_cube[i] = coords[i];

  std::map<Cube, int> hex_index;
  for (int i = 0; i < kNumHexes; ++i) hex_index[coords[i]] = i;

  std::map<VKey, int> vert_id;
  auto get_vert = [&](VKey key) {
    auto it = vert_id.find(key);
    if (it != vert_id.end()) return it->second;
    int id = static_cast<int>(vert_id.size());
    if (id >= kNumVertices) throw std::runtime_error("too many vertices");
    vert_id.emplace(key, id);
    return id;
  };

  for (int h = 0; h < kNumHexes; ++h) {
    for (int c = 0; c < 6; ++c) {
      t.hex_vertices[h][c] = get_vert(corner_key(coords[h], c));
    }
  }
  if (static_cast<int>(vert_id.size()) != kNumVertices) {
    throw std::runtime_error("expected 54 vertices, got " + std::to_string(vert_id.size()));
  }

  for (int h = 0; h < kNumHexes; ++h) {
    for (int c = 0; c < 6; ++c) {
      int v = t.hex_vertices[h][c];
      auto& count = t.vertex_hex_count[v];
      bool seen = false;
      for (int i = 0; i < count; ++i) {
        if (t.vertex_hexes[v][i] == h) seen = true;
      }
      if (!seen && count < 3) t.vertex_hexes[v][count++] = h;
    }
  }

  std::map<std::pair<int, int>, int> edge_id;
  auto get_edge = [&](int a, int b) {
    if (a > b) std::swap(a, b);
    auto key = std::make_pair(a, b);
    auto it = edge_id.find(key);
    if (it != edge_id.end()) return it->second;
    int id = static_cast<int>(edge_id.size());
    if (id >= kNumEdges) throw std::runtime_error("too many edges");
    edge_id.emplace(key, id);
    t.edge_vertices[id] = {a, b};
    return id;
  };

  for (int h = 0; h < kNumHexes; ++h) {
    for (int c = 0; c < 6; ++c) {
      int a = t.hex_vertices[h][c];
      int b = t.hex_vertices[h][(c + 1) % 6];
      int e = get_edge(a, b);
      t.hex_edges[h][c] = e;
      auto& hc = t.edge_hex_count[e];
      bool seen = false;
      for (int i = 0; i < hc; ++i) {
        if (t.edge_hexes[e][i] == h) seen = true;
      }
      if (!seen && hc < 2) t.edge_hexes[e][hc++] = h;
    }
  }
  if (static_cast<int>(edge_id.size()) != kNumEdges) {
    throw std::runtime_error("expected 72 edges, got " + std::to_string(edge_id.size()));
  }

  for (int e = 0; e < kNumEdges; ++e) {
    int a = t.edge_vertices[e][0];
    int b = t.edge_vertices[e][1];
    auto add_n = [&](int v, int other, int edge) {
      auto& nc = t.vertex_neighbor_count[v];
      for (int i = 0; i < nc; ++i) {
        if (t.vertex_neighbors[v][i] == other) return;
      }
      if (nc >= 3) throw std::runtime_error("vertex degree > 3");
      t.vertex_neighbors[v][nc] = other;
      t.vertex_edges[v][nc] = edge;
      ++nc;
      t.vertex_edge_count[v] = nc;
    };
    add_n(a, b, e);
    add_n(b, a, e);
  }

  for (int v = 0; v < kNumVertices; ++v) {
    uint64_t mask = (1ULL << v);
    for (int i = 0; i < t.vertex_neighbor_count[v]; ++i) {
      mask |= (1ULL << t.vertex_neighbors[v][i]);
    }
    t.dist2_block[v] = mask;
  }

  (void)hex_index;
  return t;
}

int Topology::find_vertex(int h0, int h1, int h2) const {
  int hs[3] = {h0, h1, h2};
  std::sort(hs, hs + 3);
  for (int v = 0; v < kNumVertices; ++v) {
    if (vertex_hex_count[v] != 3) continue;
    int a[3] = {vertex_hexes[v][0], vertex_hexes[v][1], vertex_hexes[v][2]};
    std::sort(a, a + 3);
    if (a[0] == hs[0] && a[1] == hs[1] && a[2] == hs[2]) return v;
  }
  throw std::runtime_error("vertex triple not found");
}

int Topology::find_vertex2(int h0, int h1) const {
  if (h0 > h1) std::swap(h0, h1);
  for (int v = 0; v < kNumVertices; ++v) {
    if (vertex_hex_count[v] != 2) continue;
    int a = vertex_hexes[v][0], b = vertex_hexes[v][1];
    if (a > b) std::swap(a, b);
    if (a == h0 && b == h1) return v;
  }
  throw std::runtime_error("coastal vertex not found");
}

int Topology::find_edge(int v0, int v1) const {
  if (v0 > v1) std::swap(v0, v1);
  for (int e = 0; e < kNumEdges; ++e) {
    int a = edge_vertices[e][0], b = edge_vertices[e][1];
    if (a > b) std::swap(a, b);
    if (a == v0 && b == v1) return e;
  }
  throw std::runtime_error("edge not found");
}

}
