#include "catan/train.hpp"

#include "catan/state.hpp"
#include "catan/strategy.hpp"
#include "catan/visits.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <random>
#include <unordered_set>

namespace catan {
namespace {

uint32_t xorshift(uint32_t& x) {
  x ^= x << 13;
  x ^= x >> 17;
  x ^= x << 5;
  if (!x) x = 0xA5A5A5A5u;
  return x;
}

Action pick_action(const RuleCtx& ctx, const GameState& s, const TrainConfig& cfg, uint32_t& rng,
                   const std::array<StrategyStyle, kNumPlayers>& personas) {
  auto acts = legal_actions(ctx, s);
  if (acts.empty()) return Action{ActionType::EndTurn, 0, 0, 0, {}};

  if (acts.size() == 1) return acts[0];

  if ((xorshift(rng) % 1000) < static_cast<uint32_t>(cfg.epsilon * 1000)) {
    return acts[xorshift(rng) % acts.size()];
  }

  if (cfg.mcts_sims > 0) {
    MCTSConfig mc;
    mc.simulations = cfg.mcts_sims;
    mc.rollout_depth = 20;
    auto res = search_best_action(ctx, s, s.current, mc);
    return res.best;
  }

  int me = s.current;
  int vp0 = total_vp(s, *ctx.topo, me);
  StrategyStyle style = personas[me];
  Action best = acts[0];
  double best_v = -1e99;
  for (const auto& a : acts) {
    GameState n = s;
    apply_action(ctx, n, a);
    double v = evaluate(ctx, n, me);
    int vp1 = total_vp(n, *ctx.topo, me);
    v += 0.65 * (vp1 - vp0);
    if (n.game_over && n.winner == me) v += 5.0;
    v += strategy_action_bonus(ctx, s, a, me, style);
    if (v > best_v) {
      best_v = v;
      best = a;
    }
  }
  return best;
}

void update_weights_from_game(const RuleCtx& ctx, const GameState& final_state, int winner,
                              double lr) {
  if (winner < 0) return;
  auto fw = player_features(ctx, final_state, winner);
  std::array<double, kEvalDim> favg{};
  int n = 0;
  for (int p = 0; p < kNumPlayers; ++p) {
    if (p == winner) continue;
    auto f = player_features(ctx, final_state, p);
    for (int i = 0; i < kEvalDim; ++i) favg[i] += f[i];
    ++n;
  }
  if (n == 0) return;
  for (int i = 0; i < kEvalDim; ++i) favg[i] /= n;

  auto& W = active_weights();
  for (int i = 0; i < kEvalDim; ++i) {
    W.w[i] += lr * (fw[i] - favg[i]);
    // Keep weights in a sane band
    W.w[i] = std::clamp(W.w[i], -2.0, 12.0);
  }
  // Normalize L2 softly toward defaults magnitude
  double norm = 0;
  for (double x : W.w) norm += x * x;
  norm = std::sqrt(norm);
  if (norm > 20.0) {
    for (double& x : W.w) x *= 20.0 / norm;
  }
}

}  // namespace

int play_one_game(const RuleCtx& ctx, GameState state, const TrainConfig& cfg, TrainStats& stats,
                  uint32_t& rng, VisitStore* visits) {
  // Rotate community strategies across seats so all colors train against OWS / wood-brick / blend.
  std::array<StrategyStyle, kNumPlayers> personas{
      StrategyStyle::OwsCities,
      StrategyStyle::WoodBrickRoad,
      StrategyStyle::Balanced,
      StrategyStyle::OwsCities,
  };
  int rot = static_cast<int>(xorshift(rng) % 3);
  for (int i = 0; i < kNumPlayers; ++i) {
    personas[i] = static_cast<StrategyStyle>((static_cast<int>(personas[i]) + rot) % 3);
  }

  std::unordered_set<uint64_t> seen;
  auto note = [&](const GameState& st) {
    if (!visits) return;
    uint64_t h = hash_position(st, *ctx.board);
    if (seen.insert(h).second) visits->touch(h).game_seen += 1;
  };
  note(state);

  int decisions = 0;
  while (!state.game_over && decisions < cfg.max_decisions) {
    auto acts = legal_actions(ctx, state);
    if (acts.empty()) break;
    Action a = pick_action(ctx, state, cfg, rng, personas);
    int ti = static_cast<int>(a.type);
    if (ti >= 0 && ti < 16) stats.action_counts[ti]++;
    stats.total_actions++;
    apply_action(ctx, state, a);
    note(state);
    ++decisions;
  }
  stats.max_decisions = std::max(stats.max_decisions, decisions);
  if (state.game_over && state.winner >= 0) {
    stats.finished++;
    stats.wins[state.winner]++;
    for (int p = 0; p < kNumPlayers; ++p) {
      stats.sum_vp[p] += total_vp(state, *ctx.topo, p);
    }
    update_weights_from_game(ctx, state, state.winner, cfg.learn_rate);
    return state.winner;
  }
  // Unfinished: still record VP
  for (int p = 0; p < kNumPlayers; ++p) {
    stats.sum_vp[p] += total_vp(state, *ctx.topo, p);
  }
  return -1;
}

TrainStats run_training(const RuleCtx& ctx, const BoardSpec& board, const Topology& topo,
                        TrainConfig cfg) {
  (void)ctx;
  (void)board;
  TrainStats stats{};
  uint32_t rng = cfg.seed ? cfg.seed : 0xCA7A12u;

  // Load existing weights if present
  EvalWeights w = active_weights();
  if (load_weights(w, cfg.weights_path)) {
    set_active_weights(w);
    std::cout << "Loaded weights from " << cfg.weights_path << "\n";
  } else {
    set_active_weights(EvalWeights::defaults());
    std::cout << "Starting from default weights\n";
  }

  VisitStore visits;
  visits.load(cfg.visits_path);
  std::cout << "Position memory: " << visits.by_hash.size() << " known positions ("
            << cfg.visits_path << ")\n";

  for (int g = 0; g < cfg.games; ++g) {
    stats.games++;
    // Unique board + placements every game so training explores different positions.
    uint32_t board_rng = rng ^ (0x9E3779B9u * static_cast<uint32_t>(g + 1));
    BoardSpec board = random_board(topo, board_rng);
    RuleCtx game_ctx{&topo, &board};
    GameState state = make_initial_state(topo, board, board_rng);
    state.rng = rng;
    play_one_game(game_ctx, state, cfg, stats, rng, &visits);

    if ((g + 1) % std::max(1, cfg.games / 10) == 0 || g + 1 == cfg.games) {
      std::cout << "  game " << (g + 1) << "/" << cfg.games << " finished=" << stats.finished
                << " wins=[";
      for (int p = 0; p < kNumPlayers; ++p) {
        if (p) std::cout << ",";
        std::cout << stats.wins[p];
      }
      std::cout << "]\n";
      visits.save(cfg.visits_path);
    }
  }

  visits.save(cfg.visits_path);
  std::cout << "Wrote position visits -> " << cfg.visits_path << " (" << visits.by_hash.size()
            << " positions)\n";

  save_weights(active_weights(), cfg.weights_path);
  std::cout << "Wrote weights -> " << cfg.weights_path << "\n";

  // Stats JSON
  std::ofstream out(cfg.stats_path);
  if (out) {
    out << "{\n  \"games\": " << stats.games << ",\n  \"finished\": " << stats.finished << ",\n";
    out << "  \"wins\": [";
    for (int p = 0; p < kNumPlayers; ++p) {
      if (p) out << ", ";
      out << stats.wins[p];
    }
    out << "],\n  \"avg_vp\": [";
    for (int p = 0; p < kNumPlayers; ++p) {
      if (p) out << ", ";
      double avg = stats.games ? stats.sum_vp[p] / stats.games : 0;
      out << avg;
    }
    out << "],\n  \"total_actions\": " << stats.total_actions << ",\n";
    out << "  \"action_counts\": [";
    for (int i = 0; i < 16; ++i) {
      if (i) out << ", ";
      out << stats.action_counts[i];
    }
    out << "],\n  \"weights\": [";
    for (int i = 0; i < kEvalDim; ++i) {
      if (i) out << ", ";
      out << active_weights().w[i];
    }
    out << "]\n}\n";
    std::cout << "Wrote stats -> " << cfg.stats_path << "\n";
  }

  return stats;
}

}  // namespace catan
