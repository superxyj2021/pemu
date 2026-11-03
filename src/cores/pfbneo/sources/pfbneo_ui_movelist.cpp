//
// pfbneo move list dialog implementation (see pfbneo_ui_movelist.h).
//

#include "pfbneo_ui_movelist.h"
#include "fbneo/pfbneo_movelist.h"

#include <algorithm>

using namespace c2d;
using namespace pemu;

// dialog geometry (same as the other dialogs):
//   width 640, top edge at y=180, height 540 (180 -> bottom of the 720px screen)
#define DIALOG_WIDTH   (640.0f)
#define DIALOG_TOP_Y   (180.0f)
#define DIALOG_HEIGHT  (540.0f)
#define ROW_PAD_X      (24.0f)
#define ROWS_TOP_Y     (56.0f)
#define ROWS_BOTTOM_PAD (8.0f)
#define NAME_MAX_W     (0.40f)
#define NAME_MAX_W_FULL (0.94f)
#define VALUE_OFFSET_X (0.48f)
#define VALUE_MAX_W    (0.50f)
#define VALUE_RIGHT_X  (DIALOG_WIDTH - ROW_PAD_X)

// green used for move name rows (Material Green A700 #00C853)
static const Color MODIFIED_COLOR(0, 200, 83, 255);

// indentation for command lines under a move
static const char *COMMAND_INDENT = "　　";

// marquee scroll tuning (same feel as the rom list)
static const float SCROLL_HOLD_START = 0.5f;
static const float SCROLL_HOLD_EDGE = 0.65f;
static const float SCROLL_SPEED = 12.f;

PfbneMovelistDialog::PfbneMovelistDialog(UiMain *main) : SkinnedRectangle(main, {"STATES_MENU"}) {
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

    // rows: item text follows the rom menu (UiMenu) style
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

void PfbneMovelistDialog::open() {
    // the dialog is pre-attached to the ui tree by PFBAUiEmu::attachToUi()
    setOrigin(Origin::TopLeft);
    setPosition((ui->getSize().x - DIALOG_WIDTH) / 2, DIALOG_TOP_Y);

    optionIndex = 0;
    highlightIndex = 0;

    // start fully collapsed: every move group is one row
    int count = pfbneo_movelist::getMoveCount();
    m_expanded.assign((size_t) (count > 0 ? count : 0), false);
    rebuildRows();

    updateLines();

    // force a CLEAN visibility state: Hidden -> Visible without tween
    setVisibility(Visibility::Hidden, false);
    setVisibility(Visibility::Visible, false);
}

void PfbneMovelistDialog::close() {
    setVisibility(Visibility::Hidden, false);

    // the move list is read-only: nothing to save, just return to the ui menu
    // highlighting the option that opened the dialog
    if (ui->getUiMenu()) {
        ui->getUiMenu()->setRestoreOption(TEXT_MENU_MOVELIST);
        ui->getUiMenu()->load(true);
    }
}

void PfbneMovelistDialog::setVisibility(c2d::Visibility visibility, bool tweenPlay) {
    // keep the main ui blurred behind the dialog (only when the rom list is
    // visible, i.e. opened from the rom screen; in-game the rom list is hidden)
    if (ui->getUiRomList() && ui->getUiRomList()->isVisible()) {
        ui->getUiRomList()->getBlur()->setVisibility(visibility, true);
    }
    SkinnedRectangle::setVisibility(visibility, tweenPlay);
}

void PfbneMovelistDialog::rebuildRows() {
    m_rows.clear();
    int count = pfbneo_movelist::getMoveCount();
    for (int m = 0; m < count; m++) {
        RowRef ref;
        ref.moveIdx = m;
        ref.cmdIdx = -1;
        m_rows.push_back(ref);
        if (m < (int) m_expanded.size() && m_expanded[m]) {
            int nCmd = pfbneo_movelist::getCommandCount(m);
            for (int c = 0; c < nCmd; c++) {
                RowRef r;
                r.moveIdx = m;
                r.cmdIdx = c;
                m_rows.push_back(r);
            }
        }
    }
}

void PfbneMovelistDialog::updateLines() {
    // reset the marquee scroll animation when the selection/list changes
    m_scrollName.offset = 0.f;
    m_scrollName.dir = 1;
    m_scrollName.holdUntil = m_scrollClock.getElapsedTime().asSeconds() + SCROLL_HOLD_START;
    m_scrollValue = m_scrollName;

    title->setString(TEXT_MENU_MOVELIST);

    int total = (int) m_rows.size();
    bool highlightVisible = false;

    for (int i = 0; i < maxLines; i++) {
        int idx = optionIndex + i;
        Row &row = rows[i];
        row.name->setVisibility(Visibility::Hidden);
        row.value->setVisibility(Visibility::Hidden);
        row.name->setScrollEnabled(false);
        row.value->setScrollEnabled(false);

        if (idx >= total) {
            // first row shows the empty hint when the list is empty
            if (total == 0 && i == 0) {
                row.name->setString(TEXT_MOVELIST_EMPTY);
                row.name->setVisibility(Visibility::Visible);
                row.name->setStyle(Text::Style::Regular);
                row.name->setOutlineThickness(0);
                row.name->setSizeMax(getSize().x * NAME_MAX_W_FULL - ROW_PAD_X, 0);
                row.name->setFillColor(row.defaultColor);
            }
            continue;
        }

        int moveIdx = m_rows[idx].moveIdx;
        int cmdIdx = m_rows[idx].cmdIdx;

        if (cmdIdx < 0) {
            // move name row: green + bold, value shows the expand state
            row.name->setString(pfbneo_movelist::getMoveName(moveIdx));
            row.name->setVisibility(Visibility::Visible);
            row.name->setStyle(Text::Style::Bold);
            row.name->setOutlineThickness(row.defaultOutline);
            row.name->setSizeMax(getSize().x * NAME_MAX_W, 0);
            row.name->setScrollEnabled(i == highlightIndex);
            row.name->setFillColor(MODIFIED_COLOR);

            bool expanded = moveIdx < (int) m_expanded.size() && m_expanded[moveIdx];
            row.value->setString(expanded ? TEXT_MOVELIST_COLLAPSE : TEXT_MOVELIST_EXPAND);
            row.value->setVisibility(Visibility::Visible);
            row.value->setOutlineThickness(0);
            row.value->setScrollEnabled(i == highlightIndex);
            if (expanded) {
                row.value->setFillColor(MODIFIED_COLOR);
            } else {
                row.value->setFillColor(row.defaultColor);
            }
            if (i == highlightIndex) {
                highlightVisible = true;
            }
        } else {
            // command line: indented, skin color, full width (marquee-able)
            row.name->setString(std::string(COMMAND_INDENT)
                                + pfbneo_movelist::getCommand(moveIdx, cmdIdx));
            row.name->setVisibility(Visibility::Visible);
            row.name->setStyle(Text::Style::Regular);
            row.name->setOutlineThickness(0);
            row.name->setSizeMax(getSize().x * NAME_MAX_W_FULL - ROW_PAD_X, 0);
            row.name->setScrollEnabled(i == highlightIndex);
            row.name->setFillColor(row.defaultColor);
            if (i == highlightIndex) {
                highlightVisible = true;
            }
        }
    }

    // move the highlight to the selected row
    if (highlight) {
        if (highlightVisible && total > 0) {
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

void PfbneMovelistDialog::onKeyUp() {
    int total = (int) m_rows.size();
    if (total <= 0) {
        return;
    }
    int cur = optionIndex + highlightIndex;
    setWindowTo(cur <= 0 ? total - 1 : cur - 1);
    updateLines();
}

void PfbneMovelistDialog::onKeyDown() {
    int total = (int) m_rows.size();
    if (total <= 0) {
        return;
    }
    int cur = optionIndex + highlightIndex;
    setWindowTo(cur >= total - 1 ? 0 : cur + 1);
    updateLines();
}

void PfbneMovelistDialog::setWindowTo(int target) {
    int total = (int) m_rows.size();
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

bool PfbneMovelistDialog::onInput(c2d::Input::Player *players) {
    if (!isVisible()) {
        return false;
    }

    unsigned int buttons = players[0].buttons;

    // B: close the dialog and return to the ui menu
    if (buttons & Input::Button::B) {
        close();
        return true;
    }

    int total = (int) m_rows.size();
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

    // A: expand / collapse the highlighted move group
    if (buttons & Input::Button::A) {
        int idx = optionIndex + highlightIndex;
        if (idx >= 0 && idx < total && m_rows[idx].cmdIdx < 0) {
            int moveIdx = m_rows[idx].moveIdx;
            if (moveIdx >= 0 && moveIdx < (int) m_expanded.size()) {
                m_expanded[moveIdx] = !m_expanded[moveIdx];
                rebuildRows();
                // rows before the toggled move are unchanged, so its row keeps
                // the same index; only clamp the window to the new list length
                int newTotal = (int) m_rows.size();
                int maxOption = std::max(0, newTotal - maxLines);
                if (optionIndex > maxOption) {
                    optionIndex = maxOption;
                }
                updateLines();
            }
        }
        return true;
    }

    // any other button: consume so the game/menu below does not react
    return true;
}

// advance one marquee state (bounce with pauses)
void PfbneMovelistDialog::scrollAdvance(ScrollState &st, float elapsed, float dt, float limit) {
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

void PfbneMovelistDialog::onUpdate() {
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

void PfbneMovelistDialog::onDraw(c2d::Transform &transform, bool draw) {
    SkinnedRectangle::onDraw(transform, draw);
}