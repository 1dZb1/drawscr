#pragma once
#include <windows.h>
#include <cstdint>
#include <string>
#include <vector>

namespace drawscr {
enum class Tool { Select, Pen, Arrow, Line, Rectangle, Marker, Text, Redact };

struct Mark {
    Tool tool = Tool::Pen;
    std::vector<POINT> points;
    COLORREF color = RGB(255, 66, 66);
    float width = 3.0f;
    float text_size = 0.0f; // Physical pixels; zero supports older width-derived marks.
    std::wstring text;
};

RECT normalized(POINT a, POINT b);
RECT intersection(RECT a, RECT b);
bool empty(RECT r);
RECT mark_bounds(const Mark& mark);
int hit_test(const std::vector<Mark>& marks, POINT p);
void translate(Mark& mark, int dx, int dy);
RECT place_toolbar(RECT selection, RECT monitor, SIZE toolbar, int gap);
void draw_marks(HDC dc, const std::vector<Mark>& marks, int selected = -1,
                POINT screen_origin = {});
void draw_marks_layer(uint8_t* pixels, int width, int height, const std::vector<Mark>& marks,
                      int selected = -1, POINT screen_origin = {},
                      const RECT* local_clip = nullptr);

class History {
  public:
    const std::vector<Mark>& current() const {
        return states_[index_];
    }
    void commit(std::vector<Mark> marks);
    bool undo();
    bool redo();
    bool can_undo() const {
        return index_ > 0;
    }
    bool can_redo() const {
        return index_ + 1 < states_.size();
    }

  private:
    std::vector<std::vector<Mark>> states_{1};
    size_t index_ = 0;
};
}
