// Self-play training and weight updates.

#include "catan/train.hpp"

#include "catan/state.hpp"
#include "catan/strategy.hpp"
#include "catan/visits.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <unordered_set>
#include <utility>
#include <vector>

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
                   const std::array<StrategyStyle, kNumPlayers>& personas, VisitStore* visits) {
  auto acts = legal_actions(ctx, s);
  if (acts.empty()) return Action{ActionType::EndTurn, 0, 0, 0, {}};

  if (acts.size() == 1) return acts[0];

  bool road_available = false, settle_available = false, city_available = false;
  for (const auto& a : acts) {
    if (a.type == ActionType::BuildRoad && a.a >= 0) road_available = true;
    if (a.type == ActionType::BuildSettlement) settle_available = true;
    if (a.type == ActionType::BuildCity) city_available = true;
  }
  const bool any_build = road_available || settle_available || city_available;

  if ((xorshift(rng) % 1000) < static_cast<uint32_t>(cfg.epsilon * 1000)) {
    if (any_build && (xorshift(rng) % 100) < 75) {
      std::vector<Action> builds;
      for (const auto& a : acts) {
        if (a.type == ActionType::BuildSettlement || a.type == ActionType::BuildCity ||
            (a.type == ActionType::BuildRoad && a.a >= 0)) {
          builds.push_back(a);
        }
      }
      if (!builds.empty()) return builds[xorshift(rng) % builds.size()];
    }
    return acts[xorshift(rng) % acts.size()];
  }

  if (cfg.mcts_sims > 0) {
    MCTSConfig mc;
    mc.simulations = cfg.mcts_sims;
    mc.rollout_depth = 20;
    mc.visits = visits;
    auto res = search_best_action(ctx, s, s.current, mc);
    return res.best;
  }

  int me = s.current;
  int vp0 = total_vp(s, *ctx.topo, me);
  StrategyStyle style = personas[me];
  uint64_t h = hash_position(s, *ctx.board);
  Action best = acts[0];
  double best_v = -1e99;
  for (const auto& a : acts) {
    GameState n = s;
    apply_action(ctx, n, a);
    double v = evaluate(ctx, n, me);
    int vp1 = total_vp(n, *ctx.topo, me);
    v += 4.0 * (vp1 - vp0);
    if (n.game_over && n.winner == me) v += 10.0;
    if (n.longest_road == me && s.longest_road != me) {
      int old = s.longest_road;
      double claim = 1.0;
      if (old >= 0 &&
          longest_road_len(s, *ctx.topo, old) - longest_road_len(s, *ctx.topo, me) <= 2)
        claim = 1.75;
      v += claim;
    }
    if (n.largest_army == me && s.largest_army != me) {
      int old = s.largest_army;
      double claim = 1.0;
      if (old >= 0 && s.players[old].knights_played - s.players[me].knights_played <= 2)
        claim = 1.75;
      v += claim;
    }
    if (a.type == ActionType::MaritimeTrade) {
      if (maritime_trade_score(s, me, a.a, a.b, a.c) < 0) v -= 2.0;
    }
    v += strategy_action_bonus(ctx, s, a, me, style);
    double pi = move_prior(visits, h, a, acts);
    double prior_w = (a.type == ActionType::BuildSettlement || a.type == ActionType::BuildCity ||
                      a.type == ActionType::BuildRoad || a.type == ActionType::PlayKnight)
                         ? 0.12
                         : 0.3;
    v += prior_w * std::log1p(pi * 50.0);
    if (a.type == ActionType::PlaceRobber || a.type == ActionType::PlayKnight) {
      if (robber_hits_self(ctx, s, me, a.a))
        v -= 50.0;
      else {
        v += 0.5 * std::max(0.0, robber_hex_score(ctx, s, me, a.a));
        if (a.b >= 0 && a.b < kNumPlayers) v += 0.4 * total_vp(s, *ctx.topo, a.b);
      }
    }
    if (v > best_v) {
      best_v = v;
      best = a;
    }
  }
  return best;
}

void clamp_race_weights(EvalWeights& W) { sanitize_race_weights(W); }

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
  }
  clamp_race_weights(W);
}

void update_weights_from_trajectory(
    const std::vector<std::pair<int, std::array<double, kEvalDim>>>& traj, int winner, double lr) {
  if (winner < 0 || traj.empty()) return;
  auto& W = active_weights();
  double G = 1.0;
  for (int t = static_cast<int>(traj.size()) - 1; t >= 0; --t) {
    int player = traj[t].first;
    const auto& feat = traj[t].second;
    double advantage = (player == winner) ? G : -0.55 * G;
    for (int i = 0; i < kEvalDim; ++i) {
      W.w[i] += lr * 0.15 * advantage * feat[i];
    }
    G *= 0.985;
  }
  clamp_race_weights(W);
}

}

int play_one_game(const RuleCtx& ctx, GameState state, const TrainConfig& cfg, TrainStats& stats,
                  uint32_t& rng, VisitStore* visits) {
  std::array<StrategyStyle, kNumPlayers> personas{};
  for (int i = 0; i < kNumPlayers; ++i) {
    personas[i] = static_cast<StrategyStyle>(xorshift(rng) % 3);
  }
  if (personas[0] == personas[1] && personas[1] == personas[2] && personas[2] == personas[3]) {
    personas[1] = StrategyStyle::WoodBrickRoad;
    personas[2] = StrategyStyle::OwsCities;
  }

  std::unordered_set<uint64_t> seen;
  std::vector<std::pair<int, std::array<double, kEvalDim>>> traj;
  traj.reserve(256);

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
    int actor = state.current;
    if (state.phase == Phase::Discard && !acts.empty() && acts[0].type == ActionType::Discard) {
      actor = acts[0].a;
    }
    uint64_t h_before = hash_position(state, *ctx.board);
    Action a = pick_action(ctx, state, cfg, rng, personas, visits);
    if (visits) visits->record_move(h_before, a);
    int ti = static_cast<int>(a.type);
    if (ti >= 0 && ti < 16) stats.action_counts[ti]++;
    stats.total_actions++;
    apply_action(ctx, state, a);
    if (a.type != ActionType::Roll && a.type != ActionType::Discard) {
      traj.push_back({actor, player_features(ctx, state, actor)});
    }
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
    if (cfg.learn_rate > 0.0) {
      update_weights_from_trajectory(traj, state.winner, cfg.learn_rate);
      update_weights_from_game(ctx, state, state.winner, cfg.learn_rate);
    }

    if (!cfg.samples_path.empty()) {
      std::ofstream samples_out(cfg.samples_path, std::ios::app);
      if (samples_out) {
        for (const auto& step : traj) {
          samples_out << "{\"feat\":[";
          for (int i = 0; i < kEvalDim; ++i) {
            if (i) samples_out << ",";
            samples_out << step.second[i];
          }
          samples_out << "],\"y\":" << (step.first == state.winner ? 1 : 0) << "}\n";
        }
      }
    }
    return state.winner;
  }
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

  const char* seed_path = "build/board_seeds.txt";
  constexpr int kTargetPerOpening = 80;
  constexpr int kMaxPool = 36;
  int target_pool = std::max(12, cfg.games / kTargetPerOpening);
  target_pool = std::min(target_pool, kMaxPool);

  std::vector<uint32_t> loaded = load_board_seeds(seed_path);
  std::vector<uint32_t> pool;
  int keep = std::max(target_pool, std::min(kMaxPool, static_cast<int>(loaded.size())));
  for (uint32_t s : loaded) {
    if (static_cast<int>(pool.size()) >= keep) break;
    pool.push_back(s);
  }
  while (static_cast<int>(pool.size()) < target_pool) {
    uint32_t s = xorshift(rng) ^ (0x9E3779B9u * static_cast<uint32_t>(pool.size() + 1));
    if (!s) s = 0xC47A11u;
    bool dup = false;
    for (uint32_t x : pool) {
      if (x == s) {
        dup = true;
        break;
      }
    }
    if (!dup) pool.push_back(s);
  }
  write_board_seeds(seed_path, pool);
  std::cout << "Board pool: " << pool.size() << " layouts (~" << (cfg.games / pool.size())
            << " games each → opening prior should land near that)\n";

  if (!cfg.samples_path.empty()) {
    std::ofstream wipe(cfg.samples_path, std::ios::trunc);
    std::cout << "Dumping feature samples -> " << cfg.samples_path << "\n";
  }

  for (int g = 0; g < cfg.games; ++g) {
    stats.games++;
    uint32_t board_start = pool[xorshift(rng) % pool.size()];
    uint32_t board_rng = board_start;
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
      visits.prune(200000);
      visits.save(cfg.visits_path);
    }
  }

  visits.prune(200000);
  visits.save(cfg.visits_path);
  std::cout << "Wrote position visits -> " << cfg.visits_path << " (" << visits.by_hash.size()
            << " positions)\n";

  if (cfg.learn_rate > 0.0) {
    save_weights(active_weights(), cfg.weights_path);
    std::cout << "Wrote weights -> " << cfg.weights_path << "\n";
  } else {
    std::cout << "Skipped C++ weight write (learn_rate=0; PyTorch owns eval_weights.json)\n";
  }

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

}
