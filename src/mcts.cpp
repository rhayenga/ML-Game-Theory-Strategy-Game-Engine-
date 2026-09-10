#include "catan/mcts.hpp"

#include "catan/strategy.hpp"

#include <algorithm>
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
  int to_move = 0;
};

double ucb(const Node& child, int parent_visits, double c) {
  if (child.visits == 0) return std::numeric_limits<double>::infinity();
  double q = child.value_sum / child.visits;
  return q + c * std::sqrt(std::log(parent_visits + 1) / child.visits);
}

Node* select(Node* node, double c) {
  while (!node->children.empty() && node->untried.empty()) {
    Node* best = nullptr;
    double best_u = -1e99;
    for (auto& ch : node->children) {
      double u = ucb(*ch, node->visits, c);
      if (u > best_u) {
        best_u = u;
        best = ch.get();
      }
    }
    node = best;
  }
  return node;
}

Node* expand(const RuleCtx& ctx, Node* node) {
  if (node->untried.empty()) return node;
  Action act = node->untried.back();
  node->untried.pop_back();
  auto child = std::make_unique<Node>();
  child->state = node->state;
  apply_action(ctx, child->state, act);
  child->action_from_parent = act;
  child->parent = node;
  child->to_move = child->state.current;
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

std::vector<RankedAction> rank_from_root(Node& root_node, const RuleCtx& ctx, const GameState& root,
                                         int perspective, int k) {
  StrategyStyle style = infer_strategy(ctx, root, perspective);
  std::vector<RankedAction> ranked;
  ranked.reserve(root_node.children.size());
  for (auto& ch : root_node.children) {
    RankedAction r;
    r.action = ch->action_from_parent;
    r.visits = ch->visits;
    r.value = ch->visits ? ch->value_sum / ch->visits : -1e9;
    r.value += 0.05 * strategy_action_bonus(ctx, root, r.action, perspective, style);
    r.label = strategy_name(style);
    ranked.push_back(r);
  }

  bool has_real = false;
  for (const auto& r : ranked) {
    if (r.action.type != ActionType::EndTurn) {
      has_real = true;
      break;
    }
  }
  if (has_real) {
    ranked.erase(std::remove_if(ranked.begin(), ranked.end(),
                                [](const RankedAction& r) {
                                  return r.action.type == ActionType::EndTurn;
                                }),
                 ranked.end());
  }

  std::sort(ranked.begin(), ranked.end(), [](const RankedAction& a, const RankedAction& b) {
    if (a.visits != b.visits) return a.visits > b.visits;
    return a.value > b.value;
  });
  if (static_cast<int>(ranked.size()) > k) ranked.resize(k);
  return ranked;
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
    Node* leaf = expand(ctx, node);
    double v = rollout(ctx, leaf->state, perspective, cfg.rollout_depth, rng);
    backup(leaf, v);
  }

  StrategyStyle style = infer_strategy(ctx, root, perspective);
  auto acts = legal_actions(ctx, root);
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
    child->visits = 1;
    child->value_sum =
        evaluate(ctx, n, perspective) + strategy_action_bonus(ctx, root, a, perspective, style);
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
