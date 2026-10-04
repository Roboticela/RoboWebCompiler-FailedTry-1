#include "compiler/semantic/ComponentCatalog.h"

#include <initializer_list>

namespace Runtime {
namespace {

std::vector<PropertySpec> SharedBoxProperties() {
    return {
        {"id", TypeKind::STRING},
        {"width", TypeKind::LENGTH},
        {"height", TypeKind::LENGTH},
        {"minWidth", TypeKind::LENGTH},
        {"maxWidth", TypeKind::LENGTH},
        {"minHeight", TypeKind::LENGTH},
        {"maxHeight", TypeKind::LENGTH},
        {"margin", TypeKind::EDGES},
        {"opacity", TypeKind::FLOAT},
        {"visible", TypeKind::BOOL},
        {"visibility", TypeKind::STRING},
        {"background", TypeKind::COLOR},
        {"borderWidth", TypeKind::LENGTH},
        {"borderColor", TypeKind::COLOR},
        {"cornerRadius", TypeKind::LENGTH},
        {"x", TypeKind::LENGTH},
        {"y", TypeKind::LENGTH},
        {"zIndex", TypeKind::INT},
        {"style", TypeKind::STRING},
        {"grow", TypeKind::FLOAT},
        {"tip", TypeKind::STRING},
        {"hiddenBelow", TypeKind::FLOAT},
        {"columnSpan", TypeKind::INT},
        {"accessibilityLabel", TypeKind::STRING},
        {"accessibilityRole", TypeKind::STRING},
    };
}

std::vector<PropertySpec> With(std::vector<PropertySpec> properties, std::initializer_list<PropertySpec> extra) {
    properties.insert(properties.end(), extra.begin(), extra.end());
    return properties;
}

std::vector<PropertySpec> FlowProperties() {
    return With(SharedBoxProperties(),
                {{"padding", TypeKind::EDGES},
                 {"spacing", TypeKind::LENGTH},
                 {"gap", TypeKind::LENGTH},
                 {"alignment", TypeKind::ALIGNMENT},
                 {"justify", TypeKind::STRING}});
}

std::vector<PropertySpec> TextProperties() {
    return With(SharedBoxProperties(),
                {{"text", TypeKind::STRING},
                 {"value", TypeKind::STRING},
                 {"role", TypeKind::STRING},
                 {"fontSize", TypeKind::FONT_SIZE},
                 {"fontWeight", TypeKind::INT},
                 {"color", TypeKind::COLOR},
                 {"textAlign", TypeKind::ALIGNMENT},
                 {"verticalAlign", TypeKind::ALIGNMENT}});
}

}  // namespace

const std::vector<ComponentSpec>& ComponentCatalog() {
    static const std::vector<ComponentSpec> catalog = {
        {"window",
         NodeKind::WINDOW,
         true,
         With(SharedBoxProperties(), {{"title", TypeKind::STRING}})},
        {"column", NodeKind::COLUMN, true, FlowProperties()},
        {"row", NodeKind::ROW, true, FlowProperties()},
        {"flex",
         NodeKind::FLEX,
         true,
         With(SharedBoxProperties(),
              {{"direction", TypeKind::STRING},
               {"justify", TypeKind::STRING},
               {"alignment", TypeKind::ALIGNMENT},
               {"wrap", TypeKind::STRING},
               {"padding", TypeKind::EDGES},
               {"gap", TypeKind::LENGTH}})},
        {"grid",
         NodeKind::GRID,
         true,
         With(SharedBoxProperties(),
              {{"columns", TypeKind::INT},
               {"padding", TypeKind::EDGES},
               {"columnGap", TypeKind::LENGTH},
               {"rowGap", TypeKind::LENGTH}})},
        {"stack", NodeKind::STACK, true, With(SharedBoxProperties(), {{"padding", TypeKind::EDGES}})},
        {"container", NodeKind::CONTAINER, true, FlowProperties()},
        {"wrap", NodeKind::WRAP, true, With(SharedBoxProperties(), {{"gap", TypeKind::LENGTH}, {"padding", TypeKind::EDGES}, {"alignment", TypeKind::ALIGNMENT}})},
        {"overlay", NodeKind::OVERLAY, true, SharedBoxProperties()},
        {"clip", NodeKind::CLIP, true, SharedBoxProperties()},
        {"aspect", NodeKind::ASPECT, true, With(SharedBoxProperties(), {{"ratio", TypeKind::FLOAT}})},
        {"navbar", NodeKind::NAVBAR, true, FlowProperties()},
        {"header", NodeKind::HEADER, true, FlowProperties()},
        {"section", NodeKind::SECTION, true, FlowProperties()},
        {"footer", NodeKind::FOOTER, true, FlowProperties()},
        {"list", NodeKind::LIST, true, FlowProperties()},
        {"listItem", NodeKind::LIST_ITEM, true, With(FlowProperties(), {{"onClick", TypeKind::EVENT_HANDLER}})},
        {"menu", NodeKind::MENU, true, FlowProperties()},
        {"menuItem", NodeKind::MENU_ITEM, false, With(SharedBoxProperties(), {{"text", TypeKind::STRING}, {"onClick", TypeKind::EVENT_HANDLER}})},
        {"dialog", NodeKind::DIALOG, true, SharedBoxProperties()},
        {"form", NodeKind::FORM, true, FlowProperties()},
        {"skeleton", NodeKind::SKELETON, false, With(SharedBoxProperties(), {{"lines", TypeKind::INT}})},
        {"avatar", NodeKind::AVATAR, false, With(SharedBoxProperties(), {{"text", TypeKind::STRING}})},
        {"empty", NodeKind::EMPTY, false, With(SharedBoxProperties(), {{"title", TypeKind::STRING}, {"text", TypeKind::STRING}})},
        {"breadcrumb", NodeKind::BREADCRUMB, true, FlowProperties()},
        {"pagination", NodeKind::PAGINATION, false, With(SharedBoxProperties(), {{"pages", TypeKind::INT}, {"page", TypeKind::INT}})},
        {"banner", NodeKind::ALERT, false, With(SharedBoxProperties(), {{"title", TypeKind::STRING}, {"text", TypeKind::STRING}, {"tone", TypeKind::STRING}})},
        {"searchField",
         NodeKind::TEXT_FIELD,
         false,
         With(SharedBoxProperties(), {{"text", TypeKind::STRING}, {"placeholder", TypeKind::STRING}, {"enabled", TypeKind::BOOL}, {"validation", TypeKind::STRING}})},
        {"scroll",
         NodeKind::SCROLL,
         true,
         With(SharedBoxProperties(), {{"padding", TypeKind::EDGES}, {"spacing", TypeKind::LENGTH}, {"gap", TypeKind::LENGTH}})},
        {"panel", NodeKind::PANEL, true, FlowProperties()},
        {"card", NodeKind::CARD, true, FlowProperties()},
        {"spacer", NodeKind::SPACER, false, SharedBoxProperties()},
        {"divider", NodeKind::DIVIDER, false, With(SharedBoxProperties(), {{"orientation", TypeKind::STRING}, {"color", TypeKind::COLOR}})},
        {"absolute", NodeKind::ABSOLUTE, true, SharedBoxProperties()},
        {"split",
         NodeKind::SPLIT,
         true,
         With(SharedBoxProperties(), {{"direction", TypeKind::STRING}, {"ratio", TypeKind::FLOAT}})},
        {"image", NodeKind::IMAGE, false, With(SharedBoxProperties(), {{"source", TypeKind::STRING}})},
        {"checkbox",
         NodeKind::CHECKBOX,
         false,
         With(SharedBoxProperties(), {{"text", TypeKind::STRING}, {"checked", TypeKind::BOOL}, {"enabled", TypeKind::BOOL}})},
        {"switch",
         NodeKind::SWITCH,
         false,
         With(SharedBoxProperties(), {{"text", TypeKind::STRING}, {"checked", TypeKind::BOOL}, {"enabled", TypeKind::BOOL}})},
        {"radio",
         NodeKind::RADIO,
         false,
         With(SharedBoxProperties(),
              {{"text", TypeKind::STRING}, {"checked", TypeKind::BOOL}, {"enabled", TypeKind::BOOL}, {"name", TypeKind::STRING}})},
        {"slider",
         NodeKind::SLIDER,
         false,
         With(SharedBoxProperties(),
              {{"value", TypeKind::FLOAT}, {"min", TypeKind::FLOAT}, {"max", TypeKind::FLOAT}, {"enabled", TypeKind::BOOL}})},
        {"progress", NodeKind::PROGRESS, false, With(SharedBoxProperties(), {{"value", TypeKind::FLOAT}})},
        {"textField",
         NodeKind::TEXT_FIELD,
         false,
         With(SharedBoxProperties(), {{"text", TypeKind::STRING}, {"placeholder", TypeKind::STRING}, {"enabled", TypeKind::BOOL}, {"validation", TypeKind::STRING}})},
        {"passwordField",
         NodeKind::TEXT_FIELD,
         false,
         With(SharedBoxProperties(), {{"text", TypeKind::STRING}, {"placeholder", TypeKind::STRING}, {"enabled", TypeKind::BOOL}})},
        {"textArea",
         NodeKind::TEXT_FIELD,
         false,
         With(SharedBoxProperties(), {{"text", TypeKind::STRING}, {"placeholder", TypeKind::STRING}, {"enabled", TypeKind::BOOL}})},
        {"numberField",
         NodeKind::TEXT_FIELD,
         false,
         With(SharedBoxProperties(), {{"text", TypeKind::STRING}, {"placeholder", TypeKind::STRING}, {"enabled", TypeKind::BOOL}})},
        {"select", NodeKind::SELECT, true, With(SharedBoxProperties(), {{"placeholder", TypeKind::STRING}})},
        {"option", NodeKind::OPTION, false, With(SharedBoxProperties(), {{"text", TypeKind::STRING}})},
        {"alert",
         NodeKind::ALERT,
         false,
         With(SharedBoxProperties(), {{"title", TypeKind::STRING}, {"text", TypeKind::STRING}, {"tone", TypeKind::STRING}})},
        {"badge", NodeKind::BADGE, false, With(SharedBoxProperties(), {{"text", TypeKind::STRING}, {"tone", TypeKind::STRING}})},
        {"link",
         NodeKind::LINK,
         false,
         With(SharedBoxProperties(), {{"text", TypeKind::STRING}, {"onClick", TypeKind::EVENT_HANDLER}})},
        {"iconButton",
         NodeKind::ICON_BUTTON,
         false,
         With(SharedBoxProperties(),
              {{"text", TypeKind::STRING}, {"variant", TypeKind::STRING}, {"onClick", TypeKind::EVENT_HANDLER}, {"enabled", TypeKind::BOOL}})},
        {"tabs", NodeKind::TABS, true, SharedBoxProperties()},
        {"tab", NodeKind::TAB, true, With(SharedBoxProperties(), {{"title", TypeKind::STRING}})},
        {"text", NodeKind::TEXT, false, TextProperties()},
        {"button",
         NodeKind::BUTTON,
         false,
         With(SharedBoxProperties(),
              {{"text", TypeKind::STRING},
               {"variant", TypeKind::STRING},
               {"size", TypeKind::STRING},
               {"onClick", TypeKind::EVENT_HANDLER},
               {"background", TypeKind::COLOR},
               {"enabled", TypeKind::BOOL},
               {"cornerRadius", TypeKind::LENGTH}})},
    };
    return catalog;
}

const ComponentSpec* FindComponent(std::string_view name) {
    for (const ComponentSpec& spec : ComponentCatalog()) {
        if (name == spec.name) {
            return &spec;
        }
    }
    return nullptr;
}

}  // namespace Runtime
