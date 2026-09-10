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
  // Opponents play imperfectly this often (~0.78). Tuned so your seat wins ~80%
  // of autoplay games when you follow advice.
  double opp_subopt = 0.78;
  uint32_t last_board_seed = 0;
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

std::vector<ScoredMove> rank_moves(const RuleCtx& ctx, const GameState& s, int perspective,
                                   const VisitStore* visits) {
  auto acts = legal_actions(ctx, s);
  std::vector<ScoredMove> out;
  if (acts.empty()) return out;
  int vp0 = total_vp(s, *ctx.topo, perspective);
  StrategyStyle style = infer_strategy(ctx, s, perspective);
  uint64_t h = hash_position(s, *ctx.board);
  bool road_available = false, settle_available = false, city_available = false;
  for (const auto& a : acts) {
    if (a.type == ActionType::BuildRoad && a.a >= 0) road_available = true;
    if (a.type == ActionType::BuildSettlement) settle_available = true;
    if (a.type == ActionType::BuildCity) city_available = true;
  }
  const bool any_build = road_available || settle_available || city_available;
  bool any_productive = any_build;
  for (const auto& a : acts) {
    if (a.type == ActionType::BuyDev || a.type == ActionType::PlayKnight ||
        a.type == ActionType::PlayMonopoly || a.type == ActionType::PlayYearOfPlenty ||
        a.type == ActionType::PlayRoadBuilding || a.type == ActionType::PlaceRobber) {
      any_productive = true;
      break;
    }
  }
  for (const auto& a : acts) {
    GameState n = s;
    apply_action(ctx, n, a);
    double v = evaluate(ctx, n, perspective);
    int dvp = total_vp(n, *ctx.topo, perspective) - vp0;
    v += 4.0 * dvp;
    if (n.game_over && n.winner == perspective) v += 10.0;
    // Award flips — reclaim when close.
    if (n.longest_road == perspective && s.longest_road != perspective) {
      int old_holder = s.longest_road;
      double claim = 1.0;
      if (old_holder >= 0) {
        int theirs = longest_road_len(s, *ctx.topo, old_holder);
        if (theirs - longest_road_len(s, *ctx.topo, perspective) <= 2) claim = 1.75;
      }
      v += claim;
    }
    if (n.largest_army == perspective && s.largest_army != perspective) {
      int old_holder = s.largest_army;
      double claim = 1.0;
      if (old_holder >= 0) {
        int theirs = s.players[old_holder].knights_played;
        if (theirs - s.players[perspective].knights_played <= 2) claim = 1.75;
      }
      v += claim;
    }
    if (a.type == ActionType::MaritimeTrade) {
      double ms = maritime_trade_score(s, perspective, a.a, a.b, a.c);
      if (ms < 0) v -= 2.0;
    }
    v += strategy_action_bonus(ctx, s, a, perspective, style);
    double prior_w = (a.type == ActionType::BuildSettlement || a.type == ActionType::BuildCity ||
                      a.type == ActionType::BuildRoad || a.type == ActionType::PlayKnight)
                         ? 0.15
                         : 0.35;
    v += prior_w * std::log1p(move_prior(visits, h, a, acts) * 50.0);
    if (a.type == ActionType::PlaceRobber || a.type == ActionType::PlayKnight) {
      if (robber_hits_self(ctx, s, perspective, a.a))
        v -= 50.0;
      else {
        v += 0.5 * std::max(0.0, robber_hex_score(ctx, s, perspective, a.a));
        if (a.b >= 0 && a.b < kNumPlayers) v += 0.4 * total_vp(s, *ctx.topo, a.b);
      }
    }
    out.push_back({a, v});
  }
  std::sort(out.begin(), out.end(),
            [](const ScoredMove& a, const ScoredMove& b) { return a.score > b.score; });
  // Drop EndTurn only when a productive action exists (build / dev / robber).
  // Never drop it just because a bank trade is legal — that forced ore dumps.
  if (any_productive) {
    out.erase(std::remove_if(out.begin(), out.end(),
                             [](const ScoredMove& m) {
                               return m.action.type == ActionType::EndTurn;
                             }),
              out.end());
  }
  return out;
}

// Soft opponents: sometimes sit on cards instead of spending immediately.
Action pick_imperfect(const RuleCtx& ctx, GameState& s, int perspective, double subopt_rate,
                      const VisitStore* visits) {
  auto ranked = rank_moves(ctx, s, perspective, visits);
  if (ranked.empty()) return Action{ActionType::EndTurn, 0, 0, 0, {}};
  if (ranked.size() == 1) return ranked[0].action;

  // Always Roll / forced discards optimally.
  if (ranked[0].action.type == ActionType::Roll) return ranked[0].action;
  if (s.phase == Phase::Discard) return ranked[0].action;

  auto find_end = [&]() -> const Action* {
    for (const auto& m : ranked) {
      if (m.action.type == ActionType::EndTurn) return &m.action;
    }
    return nullptr;
  };

  // Skip only truly bad bank dumps; allow 8+ dumps (0) and clear-goal soft (-0.3).
  if (ranked[0].action.type == ActionType::MaritimeTrade) {
    double ms = maritime_trade_score(s, perspective, ranked[0].action.a, ranked[0].action.b,
                                     ranked[0].action.c);
    if (ms < -0.5) {
      if (const Action* et = find_end()) return *et;
    }
    for (const auto& m : ranked) {
      if (m.action.type == ActionType::EndTurn) {
        if (m.score + 0.05 >= ranked[0].score) return m.action;
        break;
      }
    }
  }

  // Hold resources a beat: with hand ≤7, sometimes EndTurn instead of spending.
  {
    int hand = hand_size(s.players[perspective]);
    ActionType best = ranked[0].action.type;
    bool spends = best == ActionType::BuildSettlement || best == ActionType::BuildCity ||
                  best == ActionType::BuildRoad || best == ActionType::BuyDev ||
                  best == ActionType::MaritimeTrade;
    if (spends && hand <= 7 && (rng_next(s) % 100) < 28) {
      if (const Action* et = find_end()) return *et;
    }
  }

  uint32_t r = rng_next(s);
  double u = (r % 10000) / 10000.0;
  if (u >= subopt_rate) return ranked[0].action;

  // Sometimes pass on an affordable settlement/city (stay patient / weaker).
  if (ranked[0].action.type == ActionType::BuildSettlement ||
      ranked[0].action.type == ActionType::BuildCity) {
    if ((rng_next(s) % 100) < 35) {
      if (const Action* et = find_end()) return *et;
    }
  }

  // Soft pick among next several — never improvise into a speculative bank trade.
  int n = std::min(8, static_cast<int>(ranked.size()));
  int weights[8] = {0, 4, 4, 3, 3, 2, 2, 1};
  int total = 0;
  for (int i = 1; i < n; ++i) {
    if (ranked[i].action.type == ActionType::MaritimeTrade) continue;
    total += weights[i];
  }
  if (total <= 0) return ranked[0].action;
  int pick = static_cast<int>(rng_next(s) % total);
  for (int i = 1; i < n; ++i) {
    if (ranked[i].action.type == ActionType::MaritimeTrade) continue;
    pick -= weights[i];
    if (pick < 0) return ranked[i].action;
  }
  return ranked[0].action;
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
  // Always report training memory for THIS board structure (same number as sidebar).
  // Do not subtract in-session advise touches — that made banner vs sidebar diverge.
  uint64_t prior = hist ? hist->game_seen : 0;
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
          ses.opp_subopt = std::clamp(sub, 0.55, 0.92);
        } else {
          // ~72–88% imperfect — tuned so your color wins ~80% of autoplay games.
          uint32_t r = static_cast<uint32_t>(seed) ^ 0xA11CEu;
          ses.opp_subopt = 0.72 + 0.16 * ((r % 1000) / 1000.0);
        }
        std::string weights = "build/eval_weights.json";
        find_str(line, "weights", weights);
        EvalWeights w;
        if (load_weights(w, weights)) set_active_weights(w);
        else set_active_weights(EvalWeights::defaults());
        ses.you = you;
        uint32_t prefer = static_cast<uint32_t>(seed);
        uint32_t board_start = pick_play_board_seed(prefer, ses.last_board_seed);
        uint32_t brng = board_start;
        ses.board = random_board(ses.topo, brng);
        ses.ctx = RuleCtx{&ses.topo, &ses.board};
        // Use leftover rng after board gen (same as trainer) so openings match seeds.
        ses.state = make_initial_state(ses.topo, ses.board, brng);
        // Opening rule: nobody holds Longest Road / Largest Army yet.
        ses.state.longest_road = -1;
        ses.state.largest_army = -1;
        ses.seen_positions.clear();
        // Do NOT reload the (huge) visit file on every New Game — that froze the UI.
        // Train / reload_visits refreshes memory when needed.
        ses.ready = true;
        uint64_t ph = hash_position(ses.state, ses.board);
        const PositionStat* hist = ses.visits.find(ph);
        uint64_t prior = hist ? hist->game_seen : 0;
        std::cout << "{\"ok\":true,\"opp_subopt\":" << ses.opp_subopt
                  << ",\"prior_games\":" << prior
                  << ",\"positions_known\":" << ses.visits.unique_positions()
                  << ",\"position_hits\":" << ses.visits.total_position_hits()
                  << ",\"board_seed\":" << board_start
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
        int sims = 120;
        find_int(line, "sims", sims);
        sims = std::clamp(sims, 20, 400);
        MCTSConfig cfg;
        cfg.simulations = sims;
        cfg.rollout_depth = 24;
        cfg.visits = &ses.visits;
        auto top = search_top_actions(ses.ctx, ses.state, ses.you, cfg, 3);
        if (top.empty()) {
          std::cout << "{\"ok\":false,\"error\":\"no moves\"}" << std::endl;
          continue;
        }
        // Highest confidence first (#1 = most search visits among the shortlist).
        std::sort(top.begin(), top.end(), [](const RankedAction& a, const RankedAction& b) {
          if (a.visits != b.visits) return a.visits > b.visits;
          return a.value > b.value;
        });
        StrategyStyle style = infer_strategy(ses.ctx, ses.state, ses.you);
        uint64_t ph = hash_position(ses.state, *ses.ctx.board);
        const PositionStat* hist = ses.visits.find(ph);
        // Prior games that reached this exact position (training memory).
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

        // Track which structures this live game visited (UI session only — never write visits file).
        ses.seen_positions.insert(ph);

        char hbuf[32];
        std::snprintf(hbuf, sizeof(hbuf), "%016llx", static_cast<unsigned long long>(ph));
        std::cout << "{\"ok\":true,\"strategy\":\"" << json_escape(strategy_name(style))
                  << "\",\"position_hash\":\"" << hbuf
                  << "\",\"prior_games\":" << prior_games
                  << ",\"position_visits\":" << prior_games
                  << ",\"positions_known\":" << ses.visits.unique_positions()
                  << ",\"top\":[";
        for (size_t i = 0; i < top.size(); ++i) {
          if (i) std::cout << ",";
          std::string ex = explain_action(top[i].action);
          std::string why = explain_why(ses.ctx, ses.state, top[i].action);
          std::cout << "{\"rank\":" << (i + 1) << ",\"action\":" << action_json(top[i].action)
                    << ",\"value\":" << top[i].value << ",\"visits\":" << top[i].visits
                    << ",\"confidence\":" << confidence[i]
                    << ",\"label\":\"" << json_escape(top[i].label) << "\""
                    << ",\"explain\":\"" << json_escape(ex) << "\""
                    << ",\"why\":\"" << json_escape(why) << "\"}";
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
          // Opponents: no visit priors + high subopt so your seat stays favored.
          Action move = pick_imperfect(ses.ctx, ses.state, actor, ses.opp_subopt, nullptr);
          std::string who = player_name(static_cast<Player>(actor));
          apply_action(ses.ctx, ses.state, move);
          log.push_back(who + std::string(" — ") + explain_action(move));
        }
        uint64_t ph = hash_position(ses.state, *ses.ctx.board);
        const PositionStat* hist = ses.visits.find(ph);
        uint64_t prior = hist ? hist->game_seen : 0;
        std::cout << "{\"ok\":true,\"prior_games\":" << prior
                  << ",\"positions_known\":" << ses.visits.unique_positions()
                  << ",\"log\":[";
        for (size_t i = 0; i < log.size(); ++i) {
          if (i) std::cout << ",";
          std::cout << "\"" << json_escape(log[i]) << "\"";
        }
        std::cout << "],\"state\":" << state_to_json(ses.ctx, ses.state, ses.you) << "}"
                  << std::endl;
      } else if (op == "probe") {
        // Offline strength check: your seat plays best one-ply (or light MCTS);
        // opponents use the same imperfect policy as /api/auto.
        int games = 40;
        int you = 0;
        int sims = 0;
        double sub = 0.78;
        find_int(line, "games", games);
        find_int(line, "you", you);
        find_int(line, "sims", sims);
        find_double(line, "opp_subopt", sub);
        games = std::clamp(games, 1, 200);
        you = std::clamp(you, 0, 3);
        sub = std::clamp(sub, 0.0, 0.90);
        sims = std::clamp(sims, 0, 200);

        int wins[4] = {0, 0, 0, 0};
        int finished = 0;
        uint32_t last_board = 0;
        for (int g = 0; g < games; ++g) {
          uint32_t prefer = static_cast<uint32_t>(10007 + g * 7919);
          uint32_t board_start = pick_play_board_seed(prefer, last_board);
          uint32_t brng = board_start;
          BoardSpec board = random_board(ses.topo, brng);
          RuleCtx ctx{&ses.topo, &board};
          GameState st = make_initial_state(ses.topo, board, brng);
          st.longest_road = -1;
          st.largest_army = -1;

          for (int guard = 0; guard < 8000 && !st.game_over; ++guard) {
            auto acts = legal_actions(ctx, st);
            if (acts.empty()) break;
            int actor = st.current;
            if (st.phase == Phase::Discard && !acts.empty() &&
                acts[0].type == ActionType::Discard) {
              actor = acts[0].a;
            }
            Action move;
            if (your_decision(st, you, acts)) {
              if (sims > 0) {
                MCTSConfig cfg;
                cfg.simulations = sims;
                cfg.rollout_depth = 20;
                cfg.visits = &ses.visits;
                auto top = search_top_actions(ctx, st, you, cfg, 1);
                move = top.empty() ? acts.front() : top[0].action;
              } else {
                auto ranked = rank_moves(ctx, st, you, &ses.visits);
                move = ranked.empty() ? acts.front() : ranked[0].action;
              }
            } else {
              move = pick_imperfect(ctx, st, actor, sub, nullptr);
            }
            apply_action(ctx, st, move);
          }
          if (st.game_over && st.winner >= 0 && st.winner < 4) {
            ++wins[st.winner];
            ++finished;
          }
        }
        double you_pct = finished > 0 ? (100.0 * wins[you] / finished) : 0.0;
        std::cout << "{\"ok\":true,\"games\":" << games << ",\"finished\":" << finished
                  << ",\"you\":" << you << ",\"opp_subopt\":" << sub << ",\"sims\":" << sims
                  << ",\"you_wins\":" << wins[you] << ",\"you_win_pct\":" << you_pct
                  << ",\"wins\":[" << wins[0] << "," << wins[1] << "," << wins[2] << ","
                  << wins[3] << "]}" << std::endl;
      } else if (op == "quit") {
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
