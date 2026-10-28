//
// pfbneo cheat dialog implementation.
//

#include "pfbneo_ui_cheat.h"
#include "fbneo/pfbneo_cheat.h"

#include <cstdarg>
#include <algorithm>

using namespace c2d;
using namespace pemu;

// tiny helper to trace the cheat dialog (log file: /switch/cheat_dialog.log,
// controlled by the master switch PEMU_ENABLE_DEBUG_LOG in pemu.h, OFF by default)
#if PEMU_ENABLE_DEBUG_LOG
static void dialogLog(const char *fmt, ...) {
    FILE *f = fopen("/switch/cheat_dialog.log", "a");
    if (f) {
        va_list args;
        va_start(args, fmt);
        vfprintf(f, fmt, args);
        va_end(args);
        fputc('\n', f);
        fclose(f);
    }
}
#else
static void dialogLog(const char *fmt, ...) { }
#endif

// dialog geometry (overrides the STATES_MENU skin rect):
//   width 640, top edge at y=180, height 540 (180 -> bottom of the 720px screen)
#define DIALOG_WIDTH   (640.0f)
#define DIALOG_TOP_Y   (180.0f)
#define DIALOG_HEIGHT  (540.0f)
#define ROW_PAD_X      (24.0f)
// rows start a bit lower than the title so the first line is not glued to it
// (title bottom ~ y=41; 56 leaves about half a line of breathing room).
#define ROWS_TOP_Y     (56.0f)
#define ROWS_BOTTOM_PAD (8.0f)
#define NAME_MAX_W     (0.55f)
#define VALUE_OFFSET_X (0.60f)
#define VALUE_MAX_W    (0.38f)
// full width used by a selectable row: name (0..0.55) + value (0.60..0.98).
// non-selectable rows show only the name, which may use this whole width.
#define NAME_MAX_W_FULL (VALUE_OFFSET_X + VALUE_MAX_W)
// right edge of the value column: inside the highlight box (box right edge
// is at width - ROW_PAD_X), so a right-aligned value never leaves the box.
#define VALUE_RIGHT_X (DIALOG_WIDTH - ROW_PAD_X)

// green used for option values that were changed away from their default
// (Material Green A700 #00C853: bright enough to spot at a glance on the dark
// skin, not as glaring as pure #00FF00)
static const Color MODIFIED_COLOR(0, 200, 83, 255);

// marquee scroll tuning (same feel as the rom list)
static const float SCROLL_HOLD_START = 0.5f;
static const float SCROLL_HOLD_EDGE = 0.65f;
static const float SCROLL_SPEED = 12.f;

PfbneCheatDialog::PfbneCheatDialog(UiMain *main) : SkinnedRectangle(main, {"STATES_MENU"}) {
    ui = main;

    // size: user wants a wider-but-shorter-than-before dialog: width 640,
    // tall box from y=180 to the bottom of the screen.
    setSize(DIALOG_WIDTH, DIALOG_HEIGHT);

    // title
    title = new SkinnedText(ui, {"STATES_MENU", "TITLE_TEXT"});
    title->setOrigin(Origin::Center);
    title->setPosition(getSize().x / 2, 28);
    SkinnedRectangle::add(title);

    // highlight (selection box): styled like the menus, animated gradient
    highlight = new RectangleShape({16, 16});
    ui->getSkin()->loadRectangleShape(highlight, {"SKIN_CONFIG", "HIGHLIGHT"});
    SkinnedRectangle::add(highlight);
    highlight->setVisibility(Visibility::Hidden);

    // rows: one per cheat, cheat name (left) + current option value (right),
    // SAME text setup as the UiMenu lines (OPTIONS_MENU > ITEMS_TEXT group:
    // font, character size, color, outline) so the dialog rows look exactly
    // like the menu items (no extra boldness).
    auto textGroup = ui->getSkin()->getText({"OPTIONS_MENU", "ITEMS_TEXT"});
    Font *font = ui->getSkin()->getFont();
    lineHeight = (float) textGroup.size + (2 * ui->getScaling().y);
    // fill the box: rows start below the title (ROWS_TOP_Y) and extend down to
    // a small margin above the bottom edge. no artificial 16-row cap anymore.
    maxLines = (int) ((getSize().y - ROWS_TOP_Y - ROWS_BOTTOM_PAD) / lineHeight);
    if (maxLines > 24) {
        maxLines = 24;
    }
    for (int i = 0; i < maxLines; i++) {
        Row row;
        row.name = new Text("", textGroup.size, font);
        row.name->setFillColor(textGroup.color);
        row.name->setOutlineThickness(textGroup.outlineSize);
        row.name->setOutlineColor(textGroup.outlineColor);
        row.name->setOrigin(Origin::Left);
        row.name->setPosition(ROW_PAD_X,
                              ROWS_TOP_Y + (float) i * lineHeight + lineHeight / 2);
        row.name->setSizeMax(getSize().x * NAME_MAX_W, 0);
        row.name->setVisibility(Visibility::Hidden);
        SkinnedRectangle::add(row.name);
        row.defaultOutline = textGroup.outlineSize;

        row.value = new Text("", textGroup.size, font);
        row.value->setFillColor(textGroup.color);
        row.value->setOutlineThickness(textGroup.outlineSize);
        row.value->setOutlineColor(textGroup.outlineColor);
        // value column is right-aligned: its right edge is fixed at the inner
        // right edge of the highlight box, text grows to the left.
        row.value->setOrigin(Origin::Right);
        row.value->setPosition(VALUE_RIGHT_X,
                               ROWS_TOP_Y + (float) i * lineHeight + lineHeight / 2);
        row.value->setSizeMax(getSize().x * VALUE_MAX_W, 0);
        row.value->setVisibility(Visibility::Hidden);
        SkinnedRectangle::add(row.value);
        row.defaultColor = textGroup.color;

        rows.push_back(row);
    }

    SkinnedRectangle::setVisibility(Visibility::Hidden);
}

void PfbneCheatDialog::open() {
    // the dialog is pre-attached to the ui tree by PFBAUiEmu::attachToUi()
    // (right after UiMain::init(), like UiMenuState). no runtime attach here.
    //
    // POSITION: the STATES_MENU skin rect anchor (origin) is NOT top-left, so
    // set a clean top-left anchor and center the 640x540 box horizontally:
    //   x = (1280 - 640) / 2 = 320, top edge at y=180.
    setOrigin(Origin::TopLeft);
    setPosition((ui->getSize().x - DIALOG_WIDTH) / 2, DIALOG_TOP_Y);

    optionIndex = 0;
    highlightIndex = 0;

    // snapshot the default option of every cheat: a value that differs from
    // this snapshot is drawn in green (user-modified). all cheats start from
    // option 0 (Disabled) - snapshot 0, NOT the current selection, so a
    // previously modified value stays green when the dialog is reopened
    // during the same game session.
    defaultOptions.clear();
    int count = pfbneo_cheat::getCheatCount();
    for (int i = 0; i < count; i++) {
        defaultOptions.push_back(0);
    }

    // the initial highlight must not land on a cheat without selectable
    // options: move down to the first selectable row.
    if (count > 0) {
        int guard = 0;
        while (!isSelectable(optionIndex + highlightIndex) && guard++ < count) {
            // same "down" movement as onKeyDown()
            int middle = maxLines / 2;
            int index = optionIndex + highlightIndex;
            if (highlightIndex >= middle && index - middle < count - 1
                && optionIndex + maxLines < count) {
                optionIndex++;
            } else {
                highlightIndex++;
            }
            if (highlightIndex >= maxLines) {
                highlightIndex = maxLines - 1;
            }
            if (optionIndex + highlightIndex >= count) {
                highlightIndex = count - 1 - optionIndex;
                if (highlightIndex < 0) {
                    highlightIndex = 0;
                    optionIndex = count > 0 ? count - 1 : 0;
                }
            }
        }
    }

    updateLines();

    // force a CLEAN visibility state: Hidden -> Visible without tween, so the
    // render traversal (which checks visibility_current) always sees the
    // dialog as Visible. the two calls happen in the same frame -> no flicker.
    setVisibility(Visibility::Hidden, false);
    setVisibility(Visibility::Visible, false);

    dialogLog("dialog: open, this=%p, cheats=%d isVis=%d", (void *) this,
              count, isVisible() ? 1 : 0);
}

void PfbneCheatDialog::close() {
    setVisibility(Visibility::Hidden, false);
    // the rom menu was hidden when the dialog was opened (UiMenu A-key branch),
    // so resume the game directly.
    if (ui->getUiEmu()) {
        ui->getUiEmu()->resume();
    }
    dialogLog("dialog: close (resume game)");
}

bool PfbneCheatDialog::isSelectable(int cheatIndex) {
    return cheatIndex >= 0 && cheatIndex < pfbneo_cheat::getCheatCount()
           && pfbneo_cheat::getOptionCount(cheatIndex) > 0;
}

void PfbneCheatDialog::updateLines() {
    title->setString(TEXT_CHEATS);

    // reset the marquee scroll animation when the selection/list changes
    m_scrollName.offset = 0.f;
    m_scrollName.dir = 1;
    m_scrollName.holdUntil = m_scrollClock.getElapsedTime().asSeconds() + SCROLL_HOLD_START;
    m_scrollValue = m_scrollName;

    int count = pfbneo_cheat::getCheatCount();
    bool highlightVisible = false;

    for (int i = 0; i < maxLines; i++) {
        int idx = optionIndex + i;
        if (idx >= count) {
            // first row shows the "no cheat" hint when the list is empty
            if (count == 0 && i == 0) {
                rows[i].name->setString(TEXT_DIALOG_EMPTY);
                rows[i].name->setVisibility(Visibility::Visible);
                rows[i].name->setStyle(Text::Style::Regular);
                rows[i].name->setOutlineThickness(rows[i].defaultOutline);
                rows[i].value->setString("");
                rows[i].value->setVisibility(Visibility::Hidden);
                rows[i].name->setScrollEnabled(false);
            } else {
                rows[i].name->setVisibility(Visibility::Hidden);
                rows[i].value->setVisibility(Visibility::Hidden);
                rows[i].name->setScrollEnabled(false);
            }
            continue;
        }

        rows[i].name->setString(pfbneo_cheat::getCheatName(idx));
        rows[i].name->setVisibility(Visibility::Visible);

        int cur = pfbneo_cheat::getSelectedOption(idx);
        int nOpt = pfbneo_cheat::getOptionCount(idx);
        if (nOpt > 0) {
            // selectable cheat: regular (non-bold) name, no outline so the
            // text looks lighter next to the non-selectable (bold) rows
            rows[i].name->setStyle(Text::Style::Regular);
            rows[i].name->setOutlineThickness(0);
            rows[i].name->setSizeMax(getSize().x * NAME_MAX_W, 0);
            rows[i].value->setString(pfbneo_cheat::getOptionName(idx, cur));
            rows[i].value->setVisibility(Visibility::Visible);
            rows[i].value->setOutlineThickness(0);
            // modified value -> green, back to default -> skin color
            if (cur != defaultOptions[idx]) {
                rows[i].value->setFillColor(MODIFIED_COLOR);
            } else {
                rows[i].value->setFillColor(rows[i].defaultColor);
            }
            // marquee: only the highlighted row scrolls
            rows[i].name->setScrollEnabled((int) i == highlightIndex);
            rows[i].value->setScrollEnabled((int) i == highlightIndex);
            if (i == highlightIndex) {
                highlightVisible = true;
            }
        } else {
            // non-selectable cheat: keep the bold skin font + outline, and the
            // name may use the full row width (name + value column) so long
            // names are not truncated early. it is never highlighted.
            rows[i].name->setStyle(Text::Style::Bold);
            rows[i].name->setOutlineThickness(rows[i].defaultOutline);
            rows[i].name->setSizeMax(getSize().x * NAME_MAX_W_FULL - ROW_PAD_X, 0);
            rows[i].name->setScrollEnabled(false);
            rows[i].value->setString("");
            rows[i].value->setVisibility(Visibility::Hidden);
        }
    }

    // move the highlight to the selected row (only shown on a selectable row)
    if (highlight) {
        if (highlightVisible) {
            highlight->setSize(getSize().x - 2 * ROW_PAD_X, lineHeight - 6);
            highlight->setPosition(ROW_PAD_X,
                                   ROWS_TOP_Y + (float) highlightIndex * lineHeight);
            highlight->setVisibility(Visibility::Visible);
        } else {
            highlight->setVisibility(Visibility::Hidden);
        }
    }
}

void PfbneCheatDialog::onKeyUp() {
    int total = pfbneo_cheat::getCheatCount();
    if (total <= 0) {
        return;
    }

    // move up, then keep moving until the highlighted row is selectable
    for (int guard = 0; guard <= total; guard++) {
        int index = optionIndex + highlightIndex;
        int middle = maxLines / 2;

        if (highlightIndex <= middle && index - middle > 0) {
            optionIndex--;
        } else {
            highlightIndex--;
        }

        if (highlightIndex < 0) {
            highlightIndex = total < maxLines - 1 ? total - 1 : maxLines - 1;
            optionIndex = (total - 1) - highlightIndex;
        }
        if (optionIndex < 0) {
            optionIndex = 0;
        }

        if (isSelectable(optionIndex + highlightIndex)) {
            break;
        }
    }

    updateLines();
}

void PfbneCheatDialog::onKeyDown() {
    int total = pfbneo_cheat::getCheatCount();
    if (total <= 0) {
        return;
    }

    // move down, then keep moving until the highlighted row is selectable.
    // the window only scrolls (optionIndex++) while there are still hidden
    // rows below (optionIndex + maxLines < total); once the last items are
    // fully visible at the bottom, the highlight box moves down instead.
    for (int guard = 0; guard <= total; guard++) {
        int index = optionIndex + highlightIndex;
        int middle = maxLines / 2;

        if (highlightIndex >= middle && index - middle < total - 1
            && optionIndex + maxLines < total) {
            optionIndex++;
        } else {
            highlightIndex++;
        }

        if (highlightIndex >= maxLines) {
            highlightIndex = maxLines - 1;
        }

        // keep the highlighted row valid when the list is shorter than maxLines
        if (optionIndex + highlightIndex >= total) {
            highlightIndex = total - 1 - optionIndex;
            if (highlightIndex < 0) {
                highlightIndex = 0;
                optionIndex = total > 0 ? total - 1 : 0;
            }
        }

        if (isSelectable(optionIndex + highlightIndex)) {
            break;
        }
    }

    updateLines();
}

bool PfbneCheatDialog::onInput(c2d::Input::Player *players) {
    if (!isVisible()) {
        return false;
    }

    unsigned int buttons = players[0].buttons;
    int count = pfbneo_cheat::getCheatCount();

    // B: close the dialog and resume the game
    if (buttons & Input::Button::B) {
        close();
        return true;
    }

    // empty list: consume input, nothing to change
    if (count <= 0) {
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

    // LEFT/RIGHT: change the option of the highlighted cheat immediately
    if (buttons & Input::Button::Left || buttons & Input::Button::Right) {
        int idx = optionIndex + highlightIndex;
        int nOpt = pfbneo_cheat::getOptionCount(idx);
        if (nOpt > 1) {
            int cur = pfbneo_cheat::getSelectedOption(idx);
            if (buttons & Input::Button::Left) {
                cur = (cur + nOpt - 1) % nOpt;
            } else {
                cur = (cur + 1) % nOpt;
            }
            pfbneo_cheat::selectOption(idx, cur);
            updateLines();
            dialogLog("dialog: cheat[%d] option -> %d (%s)", idx, cur,
                      pfbneo_cheat::getOptionName(idx, cur));
        }
        return true;
    }

    // A and any other button: consume so the game/menu below does not react
    return true;
}

// advance one marquee state (bounce with pauses). returns true when the text
// is currently scrolling (not on hold), so callers can skip the clock sync.
void PfbneCheatDialog::scrollAdvance(ScrollState &st, float elapsed, float dt, float limit) {
    if (elapsed < st.holdUntil) {
        return;  // pause at the start / at an edge
    }
    st.offset += (float) st.dir * dt * SCROLL_SPEED;
    if (st.offset >= limit) {
        // tail reached the right edge: pause, then bounce back right
        st.offset = limit;
        st.dir = -1;
        st.holdUntil = elapsed + SCROLL_HOLD_EDGE;
    } else if (st.offset <= 0.f) {
        // head reached the left edge: pause, then bounce back left
        st.offset = 0.f;
        st.dir = 1;
        st.holdUntil = elapsed + SCROLL_HOLD_EDGE;
    }
}

void PfbneCheatDialog::onUpdate() {
    SkinnedRectangle::onUpdate();

    // animated highlight outline (same two-color gradient as the menus)
    if (highlight && highlight->isVisible()) {
        highlight->setOutlineColor(ui->getSkin()->getHighlightGradientColor());
    }

    // marquee scroll animation for the highlighted (selectable) row:
    // name and value scroll INDEPENDENTLY - only the side that is too long
    // for its column moves; the other side stays put.
    if (!rows.empty() && highlightIndex >= 0 && highlightIndex < (int) rows.size()
        && highlight->isVisible()) {
        auto *name = rows[highlightIndex].name;
        auto *value = rows[highlightIndex].value;
        float elapsed = m_scrollClock.getElapsedTime().asSeconds();
        float dt = std::min(elapsed - m_lastScrollTime, 0.1f);
        m_lastScrollTime = elapsed;

        // name column
        if (name && name->isScrollNeeded()) {
            float limit = std::max(0.f, name->getScrollFullWidth() - name->getSizeMax().x);
            scrollAdvance(m_scrollName, elapsed, dt, limit);
            name->setScrollOffset(m_scrollName.offset);
        } else if (name) {
            name->setScrollOffset(0.f);
        }

        // value column (right-aligned: offset 0 = tail flush right, moving
        // towards limit reveals the head)
        if (value && value->isScrollNeeded()) {
            float limit = std::max(0.f, value->getScrollFullWidth() - value->getSizeMax().x);
            scrollAdvance(m_scrollValue, elapsed, dt, limit);
            value->setScrollOffset(m_scrollValue.offset);
        } else if (value) {
            value->setScrollOffset(0.f);
        }
    }

    // render-trace: if this logs, the update pass reaches the dialog.
    // throttled so the file does not grow unbounded.
    if (isVisible() && (m_logCounter++ % 30) == 0) {
        dialogLog("dialog::onUpdate vis=VISIBLE");
    }
}

void PfbneCheatDialog::onDraw(c2d::Transform &transform, bool draw) {
    // render-trace: throttled entry log (the position/visibility bug is fixed;
    // this stays for one more validation round, then it will be removed).
    if ((m_logCounter++ % 60) == 0) {
        dialogLog("dialog::onDraw draw=%d isVis=%d pos=(%.1f,%.1f) size=(%.1f,%.1f)",
                  draw ? 1 : 0, isVisible() ? 1 : 0,
                  getPosition().x, getPosition().y,
                  getSize().x, getSize().y);
    }
    SkinnedRectangle::onDraw(transform, draw);
}
