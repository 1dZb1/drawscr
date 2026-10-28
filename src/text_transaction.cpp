#include "text_transaction.h"
#include <algorithm>

namespace drawscr {
int load_text_size(const std::wstring& ini) {
    const UINT value = GetPrivateProfileIntW(L"Text", L"Size", default_text_size, ini.c_str());
    return value >= min_text_size && value <= max_text_size ? static_cast<int>(value)
                                                            : default_text_size;
}
bool save_text_size(const std::wstring& ini, int size) {
    if (size < min_text_size || size > max_text_size)
        return false;
    const std::wstring value = std::to_wstring(size);
    return WritePrivateProfileStringW(L"Text", L"Size", value.c_str(), ini.c_str()) != FALSE;
}
InlineTextLayout place_inline_text(POINT anchor, RECT monitor, RECT selection, UINT dpi,
                                   int size_dip, SIZE content, bool overflow_preview) {
    InlineTextLayout out;
    if (!dpi)
        dpi = 96;
    if (monitor.right <= monitor.left || monitor.bottom <= monitor.top ||
        !PtInRect(&monitor, anchor))
        return out;
    auto px = [&](int value) { return MulDiv(value, static_cast<int>(dpi), 96); };
    size_dip = std::clamp(size_dip, min_text_size, max_text_size);
    int panel_width = std::min<int>(px(446), monitor.right - monitor.left),
        controls_height =
            std::min<int>(px(overflow_preview ? 104 : 70), monitor.bottom - monitor.top);
    int field_width = std::max(px(96), static_cast<int>(content.cx) + px(8));
    int field_height = std::max(px(size_dip + 8), static_cast<int>(content.cy) + px(6));
    if (panel_width <= 0 || controls_height <= 0)
        return out;
    LONG left = std::clamp<LONG>(anchor.x - px(12), monitor.left, monitor.right - panel_width);
    LONG right = left + panel_width,
         field_right = std::min<LONG>(anchor.x + field_width, monitor.right),
         field_bottom = std::min<LONG>(anchor.y + field_height, monitor.bottom);
    if (PtInRect(&selection, anchor)) {
        field_right = std::min(field_right, selection.right);
        field_bottom = std::min(field_bottom, selection.bottom);
    }
    field_right = std::max<LONG>(anchor.x + 1, field_right);
    field_bottom = std::max<LONG>(anchor.y + 1, field_bottom);
    LONG gap = px(6), control_top = 0;
    if (field_bottom + gap + controls_height <= monitor.bottom)
        control_top = field_bottom + gap;
    else if (anchor.y - gap - controls_height >= monitor.top)
        control_top = anchor.y - gap - controls_height;
    else if (anchor.y + 1 + gap + controls_height <= monitor.bottom) {
        field_bottom = monitor.bottom - gap - controls_height;
        control_top = field_bottom + gap;
    } else
        control_top = std::clamp<LONG>(anchor.y - controls_height, monitor.top,
                                       monitor.bottom - controls_height);
    out.field = {anchor.x, anchor.y, field_right, field_bottom};
    out.controls = {left, control_top, right, control_top + controls_height};
    out.window = out.controls;
    out.valid = out.window.right > out.window.left && out.window.bottom > out.window.top;
    return out;
}
EscapeTarget escape_target(bool modal, bool editing) {
    return modal     ? EscapeTarget::PassThrough
           : editing ? EscapeTarget::CancelText
                     : EscapeTarget::CloseCapture;
}
bool TextTransaction::begin_new(POINT anchor, COLORREF color, float size) {
    if (active_)
        return false;
    original_ = Mark{};
    original_.tool = Tool::Text;
    original_.points = {anchor};
    original_.color = color;
    original_.text_size = size;
    existing_ = false;
    active_ = true;
    return true;
}
bool TextTransaction::begin_existing(const History& history, size_t index) {
    if (active_ || index >= history.current().size() || history.current()[index].tool != Tool::Text)
        return false;
    original_ = history.current()[index];
    index_ = index;
    existing_ = true;
    active_ = true;
    return true;
}
bool TextTransaction::apply(History& history, const std::wstring& text, COLORREF color,
                            float size) {
    if (!active_)
        return false;
    // Applying an empty new field is a no-op; an existing item is left unchanged.
    if (text.empty()) {
        cancel();
        return true;
    }
    auto marks = history.current();
    Mark changed = original_;
    changed.text = text;
    changed.color = color;
    changed.text_size = size;
    if (existing_) {
        if (index_ >= marks.size())
            return false;
        const float previous =
            original_.text_size > 0 ? original_.text_size : std::max(16.0f, original_.width * 6);
        if (original_.text != text || original_.color != color || previous != size) {
            marks[index_] = std::move(changed);
            history.commit(std::move(marks));
        }
    } else {
        marks.push_back(std::move(changed));
        history.commit(std::move(marks));
    }
    cancel();
    return true;
}
void TextTransaction::cancel() {
    active_ = false;
    existing_ = false;
    original_ = Mark{};
}
TextApplyResult commit_text_edit(TextTransaction& transaction, History& history,
                                 const std::wstring& text, COLORREF color, float size_px,
                                 int size_dip, const std::wstring& ini, bool allow_unpersisted) {
    if (!transaction.active())
        return TextApplyResult::InvalidTransaction;
    if (!text.empty() && !ini.empty() && size_dip != load_text_size(ini) && !allow_unpersisted &&
        !save_text_size(ini, size_dip))
        return TextApplyResult::PreferencesUnavailable;
    return transaction.apply(history, text, color, size_px) ? TextApplyResult::Applied
                                                            : TextApplyResult::InvalidTransaction;
}
}
