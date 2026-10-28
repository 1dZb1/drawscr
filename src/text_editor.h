#pragma once
#include "model.h"
#include "text_overlay.h"
#include "tooltips.h"
#include <string>
namespace drawscr {
constexpr UINT WM_DRAW_TEXT_APPLY = WM_APP + 31;
constexpr UINT WM_DRAW_TEXT_CANCEL = WM_APP + 32;
constexpr UINT WM_DRAW_TEXT_CHANGED = WM_APP + 34;
// Creates the real native input sink, clipped outside its parent's client area.
HWND create_text_input_window(HWND parent, UINT dpi, const std::wstring& initial);
class TextEditor {
  public:
    TextEditor() = default;
    ~TextEditor();
    bool create(HWND owner, POINT anchor, RECT selection, const std::wstring& text, COLORREF color,
                int size_dip, float existing_size_px, bool require_exclusion);
    std::wstring text() const;
    COLORREF color() const {
        return color_;
    }
    int size_dip() const {
        return size_;
    }
    float size_px() const {
        return original_px_ > 0 && size_ == initial_size_
                   ? original_px_
                   : static_cast<float>(MulDiv(size_, static_cast<int>(dpi_), 96));
    }
    HWND window() const {
        return hwnd_;
    }
    const InlineTextView& view() const {
        return view_;
    }
    void focus(const wchar_t* notice = nullptr);
    void refresh_draft();
    void select_at(POINT screen, bool extend);
    void prepare_apply();
  private:
    static LRESULT CALLBACK proc(HWND, UINT, WPARAM, LPARAM);
    static LRESULT CALLBACK edit_proc(HWND, UINT, WPARAM, LPARAM, UINT_PTR, DWORD_PTR);
    LRESULT message(UINT, WPARAM, LPARAM);
    void paint(HDC) const;
    void update_font();
    bool layout_controls();
    void position_ime();
    void click(POINT);
    HWND owner_ = nullptr, hwnd_ = nullptr, edit_ = nullptr;
    HFONT font_ = nullptr;
    Tooltips tips_;
    UINT dpi_ = 96;
    POINT anchor_{};
    RECT monitor_{}, selection_{};
    InlineTextView view_{};
    SIZE region_size_{};
    COLORREF color_ = RGB(244, 83, 81);
    int size_ = 24, initial_size_ = 24;
    float original_px_ = 0;
    size_t mouse_anchor_ = 0, composition_start_ = 0, composition_end_ = 0;
    bool composing_ = false;
    std::wstring composition_, notice_;
};
}
