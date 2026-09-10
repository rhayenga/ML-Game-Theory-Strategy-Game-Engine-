#pragma once

#include <cstdint>
#include <string_view>

namespace catan {

inline constexpr int kNumHexes = 19;
inline constexpr int kNumVertices = 54;
inline constexpr int kNumEdges = 72;
inline constexpr int kNumPlayers = 4;
inline constexpr int kWinVp = 10;
inline constexpr int kMaxHandSafe = 7;

enum class Resource : uint8_t { Brick = 0, Lumber, Ore, Grain, Wool, Desert = 5, None = 6 };
enum class Terrain : uint8_t { Hills, Forest, Mountains, Fields, Pasture, Desert };

enum class Player : uint8_t { Red = 0, White, Orange, Blue };

enum class DevType : uint8_t {
  Knight = 0,
  VictoryPoint,
  Monopoly,
  YearOfPlenty,
  RoadBuilding,
  Count
};

enum class Phase : uint8_t {
  PreRoll,       // may play one development card, then must roll
  Discard,       // after 7, players with >7 resource cards
  RobberMove,    // place robber + steal
  Main,          // trade / build / play one development card (if not already)
  GameOver
};

inline constexpr int kPips[13] = {0, 0, 1, 2, 3, 4, 5, 6, 5, 4, 3, 2, 1};

inline Terrain terrain_of(Resource r) {
  switch (r) {
    case Resource::Brick: return Terrain::Hills;
    case Resource::Lumber: return Terrain::Forest;
    case Resource::Ore: return Terrain::Mountains;
    case Resource::Grain: return Terrain::Fields;
    case Resource::Wool: return Terrain::Pasture;
    default: return Terrain::Desert;
  }
}

inline Resource resource_of(Terrain t) {
  switch (t) {
    case Terrain::Hills: return Resource::Brick;
    case Terrain::Forest: return Resource::Lumber;
    case Terrain::Mountains: return Resource::Ore;
    case Terrain::Fields: return Resource::Grain;
    case Terrain::Pasture: return Resource::Wool;
    default: return Resource::None;
  }
}

inline const char* resource_name(Resource r) {
  switch (r) {
    case Resource::Brick: return "brick";
    case Resource::Lumber: return "lumber";
    case Resource::Ore: return "ore";
    case Resource::Grain: return "grain";
    case Resource::Wool: return "wool";
    default: return "none";
  }
}

inline const char* player_name(Player p) {
  switch (p) {
    case Player::Red: return "Red";
    case Player::White: return "White";
    case Player::Orange: return "Orange";
    case Player::Blue: return "Blue";
    default: return "?";
  }
}

}  // namespace catan
