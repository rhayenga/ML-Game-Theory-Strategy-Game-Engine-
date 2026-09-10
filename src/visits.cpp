#include "catan/visits.hpp"

#include "catan/json_api.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <vector>

namespace catan {
namespace {

void mix(uint64_t& h, uint64_t v) {
  h ^= v + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2);
}

}  // namespace

uint64_t hash_position(const GameState& s, const BoardSpec& board) {
  // Structural fingerprint only: board layout + pieces + turn/awards.
  // Excludes exact hands, bank, and shuffled deck so the same board state keeps
  // accumulating "prior games" across rolls/trades (otherwise priors die to 0
  // after the first dice roll).
  uint64_t h = 0xcbf29ce484222325ULL;
  for (int i = 0; i < kNumHexes; ++i) {
    mix(h, static_cast<uint64_t>(board.hexes[i].terrain));
    mix(h, board.hexes[i].number);
  }
  for (int v = 0; v < kNumVertices; ++v) mix(h, static_cast<uint64_t>(board.port_at[v]));
  mix(h, static_cast<uint64_t>(board.desert_hex));
  for (uint8_t b : s.building) mix(h, b);
  for (uint8_t r : s.road) mix(h, r);
  for (const auto& p : s.players) {
    mix(h, p.knights_played);
    mix(h, p.vp_cards);
    mix(h, p.roads_left);
    mix(h, p.settles_left);
    mix(h, p.cities_left);
  }
  mix(h, s.robber);
  mix(h, s.current);
  mix(h, static_cast<uint64_t>(s.phase));
  mix(h, static_cast<uint64_t>(s.longest_road + 1));
  mix(h, static_cast<uint64_t>(s.largest_army + 1));
  mix(h, s.free_roads);
  return h;
}

std::string action_key(const Action& a) {
  return action_type_name(a.type) + "|" + std::to_string(a.a) + "|" + std::to_string(a.b) + "|" +
         std::to_string(a.c);
}

bool VisitStore::load(const std::string& p) {
  path = p;
  std::ifstream in(p);
  if (!in) return false;
  std::stringstream buf;
  buf << in.rdbuf();
  std::string s = buf.str();
  by_hash.clear();
  // Minimal parse: "HASH":{"v":N,"m":{"KEY":V,...}}
  size_t i = 0;
  while (i < s.size()) {
    auto q1 = s.find('"', i);
    if (q1 == std::string::npos) break;
    auto q2 = s.find('"', q1 + 1);
    if (q2 == std::string::npos) break;
    std::string key = s.substr(q1 + 1, q2 - q1 - 1);
    // skip non-hex keys like "positions"
    bool hexish = !key.empty();
    for (char c : key) {
      if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'))) {
        hexish = false;
        break;
      }
    }
    if (!hexish || key.size() < 4) {
      i = q2 + 1;
      continue;
    }
    auto brace = s.find('{', q2);
    if (brace == std::string::npos) break;
    int depth = 0;
    size_t end = brace;
    for (; end < s.size(); ++end) {
      if (s[end] == '{') ++depth;
      else if (s[end] == '}') {
        --depth;
        if (depth == 0) {
          ++end;
          break;
        }
      }
    }
    std::string block = s.substr(brace, end - brace);
    PositionStat st;
    auto vp = block.find("\"v\"");
    if (vp != std::string::npos) {
      auto col = block.find(':', vp);
      if (col != std::string::npos) st.visits = std::strtoull(block.c_str() + col + 1, nullptr, 10);
    }
    auto gp = block.find("\"g\"");
    if (gp != std::string::npos) {
      auto col = block.find(':', gp);
      if (col != std::string::npos)
        st.game_seen = std::strtoull(block.c_str() + col + 1, nullptr, 10);
    } else {
      // Legacy files: treat old v as game_seen if small, else 0.
      st.game_seen = (st.visits < 1000) ? st.visits : 0;
    }
    auto mp = block.find("\"m\"");
    if (mp != std::string::npos) {
      auto lb = block.find('{', mp);
      auto rb = block.rfind('}');
      if (lb != std::string::npos && rb != std::string::npos && rb > lb) {
        size_t j = lb + 1;
        while (j < rb) {
          auto a = block.find('"', j);
          if (a == std::string::npos || a >= rb) break;
          auto b = block.find('"', a + 1);
          if (b == std::string::npos) break;
          std::string mk = block.substr(a + 1, b - a - 1);
          auto c = block.find(':', b);
          if (c == std::string::npos) break;
          int mv = std::atoi(block.c_str() + c + 1);
          st.moves[mk].visits = mv;
          j = c + 1;
        }
      }
    }
    by_hash[key] = st;
    i = end;
  }
  return true;
}

bool VisitStore::save(const std::string& p) const {
  std::ofstream out(p);
  if (!out) return false;
  out << "{\n  \"positions\": {\n";
  bool first = true;
  for (const auto& [k, st] : by_hash) {
    if (!first) out << ",\n";
    first = false;
    out << "    \"" << k << "\": {\"v\": " << st.visits << ", \"g\": " << st.game_seen
        << ", \"m\": {";
    bool fm = true;
    for (const auto& [mk, ms] : st.moves) {
      if (!fm) out << ", ";
      fm = false;
      out << "\"" << mk << "\": " << ms.visits;
    }
    out << "}}";
  }
  out << "\n  }\n}\n";
  return true;
}

PositionStat& VisitStore::touch(uint64_t h) {
  char buf[32];
  std::snprintf(buf, sizeof(buf), "%016llx", static_cast<unsigned long long>(h));
  return by_hash[buf];
}

const PositionStat* VisitStore::find(uint64_t h) const {
  char buf[32];
  std::snprintf(buf, sizeof(buf), "%016llx", static_cast<unsigned long long>(h));
  auto it = by_hash.find(buf);
  if (it == by_hash.end()) return nullptr;
  return &it->second;
}

void VisitStore::record_move(uint64_t h, const Action& a) {
  auto& st = touch(h);
  st.visits += 1;
  auto& mv = st.moves[action_key(a)];
  mv.visits += 1;
  if (st.moves.size() > 48) {
    std::string worst;
    int worst_v = 1e9;
    for (const auto& [k, m] : st.moves) {
      if (m.visits < worst_v) {
        worst_v = m.visits;
        worst = k;
      }
    }
    if (!worst.empty() && worst_v <= 1) st.moves.erase(worst);
  }
}

void VisitStore::prune(size_t max_positions) {
  if (by_hash.size() <= max_positions) return;
  std::vector<std::pair<uint64_t, std::string>> ranked;
  ranked.reserve(by_hash.size());
  for (const auto& [k, st] : by_hash) {
    uint64_t score = st.game_seen * 10 + st.visits;
    if (!st.moves.empty()) score += 5;
    ranked.push_back({score, k});
  }
  std::sort(ranked.begin(), ranked.end(),
            [](const auto& a, const auto& b) { return a.first > b.first; });
  std::unordered_map<std::string, PositionStat> keep;
  keep.reserve(max_positions);
  for (size_t i = 0; i < ranked.size() && keep.size() < max_positions; ++i) {
    keep.emplace(ranked[i].second, by_hash[ranked[i].second]);
  }
  by_hash.swap(keep);
}

double move_prior(const VisitStore* visits, uint64_t h, const Action& a,
                  const std::vector<Action>& legal) {
  const int n = std::max(1, static_cast<int>(legal.size()));
  if (!visits || legal.empty()) return 1.0 / n;
  const PositionStat* st = visits->find(h);
  if (!st || st->moves.empty()) return 1.0 / n;
  int total = 0;
  for (const auto& cand : legal) {
    auto it = st->moves.find(action_key(cand));
    total += (it != st->moves.end()) ? it->second.visits : 0;
  }
  auto it = st->moves.find(action_key(a));
  int v = (it != st->moves.end()) ? it->second.visits : 0;
  // Laplace smoothing — AlphaZero-style prior from empirical policy.
  return (static_cast<double>(v) + 1.0) / (static_cast<double>(total) + n);
}

uint64_t VisitStore::total_position_hits() const {
  uint64_t sum = 0;
  for (const auto& [_, st] : by_hash) sum += st.game_seen;
  return sum;
}

}  // namespace catan
