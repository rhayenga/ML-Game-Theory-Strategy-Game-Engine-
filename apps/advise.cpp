#include "catan/board.hpp"
#include "catan/eval.hpp"
#include "catan/mcts.hpp"
#include "catan/rules.hpp"
#include "catan/state.hpp"
#include "catan/topology.hpp"

#include <iostream>
#include <limits>
#include <string>

using namespace catan;

static const char* phase_label(Phase p) {
  switch (p) {
    case Phase::PreRoll: return "before dice (play knight or roll)";
    case Phase::Discard: return "discard (rolled a 7)";
    case Phase::RobberMove: return "move the robber";
    case Phase::Main: return "trade / build / end turn";
    case Phase::GameOver: return "game over";
  }
  return "?";
}

static std::string explain_action(const Action& act) {
  switch (act.type) {
    case ActionType::Roll:
      return "ROLL the dice";
    case ActionType::EndTurn:
      return "END your turn (pass)";
    case ActionType::Discard:
      return "DISCARD half your cards (7 was rolled)";
    case ActionType::PlaceRobber:
      return "MOVE ROBBER to hex " + std::to_string(act.a) +
             (act.b >= 0 ? " and steal from " + std::string(player_name(static_cast<Player>(act.b)))
                         : "");
    case ActionType::BuildRoad:
      return "BUILD a ROAD on edge " + std::to_string(act.a) + " (costs brick+lumber)";
    case ActionType::BuildSettlement:
      return "BUILD a SETTLEMENT at intersection " + std::to_string(act.a) +
             " (brick+lumber+grain+wool)";
    case ActionType::BuildCity:
      return "UPGRADE to a CITY at intersection " + std::to_string(act.a) + " (3 ore + 2 grain)";
    case ActionType::BuyDev:
      return "BUY a DEVELOPMENT CARD (ore+grain+wool)";
    case ActionType::PlayKnight:
      return "PLAY KNIGHT → move robber to hex " + std::to_string(act.a);
    case ActionType::PlayMonopoly:
      return "PLAY MONOPOLY on " + std::string(resource_name(static_cast<Resource>(act.a)));
    case ActionType::PlayYearOfPlenty:
      return "PLAY YEAR OF PLENTY → take " + std::string(resource_name(static_cast<Resource>(act.a))) +
             " and " + std::string(resource_name(static_cast<Resource>(act.b)));
    case ActionType::PlayRoadBuilding:
      return "PLAY ROAD BUILDING on edge(s) " + std::to_string(act.a) +
             (act.b >= 0 ? ", " + std::to_string(act.b) : "");
    case ActionType::MaritimeTrade:
      return "BANK TRADE: give " + std::to_string(act.c) + " " +
             std::string(resource_name(static_cast<Resource>(act.a))) + " → get 1 " +
             std::string(resource_name(static_cast<Resource>(act.b)));
  }
  return action_to_string(act);
}

static void print_hand(const GameState& s, int p) {
  std::cout << "  Hand:";
  bool any = false;
  for (int r = 0; r < 5; ++r) {
    if (s.players[p].res[r]) {
      std::cout << " " << resource_name(static_cast<Resource>(r)) << "=" << int(s.players[p].res[r]);
      any = true;
    }
  }
  if (!any) std::cout << " (empty)";
  std::cout << "\n";
}

static void print_scoreboard(const GameState& s, const Topology& topo) {
  std::cout << "  Scores:";
  for (int p = 0; p < kNumPlayers; ++p) {
    std::cout << " " << player_name(static_cast<Player>(p)) << "=" << total_vp(s, topo, p) << "VP";
  }
  std::cout << "\n";
}

static Action pick_fast(const RuleCtx& ctx, const GameState& s, int perspective) {
  auto acts = legal_actions(ctx, s);
  if (acts.empty()) return Action{ActionType::EndTurn, 0, 0, 0, {}};
  Action best = acts[0];
  double best_v = -1e99;
  for (const auto& a : acts) {
    GameState n = s;
    apply_action(ctx, n, a);
    double v = evaluate(ctx, n, perspective);
    if (a.type == ActionType::EndTurn) v -= 0.02;  // slight prefer doing something
    if (v > best_v) {
      best_v = v;
      best = a;
    }
  }
  return best;
}

static void wait_enter(const std::string& prompt) {
  std::cout << prompt << std::flush;
  std::string line;
  std::getline(std::cin, line);
}

int main(int argc, char** argv) {
  int you = 0;  // Red
  int your_sims = 400;
  int max_steps = 2000;
  std::string weights_path = "build/eval_weights.json";

  for (int i = 1; i < argc; ++i) {
    std::string a = argv[i];
    if (a == "--help" || a == "-h") {
      std::cout
          << "Interactive turn-by-turn Catan coach (beginner map)\n\n"
          << "Usage: " << argv[0] << " [--you N] [--sims N]\n\n"
          << "  --you N    Your seat: 0=Red 1=White 2=Orange 3=Blue (default 0)\n"
          << "  --sims N   MCTS think-time on YOUR turns (default 400)\n\n"
          << "How it works:\n"
          << "  1. Game starts with the rulebook beginner placements.\n"
          << "  2. On YOUR turn the engine says exactly what to do (roll, build, trade…).\n"
          << "  3. Press Enter to play that move.\n"
          << "  4. Opponents are played automatically by the AI.\n"
          << "  5. Continues until someone hits 10 VP.\n\n"
          << "Tip: train first with ./build/catan_train 1000 so advice uses learned weights.\n";
      return 0;
    }
    if (a == "--you" && i + 1 < argc) you = std::stoi(argv[++i]);
    else if (a == "--sims" && i + 1 < argc) your_sims = std::stoi(argv[++i]);
    else if (a == "--weights" && i + 1 < argc) weights_path = argv[++i];
    else if (a[0] != '-') {
      // backward compat: first positional = sims
      your_sims = std::stoi(a);
    }
  }

  EvalWeights w;
  if (load_weights(w, weights_path)) {
    set_active_weights(w);
    std::cout << "Loaded learned weights from " << weights_path << "\n";
  } else {
    set_active_weights(EvalWeights::defaults());
    std::cout << "Using default weights (run ./build/catan_train 1000 to improve).\n";
  }

  Topology topo = build_topology();
  BoardSpec board = beginner_board(topo);
  GameState state = make_initial_state(topo, board, 42);
  RuleCtx ctx{&topo, &board};

  std::cout << "\n========================================\n";
  std::cout << "  CATAN COACH — turn by turn\n";
  std::cout << "========================================\n";
  std::cout << "You are playing as " << player_name(static_cast<Player>(you)) << ".\n";
  std::cout << "Beginner map: settlements already placed (rulebook Illustration A).\n";
  std::cout << "Each time it's your turn, I'll tell you the best next action.\n";
  std::cout << "Press Enter after each tip to apply it and continue.\n\n";

  for (int p = 0; p < kNumPlayers; ++p) {
    std::cout << player_name(static_cast<Player>(p)) << " starts with:";
    bool any = false;
    for (int r = 0; r < 5; ++r) {
      if (state.players[p].res[r]) {
        std::cout << " " << resource_name(static_cast<Resource>(r)) << "="
                  << int(state.players[p].res[r]);
        any = true;
      }
    }
    if (!any) std::cout << " (nothing)";
    std::cout << "\n";
  }
  std::cout << "\n";
  wait_enter("Press Enter to start the game...");

  int step = 0;
  int turn_no = 1;
  while (!state.game_over && step < max_steps) {
    ++step;
    auto acts = legal_actions(ctx, state);
    if (acts.empty()) {
      std::cout << "No legal actions — stopping.\n";
      break;
    }

    // Discard phase can involve any player; treat as "your" decision if you must discard.
    bool your_decision = false;
    if (state.phase == Phase::Discard) {
      for (int p = 0; p < kNumPlayers; ++p) {
        if ((state.discard_left & (1u << p)) && p == you) your_decision = true;
      }
      // Engine still applies discards via legal_actions which targets one player at a time.
      if (!your_decision && acts[0].type == ActionType::Discard && acts[0].a != you) {
        apply_action(ctx, state, acts[0]);
        continue;
      }
      if (acts[0].type == ActionType::Discard && acts[0].a == you) your_decision = true;
    } else {
      your_decision = (state.current == you);
    }

    if (your_decision) {
      std::cout << "\n---------- YOUR TURN (#" << turn_no << ") ----------\n";
      std::cout << "Phase: " << phase_label(state.phase) << "\n";
      if (state.last_roll) std::cout << "Last roll: " << int(state.last_roll) << "\n";
      print_hand(state, you);
      print_scoreboard(state, topo);
      std::cout << "Robber on hex " << int(state.robber) << "\n";

      std::cout << "Thinking (" << your_sims << " simulations)...\n";
      MCTSConfig cfg;
      cfg.simulations = your_sims;
      cfg.rollout_depth = 25;
      auto result = search_best_action(ctx, state, you, cfg);

      std::cout << "\n>>> DO THIS: " << explain_action(result.best) << "\n";
      std::cout << "    (engine: " << action_to_string(result.best) << ", confidence≈"
                << result.visits << " visits, value=" << result.value << ")\n";

      wait_enter("Press Enter to play this move (or Ctrl+C to quit)...");
      apply_action(ctx, state, result.best);

      if (result.best.type == ActionType::EndTurn) ++turn_no;
      if (result.best.type == ActionType::Roll && state.last_roll)
        std::cout << "  Dice came up: " << int(state.last_roll) << "\n";
    } else {
      // Opponent / auto
      int actor = state.current;
      Action move = pick_fast(ctx, state, actor);
      if (state.phase == Phase::Discard && acts[0].type == ActionType::Discard)
        move = acts[0];

      std::cout << "  " << player_name(static_cast<Player>(actor)) << " → "
                << explain_action(move) << "\n";
      apply_action(ctx, state, move);
      if (move.type == ActionType::EndTurn && state.current == you) {
        // about to become your turn again after others finish
      }
    }
  }

  std::cout << "\n============= GAME OVER =============\n";
  print_scoreboard(state, topo);
  if (state.game_over && state.winner >= 0) {
    std::cout << "Winner: " << player_name(static_cast<Player>(state.winner)) << "\n";
    if (state.winner == you) std::cout << "You won!\n";
    else std::cout << "You lost — train more or raise --sims for stronger tips.\n";
  } else {
    std::cout << "Stopped early (step cap).\n";
  }
  return 0;
}
