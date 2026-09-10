#include "catan/mcts.hpp"

#include "catan/strategy.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <memory>
#include <random>
#include <vector>

namespace catan {
namespace {

struct Node {
  GameState state;
  Action action_from_parent{};
  Node* parent = nullptr;
  std::vector<std::unique_ptr<Node>> children;
  std::vector<Action> untried;
  int visits = 0;
  double value_sum = 0;
  double prior = 1.0;  // policy prior from self-play memory
  int to_move = 0;
};

// AlphaZero-style PUCT: Q + c * P * sqrt(N) / (1+n)
double puct_score(const Node& child, int parent_visits, double c) {
  double q = child.visits ? (child.value_sum / child.visits) : 0.0;
  return q + c * child.prior * std::sqrt(static_cast<double>(parent_visits) + 1e-9) /
                 (1.0 + child.visits);
}

Node* select(Node* node, double c) {
  while (!node->children.empty() && node->untried.empty()) {
    Node* best = nullptr;
    double best_u = -1e99;
    for (auto& ch : node->children) {
      double u = puct_score(*ch, node->visits, c);
      if (u > best_u) {
        best_u = u;
        best = ch.get();
      }
    }
    node = best;
  }
  return node;
}

Node* expand(const RuleCtx& ctx, Node* node, const VisitStore* visits) {
  if (node->untried.empty()) return node;
  // Assign priors for remaining untried from this state's empirical policy.
  uint64_t h = hash_position(node->state, *ctx.board);
  std::vector<Action> all = node->untried;
  for (const auto& ch : node->children) all.push_back(ch->action_from_parent);

  Action act = node->untried.back();
  node->untried.pop_back();
  auto child = std::make_unique<Node>();
  child->state = node->state;
  apply_action(ctx, child->state, act);
  child->action_from_parent = act;
  child->parent = node;
  child->to_move = child->state.current;
  child->prior = move_prior(visits, h, act, all);
  if (act.type == ActionType::PlaceRobber || act.type == ActionType::PlayKnight) {
    int me = node->state.current;
    if (robber_hits_self(ctx, node->state, me, act.a)) {
      child->prior = 1e-9;  // never explore self-block hexes
    } else {
      child->prior += 0.35 * std::max(0.0, robber_hex_score(ctx, node->state, me, act.a));
      if (act.b >= 0 && act.b < kNumPlayers)
        child->prior += 0.15 * total_vp(node->state, *ctx.topo, act.b);
    }
  }
  if (act.type == ActionType::MaritimeTrade) {
    if (maritime_trade_score(node->state, node->state.current, act.a, act.b, act.c) < -0.5)
      child->prior = 1e-9;
  }
  if (!child->state.game_over) {
    child->untried = legal_actions(ctx, child->state);
  }
  Node* raw = child.get();
  node->children.push_back(std::move(child));
  return raw;
}

double rollout(const RuleCtx& ctx, GameState s, int perspective, int depth, std::mt19937& rng) {
  RuleCtx local = ctx;
  for (int d = 0; d < depth && !s.game_over; ++d) {
    auto acts = legal_actions(local, s);
    if (acts.empty()) break;
    Action pick = acts[0];
    double best = -1e99;
    int me = s.current;
    int vp0 = total_vp(s, *ctx.topo, me);
    StrategyStyle style = infer_strategy(local, s, me);
    for (const auto& a : acts) {
      GameState n = s;
      apply_action(local, n, a);
      double v = evaluate(local, n, me);
      v += 0.5 * (total_vp(n, *ctx.topo, me) - vp0);
      v += strategy_action_bonus(local, s, a, me, style);
      v += (static_cast<int>(rng() % 100) - 50) * 0.001;
      if (v > best) {
        best = v;
        pick = a;
      }
    }
    apply_action(local, s, pick);
  }
  return evaluate(ctx, s, perspective);
}

void backup(Node* node, double value) {
  while (node) {
    node->visits++;
    node->value_sum += value;
    node = node->parent;
  }
}

int action_bucket(const Action& a) {
  switch (a.type) {
    case ActionType::MaritimeTrade: return 1;
    case ActionType::BuildRoad: return 2;
    case ActionType::BuildSettlement: return 3;
    case ActionType::BuildCity: return 4;
    case ActionType::BuyDev: return 5;
    case ActionType::PlayKnight:
    case ActionType::PlayMonopoly:
    case ActionType::PlayYearOfPlenty:
    case ActionType::PlayRoadBuilding: return 6;
    case ActionType::EndTurn: return 7;
    case ActionType::Roll: return 8;
    case ActionType::PlaceRobber: return 9;
    case ActionType::Discard: return 10;
  }
  return 0;
}

std::vector<RankedAction> diversify_top(std::vector<RankedAction> ranked, int k) {
  if (ranked.empty() || k <= 0) return {};
  std::sort(ranked.begin(), ranked.end(), [](const RankedAction& a, const RankedAction& b) {
    if (a.visits != b.visits) return a.visits > b.visits;
    return a.value > b.value;
  });

  std::vector<RankedAction> out;
  std::array<uint8_t, 16> used{};
  // Pass 1: one best move per action family (trade vs road vs settle…).
  for (const auto& r : ranked) {
    if (static_cast<int>(out.size()) >= k) break;
    int b = action_bucket(r.action);
    if (b >= 0 && b < 16 && used[b]) continue;
    if (b >= 0 && b < 16) used[b] = 1;
    out.push_back(r);
  }
  // Pass 2: fill remaining slots with next-best (may repeat a family).
  for (const auto& r : ranked) {
    if (static_cast<int>(out.size()) >= k) break;
    bool dup = false;
    for (const auto& o : out) {
      if (o.action.type == r.action.type && o.action.a == r.action.a && o.action.b == r.action.b &&
          o.action.c == r.action.c) {
        dup = true;
        break;
      }
    }
    if (!dup) out.push_back(r);
  }
  return out;
}

// Build top-K by mixing three strategy lenses so recommendations aren't always the same line.
std::vector<RankedAction> rank_from_root(Node& root_node, const RuleCtx& ctx, const GameState& root,
                                         int perspective, int k) {
  StrategyStyle primary = infer_strategy(ctx, root, perspective);
  StrategyStyle styles[3] = {StrategyStyle::OwsCities, StrategyStyle::WoodBrickRoad,
                             StrategyStyle::Balanced};
  // Put inferred style first.
  for (int i = 0; i < 3; ++i) {
    if (styles[i] == primary) std::swap(styles[0], styles[i]);
  }

  const bool robber_phase =
      root.phase == Phase::RobberMove ||
      (!root_node.children.empty() &&
       (root_node.children[0]->action_from_parent.type == ActionType::PlaceRobber ||
        root_node.children[0]->action_from_parent.type == ActionType::PlayKnight));

  // Prefer enemy-only robber hexes whenever any exist.
  bool any_enemy_robber = false;
  for (auto& ch : root_node.children) {
    const auto& a = ch->action_from_parent;
    if ((a.type == ActionType::PlaceRobber || a.type == ActionType::PlayKnight) &&
        !robber_hits_self(ctx, root, perspective, a.a) &&
        robber_hex_score(ctx, root, perspective, a.a) > -1e8) {
      any_enemy_robber = true;
      break;
    }
  }

  std::vector<RankedAction> pool;
  pool.reserve(root_node.children.size());
  for (auto& ch : root_node.children) {
    const auto& act = ch->action_from_parent;
    if ((act.type == ActionType::PlaceRobber || act.type == ActionType::PlayKnight) &&
        any_enemy_robber && robber_hits_self(ctx, root, perspective, act.a)) {
      continue;  // automatic no — don't even list self-hexes
    }
    // Automatic no — purposeless bank trades must not appear in top-3.
    // Skip only bad dumps; keep clear-goal (-0.3) and 8+ hand dumps (0).
    if (act.type == ActionType::MaritimeTrade &&
        maritime_trade_score(root, perspective, act.a, act.b, act.c) < -0.5) {
      continue;
    }
    RankedAction r;
    r.action = act;
    r.visits = ch->visits;
    r.value = ch->visits ? ch->value_sum / ch->visits : -1e9;
    r.value += 0.04 * (((ch->visits * 17 + perspective * 3) % 21) - 10) / 10.0;
    r.value += 0.08 * strategy_action_bonus(ctx, root, r.action, perspective, primary);
    if (r.action.type == ActionType::PlaceRobber || r.action.type == ActionType::PlayKnight) {
      // Robber decisions are tactical — MCTS visit concentration on no-steal hexes is misleading.
      double rs = robber_hex_score(ctx, root, perspective, r.action.a);
      r.value = rs;
      if (r.action.b >= 0 && r.action.b < kNumPlayers)
        r.value += 0.55 * total_vp(root, *ctx.topo, r.action.b);
      // Keep a little search signal as tie-break only.
      r.value += 0.02 * (ch->visits ? ch->value_sum / ch->visits : 0.0);
    }
    r.label = strategy_name(primary);
    pool.push_back(r);
  }

  if (robber_phase && !pool.empty()) {
    std::sort(pool.begin(), pool.end(),
              [](const RankedAction& a, const RankedAction& b) { return a.value > b.value; });
    if (static_cast<int>(pool.size()) > k) pool.resize(k);
    // Fake visit shares from relative score so confidence isn't 98% on a garbage hex.
    double sum = 0;
    for (auto& r : pool) sum += std::max(0.05, r.value + 5.0);
    int visit_budget = 100;
    for (auto& r : pool) {
      r.visits = std::max(1, static_cast<int>(visit_budget * std::max(0.05, r.value + 5.0) / sum));
    }
    return pool;
  }

  bool has_productive = false;
  for (const auto& r : pool) {
    const auto& a = r.action;
    if (a.type == ActionType::BuildRoad || a.type == ActionType::BuildSettlement ||
        a.type == ActionType::BuildCity || a.type == ActionType::BuyDev ||
        a.type == ActionType::PlayKnight || a.type == ActionType::PlayMonopoly ||
        a.type == ActionType::PlayYearOfPlenty || a.type == ActionType::PlayRoadBuilding ||
        a.type == ActionType::PlaceRobber) {
      has_productive = true;
      break;
    }
  }
  if (has_productive) {
    for (auto& r : pool) {
      if (r.action.type == ActionType::EndTurn) r.visits = std::max(0, r.visits / 3);
    }
  } else {
    for (auto& r : pool) {
      if (r.action.type == ActionType::EndTurn) r.value += 0.15;
    }
  }

  // For each style, pick its favorite unused action → naturally different lines of play.
  std::vector<RankedAction> out;
  std::array<uint8_t, 16> used_bucket{};
  for (int si = 0; si < 3 && static_cast<int>(out.size()) < k; ++si) {
    StrategyStyle st = styles[si];
    int best_i = -1;
    double best_v = -1e99;
    for (size_t i = 0; i < pool.size(); ++i) {
      const auto& r = pool[i];
      bool taken = false;
      for (const auto& o : out) {
        if (o.action.type == r.action.type && o.action.a == r.action.a && o.action.b == r.action.b &&
            o.action.c == r.action.c) {
          taken = true;
          break;
        }
      }
      if (taken) continue;
      int b = action_bucket(r.action);
      if (si > 0 && b >= 0 && b < 16 && used_bucket[b]) continue;
      double v = r.value + 0.55 * strategy_action_bonus(ctx, root, r.action, perspective, st);
      if (v > best_v) {
        best_v = v;
        best_i = static_cast<int>(i);
      }
    }
    if (best_i < 0) continue;
    RankedAction pick = pool[best_i];
    pick.label = strategy_name(st);
    pick.value = best_v;
    int b = action_bucket(pick.action);
    if (b >= 0 && b < 16) used_bucket[b] = 1;
    out.push_back(pick);
  }

  if (static_cast<int>(out.size()) < k) {
    auto rest = diversify_top(pool, k);
    for (const auto& r : rest) {
      if (static_cast<int>(out.size()) >= k) break;
      bool taken = false;
      for (const auto& o : out) {
        if (o.action.type == r.action.type && o.action.a == r.action.a && o.action.b == r.action.b &&
            o.action.c == r.action.c) {
          taken = true;
          break;
        }
      }
      if (!taken) out.push_back(r);
    }
  }
  return out;
}

}  // namespace

std::vector<RankedAction> search_top_actions(const RuleCtx& ctx, const GameState& root,
                                             int perspective, const MCTSConfig& cfg, int k) {
  Node root_node;
  root_node.state = root;
  root_node.to_move = root.current;
  root_node.untried = legal_actions(ctx, root);
  if (root_node.untried.empty()) return {};

  std::mt19937 rng(root.rng ^ 0x9E3779B9u);
  for (int i = 0; i < cfg.simulations; ++i) {
    Node* node = select(&root_node, cfg.c_puct);
    Node* leaf = expand(ctx, node, cfg.visits);
    double v = rollout(ctx, leaf->state, perspective, cfg.rollout_depth, rng);
    // Negamax-ish: value is always from root perspective already via evaluate(..., perspective).
    backup(leaf, v);
  }

  StrategyStyle style = infer_strategy(ctx, root, perspective);
  auto acts = legal_actions(ctx, root);
  uint64_t root_h = hash_position(root, *ctx.board);
  for (const auto& a : acts) {
    bool seen = false;
    for (auto& ch : root_node.children) {
      if (ch->action_from_parent.type == a.type && ch->action_from_parent.a == a.a &&
          ch->action_from_parent.b == a.b && ch->action_from_parent.c == a.c) {
        seen = true;
        break;
      }
    }
    if (seen) continue;
    GameState n = root;
    apply_action(ctx, n, a);
    auto child = std::make_unique<Node>();
    child->state = n;
    child->action_from_parent = a;
    child->parent = &root_node;
    child->prior = move_prior(cfg.visits, root_h, a, acts);
    child->visits = 1;
    // Warm-start with value net + policy prior bonus (past ML moves).
    child->value_sum = evaluate(ctx, n, perspective) +
                       strategy_action_bonus(ctx, root, a, perspective, style) +
                       0.15 * std::log1p(child->prior * 40.0);
    root_node.children.push_back(std::move(child));
  }

  return rank_from_root(root_node, ctx, root, perspective, k);
}

MCTSResult search_best_action(const RuleCtx& ctx, const GameState& root, int perspective,
                              const MCTSConfig& cfg) {
  auto top = search_top_actions(ctx, root, perspective, cfg, 1);
  if (top.empty()) return MCTSResult{Action{ActionType::EndTurn, 0, 0, 0, {}}, 0, 0};
  return MCTSResult{top[0].action, top[0].value, top[0].visits};
}

}  // namespace catan
