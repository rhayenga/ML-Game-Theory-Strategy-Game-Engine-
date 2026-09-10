// Heuristic evaluation and weight load/save.

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

}

EvalWeights& active_weights() { return g_weights; }

void set_active_weights(const EvalWeights& w) { g_weights = w; }

void sanitize_race_weights(EvalWeights& W) {
  for (double& x : W.w) x = std::clamp(x, 0.0, 12.0);
  W.w[0] = std::clamp(W.w[0], 4.5, 12.0);
  W.w[1] = std::clamp(W.w[1], 1.6, W.w[0] * 0.95);
  W.w[2] = std::clamp(W.w[2], 0.25, W.w[0] * 0.35);
  W.w[3] = std::clamp(W.w[3], 0.35, W.w[0] * 0.45);
  W.w[4] = std::clamp(W.w[4], 0.75, W.w[0] * 0.4);
  W.w[5] = std::clamp(W.w[5], 0.4, 2.5);
  if (W.scale < 2.0) W.scale = 2.0;
  if (W.scale > 12.0) W.scale = 12.0;
}

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
  sanitize_race_weights(w);
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
    if (ri == 0 || ri == 1) need += 1.6;
  }
  if (p.res[0] < 1 || p.res[1] < 1 || p.res[3] < 1 || p.res[4] < 1) {
    if (ri == 0 || ri == 1) need += 1.2;
    if (ri == 3 || ri == 4) need += 1.3;
  }
  if (p.res[2] < 3 || p.res[3] < 2) {
    if (ri == 2) need += 2.0;
    if (ri == 3) need += 1.1;
  }
  if (p.res[2] < 1 || p.res[3] < 1 || p.res[4] < 1) {
    if (ri >= 2) need += 0.9;
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
    if (s.longest_road == p) {
      int rival = 0;
      for (int o = 0; o < kNumPlayers; ++o) {
        if (o == p) continue;
        rival = std::max(rival, longest_road_len(s, *ctx.topo, o));
      }
      if (f[4] - rival >= 2.0) f[4] = rival + 1.0;
    }
    if (s.largest_army == p) {
      int rival = 0;
      for (int o = 0; o < kNumPlayers; ++o) {
        if (o == p) continue;
        rival = std::max(rival, static_cast<int>(s.players[o].knights_played));
      }
      if (f[3] - rival >= 2.0) f[3] = rival + 1.0;
    }
    double acc = 0;
    for (int i = 0; i < kEvalDim; ++i) acc += W.w[i] * f[i];
    acc += 0.7 * f[0] * f[0];
    if (vps[p] >= 7) acc += 2.5 * (vps[p] - 6);
    if (vps[p] >= 9) acc += 5.0;

    if (s.longest_road == p) {
      acc += 2.2;
    } else {
      double road = f[4];
      if (road >= 3) acc += 0.2 * (road - 2);
      if (road >= 5) acc += 0.7;
      if (s.longest_road >= 0) {
        double theirs = longest_road_len(s, *ctx.topo, s.longest_road);
        double deficit = theirs - road;
        if (road >= 4 && deficit <= 2) acc += 1.0 + 0.35 * (2.0 - std::max(0.0, deficit));
      }
    }
    if (s.largest_army == p) {
      acc += 2.2;
    } else {
      double army = f[3];
      if (army >= 1) acc += 0.15 * army;
      if (army >= 3) acc += 0.7;
      if (s.largest_army >= 0) {
        double theirs = s.players[s.largest_army].knights_played;
        double deficit = theirs - army;
        if (army >= 2 && deficit <= 2) acc += 0.95 + 0.35 * (2.0 - std::max(0.0, deficit));
      }
    }

    score[p] = acc;
  }

  double my = score[perspective];
  double best_other = -1e9;
  int best_other_vp = 0;
  double worst_pair = 1.0;
  double sc = W.scale > 1e-6 ? W.scale : 5.0;
  for (int p = 0; p < kNumPlayers; ++p) {
    if (p == perspective) continue;
    if (score[p] > best_other) {
      best_other = score[p];
      best_other_vp = vps[p];
    }
    double pair = std::tanh((score[perspective] - score[p]) / sc);
    worst_pair = std::min(worst_pair, pair);
    if (vps[p] >= 8) my -= 0.8 * (vps[p] - 7);
    if (vps[p] >= 9 && s.current == p) my -= 1.5;
  }
  if (best_other_vp >= 7) my -= 1.2 * (best_other_vp - 6);

  double vs_leader = std::tanh((my - best_other) / sc);
  return 0.6 * vs_leader + 0.4 * worst_pair;
}

}
