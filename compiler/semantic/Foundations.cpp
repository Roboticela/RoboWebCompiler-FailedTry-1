#include "compiler/semantic/Foundations.h"

#include <string_view>

namespace Runtime {
namespace {

struct Alias {
    const char* key;
    const char* name;
};

const FoundationToken TOKENS[] = {
    {FoundationGroup::COLOR, "brand-primary", "#0E7AFF", "Main interactive color."},
    {FoundationGroup::COLOR, "brand-primary-hover", "#0A6AE8", "Hover for the primary color."},
    {FoundationGroup::COLOR, "brand-primary-active", "#0757C7", "Pressed primary color."},
    {FoundationGroup::COLOR, "brand-primary-disabled", "#B8D9FF", "Disabled primary color."},
    {FoundationGroup::COLOR, "brand-primary-subtle", "#0E7AFF1A", "Primary tint at 10%."},
    {FoundationGroup::COLOR, "accent-teal", "#14B8A6", "Success, verified, and active status."},
    {FoundationGroup::COLOR, "accent-teal-subtle", "#14B8A61A", "Teal tint at 10%."},
    {FoundationGroup::COLOR, "success", "#30D158", "Positive status."},
    {FoundationGroup::COLOR, "success-subtle", "#30D1581A", "Success tint at 10%."},
    {FoundationGroup::COLOR, "warning", "#FF9F0A", "Caution status."},
    {FoundationGroup::COLOR, "warning-subtle", "#FF9F0A1A", "Warning tint at 10%."},
    {FoundationGroup::COLOR, "danger", "#FF3B30", "Errors and destructive actions."},
    {FoundationGroup::COLOR, "danger-hover", "#E0342A", "Hover for danger."},
    {FoundationGroup::COLOR, "danger-subtle", "#FF3B301A", "Danger tint at 10%."},
    {FoundationGroup::COLOR, "text-primary", "#1D1D1F", "Headlines."},
    {FoundationGroup::COLOR, "text-secondary", "#6E6E73", "Body copy."},
    {FoundationGroup::COLOR, "text-tertiary", "#86868B", "Captions and hints."},
    {FoundationGroup::COLOR, "text-disabled", "#C7C7CC", "Unavailable text."},
    {FoundationGroup::COLOR, "text-on-brand", "#FFFFFF", "Text on the primary color."},
    {FoundationGroup::COLOR, "text-link", "#0E7AFF", "Inline links."},
    {FoundationGroup::COLOR, "bg-primary", "#FFFFFF", "Page background."},
    {FoundationGroup::COLOR, "bg-secondary", "#F5F5F7", "Grouped surface."},
    {FoundationGroup::COLOR, "bg-tertiary", "#EDEDEF", "Inset surface."},
    {FoundationGroup::COLOR, "bg-elevated", "#FFFFFF", "Raised surface."},
    {FoundationGroup::COLOR, "bg-hover", "#F5F5F7", "Hover surface."},
    {FoundationGroup::COLOR, "bg-active", "#EDEDEF", "Pressed surface."},
    {FoundationGroup::COLOR, "border-default", "#D2D2D7", "Standard border."},
    {FoundationGroup::COLOR, "border-subtle", "#E5E5E7", "Quiet separator."},
    {FoundationGroup::COLOR, "border-strong", "#B8B8BD", "Prominent border."},
    {FoundationGroup::COLOR, "border-focus", "#0E7AFF", "Keyboard focus."},
    {FoundationGroup::COLOR, "border-danger", "#FF3B304D", "Danger border at 30%."},

    {FoundationGroup::TYPE, "type-display", "80", "Hero. 40px below 735px."},
    {FoundationGroup::TYPE, "type-display-mobile", "40", "Display size on a phone."},
    {FoundationGroup::TYPE, "type-h1", "56", "Page title. 32px below 735px."},
    {FoundationGroup::TYPE, "type-h1-mobile", "32", "Page title on a phone."},
    {FoundationGroup::TYPE, "type-h2", "40", "Section title. 28px below 735px."},
    {FoundationGroup::TYPE, "type-h2-mobile", "28", "Section title on a phone."},
    {FoundationGroup::TYPE, "type-h3", "28", "Card title. 21px below 735px."},
    {FoundationGroup::TYPE, "type-h3-mobile", "21", "Card title on a phone."},
    {FoundationGroup::TYPE, "type-subhead", "24", "Line under a heading. 19px below 735px."},
    {FoundationGroup::TYPE, "type-subhead-mobile", "19", "Subhead on a phone."},
    {FoundationGroup::TYPE, "type-content", "19", "Reading size. 17px below 735px."},
    {FoundationGroup::TYPE, "type-content-mobile", "17", "Reading size on a phone."},
    {FoundationGroup::TYPE, "type-content-sm", "17", "Dense supporting copy."},
    {FoundationGroup::TYPE, "type-caption", "12", "Meta, timestamps, hints."},
    {FoundationGroup::TYPE, "type-eyebrow", "12", "Short label above a heading."},
    {FoundationGroup::TYPE, "type-label", "17", "Controls and fields."},
    {FoundationGroup::TYPE, "weight-regular", "400", "Subhead, content, caption, eyebrow."},
    {FoundationGroup::TYPE, "weight-medium", "500", "Labels."},
    {FoundationGroup::TYPE, "weight-semibold", "600", "Display and headings."},
    {FoundationGroup::TYPE, "tracking-display", "-0.015", "Display and heading 1."},
    {FoundationGroup::TYPE, "tracking-h1", "-0.015", "Page title."},
    {FoundationGroup::TYPE, "tracking-h2", "-0.01", "Section title."},
    {FoundationGroup::TYPE, "tracking-h3", "-0.005", "Card title."},
    {FoundationGroup::TYPE, "tracking-eyebrow", "0.05", "Uppercase eyebrow."},
    {FoundationGroup::TYPE, "leading-display", "1.05", "Display line height."},
    {FoundationGroup::TYPE, "leading-h1", "1.07", "Heading 1 line height."},
    {FoundationGroup::TYPE, "leading-h2", "1.1", "Heading 2 line height."},
    {FoundationGroup::TYPE, "leading-h3", "1.14", "Heading 3 line height."},
    {FoundationGroup::TYPE, "leading-subhead", "1.33", "Subhead line height."},
    {FoundationGroup::TYPE, "leading-content", "1.42", "Reading line height from 735px."},
    {FoundationGroup::TYPE, "leading-content-mobile", "1.47", "Reading line height on a phone."},
    {FoundationGroup::TYPE, "leading-content-sm", "1.47", "Dense copy line height."},
    {FoundationGroup::TYPE, "leading-caption", "1.33", "Caption line height."},
    {FoundationGroup::TYPE, "leading-eyebrow", "1.33", "Eyebrow line height."},
    {FoundationGroup::TYPE, "leading-label", "1", "Label line height."},

    {FoundationGroup::SPACING, "space-1", "4", "Icon padding, hairline gaps."},
    {FoundationGroup::SPACING, "space-2", "8", "Inline icon-to-label."},
    {FoundationGroup::SPACING, "space-3", "12", "Compact stacks."},
    {FoundationGroup::SPACING, "space-4", "16", "Field padding, mobile gutters."},
    {FoundationGroup::SPACING, "space-5", "24", "Desktop gutters, related blocks."},
    {FoundationGroup::SPACING, "space-6", "32", "Card padding."},
    {FoundationGroup::SPACING, "space-7", "48", "Grouped sections."},
    {FoundationGroup::SPACING, "space-8", "64", "Large section gaps."},
    {FoundationGroup::SPACING, "space-9", "96", "Marketing breathing room."},
    {FoundationGroup::SPACING, "space-10", "128", "Hero isolation."},

    {FoundationGroup::RADIUS, "radius-sm", "6", "Small chrome."},
    {FoundationGroup::RADIUS, "radius-md", "8", "Text fields, compact menus, 44px inputs."},
    {FoundationGroup::RADIUS, "radius-lg", "12", "Small cards, grouped lists, docs panels."},
    {FoundationGroup::RADIUS, "radius-xl", "16", "Large compact surfaces."},
    {FoundationGroup::RADIUS, "radius-2xl", "20", "Wide rounded surfaces."},
    {FoundationGroup::RADIUS, "radius-panel", "18", "Large cards, images, and dialogs."},
    {FoundationGroup::RADIUS, "radius-pill", "980", "Buttons, switches, and chips."},

    {FoundationGroup::LAYOUT, "breakpoint-mobile", "734", "Phone, up to 734px. One column."},
    {FoundationGroup::LAYOUT, "breakpoint-tablet", "1068", "Tablet, 735–1068px."},
    {FoundationGroup::LAYOUT, "breakpoint-desktop", "1069", "Desktop, 1069px and up."},
    {FoundationGroup::LAYOUT, "pad-mobile", "22", "Page padding on a phone."},
    {FoundationGroup::LAYOUT, "pad-tablet", "40", "Page padding on a tablet."},
    {FoundationGroup::LAYOUT, "pad-desktop", "80", "Page padding cap on a desktop."},
    {FoundationGroup::LAYOUT, "gutter-mobile", "16", "Column gutter on a phone."},
    {FoundationGroup::LAYOUT, "gutter", "24", "Column gutter from tablet up."},
    {FoundationGroup::LAYOUT, "columns-mobile", "1", "Columns on a phone."},
    {FoundationGroup::LAYOUT, "columns", "12", "Columns from tablet up."},
    {FoundationGroup::LAYOUT, "width-copy", "692", "Single-column copy."},
    {FoundationGroup::LAYOUT, "width-docs", "760", "Docs article."},
    {FoundationGroup::LAYOUT, "width-wide", "900", "Wide docs."},
    {FoundationGroup::LAYOUT, "width-marketing", "980", "Text-focused marketing."},
    {FoundationGroup::LAYOUT, "width-section", "1024", "Wide sections."},
};

const Alias ALIASES[] = {
    {"color.primary", "brand-primary"},
    {"color.brand", "brand-primary"},
    {"color.focus", "brand-primary"},
    {"color.info", "brand-primary"},
    {"color.primaryHover", "brand-primary-hover"},
    {"color.brandHover", "brand-primary-hover"},
    {"color.primaryActive", "brand-primary-active"},
    {"color.brandActive", "brand-primary-active"},
    {"color.onPrimary", "text-on-brand"},
    {"color.onBrand", "text-on-brand"},
    {"color.secondary", "text-secondary"},
    {"color.muted", "text-secondary"},
    {"color.textSecondary", "text-secondary"},
    {"color.foreground", "text-primary"},
    {"color.text", "text-primary"},
    {"color.textTertiary", "text-tertiary"},
    {"color.background", "bg-primary"},
    {"color.surface", "bg-secondary"},
    {"color.surfaceVariant", "bg-tertiary"},
    {"color.border", "border-default"},
    {"color.borderStrong", "border-strong"},
    {"space.xs", "space-1"},
    {"space.1", "space-1"},
    {"space.sm", "space-2"},
    {"space.2", "space-2"},
    {"space.md", "space-3"},
    {"space.3", "space-3"},
    {"space.lg", "space-4"},
    {"space.4", "space-4"},
    {"space.xl", "space-5"},
    {"space.5", "space-5"},
    {"space.2xl", "space-6"},
    {"space.6", "space-6"},
    {"space.3xl", "space-7"},
    {"space.7", "space-7"},
    {"radius.sm", "radius-sm"},
    {"radius.md", "radius-md"},
    {"radius.lg", "radius-lg"},
    {"radius.xl", "radius-xl"},
    {"radius.pill", "radius-pill"},
    {"radius.circle", "radius-pill"},
    {"type.display", "type-display"},
    {"type.h1", "type-h1"},
    {"type.heading1", "type-h1"},
    {"type.h2", "type-h2"},
    {"type.heading2", "type-h2"},
    {"type.h3", "type-h3"},
    {"type.heading3", "type-h3"},
    {"type.title", "type-subhead"},
    {"type.subhead", "type-subhead"},
    {"type.caption", "type-caption"},
    {"type.code", "type-caption"},
    {"weight.regular", "weight-regular"},
    {"weight.medium", "weight-medium"},
    {"weight.semibold", "weight-semibold"},
};

const std::unordered_set<std::string>* suppressed = nullptr;

const FoundationToken* FindToken(std::string_view name) {
    for (const FoundationToken& token : TOKENS) {
        if (name == token.name) {
            return &token;
        }
    }
    return nullptr;
}

const FoundationToken* FindAlias(std::string_view key) {
    for (const Alias& alias : ALIASES) {
        if (key == alias.key) {
            return FindToken(alias.name);
        }
    }
    return nullptr;
}

bool Removed(const FoundationToken& token, const std::unordered_set<std::string>& removed) {
    if (removed.find(token.name) != removed.end()) {
        return true;
    }
    const std::string grouped = std::string(FoundationGroupElement(token.group)) + "." + token.name;
    return removed.find(grouped) != removed.end();
}

}  // namespace

const char* FoundationGroupElement(FoundationGroup group) {
    switch (group) {
        case FoundationGroup::COLOR:
            return "color";
        case FoundationGroup::TYPE:
            return "type";
        case FoundationGroup::SPACING:
            return "spacing";
        case FoundationGroup::RADIUS:
            return "radius";
        case FoundationGroup::LAYOUT:
            return "layout";
    }
    return "color";
}

const char* FoundationGroupLabel(FoundationGroup group) {
    switch (group) {
        case FoundationGroup::COLOR:
            return "Colors";
        case FoundationGroup::TYPE:
            return "Typography";
        case FoundationGroup::SPACING:
            return "Spacing";
        case FoundationGroup::RADIUS:
            return "Radius";
        case FoundationGroup::LAYOUT:
            return "Layout";
    }
    return "Colors";
}

const FoundationToken* FoundationTokens(int& count) {
    count = static_cast<int>(sizeof(TOKENS) / sizeof(TOKENS[0]));
    return TOKENS;
}

const char* FoundationValue(const std::string& key) {
    if (const FoundationToken* token = FindToken(key)) {
        return token->value;
    }
    if (const FoundationToken* token = FindAlias(key)) {
        return token->value;
    }
    return nullptr;
}

bool FoundationSuppressed(const std::string& key) {
    if (suppressed == nullptr) {
        return false;
    }
    if (suppressed->find(key) != suppressed->end()) {
        return true;
    }
    const FoundationToken* token = FindToken(key);
    if (token == nullptr) {
        token = FindAlias(key);
    }
    return token != nullptr && Removed(*token, *suppressed);
}

void SetSuppressedFoundations(const std::unordered_set<std::string>* removed) {
    suppressed = removed;
}

void MergeFoundationTokens(std::unordered_map<std::string, std::string>& tokens, const std::unordered_set<std::string>& removed) {
    int count = 0;
    const FoundationToken* list = FoundationTokens(count);
    for (int index = 0; index < count; ++index) {
        const FoundationToken& token = list[index];
        if (Removed(token, removed)) {
            continue;
        }
        const std::string grouped = std::string(FoundationGroupElement(token.group)) + "." + token.name;
        if (tokens.find(token.name) == tokens.end()) {
            tokens.emplace(token.name, token.value);
        }
        if (tokens.find(grouped) == tokens.end()) {
            tokens.emplace(grouped, token.value);
        }
    }
    for (const Alias& alias : ALIASES) {
        const FoundationToken* token = FindToken(alias.name);
        if (token == nullptr || Removed(*token, removed) || tokens.find(alias.key) != tokens.end()) {
            continue;
        }
        tokens.emplace(alias.key, token->value);
    }
}

}  // namespace Runtime
