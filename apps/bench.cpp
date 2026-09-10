#include "catan/board.hpp"
#include "catan/eval.hpp"
#include "catan/rules.hpp"
#include "catan/state.hpp"
#include "catan/topology.hpp"

#include <chrono>
#include <iostream>

using namespace catan;

int main() {
  Topology topo = build_topology();
  BoardSpec board = beginner_board(topo);
  GameState state = make_initial_state(topo, board, 7);
  RuleCtx ctx{&topo, &board};

  constexpr int N = 200000;
  auto t0 = std::chrono::steady_clock::now();
  volatile double sink = 0;
  for (int i = 0; i < N; ++i) {
    sink += evaluate(ctx, state, i & 3);
  }
  auto t1 = std::chrono::steady_clock::now();
  double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
  std::cout << "Evaluated " << N << " states in " << ms << " ms ("
            << (N / (ms / 1000.0)) << " eval/sec) sink=" << sink << "\n";

  // Apply-move throughput: roll + end turn loop
  GameState g = state;
  t0 = std::chrono::steady_clock::now();
  int applied = 0;
  for (int i = 0; i < 50000; ++i) {
    auto acts = legal_actions(ctx, g);
    if (acts.empty()) break;
    apply_action(ctx, g, acts.front());
    ++applied;
    if (g.game_over) g = state;
  }
  t1 = std::chrono::steady_clock::now();
  ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
  std::cout << "Applied " << applied << " actions in " << ms << " ms ("
            << (applied / (ms / 1000.0)) << " actions/sec)\n";
  return 0;
}
