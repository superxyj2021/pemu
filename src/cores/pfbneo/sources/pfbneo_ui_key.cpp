//
// pfbneo key settings dialog implementation.
//

#include "pfbneo_ui_key.h"
#include "fbneo/pfbneo_keybind.h"

#include <algorithm>

using namespace c2d;
using namespace pemu;

// dialog geometry (same as the cheat dialog):
//   width 640, top edge at y=180, height 540 (180 -> bottom of the 720px screen)
#define DIALOG_WIDTH   (640.0f)
#define DIALOG_TOP_Y   (180.0f)
#define DIALOG_HEIGHT  (540.0f)
#define ROW_PAD_X      (24.0f)
// rows start a bit lower than the title so the first line is not glued to it
#define ROWS_TOP_Y     (56.0f)
#define ROWS_BOTTOM_PAD (8.0f)
// three columns: name (left), binding (middle), turbo (right)
#define NAME_MAX_W     (0.46f)
#define BIND_CENTER_X  (0.50f)
#define TURBO_MAX_W    (0.30f)
// right edge of the turbo column: inside the highlight box
#define TURBO_RIGHT_X  (DIALOG_WIDTH - ROW_PAD_X)

// green used for "on" turbo values and bound keys (Material Green A700)
static const Color MODIFIED_COLOR(0, 200, 83, 255);

// marquee scroll tuning (same feel as the rom list)
static const float SCROLL_HOLD_START = 0.5f;
static const float SCROLL_HOLD_EDGE = 0.65f;
static const float SCROLL_SPEED = 12.f;

PfbneKeyDialog::PfbneKeyDialog(UiMain *main) : SkinnedRectangle(main, {"STATES_MENU"}) {
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

    // rows: name + binding icon/text + turbo text (item text follows the rom
    // menu (UiMenu) style, with a slightly larger font size on the popup
    // dialogs)
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

        row.bind = new Sprite();
        row.bind->setOrigin(Origin::Center);
        row.bind->setPosition(getSize().x * BIND_CENTER_X, y);
        row.bind->setVisibility(Visibility::Hidden);
        SkinnedRectangle::add(row.bind);

        row.bindText = new Text("", textGroup.size, font);
        row.bindText->setFillColor(textGroup.color);
        row.bindText->setOutlineThickness(textGroup.outlineSize);
        row.bindText->setOutlineColor(textGroup.outlineColor);
        row.bindText->setOrigin(Origin::Center);
        row.bindText->setPosition(getSize().x * BIND_CENTER_X, y);
        row.bindText->setVisibility(Visibility::Hidden);
        SkinnedRectangle::add(row.bindText);

        row.turbo = new Text("", textGroup.size, font);
        row.turbo->setFillColor(textGroup.color);
        row.turbo->setOutlineThickness(textGroup.outlineSize);
        row.turbo->setOutlineColor(textGroup.outlineColor);
        row.turbo->setOrigin(Origin::Right);
        row.turbo->setPosition(TURBO_RIGHT_X, y);
        row.turbo->setSizeMax(getSize().x * TURBO_MAX_W, 0);
        row.turbo->setVisibility(Visibility::Hidden);
        SkinnedRectangle::add(row.turbo);
        row.defaultColor = textGroup.color;

        rows.push_back(row);
    }

    SkinnedRectangle::setVisibility(Visibility::Hidden);
}

void PfbneKeyDialog::open() {
    // the dialog is pre-attached to the ui tree by PFBAUiEmu::attachToUi()
    // (right after UiMain::init(), like the cheat dialog).
    setOrigin(Origin::TopLeft);
    setPosition((ui->getSize().x - DIALOG_WIDTH) / 2, DIALOG_TOP_Y);

    optionIndex = 0;
    highlightIndex = 0;
    title->setString(TEXT_MENU_KEYS);

    updateLines();

    // force a CLEAN visibility state: Hidden -> Visible without tween
    setVisibility(Visibility::Hidden, false);
    setVisibility(Visibility::Visible, false);
}

void PfbneKeyDialog::close() {
    setVisibility(Visibility::Hidden, false);

    // persist the per-rom bindings + turbo settings
    if (ui->getConfig()) {
        ui->getConfig()->saveGame();
    }

    // return to the ui menu, highlighting the option that opened the dialog
    // (the rom menu was hidden when the dialog was opened)
    if (ui->getUiMenu()) {
        ui->getUiMenu()->setRestoreOption(TEXT_MENU_KEYS);
        ui->getUiMenu()->load(true);
    }
}

void PfbneKeyDialog::setVisibility(c2d::Visibility visibility, bool tweenPlay) {
    // keep the main ui blurred behind the dialog, exactly like the
    // save-states dialog (only when the rom list is visible, i.e. opened
    // from the rom screen; in-game the rom list is hidden anyway)
    if (ui->getUiRomList() && ui->getUiRomList()->isVisible()) {
        ui->getUiRomList()->getBlur()->setVisibility(visibility, true);
    }
    SkinnedRectangle::setVisibility(visibility, tweenPlay);
}

void PfbneKeyDialog::updateLines() {
    // reset the marquee scroll animation when the selection/list changes
    m_scrollName.offset = 0.f;
    m_scrollName.dir = 1;
    m_scrollName.holdUntil = m_scrollClock.getElapsedTime().asSeconds() + SCROLL_HOLD_START;
    m_scrollTurbo = m_scrollName;

    int keyCount = pfbneo_keybind::getRowCount();
    int total = keyCount + 3;   // +3 = turbo rate row + deadzone row + spacer row
    bool highlightVisible = false;

    for (int i = 0; i < maxLines; i++) {
        int idx = optionIndex + i;
        Row &row = rows[i];
        row.bind->setVisibility(Visibility::Hidden);
        row.bindText->setVisibility(Visibility::Hidden);
        row.turbo->setVisibility(Visibility::Hidden);

        if (idx >= total) {
            row.name->setVisibility(Visibility::Hidden);
            row.name->setScrollEnabled(false);
            continue;
        }

        // name column
        if (idx == 0) {
            row.name->setString(TEXT_KEYS_RATE);
        } else if (idx == 1) {
            row.name->setString(TEXT_KEYS_DEADZONE);
        } else if (idx == 2) {
            // spacer row between the shared settings and the key list
            row.name->setString("");
        } else {
            row.name->setString(pfbneo_keybind::getRowName(idx - 3));
        }
        row.name->setVisibility(Visibility::Visible);
        row.name->setStyle(Text::Style::Regular);
        row.name->setOutlineThickness(0);
        row.name->setSizeMax(getSize().x * NAME_MAX_W, 0);
        row.name->setScrollEnabled(i == highlightIndex);

        if (idx >= 3) {
            int keyRow = idx - 3;
            // effective binding: the custom binding, or the default JOY
            // mapping for this logical key (every logical key has one);
            // -1 when the row was cleared with "-" or has no binding at all
            int key = pfbneo_keybind::getEffectiveKey(keyRow);
            if (key >= 0) {
                // show the bound physical button (icon, or name when the skin
                // has no icon for it) - same display as the old key mapping
                Skin::Button *button = ui->getSkin()->getButton(key);
                if (button && button->texture) {
                    row.bind->setTexture(button->texture, true);
                    float scaling = std::min(
                            getSize().x / (float) row.bind->getSize().x,
                            lineHeight / (float) row.bind->getSize().y);
                    if (scaling > 1.f) scaling = 1.f;
                    row.bind->setScale(scaling, scaling);
                    row.bind->setVisibility(Visibility::Visible);
                } else if (button) {
                    row.bindText->setString(button->name);
                    row.bindText->setVisibility(Visibility::Visible);
                }
            }
            // else: no binding at all -> the middle column stays empty

            // turbo column: only shown when the row can actually be set (effective
            // binding exists); rows without one stay empty instead of "N/A"
            if (pfbneo_keybind::isTurboAllowed(keyRow)) {
                c2d::config::Option *turboOpt = pfbneo_keybind::getTurboOption(keyRow);
                bool bound = key >= 0;
                if (turboOpt && bound) {
                    row.turbo->setString(turboOpt->getArrayIndex()
                                         ? TEXT_KEYS_TURBO_ON : TEXT_KEYS_TURBO_OFF);
                    row.turbo->setFillColor(turboOpt->getArrayIndex() ? MODIFIED_COLOR : row.defaultColor);
                    row.turbo->setVisibility(Visibility::Visible);
                    row.turbo->setScrollEnabled(i == highlightIndex);
                }
            }
        } else if (idx == 2) {
            // spacer row: nothing in the value column
        } else {
            // special rows: turbo column shows the current value
            c2d::config::Option *valOpt = idx == 0 ? pfbneo_keybind::getRateOption()
                                                   : ui->getConfig()->get(PEMUConfig::OptId::JOY_DEADZONE, true);
            if (valOpt) {
                row.turbo->setString(valOpt->getString());
                // green when changed away from the default (same as the DIP
                // dialog rows)
                row.turbo->setFillColor(valOpt->isDefault() ? row.defaultColor : MODIFIED_COLOR);
                row.turbo->setVisibility(Visibility::Visible);
                row.turbo->setScrollEnabled(i == highlightIndex);
            }
        }

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

void PfbneKeyDialog::onKeyUp() {
    int total = pfbneo_keybind::getRowCount() + 3;
    if (total <= 0) return;
    int cur = optionIndex + highlightIndex;
    int target = cur;
    // move up, skipping the spacer row (index 2)
    do {
        target = target <= 0 ? total - 1 : target - 1;
    } while (target == 2);
    setWindowTo(target);
    updateLines();
}

void PfbneKeyDialog::onKeyDown() {
    int total = pfbneo_keybind::getRowCount() + 3;
    if (total <= 0) return;
    int cur = optionIndex + highlightIndex;
    int target = cur;
    // move down, skipping the spacer row (index 2)
    do {
        target = target >= total - 1 ? 0 : target + 1;
    } while (target == 2);
    setWindowTo(target);
    updateLines();
}

void PfbneKeyDialog::setWindowTo(int target) {
    int total = pfbneo_keybind::getRowCount() + 3;
    int middle = maxLines / 2;
    int maxOption = std::max(0, total - maxLines);
    optionIndex = std::min(std::max(0, target - middle), maxOption);
    highlightIndex = target - optionIndex;
    if (highlightIndex < 0) highlightIndex = 0;
    if (highlightIndex >= maxLines) highlightIndex = maxLines - 1;
}

bool PfbneKeyDialog::onInput(c2d::Input::Player *players) {
    if (!isVisible()) {
        return false;
    }

    unsigned int buttons = players[0].buttons;

    // B: close the dialog (saves the per-rom config)
    if (buttons & Input::Button::B) {
        close();
        return true;
    }

    int total = pfbneo_keybind::getRowCount() + 3;

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

    // LEFT/RIGHT: rate row -> turbo rate; deadzone row -> joystick deadzone;
    // key row -> per-key turbo on/off
    if (buttons & Input::Button::Left || buttons & Input::Button::Right) {
        if (idx == 0) {
            c2d::config::Option *rateOpt = pfbneo_keybind::getRateOption();
            if (rateOpt && rateOpt->getArray().size() > 1) {
                if (buttons & Input::Button::Left) {
                    rateOpt->setArrayMovePrev();
                } else {
                    rateOpt->setArrayMoveNext();
                }
                updateLines();
            }
        } else if (idx == 1) {
            c2d::config::Option *deadzoneOpt = ui->getConfig()->get(PEMUConfig::OptId::JOY_DEADZONE, true);
            if (deadzoneOpt && deadzoneOpt->getArray().size() > 1) {
                if (buttons & Input::Button::Left) {
                    deadzoneOpt->setArrayMovePrev();
                } else {
                    deadzoneOpt->setArrayMoveNext();
                }
                updateLines();
            }
        } else if (idx == 2) {
            // spacer row: nothing to change
        } else {
            int keyRow = idx - 3;
            c2d::config::Option *bindOpt = pfbneo_keybind::getBindOption(keyRow);
            c2d::config::Option *turboOpt = pfbneo_keybind::getTurboOption(keyRow);
            // only toggle when the key has an effective binding (custom, or
            // the default JOY mapping - all logical keys have one)
            int key = pfbneo_keybind::getEffectiveKey(keyRow);
            if (turboOpt && key >= 0) {
                if (buttons & Input::Button::Left) {
                    turboOpt->setArrayMovePrev();
                } else {
                    turboOpt->setArrayMoveNext();
                }
                updateLines();
            }
        }
        return true;
    }

    // "-" (Select): clear the highlighted key's binding. Works for custom
    // bindings AND for keys that only use the default JOY mapping: the row is
    // marked as cleared (input disabled) until a new key is captured with A
    // or the default is restored with "+" (Start). Clearing cannot be undone
    // with "-" itself.
    if (buttons & Input::Button::Select) {
        int keyRow = idx - 3;
        c2d::config::Option *bindOpt = pfbneo_keybind::getBindOption(keyRow);
        if (bindOpt) {
            int cur = bindOpt->getInteger();
            if (cur != KEYBIND_BIND_CLEARED && (cur >= 0 || pfbneo_keybind::getDefaultKey(keyRow) >= 0)) {
                bindOpt->setInteger(KEYBIND_BIND_CLEARED);
                updateLines();
            }
        }
        return true;
    }

    // "+" (Start): restore the default binding for the highlighted key.
    // Only meaningful for keys that have a default JOY mapping (driver input
    // rows); combo macro rows have no default, so "+" does nothing there.
    if (buttons & Input::Button::Start) {
        int keyRow = idx - 3;
        c2d::config::Option *bindOpt = pfbneo_keybind::getBindOption(keyRow);
        if (bindOpt && pfbneo_keybind::getDefaultKey(keyRow) >= 0) {
            bindOpt->setInteger(-1);   // -1 = no custom binding -> default JOY mapping
            updateLines();
        }
        return true;
    }

    // A: capture a new physical binding (same flow as the old key mapping)
    if (buttons & Input::Button::A) {
        int keyRow = idx - 3;
        c2d::config::Option *bindOpt = pfbneo_keybind::getBindOption(keyRow);
        if (bindOpt) {
            int new_key = 0;
            int res = ui->getUiMessageBox()->show(
                    TEXT_MSG_NEW_INPUT, TEXT_MSG_PRESS_A_BUTTON, "", "", &new_key, 9);
            if (res != MessageBox::TIMEOUT) {
                bindOpt->setInteger(new_key);
                updateLines();
            }
        }
        return true;
    }

    // A on the special rows and any other button: consume
    return true;
}

// advance one marquee state (bounce with pauses)
void PfbneKeyDialog::scrollAdvance(ScrollState &st, float elapsed, float dt, float limit) {
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

void PfbneKeyDialog::onUpdate() {
    SkinnedRectangle::onUpdate();

    // animated highlight outline (same two-color gradient as the menus)
    if (highlight && highlight->isVisible()) {
        highlight->setOutlineColor(ui->getSkin()->getHighlightGradientColor());
    }

    // marquee scroll animation for the highlighted row: name and turbo scroll
    // independently - only the side that is too long for its column moves.
    if (!rows.empty() && highlightIndex >= 0 && highlightIndex < (int) rows.size()
        && highlight->isVisible()) {
        auto *name = rows[highlightIndex].name;
        auto *turbo = rows[highlightIndex].turbo;
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

        if (turbo && turbo->isScrollNeeded()) {
            float limit = std::max(0.f, turbo->getScrollFullWidth() - turbo->getSizeMax().x);
            scrollAdvance(m_scrollTurbo, elapsed, dt, limit);
            turbo->setScrollOffset(m_scrollTurbo.offset);
        } else if (turbo) {
            turbo->setScrollOffset(0.f);
        }
    }
}

void PfbneKeyDialog::onDraw(c2d::Transform &transform, bool draw) {
    SkinnedRectangle::onDraw(transform, draw);
}