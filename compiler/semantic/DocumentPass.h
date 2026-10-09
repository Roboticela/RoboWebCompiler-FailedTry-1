#pragma once

#include "compiler/ir/UiIr.h"
#include "engine/theme/VisualStyle.h"
#include "xml/ast/XmlAst.h"

#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace Runtime {

struct AnimationSpec {
    std::string name;
    std::string target;
    std::string property;
    std::string from;
    std::string to;
    std::string duration;
    std::string easing;
    std::string trigger;
    std::string mass;
    std::string stiffness;
    std::string damping;
    std::string velocity;
    std::string sequence;
    std::string parallel;
    std::string stagger;
    std::vector<std::pair<std::string, std::string>> keyframes;
};

// What a definition offers to the copies placed from it: the layers a copy may replace, and the
// places a copy may fill. A copy names each one by the name it was written with, so both are kept
// as the set of names a child of a copy may say.
struct ComponentTargets {
    std::unordered_set<std::string> layers;
    std::unordered_set<std::string> slots;
};

struct DocumentModel {
    std::unordered_map<std::string, VisualStyle> styles;
    // Keyed by the name a definition answers to, and by that name with the variant it draws
    // appended when it draws one: several definitions of one name are the variants of one component,
    // so the name alone cannot say which one a usage means.
    std::unordered_map<std::string, XmlNode> components;
    // Every key a definition was written under, so a definition written twice is reported rather than
    // silently replacing the one before it. It is kept apart from `components` because that table
    // also holds the fallback entry a variant writes under its plain name.
    std::unordered_set<std::string> declaredComponents;
    // Keyed by name alone, gathering every variant of one component, because which variant a copy
    // draws is only known once the copy is placed.
    std::unordered_map<std::string, ComponentTargets> componentTargets;
    // The ids the screens of the document answer to: the windows and boards it holds. A link that
    // names a screen is checked against these, because a link to nowhere is a shape that does
    // nothing rather than a link.
    std::unordered_set<std::string> screenIds;
    std::unordered_map<std::string, std::string> tokens;
    std::unordered_set<std::string> removedTokens;
    std::vector<AnimationSpec> animations;
    std::vector<std::pair<std::string, std::string>> states;
};

bool PrepareDocument(XmlNode& root, DocumentModel& model, std::vector<Diagnostic>& diagnostics);
MotionDecl MotionFromAnimation(const AnimationSpec& animation);

}  // namespace Runtime
