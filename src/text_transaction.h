#pragma once
#include "model.h"
#include <string>

namespace drawscr {
constexpr int default_text_size = 24;
constexpr int min_text_size = 12;
constexpr int max_text_size = 96;
int load_text_size(const std::wstring& ini);
bool save_text_size(const std::wstring& ini, int size);
struct InlineTextLayout {
    RECT window{}, field{}, controls{}; // Physical screen coordinates.
    bool valid = false;
};
InlineTextLayout place_inline_text(POINT anchor, RECT monitor, RECT selection, UINT dpi,
                                   int size_dip, SIZE content = {}, bool overflow_preview = false);
enum class EscapeTarget { PassThrough, CancelText, CloseCapture };
EscapeTarget escape_target(bool modal, bool editing);

// An edit is private until applied; History is changed at most once.
class TextTransaction {
  public:
    bool begin_new(POINT anchor, COLORREF color, float size);
    bool begin_existing(const History& history, size_t index);
    bool apply(History& history, const std::wstring& text, COLORREF color, float size);
    void cancel();
    bool active() const {
        return active_;
    }
    bool existing() const {
        return active_ && existing_;
    }
    size_t index() const {
        return index_;
    }
    const Mark& original() const {
        return original_;
    }

  private:
    Mark original_;
    size_t index_ = 0;
    bool active_ = false, existing_ = false;
};
enum class TextApplyResult { Applied, PreferencesUnavailable, InvalidTransaction };
TextApplyResult commit_text_edit(TextTransaction& transaction, History& history,
                                 const std::wstring& text, COLORREF color, float size_px,
                                 int size_dip, const std::wstring& ini, bool allow_unpersisted);
}
