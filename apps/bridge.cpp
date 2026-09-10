#include "catan/board.hpp"
#include "catan/eval.hpp"
#include "catan/json_api.hpp"
#include "catan/mcts.hpp"
#include "catan/rules.hpp"
#include "catan/state.hpp"
#include "catan/strategy.hpp"
#include "catan/topology.hpp"
#include "catan/visits.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <iostream>
#include <sstream>
#include <string>
#include <unordered_set>
#include <vector>

using namespace catan;

namespace {

struct Session {
  Topology topo = build_topology();
  BoardSpec board = beginner_board(topo);
  RuleCtx ctx{&topo, &board};
  GameState state{};
  int you = 0;
  bool ready = false;
  VisitStore visits{};
  std::unordered_set<uint64_t> seen_positions{};
  // Opponents play imperfectly this often (0.30 = 30%).
  double opp_subopt = 0.30;
};

bool find_str(const std::string& j, const std::string& key, std::string& out) {
  auto k = "\"" + key + "\"";
  auto p = j.find(k);
  if (p == std::string::npos) return false;
  p = j.find(':', p);
  if (p == std::string::npos) return false;
  p = j.find('"', p);
  if (p == std::string::npos) return false;
  auto q = j.find('"', p + 1);
  if (q == std::string::npos) return false;
  out = j.substr(p + 1, q - p - 1);
  return true;
}

bool find_int(const std::string& j, const std::string& key, int& out) {
  auto k = "\"" + key + "\"";
  auto p = j.find(k);
  if (p == std::string::npos) return false;
  p = j.find(':', p);
  if (p == std::string::npos) return false;
  ++p;
  while (p < j.size() && (j[p] == ' ' || j[p] == '\t')) ++p;
  try {
    size_t n = 0;
    out = std::stoi(j.substr(p), &n);
    return n > 0;
  } catch (...) {
    return false;
  }
}

bool find_double(const std::string& j, const std::string& key, double& out) {
  auto k = "\"" + key + "\"";
  auto p = j.find(k);
  if (p == std::string::npos) return false;
  p = j.find(':', p);
  if (p == std::string::npos) return false;
  out = std::strtod(j.c_str() + p + 1, nullptr);
  return true;
}

Action parse_action(const std::string& j) {
  Action act;
  std::string type;
  if (!find_str(j, "type", type) || !action_type_from_name(type, act.type)) {
    act.type = ActionType::EndTurn;
  }
  find_int(j, "a", act.a);
  find_int(j, "b", act.b);
  find_int(j, "c", act.c);
  auto d = j.find("\"discard\"");
  if (d != std::string::npos) {
    auto lb = j.find('[', d);
    auto rb = j.find(']', lb);
    if (lb != std::string::npos && rb != std::string::npos) {
      std::string inner = j.substr(lb + 1, rb - lb - 1);
      std::stringstream ss(inner);
      for (int i = 0; i < 5; ++i) {
        int v = 0;
        char comma;
        ss >> v;
        act.discard[i] = static_cast<uint8_t>(v);
        ss >> comma;
      }
    }
  }
  return act;
}

std::string json_escape(const std::string& s) {
  std::string o;
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

std::string action_json(const Action& act) {
  std::ostringstream o;
  o << "{\"type\":\"" << action_type_name(act.type) << "\","
    << "\"a\":" << act.a << ",\"b\":" << act.b << ",\"c\":" << act.c << ","
    << "\"discard\":[" << int(act.discard[0]) << "," << int(act.discard[1]) << ","
    << int(act.discard[2]) << "," << int(act.discard[3]) << "," << int(act.discard[4]) << "],"
    << "\"explain\":\"" << json_escape(explain_action(act)) << "\"}";
  return o.str();
}

struct ScoredMove {
  Action action;
  double score = 0;
};

std::vector<ScoredMove> rank_moves(const RuleCtx& ctx, const GameState& s, int perspective) {
  auto acts = legal_actions(ctx, s);
  std::vector<ScoredMove> out;
  if (acts.empty()) return out;
  int vp0 = total_vp(s, *ctx.topo, perspective);
  StrategyStyle style = infer_strategy(ctx, s, perspective);
  for (const auto& a : acts) {
    GameState n = s;
    apply_action(ctx, n, a);
    double v = evaluate(ctx, n, perspective);
    v += 0.65 * (total_vp(n, *ctx.topo, perspective) - vp0);
    if (n.game_over && n.winner == perspective) v += 5.0;
    v += strategy_action_bonus(ctx, s, a, perspective, style);
    out.push_back({a, v});
  }
  std::sort(out.begin(), out.end(),
            [](const ScoredMove& a, const ScoredMove& b) { return a.score > b.score; });
  // Drop EndTurn from consideration when alternatives exist (except Roll-only phases).
  bool has_real = false;
  for (const auto& m : out) {
    if (m.action.type != ActionType::EndTurn) {
      has_real = true;
      break;
    }
  }
  if (has_real) {
    out.erase(std::remove_if(out.begin(), out.end(),
                             [](const ScoredMove& m) {
                               return m.action.type == ActionType::EndTurn;
                             }),
              out.end());
  }
  return out;
}

// ~subopt_rate: pick 2nd–4th best instead of #1 (still strong, not random trash).
Action pick_imperfect(const RuleCtx& ctx, GameState& s, int perspective, double subopt_rate) {
  auto ranked = rank_moves(ctx, s, perspective);
  if (ranked.empty()) return Action{ActionType::EndTurn, 0, 0, 0, {}};
  if (ranked.size() == 1) return ranked[0].action;

  // Always Roll / forced discards optimally.
  if (ranked[0].action.type == ActionType::Roll) return ranked[0].action;
  if (s.phase == Phase::Discard) return ranked[0].action;

  uint32_t r = rng_next(s);
  double u = (r % 10000) / 10000.0;
  if (u >= subopt_rate) return ranked[0].action;

  // Soft pick among next few: weights 4,2,1 for ranks 2,3,4
  int n = std::min(4, static_cast<int>(ranked.size()));
  int weights[4] = {0, 4, 2, 1};
  int total = 0;
  for (int i = 1; i < n; ++i) total += weights[i];
  if (total <= 0) return ranked[0].action;
  int pick = static_cast<int>(rng_next(s) % total);
  for (int i = 1; i < n; ++i) {
    pick -= weights[i];
    if (pick < 0) return ranked[i].action;
  }
  return ranked[std::min(1, n - 1)].action;
}

bool your_decision(const GameState& s, int you, const std::vector<Action>& acts) {
  if (s.phase == Phase::Discard) {
    if (!acts.empty() && acts[0].type == ActionType::Discard) return acts[0].a == you;
    return false;
  }
  return s.current == you;
}

void reply_ok_state(Session& ses) {
  uint64_t ph = hash_position(ses.state, *ses.ctx.board);
  const PositionStat* hist = ses.visits.find(ph);
  uint64_t prior = hist ? hist->game_seen : 0;
  // Don't count the current live game until it notes the position once.
  if (ses.seen_positions.count(ph)) {
    // Already counted this game; prior for display = total including this game's note,
    // but "prior games" should exclude current: game_seen - 1 if we already incremented.
    if (prior > 0) prior -= 1;
  }
  std::cout << "{\"ok\":true,\"prior_games\":" << prior
            << ",\"positions_known\":" << ses.visits.unique_positions()
            << ",\"position_hits\":" << ses.visits.total_position_hits()
            << ",\"state\":" << state_to_json(ses.ctx, ses.state, ses.you) << "}" << std::endl;
}

}  // namespace

int main() {
  std::ios::sync_with_stdio(false);
  std::cin.tie(nullptr);

  Session ses;
  set_active_weights(EvalWeights::defaults());
  ses.visits.load("build/position_visits.json");

  std::string line;
  while (std::getline(std::cin, line)) {
    if (line.empty()) continue;
    std::string op;
    if (!find_str(line, "op", op)) {
      std::cout << "{\"ok\":false,\"error\":\"missing op\"}" << std::endl;
      continue;
    }

    try {
      if (op == "ping") {
        std::cout << "{\"ok\":true,\"pong\":true}" << std::endl;
      } else if (op == "new") {
        int you = 0, seed = 42;
        find_int(line, "you", you);
        find_int(line, "seed", seed);
        double sub = -1;
        if (find_double(line, "opp_subopt", sub) && sub >= 0) {
          ses.opp_subopt = std::clamp(sub, 0.20, 0.35);
        } else {
          // Intentional imperfection: 20–35% of opponent decisions take 2nd–4th ranked.
          uint32_t r = static_cast<uint32_t>(seed) ^ 0xA11CEu;
          ses.opp_subopt = 0.20 + 0.15 * ((r % 1000) / 1000.0);
        }
        std::string weights = "build/eval_weights.json";
        find_str(line, "weights", weights);
        EvalWeights w;
        if (load_weights(w, weights)) set_active_weights(w);
        else set_active_weights(EvalWeights::defaults());
        ses.you = you;
        uint32_t brng = static_cast<uint32_t>(seed) ^ 0xC47A11u;
        ses.board = random_board(ses.topo, brng);
        ses.ctx = RuleCtx{&ses.topo, &ses.board};
        ses.state = make_initial_state(ses.topo, ses.board, static_cast<uint32_t>(seed));
        ses.seen_positions.clear();
        ses.visits.load("build/position_visits.json");  // pick up training updates
        ses.ready = true;
        uint64_t ph = hash_position(ses.state, ses.board);
        const PositionStat* hist = ses.visits.find(ph);
        uint64_t prior = hist ? hist->game_seen : 0;
        std::cout << "{\"ok\":true,\"opp_subopt\":" << ses.opp_subopt
                  << ",\"prior_games\":" << prior
                  << ",\"positions_known\":" << ses.visits.unique_positions()
                  << ",\"position_hits\":" << ses.visits.total_position_hits()
                  << ",\"state\":" << state_to_json(ses.ctx, ses.state, ses.you) << "}" << std::endl;
      } else if (op == "reload_visits") {
        ses.visits.load("build/position_visits.json");
        std::cout << "{\"ok\":true,\"positions_known\":" << ses.visits.unique_positions()
                  << ",\"position_hits\":" << ses.visits.total_position_hits() << "}" << std::endl;
      } else if (op == "state") {
        if (!ses.ready) {
          std::cout << "{\"ok\":false,\"error\":\"no game\"}" << std::endl;
          continue;
        }
        reply_ok_state(ses);
      } else if (op == "advise") {
        if (!ses.ready) {
          std::cout << "{\"ok\":false,\"error\":\"no game\"}" << std::endl;
          continue;
        }
        int sims = 800;
        find_int(line, "sims", sims);
        MCTSConfig cfg;
        cfg.simulations = sims;
        cfg.rollout_depth = 24;
        auto top = search_top_actions(ses.ctx, ses.state, ses.you, cfg, 3);
        if (top.empty()) {
          std::cout << "{\"ok\":false,\"error\":\"no moves\"}" << std::endl;
          continue;
        }
        StrategyStyle style = infer_strategy(ses.ctx, ses.state, ses.you);
        uint64_t ph = hash_position(ses.state, *ses.ctx.board);
        const PositionStat* hist = ses.visits.find(ph);
        // Prior games that reached this exact position (not MCTS sims).
        uint64_t prior_games = hist ? hist->game_seen : 0;

        // Confidence from THIS search only (visit share among top moves).
        int search_total = 0;
        for (const auto& t : top) search_total += std::max(0, t.visits);
        if (search_total <= 0) search_total = static_cast<int>(top.size());

        std::vector<double> confidence(top.size(), 0.0);
        for (size_t i = 0; i < top.size(); ++i) {
          int v = std::max(0, top[i].visits);
          confidence[i] = 100.0 * static_cast<double>(v) / static_cast<double>(search_total);
        }

        // Record that this live game touched the position (once per game via session set).
        if (ses.seen_positions.insert(ph).second) {
          auto& st = ses.visits.touch(ph);
          st.game_seen += 1;
          ses.visits.save(ses.visits.path);
        }

        char hbuf[32];
        std::snprintf(hbuf, sizeof(hbuf), "%016llx", static_cast<unsigned long long>(ph));
        std::cout << "{\"ok\":true,\"strategy\":\"" << json_escape(strategy_name(style))
                  << "\",\"position_hash\":\"" << hbuf
                  << "\",\"prior_games\":" << prior_games
                  << ",\"position_visits\":" << prior_games
                  << ",\"top\":[";
        for (size_t i = 0; i < top.size(); ++i) {
          if (i) std::cout << ",";
          std::string ex = explain_action(top[i].action);
          std::cout << "{\"rank\":" << (i + 1) << ",\"action\":" << action_json(top[i].action)
                    << ",\"value\":" << top[i].value << ",\"visits\":" << top[i].visits
                    << ",\"confidence\":" << confidence[i]
                    << ",\"label\":\"" << json_escape(top[i].label) << "\""
                    << ",\"explain\":\"" << json_escape(ex) << "\"}";
        }
        std::cout << "],\"action\":" << action_json(top[0].action) << ",\"value\":" << top[0].value
                  << ",\"visits\":" << top[0].visits << ",\"confidence\":" << confidence[0]
                  << ",\"state\":" << state_to_json(ses.ctx, ses.state, ses.you) << "}" << std::endl;
      } else if (op == "apply") {
        if (!ses.ready) {
          std::cout << "{\"ok\":false,\"error\":\"no game\"}" << std::endl;
          continue;
        }
        Action act = parse_action(line);
        apply_action(ses.ctx, ses.state, act);
        reply_ok_state(ses);
      } else if (op == "auto") {
        if (!ses.ready) {
          std::cout << "{\"ok\":false,\"error\":\"no game\"}" << std::endl;
          continue;
        }
        std::vector<std::string> log;
        for (int guard = 0; guard < 200; ++guard) {
          if (ses.state.game_over) break;
          auto acts = legal_actions(ses.ctx, ses.state);
          if (acts.empty()) break;
          if (your_decision(ses.state, ses.you, acts)) break;
          int actor = ses.state.current;
          if (ses.state.phase == Phase::Discard && !acts.empty() &&
              acts[0].type == ActionType::Discard) {
            actor = acts[0].a;
          }
          // Opponents: ranked legal moves with ~opp_subopt chance of 2nd–4th pick.
          Action move = pick_imperfect(ses.ctx, ses.state, actor, ses.opp_subopt);
          std::string who = player_name(static_cast<Player>(actor));
          apply_action(ses.ctx, ses.state, move);
          log.push_back(who + std::string(" — ") + explain_action(move));
        }
        std::cout << "{\"ok\":true,\"log\":[";
        for (size_t i = 0; i < log.size(); ++i) {
          if (i) std::cout << ",";
          std::cout << "\"" << json_escape(log[i]) << "\"";
        }
        std::cout << "],\"state\":" << state_to_json(ses.ctx, ses.state, ses.you) << "}"
                  << std::endl;
      } else if (op == "quit") {
        ses.visits.save(ses.visits.path);
        std::cout << "{\"ok\":true}" << std::endl;
        break;
      } else {
        std::cout << "{\"ok\":false,\"error\":\"unknown op\"}" << std::endl;
      }
    } catch (const std::exception& e) {
      std::cout << "{\"ok\":false,\"error\":\"" << json_escape(e.what()) << "\"}" << std::endl;
    }
  }
  return 0;
}
