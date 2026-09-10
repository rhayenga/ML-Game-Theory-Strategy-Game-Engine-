#pragma once

#include "catan/board.hpp"
#include "catan/rules.hpp"
#include "catan/state.hpp"
#include "catan/topology.hpp"

#include <string>

namespace catan {

std::string explain_action(const Action& act);
std::string action_type_name(ActionType t);
bool action_type_from_name(const std::string& name, ActionType& out);

// Dump full UI state as a JSON object string (no outer whitespace dependency).
std::string state_to_json(const RuleCtx& ctx, const GameState& s, int you);

}  // namespace catan
