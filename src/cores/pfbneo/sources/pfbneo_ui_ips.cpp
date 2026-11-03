//
// pfbneo IPS patch dialog implementation (see pfbneo_ui_ips.h).
//

#include "pfbneo_ui_ips.h"
#include "fbneo/pfbneo_ips.h"

#include <algorithm>

using namespace c2d;
using namespace pemu;

// dialog geometry (same as the cheat dialog):
//   width 640, top edge at y=180, height 540 (180 -> bottom of the 720px screen)
#define DIALOG_WIDTH   (640.0f)
#define DIALOG_TOP_Y   (180.0f)
#define DIALOG_HEIGHT  (540.0f)
#define ROW_PAD_X      (24.0f)
#define ROWS_TOP_Y     (56.0f)
#define ROWS_BOTTOM_PAD (8.0f)
#define NAME_MAX_W     (0.55f)
#define NAME_MAX_W_FULL (0.94f)
#define VALUE_OFFSET_X (0.60f)
#define VALUE_MAX_W    (0.38f)
#define VALUE_RIGHT_X  (DIALOG_WIDTH - ROW_PAD_X)

// green used for an enabled patch row (Material Green A700 #00C853)
static const Color MODIFIED_COLOR(0, 200, 83, 255);

// marquee scroll tuning (same feel as the rom list)
static const float SCROLL_HOLD_START = 0.5f;
static const float SCROLL_HOLD_EDGE = 0.65f;
static const float SCROLL_SPEED = 12.f;

PfbneIpsDialog::PfbneIpsDialog(UiMain *main) : SkinnedRectangle(main, {"STATES_MENU"}) {
    ui = main;

    setSize(DIALOG_WIDTH, DIALOG_HEIGHT);

    // title
    title = new SkinnedText(ui, {"STATES_MENU", "TITLE_TEXT"});
    title->setOrigin(Origin::Center);
    title->setPosition(getSize().x / 2, 28);
    SkinnedRectangle::add(title);

    // highlight (selection box), styled like the menus
    highlight = new RectangleShape({16, 16});
    ui->getSkin()->loadRectangleShape(highlight, {"SKIN_CONFIG", "HIGHLIGHT"});
    SkinnedRectangle::add(highlight);
    highlight->setVisibility(Visibility::Hidden);

    // rows: item text follows the rom menu (UiMenu) style, with the slightly
    // larger popup font size used by the other dialogs
    auto textGroup = ui->getSkin()->getText({"OPTIONS_MENU", "ITEMS_TEXT"});
    Font *font = ui->getSkin()->getFont();
    lineHeight = (float) textGroup.size + (2 * ui->getScaling().y) + 6;
    maxLines = (int) ((getSize().y - ROWS_TOP_Y - ROWS_BOTTOM_PAD) / lineHeight);
    if (maxLines > 24) {
        maxLines = 24;
    }
    for (int i = 0; i < maxLines; i++) {
        Row row;
        float y = ROWS_TOP_Y + (float) i * lineHeight + lineHeight / 2;

        row.name = new Text("", textGroup.size, font);
        row.name->setFillColor(textGroup.color);
        row.name->setOutlineThickness(textGroup.outlineSize);
        row.name->setOutlineColor(textGroup.outlineColor);
        row.name->setOrigin(Origin::Left);
        row.name->setPosition(ROW_PAD_X, y);
        row.name->setSizeMax(getSize().x * NAME_MAX_W, 0);
        row.name->setVisibility(Visibility::Hidden);
        SkinnedRectangle::add(row.name);
        row.defaultOutline = textGroup.outlineSize;

        row.value = new Text("", textGroup.size, font);
        row.value->setFillColor(textGroup.color);
        row.value->setOutlineThickness(textGroup.outlineSize);
        row.value->setOutlineColor(textGroup.outlineColor);
        row.value->setOrigin(Origin::Right);
        row.value->setPosition(VALUE_RIGHT_X, y);
        row.value->setSizeMax(getSize().x * VALUE_MAX_W, 0);
        row.value->setVisibility(Visibility::Hidden);
        SkinnedRectangle::add(row.value);
        row.defaultColor = textGroup.color;

        rows.push_back(row);
    }

    SkinnedRectangle::setVisibility(Visibility::Hidden);
}

void PfbneIpsDialog::open() {
    // the dialog is pre-attached to the ui tree by PFBAUiEmu::attachToUi()
    setOrigin(Origin::TopLeft);
    setPosition((ui->getSize().x - DIALOG_WIDTH) / 2, DIALOG_TOP_Y);

    optionIndex = 0;
    highlightIndex = 0;
    m_changed = false;

    // the initial highlight must not land on a category row
    int total = pfbneo_ips::getRowCount();
    if (total > 0) {
        int first = findNextSelectable(-1, 1);
        if (first >= 0) {
            setWindowTo(first);
        }
    }

    updateLines();

    // force a CLEAN visibility state: Hidden -> Visible without tween
    setVisibility(Visibility::Hidden, false);
    setVisibility(Visibility::Visible, false);
}

void PfbneIpsDialog::close() {
    setVisibility(Visibility::Hidden, false);

    // persist the per-rom enabled patch list
    if (ui->getConfig()) {
        ui->getConfig()->saveGame();
    }

    // return to the ui menu, highlighting the option that opened this dialog
    if (ui->getUiMenu()) {
        ui->getUiMenu()->setRestoreOption(TEXT_MENU_IPS);
        ui->getUiMenu()->load(true);
    }

    // hint: the rom patches are only applied when the game reloads (the menu
    // close triggers the reload)
    if (m_changed && ui->getUiStatusBox()) {
        ui->getUiStatusBox()->show(TEXT_HINT_IPS_CHANGED);
    }
}

void PfbneIpsDialog::setVisibility(c2d::Visibility visibility, bool tweenPlay) {
    // keep the main ui blurred behind the dialog (only when the rom list is
    // visible, i.e. opened from the rom screen; in-game the rom list is hidden)
    if (ui->getUiRomList() && ui->getUiRomList()->isVisible()) {
        ui->getUiRomList()->getBlur()->setVisibility(visibility, true);
    }
    SkinnedRectangle::setVisibility(visibility, tweenPlay);
}

void PfbneIpsDialog::updateLines() {
    // reset the marquee scroll animation when the selection/list changes
    m_scrollName.offset = 0.f;
    m_scrollName.dir = 1;
    m_scrollName.holdUntil = m_scrollClock.getElapsedTime().asSeconds() + SCROLL_HOLD_START;
    m_scrollValue = m_scrollName;

    title->setString(TEXT_MENU_IPS);

    int total = pfbneo_ips::getRowCount();
    bool highlightVisible = false;

    for (int i = 0; i < maxLines; i++) {
        int idx = optionIndex + i;
        Row &row = rows[i];
        row.name->setVisibility(Visibility::Hidden);
        row.value->setVisibility(Visibility::Hidden);
        row.name->setScrollEnabled(false);
        row.value->setScrollEnabled(false);

        if (idx >= total) {
            continue;
        }

        row.name->setString(pfbneo_ips::getRowName(idx));
        row.name->setVisibility(Visibility::Visible);

        if (pfbneo_ips::isCategoryRow(idx)) {
            // non-selectable category header: bold + outline (like the cheat
            // dialog's non-selectable rows), full row width, never highlighted
            row.name->setStyle(Text::Style::Bold);
            row.name->setOutlineThickness(row.defaultOutline);
            row.name->setSizeMax(getSize().x * NAME_MAX_W_FULL - ROW_PAD_X, 0);
            row.name->setScrollEnabled(false);
            row.name->setFillColor(row.defaultColor);
            row.value->setString("");
            row.value->setVisibility(Visibility::Hidden);
        } else {
            // selectable patch row: regular name, value = 启用/禁用
            row.name->setStyle(Text::Style::Regular);
            row.name->setOutlineThickness(0);
            row.name->setSizeMax(getSize().x * NAME_MAX_W, 0);
            row.name->setScrollEnabled(i == highlightIndex);

            if (pfbneo_ips::isRowEnabled(idx)) {
                row.value->setString(TEXT_MENU_IPS_ENABLED);
                row.value->setFillColor(MODIFIED_COLOR);
            } else {
                row.value->setString(TEXT_MENU_IPS_DISABLED);
                row.value->setFillColor(row.defaultColor);
            }
            row.value->setVisibility(Visibility::Visible);
            row.value->setOutlineThickness(0);
            row.value->setScrollEnabled(i == highlightIndex);
            if (i == highlightIndex) {
                highlightVisible = true;
            }
        }
    }

    // move the highlight to the selected row (only shown on a selectable row)
    if (highlight) {
        if (highlightVisible) {
            float outline = highlight->getOutlineThickness();
            highlight->setSize(getSize().x - 2 * ROW_PAD_X, lineHeight - 2 * outline);
            highlight->setPosition(ROW_PAD_X,
                                   ROWS_TOP_Y + (float) highlightIndex * lineHeight + outline);
            highlight->setVisibility(Visibility::Visible);
        } else {
            highlight->setVisibility(Visibility::Hidden);
        }
    }
}

bool PfbneIpsDialog::isSelectable(int row) {
    return row >= 0 && row < pfbneo_ips::getRowCount() && !pfbneo_ips::isCategoryRow(row);
}

void PfbneIpsDialog::onKeyUp() {
    int total = pfbneo_ips::getRowCount();
    if (total <= 0) {
        return;
    }

    int cur = optionIndex + highlightIndex;
    int target = findNextSelectable(cur, -1);
    if (target >= 0) {
        setWindowTo(target);
        updateLines();
    }
}

void PfbneIpsDialog::onKeyDown() {
    int total = pfbneo_ips::getRowCount();
    if (total <= 0) {
        return;
    }

    int cur = optionIndex + highlightIndex;
    int target = findNextSelectable(cur, 1);
    if (target >= 0) {
        setWindowTo(target);
        updateLines();
    }
}

int PfbneIpsDialog::findNextSelectable(int from, int delta) {
    int total = pfbneo_ips::getRowCount();
    for (int step = 1; step <= total; step++) {
        int idx = ((from + delta * step) % total + total) % total;
        if (isSelectable(idx)) {
            return idx;
        }
    }
    return -1;
}

void PfbneIpsDialog::setWindowTo(int target) {
    int total = pfbneo_ips::getRowCount();
    int middle = maxLines / 2;
    int maxOption = std::max(0, total - maxLines);
    optionIndex = std::min(std::max(0, target - middle), maxOption);
    highlightIndex = target - optionIndex;
    if (highlightIndex < 0) {
        highlightIndex = 0;
    }
    if (highlightIndex >= maxLines) {
        highlightIndex = maxLines - 1;
    }
}

bool PfbneIpsDialog::onInput(c2d::Input::Player *players) {
    if (!isVisible()) {
        return false;
    }

    unsigned int buttons = players[0].buttons;

    // B: save the selections and return to the ui menu
    if (buttons & Input::Button::B) {
        close();
        return true;
    }

    int total = pfbneo_ips::getRowCount();
    if (total <= 0) {
        return true;
    }

    if (buttons & Input::Button::Up) {
        onKeyUp();
        return true;
    }

    if (buttons & Input::Button::Down) {
        onKeyDown();
        return true;
    }

    // LEFT/RIGHT: toggle the highlighted patch on/off immediately
    if (buttons & Input::Button::Left || buttons & Input::Button::Right) {
        int idx = optionIndex + highlightIndex;
        if (isSelectable(idx)) {
            pfbneo_ips::toggleRow(idx);
            // rom patches only apply at load: mark the game for a reload when
            // the menu closes
            m_changed = true;
            if (ui->getUiEmu()) {
                ui->getUiEmu()->markSettingsAction(2);
            }
            updateLines();
        }
        return true;
    }

    // A and any other button: consume so the game/menu below does not react
    return true;
}

// advance one marquee state (bounce with pauses)
void PfbneIpsDialog::scrollAdvance(ScrollState &st, float elapsed, float dt, float limit) {
    if (elapsed < st.holdUntil) {
        return;
    }
    st.offset += (float) st.dir * dt * SCROLL_SPEED;
    if (st.offset >= limit) {
        st.offset = limit;
        st.dir = -1;
        st.holdUntil = elapsed + SCROLL_HOLD_EDGE;
    } else if (st.offset <= 0.f) {
        st.offset = 0.f;
        st.dir = 1;
        st.holdUntil = elapsed + SCROLL_HOLD_EDGE;
    }
}

void PfbneIpsDialog::onUpdate() {
    SkinnedRectangle::onUpdate();

    // animated highlight outline (same two-color gradient as the menus)
    if (highlight && highlight->isVisible()) {
        highlight->setOutlineColor(ui->getSkin()->getHighlightGradientColor());
    }

    // marquee scroll animation for the highlighted row: name and value scroll
    // independently - only the side that is too long for its column moves
    if (!rows.empty() && highlightIndex >= 0 && highlightIndex < (int) rows.size()
        && highlight->isVisible()) {
        auto *name = rows[highlightIndex].name;
        auto *value = rows[highlightIndex].value;
        float elapsed = m_scrollClock.getElapsedTime().asSeconds();
        float dt = std::min(elapsed - m_lastScrollTime, 0.1f);
        m_lastScrollTime = elapsed;

        if (name && name->isVisible() && name->isScrollNeeded()) {
            float limit = std::max(0.f, name->getScrollFullWidth() - name->getSizeMax().x);
            scrollAdvance(m_scrollName, elapsed, dt, limit);
            name->setScrollOffset(m_scrollName.offset);
        } else if (name) {
            name->setScrollOffset(0.f);
        }

        if (value && value->isVisible() && value->isScrollNeeded()) {
            float limit = std::max(0.f, value->getScrollFullWidth() - value->getSizeMax().x);
            scrollAdvance(m_scrollValue, elapsed, dt, limit);
            value->setScrollOffset(m_scrollValue.offset);
        } else if (value) {
            value->setScrollOffset(0.f);
        }
    }
}

void PfbneIpsDialog::onDraw(c2d::Transform &transform, bool draw) {
    SkinnedRectangle::onDraw(transform, draw);
}