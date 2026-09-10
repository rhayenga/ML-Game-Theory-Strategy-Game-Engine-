#include "catan/board.hpp"
#include "catan/eval.hpp"
#include "catan/topology.hpp"
#include "catan/train.hpp"

#include <cstdlib>
#include <iostream>
#include <string>

using namespace catan;

static void print_help(const char* argv0) {
  std::cout
      << "Catan self-play trainer\n\n"
      << "Usage:\n"
      << "  " << argv0 << " [games] [options]\n\n"
      << "Arguments:\n"
      << "  games              Number of self-play games (default 1000)\n\n"
      << "Options:\n"
      << "  --help             Show this help\n"
      << "  --seed N           RNG seed (default 42)\n"
      << "  --epsilon X        Exploration rate 0..1 (default 0.15)\n"
      << "  --lr X             Weight learn rate (default 0.05)\n"
      << "  --mcts N           MCTS sims per decision (0=fast one-ply, default 0)\n"
      << "  --weights PATH     Weights JSON in/out (default build/eval_weights.json)\n"
      << "  --stats PATH       Stats JSON out (default build/train_stats.json)\n\n"
      << "Examples:\n"
      << "  " << argv0 << " 1000\n"
      << "  " << argv0 << " 100 --epsilon 0.2 --lr 0.08\n"
      << "  " << argv0 << " 50 --mcts 40\n\n"
      << "After training, run: ./build/catan_advise  [sims] [player]\n"
      << "The advisor auto-loads build/eval_weights.json when present.\n";
}

int main(int argc, char** argv) {
  TrainConfig cfg;
  for (int i = 1; i < argc; ++i) {
    std::string a = argv[i];
    if (a == "--help" || a == "-h") {
      print_help(argv[0]);
      return 0;
    }
    if (a == "--seed" && i + 1 < argc) {
      cfg.seed = static_cast<uint32_t>(std::stoul(argv[++i]));
    } else if (a == "--epsilon" && i + 1 < argc) {
      cfg.epsilon = std::stod(argv[++i]);
    } else if (a == "--lr" && i + 1 < argc) {
      cfg.learn_rate = std::stod(argv[++i]);
    } else if (a == "--mcts" && i + 1 < argc) {
      cfg.mcts_sims = std::stoi(argv[++i]);
    } else if (a == "--weights" && i + 1 < argc) {
      cfg.weights_path = argv[++i];
    } else if (a == "--stats" && i + 1 < argc) {
      cfg.stats_path = argv[++i];
    } else if (a[0] != '-') {
      cfg.games = std::stoi(a);
    } else {
      std::cerr << "Unknown option: " << a << "\n";
      print_help(argv[0]);
      return 1;
    }
  }

  Topology topo = build_topology();
  BoardSpec board = beginner_board(topo);
  RuleCtx ctx{&topo, &board};

  std::cout << "Catan train: " << cfg.games << " games, epsilon=" << cfg.epsilon
            << ", lr=" << cfg.learn_rate << ", mcts=" << cfg.mcts_sims << "\n";

  auto stats = run_training(ctx, board, topo, cfg);

  std::cout << "\n=== Summary ===\n";
  std::cout << "Games: " << stats.games << "  finished: " << stats.finished << "\n";
  for (int p = 0; p < kNumPlayers; ++p) {
    double wr = stats.finished ? 100.0 * stats.wins[p] / stats.finished : 0;
    double avp = stats.games ? stats.sum_vp[p] / stats.games : 0;
    std::cout << "  " << player_name(static_cast<Player>(p)) << ": wins=" << stats.wins[p] << " ("
              << wr << "%) avgVP=" << avp << "\n";
  }
  std::cout << "Weights:";
  for (int i = 0; i < kEvalDim; ++i) std::cout << " " << active_weights().w[i];
  std::cout << "  scale=" << active_weights().scale << "\n";
  return 0;
}
