//
// pfbneo memory card dialog implementation.
//

#include "pfbneo_ui_memcard.h"
#include "fbneo/pfbneo_memcard.h"

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
#define VALUE_OFFSET_X (0.60f)
#define VALUE_MAX_W    (0.38f)
#define VALUE_RIGHT_X  (DIALOG_WIDTH - ROW_PAD_X)

// green used for the currently selected card row / slot value
// (Material Green A700 #00C853: same accent as the cheat/key dialogs)
static const Color MODIFIED_COLOR(0, 200, 83, 255);

// marquee scroll tuning (same feel as the rom list)
static const float SCROLL_HOLD_START = 0.5f;
static const float SCROLL_HOLD_EDGE = 0.65f;
static const float SCROLL_SPEED = 12.f;

PfbneMemcardDialog::PfbneMemcardDialog(UiMain *main) : SkinnedRectangle(main, {"STATES_MENU"}) {
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

void PfbneMemcardDialog::open() {
    // the dialog is pre-attached to the ui tree by PFBAUiEmu::attachToUi()
    setOrigin(Origin::TopLeft);
    setPosition((ui->getSize().x - DIALOG_WIDTH) / 2, DIALOG_TOP_Y);

    optionIndex = 0;
    highlightIndex = 0;
    m_activeSlot = 0;
    m_changed = false;

    if (pfbneo_memcard::isPGM2()) {
        // PGM2: start on the 4-player slot page
        m_slotPage = true;
    } else {
        // NEOGEO: open directly on the card selection list
        m_slotPage = false;
        openSlotList(0);
    }

    updateLines();

    // force a CLEAN visibility state: Hidden -> Visible without tween
    setVisibility(Visibility::Hidden, false);
    setVisibility(Visibility::Visible, false);
}

void PfbneMemcardDialog::close() {
    setVisibility(Visibility::Hidden, false);

    // persist the per-rom card selections
    if (ui->getConfig()) {
        ui->getConfig()->saveGame();
    }

    // return to the ui menu, highlighting the option that opened the dialog
    // (the rom menu was hidden when the dialog was opened)
    if (ui->getUiMenu()) {
        ui->getUiMenu()->setRestoreOption(TEXT_MENU_MEMCARD);
        ui->getUiMenu()->load(true);
    }

    // hint: the new card is re-inserted when the game restarts (the menu
    // close triggers a soft reset)
    if (m_changed && ui->getUiStatusBox()) {
        ui->getUiStatusBox()->show(TEXT_HINT_MEMCARD_CHANGED);
    }
}

void PfbneMemcardDialog::setVisibility(c2d::Visibility visibility, bool tweenPlay) {
    // keep the main ui blurred behind the dialog, exactly like the other
    // popup dialogs (only when the rom list is visible, i.e. opened from the
    // rom screen; in-game the rom list is hidden anyway)
    if (ui->getUiRomList() && ui->getUiRomList()->isVisible()) {
        ui->getUiRomList()->getBlur()->setVisibility(visibility, true);
    }
    SkinnedRectangle::setVisibility(visibility, tweenPlay);
}

void PfbneMemcardDialog::openSlotList(int slot) {
    m_activeSlot = slot;
    m_slotPage = false;
    // auto-place the cursor on the currently selected card (green row)
    int cur = pfbneo_memcard::getCurrentListIndex(slot);
    setWindowTo(cur);
    updateLines();
}

void PfbneMemcardDialog::updateLines() {
    // reset the marquee scroll animation when the selection/list changes
    m_scrollName.offset = 0.f;
    m_scrollName.dir = 1;
    m_scrollName.holdUntil = m_scrollClock.getElapsedTime().asSeconds() + SCROLL_HOLD_START;
    m_scrollValue = m_scrollName;

    // title: PGM2 slot page -> 记忆卡; card list -> 记忆卡 - 玩家 N
    if (m_slotPage) {
        title->setString(TEXT_MENU_MEMCARD);
    } else if (pfbneo_memcard::isPGM2()) {
        title->setString(std::string(TEXT_MENU_MEMCARD) + " - 玩家 "
                         + std::to_string((long long) m_activeSlot + 1));
    } else {
        title->setString(TEXT_MENU_MEMCARD);
    }

    int total = m_slotPage ? pfbneo_memcard::getSlotCount() : pfbneo_memcard::getListCount(m_activeSlot);
    if (total <= 0) total = 0;
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

        if (m_slotPage) {
            // slot page: name (PGM2 卡槽 玩家 N：包含xx个角色卡文件) + value
            // (selected card name or 空插槽). name is always the skin color:
            // reset it explicitly so a green from the previous card list page
            // (the currently selected card) cannot leak onto this page.
            row.name->setString(pfbneo_memcard::getSlotName(idx));
            row.name->setVisibility(Visibility::Visible);
            row.name->setStyle(Text::Style::Regular);
            row.name->setOutlineThickness(0);
            row.name->setSizeMax(getSize().x * NAME_MAX_W, 0);
            row.name->setScrollEnabled(i == highlightIndex);
            row.name->setFillColor(row.defaultColor);

            row.value->setString(pfbneo_memcard::getSlotValue(idx));
            row.value->setVisibility(Visibility::Visible);
            row.value->setOutlineThickness(0);
            row.value->setScrollEnabled(i == highlightIndex);
            // green ONLY when a card is actually selected in this slot; an
            // empty slot keeps the original skin color
            if (pfbneo_memcard::hasSelectedCard(idx)) {
                row.value->setFillColor(MODIFIED_COLOR);
            } else {
                row.value->setFillColor(row.defaultColor);
            }
            if (i == highlightIndex) {
                highlightVisible = true;
            }
        } else {
            // card selection list: one row per option/file
            row.name->setString(pfbneo_memcard::getListName(m_activeSlot, idx));
            row.name->setVisibility(Visibility::Visible);
            row.name->setStyle(Text::Style::Regular);
            row.name->setOutlineThickness(0);
            row.name->setSizeMax(getSize().x * (VALUE_OFFSET_X + VALUE_MAX_W) - ROW_PAD_X, 0);
            row.name->setScrollEnabled(i == highlightIndex);
            // green = the card currently selected in this slot
            if (pfbneo_memcard::isListCurrent(m_activeSlot, idx)) {
                row.name->setFillColor(MODIFIED_COLOR);
            } else {
                row.name->setFillColor(row.defaultColor);
            }
            if (i == highlightIndex) {
                highlightVisible = true;
            }
        }
    }

    // move the highlight to the selected row
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

void PfbneMemcardDialog::onKeyUp() {
    int total = m_slotPage ? pfbneo_memcard::getSlotCount() : pfbneo_memcard::getListCount(m_activeSlot);
    if (total <= 0) return;
    int cur = optionIndex + highlightIndex;
    int target = cur <= 0 ? total - 1 : cur - 1;
    setWindowTo(target);
    updateLines();
}

void PfbneMemcardDialog::onKeyDown() {
    int total = m_slotPage ? pfbneo_memcard::getSlotCount() : pfbneo_memcard::getListCount(m_activeSlot);
    if (total <= 0) return;
    int cur = optionIndex + highlightIndex;
    int target = cur >= total - 1 ? 0 : cur + 1;
    setWindowTo(target);
    updateLines();
}

void PfbneMemcardDialog::setWindowTo(int target) {
    int total = m_slotPage ? pfbneo_memcard::getSlotCount() : pfbneo_memcard::getListCount(m_activeSlot);
    int middle = maxLines / 2;
    int maxOption = std::max(0, total - maxLines);
    optionIndex = std::min(std::max(0, target - middle), maxOption);
    highlightIndex = target - optionIndex;
    if (highlightIndex < 0) highlightIndex = 0;
    if (highlightIndex >= maxLines) highlightIndex = maxLines - 1;
}

bool PfbneMemcardDialog::onInput(c2d::Input::Player *players) {
    if (!isVisible()) {
        return false;
    }

    unsigned int buttons = players[0].buttons;

    // B: close the dialog (saves the per-rom config)
    if (buttons & Input::Button::B) {
        close();
        return true;
    }

    int total = m_slotPage ? pfbneo_memcard::getSlotCount() : pfbneo_memcard::getListCount(m_activeSlot);
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

    // A: slot page -> open the card list of the highlighted player;
    // card list -> apply the selection (green row); PGM2 goes back to the
    // 4-player page, NEOGEO stays on the list - only B closes the dialog,
    // so a rapid double-press cannot skip past the menu item
    if (buttons & Input::Button::A) {
        int idx = optionIndex + highlightIndex;
        if (m_slotPage) {
            if (idx >= 0 && idx < total) {
                openSlotList(idx);
            }
        } else {
            // only mark the game for a restart when the selection actually
            // changed (the new card is re-inserted on reset)
            std::string before = pfbneo_memcard::getSlotValue(m_activeSlot);
            pfbneo_memcard::applySelection(m_activeSlot, idx);
            if (pfbneo_memcard::getSlotValue(m_activeSlot) != before) {
                m_changed = true;
                if (ui->getUiEmu()) {
                    ui->getUiEmu()->markSettingsAction(1);
                }
            }
            if (pfbneo_memcard::isPGM2()) {
                // back to the 4-player page, keep the slot highlighted
                m_slotPage = true;
                int slot = m_activeSlot;
                m_activeSlot = 0;
                setWindowTo(slot);
                updateLines();
            } else {
                // NEOGEO: keep the list open, the selected row turned green
                updateLines();
            }
        }
        return true;
    }

    // any other button: consume so the game/menu below does not react
    return true;
}

// advance one marquee state (bounce with pauses)
void PfbneMemcardDialog::scrollAdvance(ScrollState &st, float elapsed, float dt, float limit) {
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

void PfbneMemcardDialog::onUpdate() {
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

void PfbneMemcardDialog::onDraw(c2d::Transform &transform, bool draw) {
    SkinnedRectangle::onDraw(transform, draw);
}