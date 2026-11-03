//
// pfbneo DIP switch dialog implementation.
//

#include "pfbneo_ui_dip.h"
#include "fbneo/pfbneo_dip.h"

#include <algorithm>

using namespace c2d;
using namespace pemu;

// dialog geometry (same as the cheat/key dialogs except width):
//   width 530 (fixed per user request), top edge at y=180,
//   height 540 (180 -> bottom of the 720px screen)
#define DIALOG_WIDTH   (530.0f)
#define DIALOG_TOP_Y   (180.0f)
#define DIALOG_HEIGHT  (540.0f)
#define ROW_PAD_X      (24.0f)
#define ROWS_TOP_Y     (56.0f)
#define ROWS_BOTTOM_PAD (8.0f)
// two columns: name (left) + value (right)
#define NAME_MAX_W     (0.62f)
#define VALUE_MAX_W    (0.36f)

// green used for changed (non-default) DIP values (Material Green A700)
static const Color MODIFIED_COLOR(0, 200, 83, 255);

// marquee scroll tuning (same feel as the rom list)
static const float SCROLL_HOLD_START = 0.5f;
static const float SCROLL_HOLD_EDGE = 0.65f;
static const float SCROLL_SPEED = 12.f;

PfbneDipDialog::PfbneDipDialog(UiMain *main) : SkinnedRectangle(main, {"STATES_MENU"}) {
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

    // rows: name + value text (item text follows the rom menu (UiMenu) style,
    // with a slightly larger font size on the popup dialogs)
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

        row.value = new Text("", textGroup.size, font);
        row.value->setFillColor(textGroup.color);
        row.value->setOutlineThickness(textGroup.outlineSize);
        row.value->setOutlineColor(textGroup.outlineColor);
        row.value->setOrigin(Origin::Right);
        row.value->setPosition(getSize().x - ROW_PAD_X, y);
        row.value->setSizeMax(getSize().x * VALUE_MAX_W, 0);
        row.value->setVisibility(Visibility::Hidden);
        SkinnedRectangle::add(row.value);
        row.defaultColor = textGroup.color;

        rows.push_back(row);
    }

    // empty-game message (centered under the title)
    emptyText = new Text("", textGroup.size, font);
    emptyText->setFillColor(textGroup.color);
    emptyText->setOutlineThickness(textGroup.outlineSize);
    emptyText->setOutlineColor(textGroup.outlineColor);
    emptyText->setOrigin(Origin::Center);
    emptyText->setPosition(getSize().x / 2, getSize().y / 2);
    emptyText->setVisibility(Visibility::Hidden);
    SkinnedRectangle::add(emptyText);

    SkinnedRectangle::setVisibility(Visibility::Hidden);
}

void PfbneDipDialog::open() {
    // the dialog is pre-attached to the ui tree by PFBAUiEmu::attachToUi()
    // (right after UiMain::init(), like the cheat dialog).
    setSize(DIALOG_WIDTH, DIALOG_HEIGHT);
    setOrigin(Origin::TopLeft);
    setPosition((ui->getSize().x - getSize().x) / 2, DIALOG_TOP_Y);

    // re-apply the width-dependent layout (title, column sizes/positions)
    title->setPosition(getSize().x / 2, 28);
    for (auto &row : rows) {
        row.name->setSizeMax(getSize().x * NAME_MAX_W, 0);
        row.value->setPosition(getSize().x - ROW_PAD_X, row.value->getPosition().y);
        row.value->setSizeMax(getSize().x * VALUE_MAX_W, 0);
    }
    emptyText->setPosition(getSize().x / 2, getSize().y / 2);

    optionIndex = 0;
    highlightIndex = 0;
    m_changed = false;
    title->setString(TEXT_MENU_DIP);
    emptyText->setString(TEXT_DIP_EMPTY);

    updateLines();

    // force a CLEAN visibility state: Hidden -> Visible without tween
    setVisibility(Visibility::Hidden, false);
    setVisibility(Visibility::Visible, false);
}

void PfbneDipDialog::close() {
    setVisibility(Visibility::Hidden, false);

    // persist the per-rom DIP settings
    if (ui->getConfig()) {
        ui->getConfig()->saveGame();
    }

    // return to the ui menu, highlighting the option that opened the dialog
    // (the rom menu was hidden when the dialog was opened)
    if (ui->getUiMenu()) {
        ui->getUiMenu()->setRestoreOption(TEXT_MENU_DIP);
        ui->getUiMenu()->load(true);
    }

    // hint: the changes only latch at reset (the menu close triggers it)
    if (m_changed && ui->getUiStatusBox()) {
        ui->getUiStatusBox()->show(TEXT_HINT_DIP_CHANGED);
    }
}

void PfbneDipDialog::setVisibility(c2d::Visibility visibility, bool tweenPlay) {
    // keep the main ui blurred behind the dialog, exactly like the
    // save-states dialog (only when the rom list is visible, i.e. opened
    // from the rom screen; in-game the rom list is hidden anyway)
    if (ui->getUiRomList() && ui->getUiRomList()->isVisible()) {
        ui->getUiRomList()->getBlur()->setVisibility(visibility, true);
    }
    SkinnedRectangle::setVisibility(visibility, tweenPlay);
}

void PfbneDipDialog::updateLines() {
    // reset the marquee scroll animation when the selection/list changes
    m_scrollName.offset = 0.f;
    m_scrollName.dir = 1;
    m_scrollName.holdUntil = m_scrollClock.getElapsedTime().asSeconds() + SCROLL_HOLD_START;
    m_scrollValue = m_scrollName;

    int dipCount = pfbneo_dip::getRowCount();
    bool highlightVisible = false;

    if (dipCount == 0) {
        emptyText->setVisibility(Visibility::Visible);
        for (int i = 0; i < maxLines; i++) {
            rows[i].name->setVisibility(Visibility::Hidden);
            rows[i].name->setScrollEnabled(false);
            rows[i].value->setVisibility(Visibility::Hidden);
            rows[i].value->setScrollEnabled(false);
        }
        if (highlight) {
            highlight->setVisibility(Visibility::Hidden);
        }
        return;
    }
    emptyText->setVisibility(Visibility::Hidden);

    for (int i = 0; i < maxLines; i++) {
        int idx = optionIndex + i;
        Row &row = rows[i];

        if (idx >= dipCount) {
            row.name->setVisibility(Visibility::Hidden);
            row.name->setScrollEnabled(false);
            row.value->setVisibility(Visibility::Hidden);
            row.value->setScrollEnabled(false);
            continue;
        }

        // name column (translated switch name)
        row.name->setString(pfbneo_dip::getDisplayName(pfbneo_dip::getRowName(idx)));
        row.name->setVisibility(Visibility::Visible);
        row.name->setStyle(Text::Style::Regular);
        row.name->setOutlineThickness(0);
        row.name->setSizeMax(getSize().x * NAME_MAX_W, 0);
        row.name->setScrollEnabled(i == highlightIndex);

        // value column (currently selected option, green when != factory default)
        c2d::config::Option *opt = pfbneo_dip::getValueOption(idx);
        row.value->setString(opt ? opt->getString() : "");
        row.value->setFillColor(pfbneo_dip::isChanged(idx) ? MODIFIED_COLOR : row.defaultColor);
        row.value->setVisibility(Visibility::Visible);
        row.value->setScrollEnabled(i == highlightIndex);

        if (i == highlightIndex) {
            highlightVisible = true;
        }
    }

    // move the highlight to the selected row
    if (highlight) {
        if (highlightVisible) {
            // same inset as the rom menu highlight: top = row top + outline,
            // height = row height - 2*outline -> centered on the row
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

void PfbneDipDialog::onKeyUp() {
    int total = pfbneo_dip::getRowCount();
    if (total <= 0) return;
    int cur = optionIndex + highlightIndex;
    int target = cur <= 0 ? total - 1 : cur - 1;
    setWindowTo(target);
    updateLines();
}

void PfbneDipDialog::onKeyDown() {
    int total = pfbneo_dip::getRowCount();
    if (total <= 0) return;
    int cur = optionIndex + highlightIndex;
    int target = cur >= total - 1 ? 0 : cur + 1;
    setWindowTo(target);
    updateLines();
}

void PfbneDipDialog::setWindowTo(int target) {
    int total = pfbneo_dip::getRowCount();
    int middle = maxLines / 2;
    int maxOption = std::max(0, total - maxLines);
    optionIndex = std::min(std::max(0, target - middle), maxOption);
    highlightIndex = target - optionIndex;
    if (highlightIndex < 0) highlightIndex = 0;
    if (highlightIndex >= maxLines) highlightIndex = maxLines - 1;
}

bool PfbneDipDialog::onInput(c2d::Input::Player *players) {
    if (!isVisible()) {
        return false;
    }

    unsigned int buttons = players[0].buttons;

    // B: close the dialog (saves the per-rom config)
    if (buttons & Input::Button::B) {
        close();
        return true;
    }

    int total = pfbneo_dip::getRowCount();

    if (buttons & Input::Button::Up) {
        onKeyUp();
        return true;
    }

    if (buttons & Input::Button::Down) {
        onKeyDown();
        return true;
    }

    int idx = optionIndex + highlightIndex;
    if (idx < 0 || idx >= total) {
        return true;
    }

    // LEFT/RIGHT: cycle the highlighted switch's options (live apply)
    if (buttons & Input::Button::Left || buttons & Input::Button::Right) {
        c2d::config::Option *opt = pfbneo_dip::getValueOption(idx);
        if (opt && opt->getArray().size() > 1) {
            if (buttons & Input::Button::Left) {
                opt->setArrayMovePrev();
            } else {
                opt->setArrayMoveNext();
            }
            pfbneo_dip::applyRow(idx);
            updateLines();
            // most drivers only latch DIP switches at init/reset: remember
            // the change so a soft reset is triggered when the menu closes
            // (see UiMenu B-handler / PFBAUiEmu::markSettingsAction). the
            // values were applied to GameInp[] immediately by applyRow().
            if (ui->getUiEmu()) {
                ui->getUiEmu()->markSettingsAction(1);
            }
            m_changed = true;
        }
        return true;
    }

    // any other button: consume
    return true;
}

// advance one marquee state (bounce with pauses)
void PfbneDipDialog::scrollAdvance(ScrollState &st, float elapsed, float dt, float limit) {
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

void PfbneDipDialog::onUpdate() {
    SkinnedRectangle::onUpdate();

    // animated highlight outline (same two-color gradient as the menus)
    if (highlight && highlight->isVisible()) {
        highlight->setOutlineColor(ui->getSkin()->getHighlightGradientColor());
    }

    // marquee scroll animation for the highlighted row: name and value scroll
    // independently - only the side that is too long for its column moves.
    if (!rows.empty() && highlightIndex >= 0 && highlightIndex < (int) rows.size()
        && highlight->isVisible()) {
        auto *name = rows[highlightIndex].name;
        auto *value = rows[highlightIndex].value;
        float elapsed = m_scrollClock.getElapsedTime().asSeconds();
        float dt = std::min(elapsed - m_lastScrollTime, 0.1f);
        m_lastScrollTime = elapsed;

        if (name && name->isScrollNeeded()) {
            float limit = std::max(0.f, name->getScrollFullWidth() - name->getSizeMax().x);
            scrollAdvance(m_scrollName, elapsed, dt, limit);
            name->setScrollOffset(m_scrollName.offset);
        } else if (name) {
            name->setScrollOffset(0.f);
        }

        if (value && value->isScrollNeeded()) {
            float limit = std::max(0.f, value->getScrollFullWidth() - value->getSizeMax().x);
            scrollAdvance(m_scrollValue, elapsed, dt, limit);
            value->setScrollOffset(m_scrollValue.offset);
        } else if (value) {
            value->setScrollOffset(0.f);
        }
    }
}

void PfbneDipDialog::onDraw(c2d::Transform &transform, bool draw) {
    SkinnedRectangle::onDraw(transform, draw);
}