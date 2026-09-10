// Serialize game state and explain moves for the UI.

#include "catan/json_api.hpp"

#include "catan/strategy.hpp"

#include <array>
#include <cmath>
#include <sstream>

namespace catan {
namespace {

std::string esc(const std::string& s) {
  std::string o;
  o.reserve(s.size() + 8);
  for (char c : s) {
    if (c == '"' || c == '\\') {
      o.push_back('\\');
      o.push_back(c);
    } else if (c == '\n') {
      o += "\\n";
    } else {
      o.push_back(c);
    }
  }
  return o;
}

const char* terrain_name(Terrain t) {
  switch (t) {
    case Terrain::Hills: return "hills";
    case Terrain::Forest: return "forest";
    case Terrain::Mountains: return "mountains";
    case Terrain::Fields: return "fields";
    case Terrain::Pasture: return "pasture";
    default: return "desert";
  }
}

const char* port_type_name(PortType p) {
  switch (p) {
    case PortType::Generic3: return "generic3";
    case PortType::Brick2: return "brick2";
    case PortType::Lumber2: return "lumber2";
    case PortType::Ore2: return "ore2";
    case PortType::Grain2: return "grain2";
    case PortType::Wool2: return "wool2";
    default: return "none";
  }
}

const char* phase_name(Phase p) {
  switch (p) {
    case Phase::PreRoll: return "pre_roll";
    case Phase::Discard: return "discard";
    case Phase::RobberMove: return "robber";
    case Phase::Main: return "main";
    default: return "game_over";
  }
}

void hex_pixel(Cube c, double size, double& x, double& y) {
  double q = c.x;
  double r = c.z;
  x = size * (1.5 * q);
  y = size * (std::sqrt(3.0) * (r + q / 2.0));
}

constexpr Cube kDir[6] = {
    {1, -1, 0}, {1, 0, -1}, {0, 1, -1}, {-1, 1, 0}, {-1, 0, 1}, {0, -1, 1},
};

Cube cube_add(Cube a, Cube b) { return Cube{a.x + b.x, a.y + b.y, a.z + b.z}; }

void vertex_pixel(const Topology& topo, int v, double size, double& x, double& y) {
  for (int h = 0; h < kNumHexes; ++h) {
    for (int c = 0; c < 6; ++c) {
      if (topo.hex_vertices[h][c] != v) continue;
      Cube H = topo.hex_cube[h];
      Cube A = cube_add(H, kDir[c]);
      Cube B = cube_add(H, kDir[(c + 5) % 6]);
      double x0, y0, x1, y1, x2, y2;
      hex_pixel(H, size, x0, y0);
      hex_pixel(A, size, x1, y1);
      hex_pixel(B, size, x2, y2);
      x = (x0 + x1 + x2) / 3.0;
      y = (y0 + y1 + y2) / 3.0;
      return;
    }
  }
  x = y = 0;
}

}

std::string action_type_name(ActionType t) {
  switch (t) {
    case ActionType::Roll: return "Roll";
    case ActionType::EndTurn: return "EndTurn";
    case ActionType::Discard: return "Discard";
    case ActionType::PlaceRobber: return "PlaceRobber";
    case ActionType::BuildRoad: return "BuildRoad";
    case ActionType::BuildSettlement: return "BuildSettlement";
    case ActionType::BuildCity: return "BuildCity";
    case ActionType::BuyDev: return "BuyDev";
    case ActionType::PlayKnight: return "PlayKnight";
    case ActionType::PlayMonopoly: return "PlayMonopoly";
    case ActionType::PlayYearOfPlenty: return "PlayYearOfPlenty";
    case ActionType::PlayRoadBuilding: return "PlayRoadBuilding";
    case ActionType::MaritimeTrade: return "MaritimeTrade";
  }
  return "EndTurn";
}

bool action_type_from_name(const std::string& name, ActionType& out) {
  if (name == "Roll") out = ActionType::Roll;
  else if (name == "EndTurn") out = ActionType::EndTurn;
  else if (name == "Discard") out = ActionType::Discard;
  else if (name == "PlaceRobber") out = ActionType::PlaceRobber;
  else if (name == "BuildRoad") out = ActionType::BuildRoad;
  else if (name == "BuildSettlement") out = ActionType::BuildSettlement;
  else if (name == "BuildCity") out = ActionType::BuildCity;
  else if (name == "BuyDev") out = ActionType::BuyDev;
  else if (name == "PlayKnight") out = ActionType::PlayKnight;
  else if (name == "PlayMonopoly") out = ActionType::PlayMonopoly;
  else if (name == "PlayYearOfPlenty") out = ActionType::PlayYearOfPlenty;
  else if (name == "PlayRoadBuilding") out = ActionType::PlayRoadBuilding;
  else if (name == "MaritimeTrade") out = ActionType::MaritimeTrade;
  else return false;
  return true;
}

std::string explain_action(const Action& act) {
  switch (act.type) {
    case ActionType::Roll: return "Roll the dice";
    case ActionType::EndTurn: return "End your turn";
    case ActionType::Discard: return "Discard half your cards (7 was rolled)";
    case ActionType::PlaceRobber:
      return "Move robber to hex " + std::to_string(act.a) +
             (act.b >= 0 ? std::string(" and steal from ") + player_name(static_cast<Player>(act.b))
                         : "");
    case ActionType::BuildRoad:
      if (act.a < 0) return "Skip remaining free roads (none legal)";
      return "Build a road";
    case ActionType::BuildSettlement: return "Build a settlement";
    case ActionType::BuildCity: return "Upgrade to a city";
    case ActionType::BuyDev: return "Buy a development card";
    case ActionType::PlayKnight:
      return "Play Knight — move robber to hex " + std::to_string(act.a);
    case ActionType::PlayMonopoly:
      return std::string("Play Monopoly on ") + resource_name(static_cast<Resource>(act.a));
    case ActionType::PlayYearOfPlenty:
      return std::string("Play Invention: take ") + resource_name(static_cast<Resource>(act.a)) +
             " and " + resource_name(static_cast<Resource>(act.b));
    case ActionType::PlayRoadBuilding:
      return "Play Road Building (place up to 2 free roads)";
    case ActionType::MaritimeTrade:
      return "Bank trade: give " + std::to_string(act.c) + " " +
             resource_name(static_cast<Resource>(act.a)) + " for 1 " +
             resource_name(static_cast<Resource>(act.b));
  }
  return action_to_string(act);
}

namespace {

bool afford(const PlayerState& p, int b, int l, int o, int g, int w) {
  return can_afford(p, b, l, o, g, w);
}

bool distance_ok(const GameState& s, const Topology& topo, int v) {
  if (s.building[v] != 0) return false;
  uint64_t occupied = 0;
  for (int u = 0; u < kNumVertices; ++u) {
    if (s.building[u] != 0) occupied |= (1ULL << u);
  }
  return (occupied & (topo.dist2_block[v] & ~(1ULL << v))) == 0;
}

bool road_opens_settle(const RuleCtx& ctx, const GameState& s, int player, int edge) {
  if (edge < 0) return false;
  int v0 = ctx.topo->edge_vertices[edge][0];
  int v1 = ctx.topo->edge_vertices[edge][1];
  for (int v : {v0, v1}) {
    if (!distance_ok(s, *ctx.topo, v)) continue;
    bool already = false;
    for (int i = 0; i < ctx.topo->vertex_edge_count[v]; ++i) {
      if (s.road[ctx.topo->vertex_edges[v][i]] == player + 1) already = true;
    }
    if (!already) return true;
  }
  return false;
}

}

std::string explain_why(const RuleCtx& ctx, const GameState& s, const Action& act) {
  const int me = s.current;
  const auto& hand = s.players[me];
  switch (act.type) {
    case ActionType::Roll:
      return "nothing happens until the dice talk";
    case ActionType::EndTurn:
      return "hold the line — no clean build this beat";
    case ActionType::Discard:
      return "seven hurts; cut the dead weight";
    case ActionType::PlaceRobber: {
      if (robber_hits_self(ctx, s, me, act.a))
        return "blocks your own hex — never do this";
      int best_owner = -1, best_vp = -1;
      for (int c = 0; c < 6; ++c) {
        int v = ctx.topo->hex_vertices[act.a][c];
        uint8_t b = s.building[v];
        if (!b) continue;
        int owner = (b <= 4) ? (b - 1) : (b - 5);
        if (owner == me) continue;
        int ovp = total_vp(s, *ctx.topo, owner);
        if (ovp > best_vp) {
          best_vp = ovp;
          best_owner = owner;
        }
      }
      if (best_owner >= 0)
        return std::string("starves ") + player_name(static_cast<Player>(best_owner)) +
               " on a hot number";
      return "weak park — no enemy buildings on that hex";
    }
    case ActionType::BuildRoad: {
      int len = longest_road_len(s, *ctx.topo, me);
      if (road_opens_settle(ctx, s, me, act.a))
        return "pushes toward a future settlement spot";
      if (len >= 4 && (s.longest_road != me))
        return "sniffs at Longest Road (+2 VP)";
      if (len >= 2) return "keeps the network growing before it gets boxed in";
      return "plants a foothold for later expansion";
    }
    case ActionType::BuildSettlement:
      return "locks in a clean +1 victory point";
    case ActionType::BuildCity:
      return "city upgrade — +1 VP and double the harvest";
    case ActionType::BuyDev:
      return "a dig into the deck (knight, progress, or hidden VP)";
    case ActionType::PlayKnight: {
      if (robber_hits_self(ctx, s, me, act.a))
        return "knight on your own hex — never";
      int best_owner = -1, best_vp = -1;
      for (int c = 0; c < 6; ++c) {
        int v = ctx.topo->hex_vertices[act.a][c];
        uint8_t b = s.building[v];
        if (!b) continue;
        int owner = (b <= 4) ? (b - 1) : (b - 5);
        if (owner == me) continue;
        int ovp = total_vp(s, *ctx.topo, owner);
        if (ovp > best_vp) {
          best_vp = ovp;
          best_owner = owner;
        }
      }
      if (s.players[me].knights_played >= 2)
        return "knight pressure toward Largest Army";
      if (best_owner >= 0)
        return std::string("knight onto ") + player_name(static_cast<Player>(best_owner)) +
               "'s best production";
      return "shove the robber and swipe a card";
    }
    case ActionType::PlayMonopoly:
      return "yoink every last " + std::string(resource_name(static_cast<Resource>(act.a)));
    case ActionType::PlayYearOfPlenty:
      return "pull exactly the missing pieces from the bank";
    case ActionType::PlayRoadBuilding:
      return "two free roads — expand without paying brick/wood";
    case ActionType::MaritimeTrade: {
      PlayerState nxt = hand;
      if (nxt.res[act.a] >= act.c) {
        nxt.res[act.a] = static_cast<uint8_t>(nxt.res[act.a] - act.c);
        nxt.res[act.b] = static_cast<uint8_t>(nxt.res[act.b] + 1);
      }
      const char* give = resource_name(static_cast<Resource>(act.a));
      const char* recv = resource_name(static_cast<Resource>(act.b));
      if (!afford(hand, 1, 1, 0, 1, 1) && afford(nxt, 1, 1, 0, 1, 1))
        return std::string("clear plan: ") + give + " → " + recv + " completes a settlement";
      if (!afford(hand, 0, 0, 3, 2, 0) && afford(nxt, 0, 0, 3, 2, 0))
        return std::string("clear plan: ") + give + " → " + recv + " completes a city";
      if (!afford(hand, 1, 1, 0, 0, 0) && afford(nxt, 1, 1, 0, 0, 0))
        return std::string("clear plan: ") + give + " → " + recv + " completes a road";
      if (!afford(hand, 0, 0, 1, 1, 1) && afford(nxt, 0, 0, 1, 1, 1))
        return std::string("clear plan: ") + give + " → " + recv + " buys a development card";
      return std::string("no clear build unlocked — better to pass than bank ") + give;
    }
  }
  return "keeps the race to 10 VP moving";
}

std::string state_to_json(const RuleCtx& ctx, const GameState& s, int you) {
  constexpr double kSize = 42.0;
  std::ostringstream o;
  o << std::boolalpha;
  o << "{";
  o << "\"you\":" << you << ",";
  o << "\"current\":" << int(s.current) << ",";
  o << "\"phase\":\"" << phase_name(s.phase) << "\",";
  o << "\"last_roll\":" << int(s.last_roll) << ",";
  o << "\"robber\":" << int(s.robber) << ",";
  o << "\"game_over\":" << s.game_over << ",";
  o << "\"winner\":" << int(s.winner) << ",";
  o << "\"longest_road\":" << int(s.longest_road) << ",";
  o << "\"largest_army\":" << int(s.largest_army) << ",";

  o << "\"hexes\":[";
  for (int h = 0; h < kNumHexes; ++h) {
    if (h) o << ",";
    double x, y;
    hex_pixel(ctx.topo->hex_cube[h], kSize, x, y);
    o << "{"
      << "\"id\":" << h << ","
      << "\"terrain\":\"" << terrain_name(ctx.board->hexes[h].terrain) << "\","
      << "\"number\":" << int(ctx.board->hexes[h].number) << ","
      << "\"q\":" << ctx.topo->hex_cube[h].x << ","
      << "\"r\":" << ctx.topo->hex_cube[h].z << ","
      << "\"x\":" << x << ","
      << "\"y\":" << y
      << "}";
  }
  o << "],";

  o << "\"vertices\":[";
  std::array<double, kNumVertices> vx{}, vy{};
  for (int v = 0; v < kNumVertices; ++v) {
    vertex_pixel(*ctx.topo, v, kSize, vx[v], vy[v]);
    if (v) o << ",";
    uint8_t b = s.building[v];
    int owner = -1;
    bool city = false;
    if (b >= 1 && b <= 4) owner = b - 1;
    if (b >= 5 && b <= 8) {
      owner = b - 5;
      city = true;
    }
    o << "{"
      << "\"id\":" << v << ","
      << "\"x\":" << vx[v] << ","
      << "\"y\":" << vy[v] << ","
      << "\"owner\":" << owner << ","
      << "\"city\":" << city << ","
      << "\"port\":\"" << port_type_name(ctx.board->port_at[v]) << "\""
      << "}";
  }
  o << "],";

  o << "\"ports\":[";
  {
    bool first = true;
    std::array<uint8_t, kNumEdges> seen{};
    for (int e = 0; e < kNumEdges; ++e) {
      if (ctx.topo->edge_hex_count[e] != 1) continue;
      int v0 = ctx.topo->edge_vertices[e][0];
      int v1 = ctx.topo->edge_vertices[e][1];
      PortType pt = ctx.board->port_at[v0];
      if (pt == PortType::None || ctx.board->port_at[v1] != pt) continue;
      if (seen[e]) continue;
      seen[e] = 1;
      if (!first) o << ",";
      first = false;
      int rate = port_rate(pt);
      Resource pr = port_resource(pt);
      o << "{"
        << "\"edge\":" << e << ","
        << "\"v0\":" << v0 << ",\"v1\":" << v1 << ","
        << "\"x0\":" << vx[v0] << ",\"y0\":" << vy[v0] << ","
        << "\"x1\":" << vx[v1] << ",\"y1\":" << vy[v1] << ","
        << "\"type\":\"" << port_type_name(pt) << "\","
        << "\"rate\":" << rate << ","
        << "\"resource\":\"" << (pr == Resource::None ? "?" : resource_name(pr)) << "\""
        << "}";
    }
  }
  o << "],";

  o << "\"edges\":[";
  for (int e = 0; e < kNumEdges; ++e) {
    if (e) o << ",";
    int v0 = ctx.topo->edge_vertices[e][0];
    int v1 = ctx.topo->edge_vertices[e][1];
    int owner = s.road[e] ? int(s.road[e]) - 1 : -1;
    o << "{"
      << "\"id\":" << e << ","
      << "\"x0\":" << vx[v0] << ",\"y0\":" << vy[v0] << ","
      << "\"x1\":" << vx[v1] << ",\"y1\":" << vy[v1] << ","
      << "\"owner\":" << owner
      << "}";
  }
  o << "],";

  o << "\"players\":[";
  for (int p = 0; p < kNumPlayers; ++p) {
    if (p) o << ",";
    const auto& pl = s.players[p];
    int building_vp = 0;
    for (int v = 0; v < kNumVertices; ++v) {
      uint8_t b = s.building[v];
      if (b == p + 1) building_vp += 1;
      if (b == p + 5) building_vp += 2;
    }
    const int award_vp = (s.longest_road == p ? 2 : 0) + (s.largest_army == p ? 2 : 0);
    o << "{"
      << "\"id\":" << p << ","
      << "\"name\":\"" << player_name(static_cast<Player>(p)) << "\","
      << "\"vp\":" << total_vp(s, *ctx.topo, p) << ","
      << "\"building_vp\":" << building_vp << ","
      << "\"award_vp\":" << award_vp << ","
      << "\"hand_size\":" << hand_size(pl) << ","
      << "\"knights\":" << int(pl.knights_played) << ","
      << "\"vp_cards_count\":" << (p == you ? int(pl.vp_cards) : 0) << ","
      << "\"longest_road\":" << (s.longest_road == p ? "true" : "false") << ","
      << "\"largest_army\":" << (s.largest_army == p ? "true" : "false");
    if (p == you) {
      o << ",\"res\":[" << int(pl.res[0]) << "," << int(pl.res[1]) << "," << int(pl.res[2])
        << "," << int(pl.res[3]) << "," << int(pl.res[4]) << "],"
        << "\"devs\":{"
        << "\"knight\":" << int(pl.devs[0]) << ","
        << "\"vp\":" << int(pl.vp_cards) << ","
        << "\"monopoly\":" << int(pl.devs[2]) << ","
        << "\"year_of_plenty\":" << int(pl.devs[3]) << ","
        << "\"road_building\":" << int(pl.devs[4])
        << "},"
        << "\"new_dev\":" << int(pl.new_dev);
    } else {
      o << ",\"res\":null,\"devs\":null";
    }
    o << "}";
  }
  o << "]";
  o << "}";
  (void)esc;
  return o.str();
}

}
