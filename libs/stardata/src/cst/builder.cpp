// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Adrian Welcker
#include "stardata/cst/builder.hpp"

#include <cassert>
#include <utility>

namespace stardata::cst {

void GreenBuilder::start_node(SyntaxKind kind) {
    open_.push_back(Open{kind, children_.size()});
}

void GreenBuilder::start_node_at(std::size_t checkpoint, SyntaxKind kind) {
    assert((open_.empty() || checkpoint >= open_.back().first_child) &&
           "a checkpoint cannot reach back past the node currently open");
    open_.push_back(Open{kind, checkpoint});
}

void GreenBuilder::token(SyntaxKind kind, std::string_view text) {
    children_.emplace_back(cache_.token(kind, text));
}

void GreenBuilder::finish_node() {
    assert(!open_.empty() && "finish_node without a matching start_node");
    const Open open = open_.back();
    open_.pop_back();

    std::vector<GreenElement> taken(
        std::make_move_iterator(children_.begin() + static_cast<std::ptrdiff_t>(open.first_child)),
        std::make_move_iterator(children_.end()));
    children_.erase(children_.begin() + static_cast<std::ptrdiff_t>(open.first_child),
                    children_.end());
    children_.emplace_back(cache_.node(open.kind, std::move(taken)));
}

GreenNodePtr GreenBuilder::finish() {
    assert(open_.empty() && "finish() with a node still open");
    assert(children_.size() == 1 && "finish() expects exactly one root");
    GreenNodePtr root = children_.front().node_ptr();
    children_.clear();
    return root;
}

} // namespace stardata::cst
