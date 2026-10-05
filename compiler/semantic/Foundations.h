#pragma once

#include <string>
#include <unordered_map>
#include <unordered_set>

namespace Runtime {

enum class FoundationGroup {
    COLOR,
    TYPE,
    SPACING,
    RADIUS,
    LAYOUT,
};

struct FoundationToken {
    FoundationGroup group;
    const char* name;
    const char* value;
    const char* note;
};

const char* FoundationGroupElement(FoundationGroup group);
const char* FoundationGroupLabel(FoundationGroup group);
const FoundationToken* FoundationTokens(int& count);
const char* FoundationValue(const std::string& key);
bool FoundationSuppressed(const std::string& key);
void SetSuppressedFoundations(const std::unordered_set<std::string>* removed);
void MergeFoundationTokens(std::unordered_map<std::string, std::string>& tokens, const std::unordered_set<std::string>& removed);

}  // namespace Runtime
