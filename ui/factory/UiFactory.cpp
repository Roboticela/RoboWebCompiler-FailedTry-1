#include "ui/factory/UiFactory.h"

#include "components/basic/Button.h"
#include "components/basic/TextBlock.h"
#include "components/controls/Controls.h"
#include "components/layout/Foundations.h"
#include "components/layout/Column.h"
#include "components/layout/LayoutBoxes.h"
#include "components/layout/Surfaces.h"
#include "components/layout/WindowElement.h"
#include "compiler/types/ValueParser.h"
#include "engine/core/Log.h"

namespace Runtime {
namespace {

void ApplyBox(Element& element, const IrNode& node) {
    element.SetId(node.id);
    if (const UiValue* width = FindProperty(node, "width")) {
        element.SetWidth(width->length);
    }
    if (const UiValue* height = FindProperty(node, "height")) {
        element.SetHeight(height->length);
    }
    if (const UiValue* zIndex = FindProperty(node, "zIndex")) {
        element.SetZIndex(static_cast<int>(zIndex->number));
    }
    if (const UiValue* minWidth = FindProperty(node, "minWidth")) {
        element.SetMinWidth(minWidth->length);
    }
    if (const UiValue* maxWidth = FindProperty(node, "maxWidth")) {
        element.SetMaxWidth(maxWidth->length);
    }
    if (const UiValue* minHeight = FindProperty(node, "minHeight")) {
        element.SetMinHeight(minHeight->length);
    }
    if (const UiValue* maxHeight = FindProperty(node, "maxHeight")) {
        element.SetMaxHeight(maxHeight->length);
    }
    if (const UiValue* margin = FindProperty(node, "margin")) {
        element.SetMargin(margin->edges);
    }
    if (const UiValue* opacity = FindProperty(node, "opacity")) {
        element.SetOpacity(static_cast<float>(opacity->number));
    }
    if (const UiValue* visible = FindProperty(node, "visible")) {
        element.SetVisible(visible->boolean);
    }
    if (const UiValue* visibility = FindProperty(node, "visibility")) {
        element.SetVisible(visibility->text != "hidden");
    }
    if (const UiValue* background = FindProperty(node, "background")) {
        element.SetChrome(background->color, true);
    }
    if (const UiValue* borderWidth = FindProperty(node, "borderWidth")) {
        const Color border = FindProperty(node, "borderColor") != nullptr ? FindProperty(node, "borderColor")->color : COLOR_BORDER_DEFAULT;
        element.SetBorder(borderWidth->length.value, border);
    } else if (const UiValue* borderColor = FindProperty(node, "borderColor")) {
        element.SetBorder(1.0f, borderColor->color);
    }
    if (const UiValue* radius = FindProperty(node, "cornerRadius")) {
        element.SetChromeRadius(radius->length);
    }
    if (const UiValue* x = FindProperty(node, "x")) {
        element.SetOffsetX(x->length);
    }
    if (const UiValue* y = FindProperty(node, "y")) {
        element.SetOffsetY(y->length);
    }
    if (node.visual.used) {
        element.SetVisualStyle(node.visual);
    }
    if (const UiValue* grow = FindProperty(node, "grow")) {
        element.SetGrow(static_cast<float>(grow->number));
    }
    if (const UiValue* tip = FindProperty(node, "tip")) {
        element.SetTip(tip->text);
    }
    if (const UiValue* hiddenBelow = FindProperty(node, "hiddenBelow")) {
        element.SetHiddenBelow(static_cast<float>(hiddenBelow->number));
    }
    if (const UiValue* span = FindProperty(node, "columnSpan")) {
        element.SetColumnSpan(static_cast<int>(span->number));
    }
    for (const auto& property : node.properties) {
        if (!property.second.binding.empty()) {
            element.AddBinding(property.first, property.second.binding);
        }
    }
}

ButtonVariant ParseVariant(const std::string& name) {
    if (name == "secondary") {
        return ButtonVariant::SECONDARY;
    }
    if (name == "action") {
        return ButtonVariant::ACTION;
    }
    if (name == "ghost") {
        return ButtonVariant::GHOST;
    }
    if (name == "danger") {
        return ButtonVariant::DANGER;
    }
    return ButtonVariant::PRIMARY;
}

ButtonSize ParseButtonSize(const std::string& name) {
    if (name == "sm") {
        return ButtonSize::SM;
    }
    if (name == "lg") {
        return ButtonSize::LG;
    }
    return ButtonSize::MD;
}

TextRole ParseRole(const std::string& name) {
    if (name == "display") {
        return TextRole::DISPLAY;
    }
    if (name == "h1") {
        return TextRole::H1;
    }
    if (name == "h2") {
        return TextRole::H2;
    }
    if (name == "h3") {
        return TextRole::H3;
    }
    if (name == "subhead") {
        return TextRole::SUBHEAD;
    }
    if (name == "caption") {
        return TextRole::CAPTION;
    }
    return TextRole::BODY;
}

JustifyContent ParseJustify(const std::string& name) {
    if (name == "center") {
        return JustifyContent::CENTER;
    }
    if (name == "end") {
        return JustifyContent::END;
    }
    if (name == "spaceBetween") {
        return JustifyContent::SPACE_BETWEEN;
    }
    return JustifyContent::START;
}

void ApplyFlow(Row& row, const IrNode& node) {
    if (const UiValue* padding = FindProperty(node, "padding")) {
        row.SetPadding(padding->edges);
    }
    if (const UiValue* spacing = FindProperty(node, "spacing")) {
        row.SetSpacing(spacing->length);
    } else if (const UiValue* gap = FindProperty(node, "gap")) {
        row.SetSpacing(gap->length);
    }
    if (const UiValue* alignment = FindProperty(node, "alignment")) {
        row.SetAlignment(alignment->alignment);
    }
    if (const UiValue* justify = FindProperty(node, "justify")) {
        row.SetJustify(ParseJustify(justify->text));
    }
}

std::unique_ptr<Element> CreateElement(const IrNode& node, const std::unordered_map<std::string, std::function<void()>>& clicks) {
    switch (node.kind) {
    case NodeKind::WINDOW:
        return std::make_unique<WindowElement>();
    case NodeKind::COLUMN: {
        auto column = std::make_unique<Column>();
        column->SetWidth(Length::Fill());
        column->SetHeight(Length::Fit());
        if (const UiValue* padding = FindProperty(node, "padding")) {
            column->SetPadding(padding->edges);
        }
        if (const UiValue* spacing = FindProperty(node, "spacing")) {
            column->SetSpacing(spacing->length);
        } else if (const UiValue* gap = FindProperty(node, "gap")) {
            column->SetSpacing(gap->length);
        }
        if (const UiValue* alignment = FindProperty(node, "alignment")) {
            column->SetAlignment(alignment->alignment);
        }
        return column;
    }
    case NodeKind::ROW: {
        auto row = std::make_unique<Row>();
        row->SetWidth(Length::Fill());
        row->SetHeight(Length::Fit());
        ApplyFlow(*row, node);
        return row;
    }
    case NodeKind::FLEX: {
        auto flex = std::make_unique<FlexBox>();
        flex->SetWidth(Length::Fill());
        flex->SetHeight(Length::Fit());
        if (const UiValue* direction = FindProperty(node, "direction")) {
            flex->SetDirection(direction->text == "column" ? FlexDirection::COLUMN : FlexDirection::ROW);
        }
        if (const UiValue* justify = FindProperty(node, "justify")) {
            flex->SetJustify(ParseJustify(justify->text));
        }
        if (const UiValue* alignment = FindProperty(node, "alignment")) {
            flex->SetAlignment(alignment->alignment);
        }
        if (const UiValue* wrap = FindProperty(node, "wrap")) {
            flex->SetWrap(wrap->text == "wrap" ? FlexWrap::WRAP : FlexWrap::NO_WRAP);
        }
        if (const UiValue* padding = FindProperty(node, "padding")) {
            flex->SetPadding(padding->edges);
        }
        if (const UiValue* gap = FindProperty(node, "gap")) {
            flex->SetGap(gap->length);
        }
        return flex;
    }
    case NodeKind::GRID: {
        auto grid = std::make_unique<Grid>();
        grid->SetWidth(Length::Fill());
        grid->SetHeight(Length::Fit());
        if (const UiValue* columns = FindProperty(node, "columns")) {
            grid->SetColumns(static_cast<int>(columns->number));
        }
        if (const UiValue* padding = FindProperty(node, "padding")) {
            grid->SetPadding(padding->edges);
        }
        if (const UiValue* columnGap = FindProperty(node, "columnGap")) {
            grid->SetColumnGap(columnGap->length);
        }
        if (const UiValue* rowGap = FindProperty(node, "rowGap")) {
            grid->SetRowGap(rowGap->length);
        }
        return grid;
    }
    case NodeKind::STACK: {
        auto stack = std::make_unique<Stack>();
        stack->SetWidth(Length::Fill());
        stack->SetHeight(Length::Fit());
        if (const UiValue* padding = FindProperty(node, "padding")) {
            stack->SetPadding(padding->edges);
        }
        return stack;
    }
    case NodeKind::SCROLL: {
        auto scroll = std::make_unique<ScrollView>();
        scroll->SetWidth(Length::Fill());
        scroll->SetHeight(Length::Fill());
        if (const UiValue* padding = FindProperty(node, "padding")) {
            scroll->SetPadding(padding->edges);
        }
        if (const UiValue* spacing = FindProperty(node, "spacing")) {
            scroll->SetSpacing(spacing->length);
        } else if (const UiValue* gap = FindProperty(node, "gap")) {
            scroll->SetSpacing(gap->length);
        }
        return scroll;
    }
    case NodeKind::PANEL:
        return std::make_unique<Panel>();
    case NodeKind::CARD:
        return std::make_unique<Card>();
    case NodeKind::SPACER:
        return std::make_unique<Spacer>();
    case NodeKind::DIVIDER: {
        auto divider = std::make_unique<Divider>();
        divider->SetWidth(Length::Fill());
        if (const UiValue* orientation = FindProperty(node, "orientation")) {
            divider->SetOrientation(orientation->text == "vertical");
        }
        if (const UiValue* color = FindProperty(node, "color")) {
            divider->SetColor(color->color);
        }
        return divider;
    }
    case NodeKind::ABSOLUTE: {
        auto absolute = std::make_unique<AbsoluteBox>();
        absolute->SetWidth(Length::Fill());
        absolute->SetHeight(Length::Fill());
        return absolute;
    }
    case NodeKind::SPLIT: {
        auto split = std::make_unique<SplitView>();
        split->SetWidth(Length::Fill());
        split->SetHeight(Length::Fill());
        if (const UiValue* direction = FindProperty(node, "direction")) {
            split->SetDirection(direction->text == "column" ? FlexDirection::COLUMN : FlexDirection::ROW);
        }
        if (const UiValue* ratio = FindProperty(node, "ratio")) {
            split->SetRatio(static_cast<float>(ratio->number));
        }
        return split;
    }
    case NodeKind::IMAGE: {
        auto image = std::make_unique<ImageView>();
        if (const UiValue* source = FindProperty(node, "source")) {
            image->SetSource(source->text);
        }
        return image;
    }
    case NodeKind::TEXT: {
        auto text = std::make_unique<TextBlock>();
        if (const UiValue* content = FindProperty(node, "text")) {
            text->SetText(content->text);
        }
        if (const UiValue* role = FindProperty(node, "role")) {
            text->SetRole(ParseRole(role->text));
        }
        if (const UiValue* fontSize = FindProperty(node, "fontSize")) {
            text->SetFontSize(static_cast<float>(fontSize->number));
        }
        if (const UiValue* fontWeight = FindProperty(node, "fontWeight")) {
            text->SetFontWeight(static_cast<int>(fontWeight->number));
        }
        if (const UiValue* color = FindProperty(node, "color")) {
            text->SetColor(color->color);
        }
        if (const UiValue* textAlign = FindProperty(node, "textAlign")) {
            text->SetTextAlign(textAlign->alignment == Alignment::CENTER
                                   ? TextAlign::CENTER
                                   : (textAlign->alignment == Alignment::END ? TextAlign::END : TextAlign::START));
        }
        if (const UiValue* verticalAlign = FindProperty(node, "verticalAlign")) {
            text->SetVerticalAlign(verticalAlign->alignment == Alignment::CENTER
                                       ? TextAlign::CENTER
                                       : (verticalAlign->alignment == Alignment::END ? TextAlign::END : TextAlign::START));
        }
        return text;
    }
    case NodeKind::BUTTON: {
        auto button = std::make_unique<Button>();
        if (const UiValue* content = FindProperty(node, "text")) {
            button->SetText(content->text);
        }
        if (const UiValue* variant = FindProperty(node, "variant")) {
            button->SetVariant(ParseVariant(variant->text));
        }
        if (const UiValue* size = FindProperty(node, "size")) {
            button->SetSize(ParseButtonSize(size->text));
            if (size->text == "sm") {
                button->SetFontSize(14.0f);
            }
        }
        if (const UiValue* background = FindProperty(node, "background")) {
            button->SetBackground(background->color);
        }
        if (const UiValue* enabled = FindProperty(node, "enabled")) {
            button->SetEnabled(enabled->boolean);
        }
        if (const UiValue* radius = FindProperty(node, "cornerRadius")) {
            button->SetCornerRadius(radius->length);
        }
        if (const UiValue* click = FindProperty(node, "onClick")) {
            if (IsAction(click->text)) {
                button->SetAction(click->text);
            } else {
                const auto found = clicks.find(click->text);
                if (found != clicks.end()) {
                    button->SetOnClick(found->second);
                } else {
                    const std::string name = click->text;
                    button->SetOnClick([name]() { LogInfo("Clicked '" + name + "'"); });
                }
            }
        }
        return button;
    }
    case NodeKind::CHECKBOX:
    case NodeKind::SWITCH:
    case NodeKind::RADIO: {
        std::unique_ptr<Element> control;
        if (node.kind == NodeKind::CHECKBOX) {
            auto box = std::make_unique<CheckBox>();
            if (const UiValue* checked = FindProperty(node, "checked")) {
                box->SetChecked(checked->boolean);
            }
            control = std::move(box);
        } else if (node.kind == NodeKind::SWITCH) {
            auto toggle = std::make_unique<ToggleSwitch>();
            if (const UiValue* checked = FindProperty(node, "checked")) {
                toggle->SetChecked(checked->boolean);
            }
            control = std::move(toggle);
        } else {
            auto radio = std::make_unique<RadioButton>();
            if (const UiValue* checked = FindProperty(node, "checked")) {
                radio->SetChecked(checked->boolean);
            }
            if (const UiValue* name = FindProperty(node, "name")) {
                radio->SetGroup(name->text);
            }
            control = std::move(radio);
        }
        if (const UiValue* content = FindProperty(node, "text")) {
            if (auto* box = dynamic_cast<CheckBox*>(control.get())) {
                box->SetText(content->text);
            } else if (auto* toggle = dynamic_cast<ToggleSwitch*>(control.get())) {
                toggle->SetText(content->text);
            } else if (auto* radio = dynamic_cast<RadioButton*>(control.get())) {
                radio->SetText(content->text);
            }
        }
        if (const UiValue* enabled = FindProperty(node, "enabled")) {
            if (auto* box = dynamic_cast<CheckBox*>(control.get())) {
                box->SetEnabled(enabled->boolean);
            } else if (auto* toggle = dynamic_cast<ToggleSwitch*>(control.get())) {
                toggle->SetEnabled(enabled->boolean);
            } else if (auto* radio = dynamic_cast<RadioButton*>(control.get())) {
                radio->SetEnabled(enabled->boolean);
            }
        }
        return control;
    }
    case NodeKind::SLIDER: {
        auto slider = std::make_unique<Slider>();
        slider->SetWidth(Length::Fill());
        float minimum = 0.0f;
        float maximum = 100.0f;
        if (const UiValue* minValue = FindProperty(node, "min")) {
            minimum = static_cast<float>(minValue->number);
        }
        if (const UiValue* maxValue = FindProperty(node, "max")) {
            maximum = static_cast<float>(maxValue->number);
        }
        slider->SetRange(minimum, maximum);
        if (const UiValue* value = FindProperty(node, "value")) {
            slider->SetValue(static_cast<float>(value->number));
        }
        if (const UiValue* enabled = FindProperty(node, "enabled")) {
            slider->SetEnabled(enabled->boolean);
        }
        return slider;
    }
    case NodeKind::PROGRESS: {
        auto progress = std::make_unique<ProgressBar>();
        progress->SetWidth(Length::Fill());
        if (const UiValue* value = FindProperty(node, "value")) {
            progress->SetValue(static_cast<float>(value->number));
        }
        return progress;
    }
    case NodeKind::TEXT_FIELD: {
        auto field = std::make_unique<TextField>();
        field->SetWidth(Length::Fill());
        if (node.name == "passwordField") {
            field->SetMode(FieldMode::PASSWORD);
        } else if (node.name == "searchField") {
            field->SetPlaceholder("Search");
        } else if (node.name == "textArea") {
            field->SetMode(FieldMode::AREA);
        } else if (node.name == "numberField") {
            field->SetMode(FieldMode::NUMBER);
        }
        if (const UiValue* content = FindProperty(node, "text")) {
            field->SetText(content->text);
        }
        if (const UiValue* placeholder = FindProperty(node, "placeholder")) {
            field->SetPlaceholder(placeholder->text);
        }
        if (const UiValue* enabled = FindProperty(node, "enabled")) {
            field->SetEnabled(enabled->boolean);
        }
        if (const UiValue* validation = FindProperty(node, "validation")) {
            field->SetValidation(validation->text);
        }
        return field;
    }
    case NodeKind::SELECT: {
        auto select = std::make_unique<SelectBox>();
        select->SetWidth(Length::Fill());
        if (const UiValue* placeholder = FindProperty(node, "placeholder")) {
            select->SetPlaceholder(placeholder->text);
        }
        return select;
    }
    case NodeKind::OPTION: {
        auto option = std::make_unique<OptionItem>();
        if (const UiValue* content = FindProperty(node, "text")) {
            option->SetText(content->text);
        }
        return option;
    }
    case NodeKind::ALERT: {
        auto alert = std::make_unique<AlertBanner>();
        alert->SetWidth(Length::Fill());
        if (const UiValue* title = FindProperty(node, "title")) {
            alert->SetTitle(title->text);
        }
        if (const UiValue* content = FindProperty(node, "text")) {
            alert->SetText(content->text);
        }
        if (const UiValue* tone = FindProperty(node, "tone")) {
            Tone parsed = Tone::INFO;
            if (tone->text == "success") {
                parsed = Tone::SUCCESS;
            } else if (tone->text == "warning") {
                parsed = Tone::WARNING;
            } else if (tone->text == "danger") {
                parsed = Tone::DANGER;
            }
            alert->SetTone(parsed);
        }
        return alert;
    }
    case NodeKind::BADGE: {
        auto badge = std::make_unique<Badge>();
        if (const UiValue* content = FindProperty(node, "text")) {
            badge->SetText(content->text);
        }
        if (const UiValue* tone = FindProperty(node, "tone")) {
            Tone parsed = Tone::INFO;
            if (tone->text == "success") {
                parsed = Tone::SUCCESS;
            } else if (tone->text == "warning") {
                parsed = Tone::WARNING;
            } else if (tone->text == "danger") {
                parsed = Tone::DANGER;
            }
            badge->SetTone(parsed);
        }
        return badge;
    }
    case NodeKind::LINK: {
        auto link = std::make_unique<Link>();
        if (const UiValue* content = FindProperty(node, "text")) {
            link->SetText(content->text);
        }
        if (const UiValue* click = FindProperty(node, "onClick")) {
            const auto found = clicks.find(click->text);
            if (found != clicks.end()) {
                link->SetOnClick(found->second);
            }
        }
        return link;
    }
    case NodeKind::ICON_BUTTON: {
        auto button = std::make_unique<IconButton>();
        if (const UiValue* content = FindProperty(node, "text")) {
            button->SetText(content->text);
        }
        if (const UiValue* variant = FindProperty(node, "variant")) {
            button->SetVariant(ParseVariant(variant->text));
        }
        if (const UiValue* enabled = FindProperty(node, "enabled")) {
            button->SetEnabled(enabled->boolean);
        }
        if (const UiValue* click = FindProperty(node, "onClick")) {
            const auto found = clicks.find(click->text);
            if (found != clicks.end()) {
                button->SetOnClick(found->second);
            }
        }
        return button;
    }
    case NodeKind::TABS: {
        auto tabs = std::make_unique<Tabs>();
        tabs->SetWidth(Length::Fill());
        return tabs;
    }
    case NodeKind::TAB: {
        auto tab = std::make_unique<TabPage>();
        if (const UiValue* title = FindProperty(node, "title")) {
            tab->SetTitle(title->text);
        }
        return tab;
    }
    case NodeKind::CONTAINER:
        return std::make_unique<Container>();
    case NodeKind::WRAP:
        return std::make_unique<WrapBox>();
    case NodeKind::NAVBAR:
        return std::make_unique<Navbar>();
    case NodeKind::HEADER:
        return std::make_unique<Header>();
    case NodeKind::SECTION:
        return std::make_unique<Section>();
    case NodeKind::FOOTER:
        return std::make_unique<Footer>();
    case NodeKind::LIST:
        return std::make_unique<UiList>();
    case NodeKind::LIST_ITEM: {
        auto item = std::make_unique<ListItem>();
        if (const UiValue* click = FindProperty(node, "onClick")) {
            if (IsAction(click->text)) {
                item->SetAction(click->text);
            } else {
                const auto found = clicks.find(click->text);
                if (found != clicks.end()) {
                    item->SetOnClick(found->second);
                }
            }
        }
        return item;
    }
    case NodeKind::MENU:
        return std::make_unique<Menu>();
    case NodeKind::MENU_ITEM: {
        auto item = std::make_unique<MenuItem>();
        if (const UiValue* content = FindProperty(node, "text")) {
            item->SetText(content->text);
        }
        if (const UiValue* click = FindProperty(node, "onClick")) {
            if (IsAction(click->text)) {
                item->SetAction(click->text);
            } else {
                const auto found = clicks.find(click->text);
                if (found != clicks.end()) {
                    item->SetOnClick(found->second);
                }
            }
        }
        return item;
    }
    case NodeKind::CLIP:
        return std::make_unique<ClipBox>();
    case NodeKind::ASPECT: {
        auto aspect = std::make_unique<AspectBox>();
        aspect->SetWidth(Length::Fill());
        if (const UiValue* ratio = FindProperty(node, "ratio")) {
            aspect->SetRatio(static_cast<float>(ratio->number));
        }
        return aspect;
    }
    case NodeKind::OVERLAY:
        return std::make_unique<OverlayBox>();
    case NodeKind::DIALOG:
        return std::make_unique<Dialog>();
    case NodeKind::FORM:
        return std::make_unique<Form>();
    case NodeKind::SKELETON: {
        auto skeleton = std::make_unique<Skeleton>();
        skeleton->SetWidth(Length::Fill());
        if (const UiValue* lines = FindProperty(node, "lines")) {
            skeleton->SetLines(static_cast<int>(lines->number));
        }
        return skeleton;
    }
    case NodeKind::AVATAR: {
        auto avatar = std::make_unique<Avatar>();
        if (const UiValue* content = FindProperty(node, "text")) {
            avatar->SetText(content->text);
        }
        return avatar;
    }
    case NodeKind::EMPTY: {
        auto empty = std::make_unique<EmptyState>();
        empty->SetWidth(Length::Fill());
        if (const UiValue* title = FindProperty(node, "title")) {
            empty->SetTitle(title->text);
        }
        if (const UiValue* content = FindProperty(node, "text")) {
            empty->SetText(content->text);
        }
        return empty;
    }
    case NodeKind::BREADCRUMB:
        return std::make_unique<Breadcrumb>();
    case NodeKind::PAGINATION: {
        auto pagination = std::make_unique<Pagination>();
        if (const UiValue* pages = FindProperty(node, "pages")) {
            pagination->SetPages(static_cast<int>(pages->number));
        }
        if (const UiValue* page = FindProperty(node, "page")) {
            pagination->SetPage(static_cast<int>(page->number));
        }
        return pagination;
    }
    }
    return nullptr;
}

void Populate(Element& element, const IrNode& node, const std::unordered_map<std::string, std::function<void()>>& clicks) {
    ApplyBox(element, node);
    for (const IrNode& child : node.children) {
        auto created = CreateElement(child, clicks);
        if (created == nullptr) {
            continue;
        }
        Populate(*created, child, clicks);
        element.AddChild(std::move(created));
    }
}

}  // namespace

std::unique_ptr<Element> Instantiate(const IrNode& root, const std::unordered_map<std::string, std::function<void()>>& clicks) {
    auto created = CreateElement(root, clicks);
    if (created == nullptr) {
        return nullptr;
    }
    Populate(*created, root, clicks);
    if (!root.states.empty()) {
        created->SeedState(root.states);
    } else {
        created->ApplyState();
    }
    return created;
}

}  // namespace Runtime
