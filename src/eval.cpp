#include "catan/eval.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <sstream>

namespace catan {
namespace {

EvalWeights g_weights = EvalWeights::defaults();

double pip_income(const RuleCtx& ctx, const GameState& s, int player, int resource) {
  double acc = 0;
  for (int h = 0; h < kNumHexes; ++h) {
    if (h == s.robber) continue;
    if (resource_of(ctx.board->hexes[h].terrain) != static_cast<Resource>(resource)) continue;
    int n = ctx.board->hexes[h].number;
    if (n < 2 || n > 12) continue;
    double pips = kPips[n];
    for (int c = 0; c < 6; ++c) {
      int v = ctx.topo->hex_vertices[h][c];
      uint8_t b = s.building[v];
      if (b == player + 1) acc += pips;
      else if (b == player + 5) acc += 2 * pips;
    }
  }
  return acc / 36.0;
}

}  // namespace

EvalWeights& active_weights() { return g_weights; }

void set_active_weights(const EvalWeights& w) { g_weights = w; }

bool save_weights(const EvalWeights& w, const std::string& path) {
  std::ofstream out(path);
  if (!out) return false;
  out << "{\n";
  out << "  \"w\": [";
  for (int i = 0; i < kEvalDim; ++i) {
    if (i) out << ", ";
    out << w.w[i];
  }
  out << "],\n";
  out << "  \"scale\": " << w.scale << "\n";
  out << "}\n";
  return true;
}

bool load_weights(EvalWeights& w, const std::string& path) {
  std::ifstream in(path);
  if (!in) return false;
  std::stringstream buf;
  buf << in.rdbuf();
  std::string s = buf.str();
  // Minimal JSON parse: find "w": [ ... ] and "scale":
  auto parse_array = [&](const char* key, std::array<double, kEvalDim>& out_arr) -> bool {
    auto pos = s.find(key);
    if (pos == std::string::npos) return false;
    pos = s.find('[', pos);
    if (pos == std::string::npos) return false;
    ++pos;
    for (int i = 0; i < kEvalDim; ++i) {
      while (pos < s.size() && (s[pos] == ' ' || s[pos] == ',' || s[pos] == '\n')) ++pos;
      char* end = nullptr;
      out_arr[i] = std::strtod(s.c_str() + pos, &end);
      if (end == s.c_str() + pos) return false;
      pos = static_cast<size_t>(end - s.c_str());
    }
    return true;
  };
  EvalWeights nw = EvalWeights::defaults();
  if (!parse_array("\"w\"", nw.w)) return false;
  auto sp = s.find("\"scale\"");
  if (sp != std::string::npos) {
    sp = s.find(':', sp);
    if (sp != std::string::npos) nw.scale = std::strtod(s.c_str() + sp + 1, nullptr);
  }
  w = nw;
  return true;
}

std::array<double, kEvalDim> player_features(const RuleCtx& ctx, const GameState& s, int player) {
  double vp = total_vp(s, *ctx.topo, player);
  double income = 0;
  for (int r = 0; r < 5; ++r) income += pip_income(ctx, s, player, r);
  double diversity = 0;
  for (int r = 0; r < 5; ++r) {
    if (pip_income(ctx, s, player, r) > 1e-9) diversity += 1;
  }
  double army = s.players[player].knights_played;
  double road = longest_road_len(s, *ctx.topo, player);
  double hand = hand_size(s.players[player]);
  double seven = (hand > 7) ? -0.5 * (hand - 7) : 0.0;
  return {vp, income, diversity, army, road, seven};
}

double resource_utility(const RuleCtx& ctx, const GameState& s, int player, Resource r) {
  int ri = static_cast<int>(r);
  const auto& p = s.players[player];
  double need = 0;
  if (p.res[0] < 1 || p.res[1] < 1) {
    if (ri == 0 || ri == 1) need += 1.5;
  }
  if (p.res[3] < 1 || p.res[4] < 1) {
    if (ri == 3 || ri == 4) need += 1.2;
  }
  if (p.res[2] < 3 || p.res[3] < 2) {
    if (ri == 2) need += 2.0;
    if (ri == 3) need += 1.0;
  }
  if (p.res[2] < 1 || p.res[3] < 1 || p.res[4] < 1) {
    if (ri >= 2) need += 0.8;
  }

  double scarcity = 1.0 / (1.0 + pip_income(ctx, s, player, ri));
  int hs = hand_size(p);
  double seven_risk = (hs >= 7) ? 1.5 : (hs >= 5 ? 0.4 : 0.0);
  double vp_gap = std::max(0, kWinVp - total_vp(s, *ctx.topo, player));
  return need * scarcity * (1.0 + 0.15 * vp_gap) - 0.25 * seven_risk;
}

double evaluate(const RuleCtx& ctx, const GameState& s, int perspective) {
  if (s.game_over) {
    if (s.winner == perspective) return 1.0;
    return -1.0;
  }

  const auto& W = active_weights();
  std::array<double, kNumPlayers> score{};
  std::array<int, kNumPlayers> vps{};
  for (int p = 0; p < kNumPlayers; ++p) {
    auto f = player_features(ctx, s, p);
    vps[p] = static_cast<int>(f[0] + 1e-9);
    double acc = 0;
    for (int i = 0; i < kEvalDim; ++i) acc += W.w[i] * f[i];
    // Win-as-fast-as-possible: quadratic VP race + late-game urgency.
    acc += 0.35 * f[0] * f[0];
    if (vps[p] >= 7) acc += 2.0 * (vps[p] - 6);
    if (vps[p] >= 9) acc += 4.0;
    score[p] = acc;
  }

  double my = score[perspective];
  double best_other = -1e9;
  int best_other_vp = 0;
  for (int p = 0; p < kNumPlayers; ++p) {
    if (p == perspective) continue;
    if (score[p] > best_other) {
      best_other = score[p];
      best_other_vp = vps[p];
    }
  }
  // Deny leaders: if someone is racing ahead, punish falling behind harder.
  if (best_other_vp >= 7) my -= 1.2 * (best_other_vp - 6);
  double diff = my - best_other;
  double sc = W.scale > 1e-6 ? W.scale : 5.0;
  return std::tanh(diff / sc);
}

}  // namespace catan
