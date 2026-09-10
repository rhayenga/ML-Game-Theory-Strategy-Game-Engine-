// Action bonuses: when to expand, chase awards, trade, or park the robber.

#include "catan/strategy.hpp"

#include "catan/eval.hpp"

#include <algorithm>
#include <array>

namespace catan {
namespace {

double pip_of(const RuleCtx& ctx, const GameState& s, int player, Resource res) {
  double acc = 0;
  for (int h = 0; h < kNumHexes; ++h) {
    if (h == s.robber) continue;
    if (resource_of(ctx.board->hexes[h].terrain) != res) continue;
    int n = ctx.board->hexes[h].number;
    if (n < 2 || n > 12) continue;
    for (int c = 0; c < 6; ++c) {
      int v = ctx.topo->hex_vertices[h][c];
      uint8_t b = s.building[v];
      if (b == player + 1) acc += kPips[n];
      else if (b == player + 5) acc += 2.0 * kPips[n];
    }
  }
  return acc;
}

bool can_afford_road(const PlayerState& p) { return can_afford(p, 1, 1, 0, 0, 0); }
bool can_afford_settle(const PlayerState& p) { return can_afford(p, 1, 1, 0, 1, 1); }
bool can_afford_city(const PlayerState& p) { return can_afford(p, 0, 0, 3, 2, 0); }

int building_count(const GameState& s, int player) {
  int n = 0;
  for (int v = 0; v < kNumVertices; ++v) {
    uint8_t b = s.building[v];
    if (b == player + 1 || b == player + 5) ++n;
  }
  return n;
}

bool distance_ok_settle(const GameState& s, const Topology& topo, int v) {
  if (s.building[v] != 0) return false;
  uint64_t occupied = 0;
  for (int u = 0; u < kNumVertices; ++u) {
    if (s.building[u] != 0) occupied |= (1ULL << u);
  }
  return (occupied & (topo.dist2_block[v] & ~(1ULL << v))) == 0;
}

bool has_open_settle_site(const RuleCtx& ctx, const GameState& s, int player) {
  if (s.players[player].settles_left == 0) return false;
  for (int v = 0; v < kNumVertices; ++v) {
    if (!distance_ok_settle(s, *ctx.topo, v)) continue;
    for (int i = 0; i < ctx.topo->vertex_edge_count[v]; ++i) {
      if (s.road[ctx.topo->vertex_edges[v][i]] == player + 1) return true;
    }
  }
  return false;
}

int best_rival_road(const GameState& s, const Topology& topo, int player) {
  int best = 0;
  for (int p = 0; p < kNumPlayers; ++p) {
    if (p == player) continue;
    best = std::max(best, longest_road_len(s, topo, p));
  }
  return best;
}

int best_rival_army(const GameState& s, int player) {
  int best = 0;
  for (int p = 0; p < kNumPlayers; ++p) {
    if (p == player) continue;
    best = std::max(best, static_cast<int>(s.players[p].knights_played));
  }
  return best;
}

}

double maritime_trade_score(const GameState& s, int player, int give, int recv, int rate) {
  const auto& hand = s.players[player];
  PlayerState nxt = hand;
  if (nxt.res[give] >= rate) {
    nxt.res[give] = static_cast<uint8_t>(nxt.res[give] - rate);
    nxt.res[recv] = static_cast<uint8_t>(nxt.res[recv] + 1);
  }
  auto afford_road = [](const PlayerState& p) { return can_afford(p, 1, 1, 0, 0, 0); };
  auto afford_settle = [](const PlayerState& p) { return can_afford(p, 1, 1, 0, 1, 1); };
  auto afford_city = [](const PlayerState& p) { return can_afford(p, 0, 0, 3, 2, 0); };
  auto afford_dev = [](const PlayerState& p) { return can_afford(p, 0, 0, 1, 1, 1); };

  double unlock = 0;
  if (!afford_settle(hand) && afford_settle(nxt)) unlock += 1.0;
  if (!afford_city(hand) && afford_city(nxt)) unlock += 1.25;
  if (!afford_road(hand) && afford_road(nxt)) unlock += 0.25;
  if (!afford_dev(hand) && afford_dev(nxt)) unlock += 0.5;

  const int hs = hand_size(hand);
  const bool need_recv =
      (!afford_settle(hand) && hand.res[recv] < nxt.res[recv] &&
       ((recv == 0 && hand.res[0] < 1) || (recv == 1 && hand.res[1] < 1) ||
        (recv == 3 && hand.res[3] < 1) || (recv == 4 && hand.res[4] < 1))) ||
      (!afford_city(hand) && ((recv == 2 && hand.res[2] < 3) || (recv == 3 && hand.res[3] < 2))) ||
      (!afford_road(hand) && ((recv == 0 && hand.res[0] < 1) || (recv == 1 && hand.res[1] < 1))) ||
      (!afford_dev(hand) &&
       ((recv == 2 && hand.res[2] < 1) || (recv == 3 && hand.res[3] < 1) || (recv == 4 && hand.res[4] < 1)));

  if (unlock <= 0) {
    if (hs > 7) return 0.1;
    return -1.15;
  }

  int settles = 0;
  for (int v = 0; v < kNumVertices; ++v)
    if (s.building[v] == player + 1) ++settles;
  bool has_settle = settles > 0;
  int after_give = hand.res[give] - rate;
  bool surplus = after_give >= 1;
  if (give == static_cast<int>(Resource::Ore) && has_settle) surplus = after_give >= 3;
  if (give == static_cast<int>(Resource::Grain)) surplus = after_give >= (has_settle ? 2 : 1);

  const bool unlocked_vp =
      (!afford_settle(hand) && afford_settle(nxt)) || (!afford_city(hand) && afford_city(nxt));
  if (!unlocked_vp && !surplus) return -0.2;

  double score = unlock;
  if (rate == 2) {
    if (need_recv || hs < 7) score += 0.55;
    else score += 0.15;
  } else if (rate == 3) {
    if (need_recv || hs < 7) score += 0.4;
    else score += 0.05;
  }
  return score;
}

bool robber_hits_self(const RuleCtx& ctx, const GameState& s, int me, int hex) {
  if (hex < 0 || hex >= kNumHexes) return true;
  for (int c = 0; c < 6; ++c) {
    int v = ctx.topo->hex_vertices[hex][c];
    uint8_t b = s.building[v];
    if (!b) continue;
    int owner = (b <= 4) ? (b - 1) : (b - 5);
    if (owner == me) return true;
  }
  return false;
}

double robber_hex_score(const RuleCtx& ctx, const GameState& s, int me, int hex) {
  if (hex < 0 || hex >= kNumHexes || hex == s.robber) return -1e9;
  if (robber_hits_self(ctx, s, me, hex)) return -1e9;
  if (ctx.board->hexes[hex].terrain == Terrain::Desert) return -2.0;
  int num = ctx.board->hexes[hex].number;
  double pips = (num >= 2 && num <= 12) ? static_cast<double>(kPips[num]) : 0.0;
  double score = 0.15 * pips;
  bool hits_enemy = false;
  for (int c = 0; c < 6; ++c) {
    int v = ctx.topo->hex_vertices[hex][c];
    uint8_t b = s.building[v];
    if (!b) continue;
    int owner = (b <= 4) ? (b - 1) : (b - 5);
    if (owner == me) continue;
    hits_enemy = true;
    const bool city = b >= 5;
    double prod = city ? 2.0 * pips : pips;
    int cities = 0, settles = 0;
    for (int u = 0; u < kNumVertices; ++u) {
      uint8_t bu = s.building[u];
      if (bu == owner + 1) ++settles;
      if (bu == owner + 5) ++cities;
    }
    int ovp = total_vp(s, *ctx.topo, owner);
    double threat = 1.0 + ovp * 1.35 + cities * 2.4 + settles * 0.55;
    if (ovp >= 6) threat += 2.0;
    if (ovp >= 8) threat += 3.5;
    score += threat * (0.55 + 0.35 * prod);
    if (hand_size(s.players[owner]) > 0) score += 1.2 + 0.35 * ovp;
  }
  if (!hits_enemy) score -= 25.0;
  return score;
}

const char* strategy_name(StrategyStyle s) {
  switch (s) {
    case StrategyStyle::OwsCities: return "OWS (cities + army)";
    case StrategyStyle::WoodBrickRoad: return "Wood/Brick (expand + road)";
    default: return "Balanced";
  }
}

StrategyStyle infer_strategy(const RuleCtx& ctx, const GameState& s, int player) {
  double ore = pip_of(ctx, s, player, Resource::Ore);
  double grain = pip_of(ctx, s, player, Resource::Grain);
  double wool = pip_of(ctx, s, player, Resource::Wool);
  double wood = pip_of(ctx, s, player, Resource::Lumber);
  double brick = pip_of(ctx, s, player, Resource::Brick);
  double ows = ore + grain + 0.6 * wool;
  double wb = wood + brick;
  if (ows >= wb + 3.0 && ore >= 2.0 && grain >= 2.0) return StrategyStyle::OwsCities;
  if (wb >= ows + 2.0) return StrategyStyle::WoodBrickRoad;
  return StrategyStyle::Balanced;
}

double strategy_action_bonus(const RuleCtx& ctx, const GameState& s, const Action& a, int player,
                             StrategyStyle style) {
  (void)style;
  double b = 0;
  const auto& hand = s.players[player];
  const int builds = building_count(s, player);
  const bool need_expand = builds < 4;
  const int road_len = longest_road_len(s, *ctx.topo, player);
  const int knights = s.players[player].knights_played;
  const bool hold_lr = s.longest_road == player;
  const bool hold_la = s.largest_army == player;
  const int rival_road = best_rival_road(s, *ctx.topo, player);
  const int rival_army = best_rival_army(s, player);
  const bool secure_lr = hold_lr && (road_len - rival_road) >= 2;
  const bool secure_la = hold_la && (knights - rival_army) >= 2;
  const bool out_of_settle_space = !has_open_settle_site(ctx, s, player);

  int settles_on_board = 0, cities_on_board = 0;
  for (int v = 0; v < kNumVertices; ++v) {
    uint8_t bb = s.building[v];
    if (bb == player + 1) ++settles_on_board;
    else if (bb == player + 5) ++cities_on_board;
  }
  const bool must_expand = (settles_on_board + cities_on_board) <= 2 || builds < 3;
  const int vp_cards = static_cast<int>(hand.vp_cards);
  const bool contest_la =
      !hold_la && knights >= 2 && s.largest_army >= 0 &&
      (s.players[s.largest_army].knights_played - knights) <= 1;
  const bool contest_lr =
      !hold_lr && road_len >= 4 && s.longest_road >= 0 &&
      (longest_road_len(s, *ctx.topo, s.longest_road) - road_len) <= 2;

  auto lr_chase = [&](int my_len) {
    if (secure_lr) return 0.0;
    double u = 0;
    if (need_expand) u = 0.85 + 0.1 * my_len;
    else if (my_len >= 3) u = 0.1 * my_len;
    if (hold_lr) return u + 0.35;
    if (s.longest_road >= 0) {
      int theirs = longest_road_len(s, *ctx.topo, s.longest_road);
      int deficit = theirs - my_len;
      if (my_len >= 4 && deficit <= 2) u += 1.75;
    } else if (my_len >= 5) {
      u += 1.0;
    }
    return u;
  };
  auto la_chase = [&](int my_k) {
    if (secure_la) return 0.0;
    double u = 0;
    if (my_k >= 1) u += 0.2;
    if (my_k >= 2) u += 0.4;
    if (my_k >= 3) u += 0.6;
    if (hold_la) return u + 0.35;
    if (s.largest_army >= 0) {
      int theirs = s.players[s.largest_army].knights_played;
      int deficit = theirs - my_k;
      if (my_k >= 2 && deficit <= 2) u += 1.75;
      if (my_k >= 2 && deficit <= 1) u += 0.85;
    } else if (my_k >= 3) {
      u += 1.0;
    }
    return u;
  };

  if (a.type == ActionType::EndTurn) {
    if (can_afford_settle(hand) || can_afford_city(hand)) b -= 0.25;
    else if (can_afford_road(hand) && !(secure_lr && !out_of_settle_space)) b -= 0.25;
  }

  if (a.type == ActionType::BuildSettlement) {
    b += 1.15;
    if (must_expand) b += 0.85;
    if (secure_lr || secure_la) b += 0.25;
    if (a.a >= 0 && a.a < kNumVertices && ctx.board->port_at[a.a] != PortType::None) {
      b += 0.4;
      if (port_rate(ctx.board->port_at[a.a]) == 2) b += 0.15;
    }
  }
  if (a.type == ActionType::BuildCity) {
    b += 1.55;
    if (must_expand) b -= 0.95;
    if (secure_lr || secure_la) b += 0.35;
  }

  if (a.type == ActionType::BuildRoad && a.a >= 0) {
    int v0 = ctx.topo->edge_vertices[a.a][0];
    int v1 = ctx.topo->edge_vertices[a.a][1];
    bool opens_site = false;
    for (int v : {v0, v1}) {
      if (!distance_ok_settle(s, *ctx.topo, v)) continue;
      bool already = false;
      for (int i = 0; i < ctx.topo->vertex_edge_count[v]; ++i) {
        if (s.road[ctx.topo->vertex_edges[v][i]] == player + 1) already = true;
      }
      if (already) continue;
      opens_site = true;
    }
    if (secure_lr && !out_of_settle_space) {
      b -= 1.25;
    } else if (secure_lr && out_of_settle_space) {
      b += opens_site ? 0.5 : -0.6;
    } else {
      b += 0.5;
      if (must_expand && opens_site) b += 0.55;
      if (contest_lr) b += 0.45;
      b += lr_chase(road_len);
    }
  }

  if (a.type == ActionType::BuyDev) {
    if (secure_la) b -= 0.85;
    else if (contest_la) b += 0.95;
    else if (must_expand || can_afford_settle(hand)) b -= 0.35;
    else if (vp_cards >= 2 && !hold_la) b -= 0.45;
    else if (can_afford_city(hand)) b += 0.05;
    else b += 0.4;
  }

  if (a.type == ActionType::PlayRoadBuilding) {
    if (secure_lr && !out_of_settle_space) b -= 0.8;
    else {
      b += 0.5;
      b += 0.65 * lr_chase(road_len);
    }
  }
  if (a.type == ActionType::PlayMonopoly) b += 0.4;
  if (a.type == ActionType::PlayYearOfPlenty) b += 0.4;

  if (a.type == ActionType::PlaceRobber || a.type == ActionType::PlayKnight) {
    if (robber_hits_self(ctx, s, player, a.a)) {
      b -= 50.0;
    } else {
      double rs = std::max(0.0, robber_hex_score(ctx, s, player, a.a));
      b += 0.2 + 0.5 * rs;
      if (a.b >= 0 && a.b < kNumPlayers) b += 0.4 * total_vp(s, *ctx.topo, a.b);
      if (a.type == ActionType::PlayKnight) {
        if (!secure_la) b += 0.25 + la_chase(knights);
        else b += 0.05;
      }
    }
  }

  if (a.type == ActionType::MaritimeTrade) {
    b += maritime_trade_score(s, player, a.a, a.b, a.c);
    if (can_afford_settle(hand) || can_afford_city(hand)) b -= 0.5;
  }

  if (a.type == ActionType::Roll) b += 0.05;
  return b;
}

}
