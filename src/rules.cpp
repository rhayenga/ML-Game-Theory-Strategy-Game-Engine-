#include "catan/rules.hpp"

#include <algorithm>
#include <functional>
#include <sstream>
#include <stdexcept>
#include <unordered_map>
#include <vector>

namespace catan {

int longest_road_len(const GameState& s, const Topology& topo, int player);

namespace {

bool connected_to_player_road(const GameState& s, const Topology& topo, int player, int vertex) {
  for (int i = 0; i < topo.vertex_edge_count[vertex]; ++i) {
    if (s.road[topo.vertex_edges[vertex][i]] == player + 1) return true;
  }
  return false;
}

// Network presence at a vertex for extending roads. Opponent buildings block
// extending past them (rulebook: cannot build on the other side of an opponent's building).
bool network_at_vertex(const GameState& s, const Topology& topo, int player, int vertex) {
  uint8_t b = s.building[vertex];
  if (b != 0) {
    int owner = (b <= 4) ? (b - 1) : (b - 5);
    return owner == player;
  }
  return connected_to_player_road(s, topo, player, vertex);
}

bool road_placement_legal(const GameState& s, const Topology& topo, int player, int edge) {
  if (s.road[edge] != 0) return false;
  int v0 = topo.edge_vertices[edge][0];
  int v1 = topo.edge_vertices[edge][1];
  return network_at_vertex(s, topo, player, v0) || network_at_vertex(s, topo, player, v1);
}

bool settlement_legal(const GameState& s, const Topology& topo, int player, int v, bool need_road) {
  if (s.building[v] != 0) return false;
  uint64_t occupied = 0;
  for (int u = 0; u < kNumVertices; ++u) {
    if (s.building[u] != 0) occupied |= (1ULL << u);
  }
  if (occupied & topo.dist2_block[v]) {
    if (occupied & (topo.dist2_block[v] & ~(1ULL << v))) return false;
  }
  if (need_road && !connected_to_player_road(s, topo, player, v)) return false;
  return true;
}

int best_maritime_rate(const GameState& s, const BoardSpec& board, int player, Resource give) {
  int rate = 4;
  for (int v = 0; v < kNumVertices; ++v) {
    uint8_t b = s.building[v];
    if (b != player + 1 && b != player + 5) continue;
    PortType pt = board.port_at[v];
    if (pt == PortType::None) continue;
    Resource pr = port_resource(pt);
    if (pt == PortType::Generic3) rate = std::min(rate, 3);
    else if (pr == give) rate = std::min(rate, 2);
  }
  return rate;
}

void give_resource(GameState& s, int player, int ri, int n) {
  for (int i = 0; i < n; ++i) {
    if (s.bank[ri] == 0) break;
    s.bank[ri]--;
    s.players[player].res[ri]++;
  }
}

// Official shortage rule (rulebook Production): if bank cannot cover everyone's demand
// for a resource, nobody gets that resource — unless only one player is due, then they
// take whatever remains.
void produce(const RuleCtx& ctx, GameState& s, int roll) {
  std::array<int, kNumPlayers> demand{};
  for (int ri = 0; ri < 5; ++ri) {
    demand.fill(0);
    for (int h = 0; h < kNumHexes; ++h) {
      if (h == s.robber) continue;
      if (ctx.board->hexes[h].number != roll) continue;
      Resource res = resource_of(ctx.board->hexes[h].terrain);
      if (static_cast<int>(res) != ri) continue;
      for (int c = 0; c < 6; ++c) {
        int v = ctx.topo->hex_vertices[h][c];
        uint8_t b = s.building[v];
        if (b == 0) continue;
        int player = (b <= 4) ? (b - 1) : (b - 5);
        int amount = (b <= 4) ? 1 : 2;
        demand[player] += amount;
      }
    }
    int total = 0;
    int claimants = 0;
    int only = -1;
    for (int p = 0; p < kNumPlayers; ++p) {
      if (demand[p] <= 0) continue;
      total += demand[p];
      ++claimants;
      only = p;
    }
    if (total == 0) continue;
    if (total <= s.bank[ri]) {
      for (int p = 0; p < kNumPlayers; ++p) give_resource(s, p, ri, demand[p]);
    } else if (claimants == 1) {
      give_resource(s, only, ri, demand[only]);
    }
    // else: insufficient bank for multiple claimants → nobody receives this resource
  }
}

void steal_random(GameState& s, int thief, int victim) {
  if (victim < 0 || victim >= kNumPlayers || victim == thief) return;
  int hs = hand_size(s.players[victim]);
  if (hs <= 0) return;
  int pick = static_cast<int>(rng_next(s) % hs);
  for (int r = 0; r < 5; ++r) {
    if (pick < s.players[victim].res[r]) {
      s.players[victim].res[r]--;
      s.players[thief].res[r]++;
      return;
    }
    pick -= s.players[victim].res[r];
  }
}

void check_winner(const RuleCtx& ctx, GameState& s) {
  // Win only checked on the active player's turn (rulebook: 10+ VPs during your turn).
  int p = s.current;
  if (total_vp(s, *ctx.topo, p) >= kWinVp) {
    s.game_over = true;
    s.winner = static_cast<int8_t>(p);
    s.phase = Phase::GameOver;
  }
}

int playable_devs(const PlayerState& p, DevType t) {
  int c = p.devs[static_cast<int>(t)];
  if (p.new_dev == static_cast<int>(t) + 1) --c;
  return std::max(0, c);
}

void append_dev_plays(const RuleCtx& ctx, const GameState& s, int me, std::vector<Action>& out) {
  if (s.players[me].played_dev) return;

  if (playable_devs(s.players[me], DevType::Knight) > 0) {
    for (int h = 0; h < kNumHexes; ++h) {
      if (h == s.robber) continue;
      std::vector<int> victims;
      for (int c = 0; c < 6; ++c) {
        int v = ctx.topo->hex_vertices[h][c];
        uint8_t b = s.building[v];
        if (!b) continue;
        int owner = (b <= 4) ? (b - 1) : (b - 5);
        if (owner == me || hand_size(s.players[owner]) <= 0) continue;
        if (std::find(victims.begin(), victims.end(), owner) == victims.end()) victims.push_back(owner);
      }
      if (victims.empty()) {
        out.push_back(Action{ActionType::PlayKnight, h, -1, 0, {}});
      } else {
        for (int vic : victims) out.push_back(Action{ActionType::PlayKnight, h, vic, 0, {}});
      }
    }
  }
  if (playable_devs(s.players[me], DevType::Monopoly) > 0) {
    for (int r = 0; r < 5; ++r) out.push_back(Action{ActionType::PlayMonopoly, r, 0, 0, {}});
  }
  if (playable_devs(s.players[me], DevType::YearOfPlenty) > 0) {
    for (int r0 = 0; r0 < 5; ++r0)
      for (int r1 = r0; r1 < 5; ++r1)
        if (s.bank[r0] > 0 && s.bank[r1] > (r0 == r1 ? 1 : 0))
          out.push_back(Action{ActionType::PlayYearOfPlenty, r0, r1, 0, {}});
  }
  if (playable_devs(s.players[me], DevType::RoadBuilding) > 0 && s.players[me].roads_left > 0) {
    // Card play itself; free road placements follow via free_roads.
    out.push_back(Action{ActionType::PlayRoadBuilding, -1, -1, 0, {}});
  }
}

}  // namespace

bool can_afford(const PlayerState& p, int brick, int lumber, int ore, int grain, int wool) {
  return p.res[0] >= brick && p.res[1] >= lumber && p.res[2] >= ore && p.res[3] >= grain &&
         p.res[4] >= wool;
}

void pay(GameState& s, int player, int brick, int lumber, int ore, int grain, int wool) {
  auto& p = s.players[player];
  p.res[0] -= brick;
  p.res[1] -= lumber;
  p.res[2] -= ore;
  p.res[3] -= grain;
  p.res[4] -= wool;
  s.bank[0] += brick;
  s.bank[1] += lumber;
  s.bank[2] += ore;
  s.bank[3] += grain;
  s.bank[4] += wool;
}

void recompute_awards(const RuleCtx& ctx, GameState& s) {
  // Longest road — need a continuous path of 5+ roads (rulebook). Also require
  // at least 5 road pieces on the board so opening setups can never claim it.
  int best_len = 0, best_p = -1;
  for (int p = 0; p < kNumPlayers; ++p) {
    int pieces = 0;
    for (int e = 0; e < kNumEdges; ++e) {
      if (s.road[e] == p + 1) ++pieces;
    }
    if (pieces < 5) continue;
    int len = longest_road_len(s, *ctx.topo, p);
    if (len >= 5 && len > best_len) {
      best_len = len;
      best_p = p;
    } else if (len >= 5 && len == best_len) {
      if (s.longest_road == p) best_p = p;
    }
  }
  s.longest_road = static_cast<int8_t>(best_p);

  // Largest army
  best_len = 0;
  best_p = -1;
  for (int p = 0; p < kNumPlayers; ++p) {
    int k = s.players[p].knights_played;
    if (k >= 3 && k > best_len) {
      best_len = k;
      best_p = p;
    } else if (k >= 3 && k == best_len && s.largest_army == p) {
      best_p = p;
    }
  }
  s.largest_army = static_cast<int8_t>(best_p);

  check_winner(ctx, s);
}

int longest_road_len(const GameState& s, const Topology& topo, int player) {
  std::vector<int> roads;
  roads.reserve(15);
  for (int e = 0; e < kNumEdges; ++e) {
    if (s.road[e] == player + 1) roads.push_back(e);
  }
  const int n = static_cast<int>(roads.size());
  if (n == 0) return 0;
  if (n == 1) return 1;

  std::array<int, kNumEdges> bit_of{};
  bit_of.fill(-1);
  for (int i = 0; i < n; ++i) bit_of[roads[i]] = i;

  // Sparse memo: (vertex, used-road-mask) → best extension length.
  std::unordered_map<uint64_t, int> memo;
  memo.reserve(static_cast<size_t>(n) * 64);

  std::function<int(int, int)> dfs = [&](int v, int mask) -> int {
    const uint64_t key = (static_cast<uint64_t>(v) << 32) | static_cast<uint32_t>(mask);
    auto it = memo.find(key);
    if (it != memo.end()) return it->second;
    int best = 0;
    for (int i = 0; i < topo.vertex_edge_count[v]; ++i) {
      int e = topo.vertex_edges[v][i];
      int bi = bit_of[e];
      if (bi < 0) continue;
      if (mask & (1 << bi)) continue;
      int other = topo.edge_vertices[e][0] == v ? topo.edge_vertices[e][1]
                                                : topo.edge_vertices[e][0];
      uint8_t b = s.building[other];
      if (b != 0) {
        int owner = (b <= 4) ? (b - 1) : (b - 5);
        if (owner != player) {
          best = std::max(best, 1);
          continue;
        }
      }
      best = std::max(best, 1 + dfs(other, mask | (1 << bi)));
    }
    memo[key] = best;
    return best;
  };

  int best = 0;
  for (int e : roads) {
    int bi = bit_of[e];
    int v0 = topo.edge_vertices[e][0];
    int v1 = topo.edge_vertices[e][1];
    auto start_from = [&](int to) {
      uint8_t b = s.building[to];
      if (b != 0) {
        int owner = (b <= 4) ? (b - 1) : (b - 5);
        if (owner != player) {
          best = std::max(best, 1);
          return;
        }
      }
      best = std::max(best, 1 + dfs(to, 1 << bi));
    };
    start_from(v1);
    start_from(v0);
  }
  return best;
}

std::string action_to_string(const Action& act) {
  std::ostringstream o;
  switch (act.type) {
    case ActionType::Roll: o << "Roll"; break;
    case ActionType::EndTurn: o << "EndTurn"; break;
    case ActionType::Discard: o << "Discard"; break;
    case ActionType::PlaceRobber:
      o << "Robber(hex=" << act.a << ",steal=" << act.b << ")";
      break;
    case ActionType::BuildRoad: o << "BuildRoad(" << act.a << ")"; break;
    case ActionType::BuildSettlement: o << "BuildSettlement(" << act.a << ")"; break;
    case ActionType::BuildCity: o << "BuildCity(" << act.a << ")"; break;
    case ActionType::BuyDev: o << "BuyDev"; break;
    case ActionType::PlayKnight:
      o << "PlayKnight(hex=" << act.a << ",steal=" << act.b << ")";
      break;
    case ActionType::PlayMonopoly: o << "Monopoly(" << resource_name(static_cast<Resource>(act.a)) << ")"; break;
    case ActionType::PlayYearOfPlenty:
      o << "YOP(" << resource_name(static_cast<Resource>(act.a)) << ","
        << resource_name(static_cast<Resource>(act.b)) << ")";
      break;
    case ActionType::PlayRoadBuilding: o << "RoadBuilding(" << act.a << "," << act.b << ")"; break;
    case ActionType::MaritimeTrade:
      o << "Trade(" << resource_name(static_cast<Resource>(act.a)) << "->"
        << resource_name(static_cast<Resource>(act.b)) << ",rate=" << act.c << ")";
      break;
  }
  return o.str();
}

std::vector<Action> legal_actions(const RuleCtx& ctx, const GameState& s) {
  std::vector<Action> out;
  if (s.game_over || s.phase == Phase::GameOver) return out;

  int me = s.current;

  if (s.phase == Phase::Discard) {
    // Only generate discard for players who need it; current sweeps in order.
    for (int p = 0; p < kNumPlayers; ++p) {
      if (!(s.discard_left & (1u << p))) continue;
      int hs = hand_size(s.players[p]);
      int need = hs / 2;
      // Enumerate is huge — generate one greedy discard action (half highest counts).
      Action act;
      act.type = ActionType::Discard;
      act.a = p;
      auto res = s.players[p].res;
      int left = need;
      while (left > 0) {
        int best = 0;
        for (int r = 1; r < 5; ++r) if (res[r] > res[best]) best = r;
        if (res[best] == 0) break;
        act.discard[best]++;
        res[best]--;
        --left;
      }
      out.push_back(act);
      return out;  // one player at a time
    }
    return out;
  }

  if (s.phase == Phase::RobberMove) {
    for (int h = 0; h < kNumHexes; ++h) {
      if (h == s.robber) continue;
      std::vector<int> victims;
      for (int c = 0; c < 6; ++c) {
        int v = ctx.topo->hex_vertices[h][c];
        uint8_t b = s.building[v];
        if (b == 0) continue;
        int owner = (b <= 4) ? (b - 1) : (b - 5);
        if (owner == me) continue;
        if (hand_size(s.players[owner]) <= 0) continue;
        if (std::find(victims.begin(), victims.end(), owner) == victims.end()) victims.push_back(owner);
      }
      if (victims.empty()) {
        out.push_back(Action{ActionType::PlaceRobber, h, -1, 0, {}});
      } else {
        for (int vic : victims) out.push_back(Action{ActionType::PlaceRobber, h, vic, 0, {}});
      }
    }
    return out;
  }

  // Road Building: must place free roads before other actions.
  if (s.free_roads > 0) {
    for (int e = 0; e < kNumEdges; ++e) {
      if (road_placement_legal(s, *ctx.topo, me, e))
        out.push_back(Action{ActionType::BuildRoad, e, 0, 0, {}});
    }
    if (out.empty()) {
      // No legal placement left (piece limit / blocked) — cancel remaining free roads.
      out.push_back(Action{ActionType::BuildRoad, -1, 0, 0, {}});
    }
    return out;
  }

  if (s.phase == Phase::PreRoll) {
    // Optional: play one development card before rolling (rulebook Production phase).
    append_dev_plays(ctx, s, me, out);
    out.push_back(Action{ActionType::Roll, 0, 0, 0, {}});
    return out;
  }

  // Main / Action phase
  if (s.phase == Phase::Main) {
    append_dev_plays(ctx, s, me, out);

    if (s.players[me].roads_left > 0 && can_afford(s.players[me], 1, 1, 0, 0, 0)) {
      for (int e = 0; e < kNumEdges; ++e) {
        if (road_placement_legal(s, *ctx.topo, me, e))
          out.push_back(Action{ActionType::BuildRoad, e, 0, 0, {}});
      }
    }

    if (s.players[me].settles_left > 0 && can_afford(s.players[me], 1, 1, 0, 1, 1)) {
      for (int v = 0; v < kNumVertices; ++v) {
        if (settlement_legal(s, *ctx.topo, me, v, true))
          out.push_back(Action{ActionType::BuildSettlement, v, 0, 0, {}});
      }
    }

    if (s.players[me].cities_left > 0 && can_afford(s.players[me], 0, 0, 3, 2, 0)) {
      for (int v = 0; v < kNumVertices; ++v) {
        if (s.building[v] == me + 1) out.push_back(Action{ActionType::BuildCity, v, 0, 0, {}});
      }
    }

    if (s.dev_next < GameState::kDevDeckSize && can_afford(s.players[me], 0, 0, 1, 1, 1)) {
      out.push_back(Action{ActionType::BuyDev, 0, 0, 0, {}});
    }

    for (int give = 0; give < 5; ++give) {
      int rate = best_maritime_rate(s, *ctx.board, me, static_cast<Resource>(give));
      if (s.players[me].res[give] < rate) continue;
      for (int recv = 0; recv < 5; ++recv) {
        if (recv == give) continue;
        if (s.bank[recv] == 0) continue;
        out.push_back(Action{ActionType::MaritimeTrade, give, recv, rate, {}});
      }
    }

    out.push_back(Action{ActionType::EndTurn, 0, 0, 0, {}});
  }

  return out;
}

void apply_action(const RuleCtx& ctx, GameState& s, const Action& act) {
  int me = s.current;

  switch (act.type) {
    case ActionType::Roll: {
      int roll = roll_dice(s);
      if (roll == 7) {
        s.discard_left = 0;
        for (int p = 0; p < kNumPlayers; ++p) {
          if (hand_size(s.players[p]) > kMaxHandSafe) s.discard_left |= (1u << p);
        }
        if (s.discard_left) s.phase = Phase::Discard;
        else s.phase = Phase::RobberMove;
      } else {
        produce(ctx, s, roll);
        s.phase = Phase::Main;
      }
      break;
    }
    case ActionType::Discard: {
      int p = act.a;
      for (int r = 0; r < 5; ++r) {
        s.players[p].res[r] -= act.discard[r];
        s.bank[r] += act.discard[r];
      }
      s.discard_left &= static_cast<uint8_t>(~(1u << p));
      if (!s.discard_left) s.phase = Phase::RobberMove;
      break;
    }
    case ActionType::PlaceRobber: {
      s.robber = static_cast<uint8_t>(act.a);
      steal_random(s, me, act.b);
      s.phase = Phase::Main;
      break;
    }
    case ActionType::BuildRoad: {
      if (act.a < 0) {
        s.free_roads = 0;
        break;
      }
      if (s.free_roads > 0) {
        s.road[act.a] = static_cast<uint8_t>(me + 1);
        s.players[me].roads_left--;
        s.free_roads--;
      } else {
        pay(s, me, 1, 1, 0, 0, 0);
        s.road[act.a] = static_cast<uint8_t>(me + 1);
        s.players[me].roads_left--;
      }
      recompute_awards(ctx, s);
      break;
    }
    case ActionType::BuildSettlement: {
      pay(s, me, 1, 1, 0, 1, 1);
      s.building[act.a] = static_cast<uint8_t>(me + 1);
      s.players[me].settles_left--;
      recompute_awards(ctx, s);
      break;
    }
    case ActionType::BuildCity: {
      pay(s, me, 0, 0, 3, 2, 0);
      s.building[act.a] = static_cast<uint8_t>(me + 5);
      s.players[me].settles_left++;  // settlement piece returns
      s.players[me].cities_left--;
      recompute_awards(ctx, s);
      break;
    }
    case ActionType::BuyDev: {
      pay(s, me, 0, 0, 1, 1, 1);
      if (s.dev_next >= GameState::kDevDeckSize) break;
      int chosen = static_cast<int>(s.dev_deck[s.dev_next++]);
      if (s.dev_bank[chosen] > 0) s.dev_bank[chosen]--;
      // VP cards are hidden until the end; they count toward win checks immediately.
      if (chosen == static_cast<int>(DevType::VictoryPoint)) {
        s.players[me].vp_cards++;
      } else {
        s.players[me].devs[chosen]++;
        s.players[me].new_dev = static_cast<uint8_t>(chosen + 1);
      }
      recompute_awards(ctx, s);
      break;
    }
    case ActionType::PlayKnight: {
      s.players[me].devs[static_cast<int>(DevType::Knight)]--;
      s.players[me].knights_played++;
      s.players[me].played_dev = 1;
      if (s.players[me].new_dev == static_cast<int>(DevType::Knight) + 1) s.players[me].new_dev = 0;
      s.robber = static_cast<uint8_t>(act.a);
      if (act.b >= 0) {
        steal_random(s, me, act.b);
      } else {
        // Fallback: steal from the strongest (VP) victim on the hex, not a random seat.
        int best = -1;
        int best_vp = -1;
        for (int c = 0; c < 6; ++c) {
          int v = ctx.topo->hex_vertices[act.a][c];
          uint8_t b = s.building[v];
          if (!b) continue;
          int owner = (b <= 4) ? (b - 1) : (b - 5);
          if (owner == me || hand_size(s.players[owner]) <= 0) continue;
          int ovp = total_vp(s, *ctx.topo, owner);
          if (ovp > best_vp) {
            best_vp = ovp;
            best = owner;
          }
        }
        if (best >= 0) steal_random(s, me, best);
      }
      recompute_awards(ctx, s);
      break;
    }
    case ActionType::PlayMonopoly: {
      s.players[me].devs[static_cast<int>(DevType::Monopoly)]--;
      s.players[me].played_dev = 1;
      if (s.players[me].new_dev == static_cast<int>(DevType::Monopoly) + 1) s.players[me].new_dev = 0;
      int r = act.a;
      for (int p = 0; p < kNumPlayers; ++p) {
        if (p == me) continue;
        s.players[me].res[r] += s.players[p].res[r];
        s.players[p].res[r] = 0;
      }
      break;
    }
    case ActionType::PlayYearOfPlenty: {
      s.players[me].devs[static_cast<int>(DevType::YearOfPlenty)]--;
      s.players[me].played_dev = 1;
      if (s.players[me].new_dev == static_cast<int>(DevType::YearOfPlenty) + 1) s.players[me].new_dev = 0;
      give_resource(s, me, act.a, 1);
      give_resource(s, me, act.b, 1);
      break;
    }
    case ActionType::PlayRoadBuilding: {
      s.players[me].devs[static_cast<int>(DevType::RoadBuilding)]--;
      s.players[me].played_dev = 1;
      if (s.players[me].new_dev == static_cast<int>(DevType::RoadBuilding) + 1) s.players[me].new_dev = 0;
      s.free_roads = static_cast<uint8_t>(std::min(2, static_cast<int>(s.players[me].roads_left)));
      break;
    }
    case ActionType::MaritimeTrade: {
      int give = act.a, recv = act.b, rate = act.c;
      s.players[me].res[give] -= rate;
      s.bank[give] += rate;
      give_resource(s, me, recv, 1);
      break;
    }
    case ActionType::EndTurn: {
      s.players[me].new_dev = 0;
      s.players[me].played_dev = 0;
      s.free_roads = 0;
      s.current = static_cast<uint8_t>((me + 1) % kNumPlayers);
      s.phase = Phase::PreRoll;
      break;
    }
  }
}

}  // namespace catan
