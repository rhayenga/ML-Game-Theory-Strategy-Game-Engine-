#pragma once

// Self-play training loop and stats.

#include "catan/eval.hpp"
#include "catan/mcts.hpp"
#include "catan/rules.hpp"
#include "catan/visits.hpp"

#include <array>
#include <cstdint>
#include <string>

namespace catan {

struct TrainStats {
  int games = 0;
  int finished = 0;
  std::array<int, kNumPlayers> wins{};
  std::array<double, kNumPlayers> sum_vp{};
  std::array<int, 16> action_counts{};
  int total_actions = 0;
  int max_decisions = 0;
};

struct TrainConfig {
  int games = 1000;
  int max_decisions = 800;
  double epsilon = 0.15;
  double learn_rate = 0.05;
  int mcts_sims = 0;
  uint32_t seed = 42;
  std::string weights_path = "build/eval_weights.json";
  std::string stats_path = "build/train_stats.json";
  std::string visits_path = "build/position_visits.json";
  std::string samples_path = "build/train_samples.jsonl";
};

int play_one_game(const RuleCtx& ctx, GameState state, const TrainConfig& cfg, TrainStats& stats,
                  uint32_t& rng, VisitStore* visits = nullptr);

TrainStats run_training(const RuleCtx& ctx, const BoardSpec& board, const Topology& topo,
                        TrainConfig cfg);

}
