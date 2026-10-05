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

struct DocumentModel {
    std::unordered_map<std::string, VisualStyle> styles;
    std::unordered_map<std::string, XmlNode> components;
    std::unordered_map<std::string, std::string> tokens;
    std::unordered_set<std::string> removedTokens;
    std::vector<AnimationSpec> animations;
    std::vector<std::pair<std::string, std::string>> states;
};

bool PrepareDocument(XmlNode& root, DocumentModel& model, std::vector<Diagnostic>& diagnostics);
MotionDecl MotionFromAnimation(const AnimationSpec& animation);

}  // namespace Runtime
