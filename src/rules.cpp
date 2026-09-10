#include "catan/rules.hpp"

#include <algorithm>
#include <functional>
#include <sstream>
#include <stdexcept>
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

bool settlement_legal(const GameState& s, const Topology& topo, int player, int v, bool need_road) {
  if (s.building[v] != 0) return false;
  uint64_t occupied = 0;
  for (int u = 0; u < kNumVertices; ++u) {
    if (s.building[u] != 0) occupied |= (1ULL << u);
  }
  if (occupied & topo.dist2_block[v]) {
    // dist2_block includes self; if only self and empty, ok — but occupied & block means
    // a building is within distance 1 (neighbor) or self.
    // Self is empty so if any neighbor occupied, blocked.
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

void produce(const RuleCtx& ctx, GameState& s, int roll) {
  for (int h = 0; h < kNumHexes; ++h) {
    if (h == s.robber) continue;
    if (ctx.board->hexes[h].number != roll) continue;
    Resource res = resource_of(ctx.board->hexes[h].terrain);
    if (res == Resource::None) continue;
    int ri = static_cast<int>(res);
    for (int c = 0; c < 6; ++c) {
      int v = ctx.topo->hex_vertices[h][c];
      uint8_t b = s.building[v];
      if (b == 0) continue;
      int player = (b <= 4) ? (b - 1) : (b - 5);
      int amount = (b <= 4) ? 1 : 2;
      give_resource(s, player, ri, amount);
    }
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
  for (int p = 0; p < kNumPlayers; ++p) {
    if (total_vp(s, *ctx.topo, p) >= kWinVp) {
      s.game_over = true;
      s.winner = static_cast<int8_t>(p);
      s.phase = Phase::GameOver;
      return;
    }
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
  // Longest road
  int best_len = 0, best_p = -1;
  for (int p = 0; p < kNumPlayers; ++p) {
    int len = longest_road_len(s, *ctx.topo, p);
    if (len >= 5 && len > best_len) {
      best_len = len;
      best_p = p;
    } else if (len >= 5 && len == best_len) {
      // keep current holder if tied
      if (s.longest_road == p) best_p = p;
    }
  }
  if (best_p >= 0) {
    if (s.longest_road != best_p) s.longest_road = static_cast<int8_t>(best_p);
  } else {
    s.longest_road = -1;
  }

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
  if (best_p >= 0) s.largest_army = static_cast<int8_t>(best_p);
  else s.largest_army = -1;

  check_winner(ctx, s);
}

int longest_road_len(const GameState& s, const Topology& topo, int player) {
  std::vector<uint8_t> visited(kNumEdges, 0);
  std::function<int(int, int)> walk = [&](int edge, int from_v) -> int {
    if (s.road[edge] != player + 1 || visited[edge]) return 0;
    visited[edge] = 1;
    int other = topo.edge_vertices[edge][0] == from_v ? topo.edge_vertices[edge][1]
                                                      : topo.edge_vertices[edge][0];
    uint8_t b = s.building[other];
    if (b != 0) {
      int owner = (b <= 4) ? (b - 1) : (b - 5);
      if (owner != player) {
        visited[edge] = 0;
        return 1;
      }
    }
    int best = 1;
    for (int i = 0; i < topo.vertex_edge_count[other]; ++i) {
      int e2 = topo.vertex_edges[other][i];
      if (e2 == edge) continue;
      best = std::max(best, 1 + walk(e2, other));
    }
    visited[edge] = 0;
    return best;
  };

  int best = 0;
  for (int e = 0; e < kNumEdges; ++e) {
    if (s.road[e] != player + 1) continue;
    for (int vi = 0; vi < 2; ++vi) {
      std::fill(visited.begin(), visited.end(), 0);
      best = std::max(best, walk(e, topo.edge_vertices[e][vi]));
    }
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

  if (s.phase == Phase::RobberMove || (s.phase == Phase::PreRoll && false)) {
    // handled below for PlaceRobber when phase is RobberMove
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

  if (s.phase == Phase::PreRoll) {
    // Play knight before roll (optional) or roll.
    if (s.players[me].devs[static_cast<int>(DevType::Knight)] >
        (s.players[me].new_dev == static_cast<int>(DevType::Knight) + 1 ? 1 : 0)) {
      // Can play knight — expand robber targets (subset).
      for (int h = 0; h < kNumHexes; ++h) {
        if (h == s.robber) continue;
        out.push_back(Action{ActionType::PlayKnight, h, -1, 0, {}});
      }
    }
    out.push_back(Action{ActionType::Roll, 0, 0, 0, {}});
    return out;
  }

  // Main phase
  if (s.phase == Phase::Main) {
    // Play knight
    int knight_cnt = s.players[me].devs[static_cast<int>(DevType::Knight)];
    if (s.players[me].new_dev == static_cast<int>(DevType::Knight) + 1) knight_cnt--;
    if (knight_cnt > 0) {
      for (int h = 0; h < kNumHexes; ++h) {
        if (h == s.robber) continue;
        out.push_back(Action{ActionType::PlayKnight, h, -1, 0, {}});
      }
    }

    // Monopoly
    int mono = s.players[me].devs[static_cast<int>(DevType::Monopoly)];
    if (s.players[me].new_dev == static_cast<int>(DevType::Monopoly) + 1) mono--;
    if (mono > 0) {
      for (int r = 0; r < 5; ++r) out.push_back(Action{ActionType::PlayMonopoly, r, 0, 0, {}});
    }

    // Year of plenty
    int yop = s.players[me].devs[static_cast<int>(DevType::YearOfPlenty)];
    if (s.players[me].new_dev == static_cast<int>(DevType::YearOfPlenty) + 1) yop--;
    if (yop > 0) {
      for (int r0 = 0; r0 < 5; ++r0)
        for (int r1 = r0; r1 < 5; ++r1)
          if (s.bank[r0] > 0 && s.bank[r1] > (r0 == r1 ? 1 : 0))
            out.push_back(Action{ActionType::PlayYearOfPlenty, r0, r1, 0, {}});
    }

    // Road building
    int rb = s.players[me].devs[static_cast<int>(DevType::RoadBuilding)];
    if (s.players[me].new_dev == static_cast<int>(DevType::RoadBuilding) + 1) rb--;
    if (rb > 0 && s.players[me].roads_left > 0) {
      for (int e = 0; e < kNumEdges; ++e) {
        if (s.road[e] != 0) continue;
        int v0 = ctx.topo->edge_vertices[e][0], v1 = ctx.topo->edge_vertices[e][1];
        if (!connected_to_player_road(s, *ctx.topo, me, v0) &&
            !connected_to_player_road(s, *ctx.topo, me, v1) &&
            s.building[v0] != me + 1 && s.building[v0] != me + 5 &&
            s.building[v1] != me + 1 && s.building[v1] != me + 5)
          continue;
        out.push_back(Action{ActionType::PlayRoadBuilding, e, -1, 0, {}});
      }
    }

    // Builds
    if (s.players[me].roads_left > 0 && can_afford(s.players[me], 1, 1, 0, 0, 0)) {
      for (int e = 0; e < kNumEdges; ++e) {
        if (s.road[e] != 0) continue;
        int v0 = ctx.topo->edge_vertices[e][0], v1 = ctx.topo->edge_vertices[e][1];
        bool ok = connected_to_player_road(s, *ctx.topo, me, v0) ||
                  connected_to_player_road(s, *ctx.topo, me, v1) ||
                  s.building[v0] == me + 1 || s.building[v0] == me + 5 ||
                  s.building[v1] == me + 1 || s.building[v1] == me + 5;
        if (ok) out.push_back(Action{ActionType::BuildRoad, e, 0, 0, {}});
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

    int dev_left = 0;
    for (int t = 0; t < 5; ++t) dev_left += s.dev_bank[t];
    if (dev_left > 0 && can_afford(s.players[me], 0, 0, 1, 1, 1)) {
      out.push_back(Action{ActionType::BuyDev, 0, 0, 0, {}});
    }

    // Maritime trades (prune: only if have rate copies)
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
      pay(s, me, 1, 1, 0, 0, 0);
      s.road[act.a] = static_cast<uint8_t>(me + 1);
      s.players[me].roads_left--;
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
      int total = 0;
      for (int t = 0; t < 5; ++t) total += s.dev_bank[t];
      int pick = static_cast<int>(rng_next(s) % total);
      int chosen = 0;
      for (int t = 0; t < 5; ++t) {
        if (pick < s.dev_bank[t]) {
          chosen = t;
          break;
        }
        pick -= s.dev_bank[t];
      }
      s.dev_bank[chosen]--;
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
      if (s.players[me].new_dev == static_cast<int>(DevType::Knight) + 1) s.players[me].new_dev = 0;
      s.robber = static_cast<uint8_t>(act.a);
      // Steal from random victim on hex if b < 0
      if (act.b >= 0) steal_random(s, me, act.b);
      else {
        std::vector<int> victims;
        for (int c = 0; c < 6; ++c) {
          int v = ctx.topo->hex_vertices[act.a][c];
          uint8_t b = s.building[v];
          if (!b) continue;
          int owner = (b <= 4) ? (b - 1) : (b - 5);
          if (owner == me || hand_size(s.players[owner]) <= 0) continue;
          if (std::find(victims.begin(), victims.end(), owner) == victims.end()) victims.push_back(owner);
        }
        if (!victims.empty()) steal_random(s, me, victims[rng_next(s) % victims.size()]);
      }
      if (s.phase == Phase::PreRoll) {
        // still need to roll
      } else {
        // stay main
      }
      recompute_awards(ctx, s);
      break;
    }
    case ActionType::PlayMonopoly: {
      s.players[me].devs[static_cast<int>(DevType::Monopoly)]--;
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
      if (s.players[me].new_dev == static_cast<int>(DevType::YearOfPlenty) + 1) s.players[me].new_dev = 0;
      give_resource(s, me, act.a, 1);
      give_resource(s, me, act.b, 1);
      break;
    }
    case ActionType::PlayRoadBuilding: {
      s.players[me].devs[static_cast<int>(DevType::RoadBuilding)]--;
      if (s.players[me].new_dev == static_cast<int>(DevType::RoadBuilding) + 1) s.players[me].new_dev = 0;
      if (act.a >= 0 && s.players[me].roads_left > 0 && s.road[act.a] == 0) {
        s.road[act.a] = static_cast<uint8_t>(me + 1);
        s.players[me].roads_left--;
      }
      if (act.b >= 0 && s.players[me].roads_left > 0 && s.road[act.b] == 0) {
        s.road[act.b] = static_cast<uint8_t>(me + 1);
        s.players[me].roads_left--;
      }
      recompute_awards(ctx, s);
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
      s.current = static_cast<uint8_t>((me + 1) % kNumPlayers);
      s.phase = Phase::PreRoll;
      break;
    }
  }
}

}  // namespace catan
