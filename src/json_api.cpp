#include "catan/json_api.hpp"

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

const char* phase_name(Phase p) {
  switch (p) {
    case Phase::PreRoll: return "pre_roll";
    case Phase::Discard: return "discard";
    case Phase::RobberMove: return "robber";
    case Phase::Main: return "main";
    default: return "game_over";
  }
}

// Axial (q,r) = (cube.x, cube.z) → pixel (pointy-top).
void hex_pixel(Cube c, double size, double& x, double& y) {
  double q = c.x;
  double r = c.z;
  x = size * (1.5 * q);
  y = size * (std::sqrt(3.0) * (r + q / 2.0));
}

// Must match topology.cpp neighbor order (clockwise).
constexpr Cube kDir[6] = {
    {1, -1, 0}, {1, 0, -1}, {0, 1, -1}, {-1, 1, 0}, {-1, 0, 1}, {0, -1, 1},
};

Cube cube_add(Cube a, Cube b) { return Cube{a.x + b.x, a.y + b.y, a.z + b.z}; }

// True intersection: centroid of the three hex centers that meet at this corner
// (sea hexes included as virtual neighbors so coastal corners aren't hex centers).
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

}  // namespace

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
      return "Build a road (edge " + std::to_string(act.a) + ")";
    case ActionType::BuildSettlement:
      return "Build a settlement (intersection " + std::to_string(act.a) + ")";
    case ActionType::BuildCity:
      return "Upgrade to a city (intersection " + std::to_string(act.a) + ")";
    case ActionType::BuyDev: return "Buy a development card";
    case ActionType::PlayKnight:
      return "Play Knight — robber to hex " + std::to_string(act.a);
    case ActionType::PlayMonopoly:
      return std::string("Play Monopoly on ") + resource_name(static_cast<Resource>(act.a));
    case ActionType::PlayYearOfPlenty:
      return std::string("Year of Plenty: take ") + resource_name(static_cast<Resource>(act.a)) +
             " and " + resource_name(static_cast<Resource>(act.b));
    case ActionType::PlayRoadBuilding:
      return "Play Road Building";
    case ActionType::MaritimeTrade:
      return "Bank trade: give " + std::to_string(act.c) + " " +
             resource_name(static_cast<Resource>(act.a)) + " for 1 " +
             resource_name(static_cast<Resource>(act.b));
  }
  return action_to_string(act);
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

  // Hexes
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

  // Vertices — positions are hex *corners* (intersections), never tile centers.
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
      << "\"city\":" << city
      << "}";
  }
  o << "],";

  // Edges / roads connect intersection coordinates.
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

  // Players
  o << "\"players\":[";
  for (int p = 0; p < kNumPlayers; ++p) {
    if (p) o << ",";
    o << "{"
      << "\"id\":" << p << ","
      << "\"name\":\"" << player_name(static_cast<Player>(p)) << "\","
      << "\"vp\":" << total_vp(s, *ctx.topo, p) << ","
      << "\"hand_size\":" << hand_size(s.players[p]) << ","
      << "\"res\":[" << int(s.players[p].res[0]) << "," << int(s.players[p].res[1]) << ","
      << int(s.players[p].res[2]) << "," << int(s.players[p].res[3]) << ","
      << int(s.players[p].res[4]) << "],"
      << "\"knights\":" << int(s.players[p].knights_played)
      << "}";
  }
  o << "]";
  o << "}";
  (void)esc;
  return o.str();
}

}  // namespace catan
