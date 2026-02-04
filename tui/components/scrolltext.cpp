#include "scrolltext.h"
#include <ftxui/component/component_base.hpp>
#include <ftxui/component/event.hpp>
#include <algorithm>

namespace waste {
namespace components {

using namespace ftxui;

namespace {

class ScrollTextComponent : public ComponentBase {
public:
    ScrollTextComponent(
        std::vector<TextLine>* lines,
        int* scrollOffset,
        ScrollTextOptions options
    ) : lines_(lines), scrollOffset_(scrollOffset), options_(options) {}

    Element Render() override {
        if (!lines_ || lines_->empty()) {
            return text("(no messages)") | dim | center;
        }

        Elements lineElements;
        int totalLines = lines_->size();

        // Calculate visible range
        // scrollOffset_ is from the bottom (0 = at bottom, showing newest)
        int startIdx = std::max(0, totalLines - visibleLines_ - *scrollOffset_);
        int endIdx = std::min(totalLines, startIdx + visibleLines_);

        for (int i = startIdx; i < endIdx; ++i) {
            const auto& line = (*lines_)[i];
            Elements parts;

            auto prefix = text(line.prefix);
            if (line.prefixColor != Color::Default) {
                prefix = prefix | color(line.prefixColor);
            }
            parts.push_back(prefix);

            auto content = text(line.content);
            if (line.dim) {
                content = content | dim;
            }
            parts.push_back(content);

            lineElements.push_back(hbox(parts));
        }

        // Scroll indicator
        if (*scrollOffset_ > 0) {
            lineElements.push_back(
                text("── " + std::to_string(*scrollOffset_) + " more below ──") | dim | center
            );
        }

        return vbox(lineElements) | flex | reflect(box_);
    }

    bool OnEvent(Event event) override {
        if (!lines_ || lines_->empty()) return false;

        int maxOffset = std::max(0, (int)lines_->size() - visibleLines_);

        if (event == Event::PageUp) {
            *scrollOffset_ = std::min(maxOffset, *scrollOffset_ + visibleLines_ / 2);
            return true;
        }

        if (event == Event::PageDown) {
            *scrollOffset_ = std::max(0, *scrollOffset_ - visibleLines_ / 2);
            return true;
        }

        if (event == Event::Home) {
            *scrollOffset_ = maxOffset;
            return true;
        }

        if (event == Event::End) {
            *scrollOffset_ = 0;
            return true;
        }

        // Arrow keys for fine scrolling
        if (event == Event::ArrowUp) {
            *scrollOffset_ = std::min(maxOffset, *scrollOffset_ + 1);
            return true;
        }

        if (event == Event::ArrowDown) {
            *scrollOffset_ = std::max(0, *scrollOffset_ - 1);
            return true;
        }

        return false;
    }

    bool Focusable() const override { return true; }

    void SetVisibleLines(int lines) { visibleLines_ = lines; }

private:
    std::vector<TextLine>* lines_;
    int* scrollOffset_;
    ScrollTextOptions options_;
    Box box_;
    int visibleLines_ = 20;  // Will be updated based on actual size
};

}  // namespace

Component ScrollText(
    std::vector<TextLine>* lines,
    int* scrollOffset,
    ScrollTextOptions options
) {
    return Make<ScrollTextComponent>(lines, scrollOffset, options);
}

Element ScrollTextElement(
    const std::vector<TextLine>& lines,
    int scrollOffset,
    int visibleLines
) {
    if (lines.empty()) {
        return text("(no messages)") | dim | center;
    }

    Elements lineElements;
    int totalLines = lines.size();

    int startIdx = std::max(0, totalLines - visibleLines - scrollOffset);
    int endIdx = std::min(totalLines, startIdx + visibleLines);

    for (int i = startIdx; i < endIdx; ++i) {
        const auto& line = lines[i];
        Elements parts;

        auto prefix = text(line.prefix);
        if (line.prefixColor != Color::Default) {
            prefix = prefix | color(line.prefixColor);
        }
        parts.push_back(prefix);

        auto content = text(line.content);
        if (line.dim) {
            content = content | dim;
        }
        parts.push_back(content);

        lineElements.push_back(hbox(parts));
    }

    if (scrollOffset > 0) {
        lineElements.push_back(
            text("── " + std::to_string(scrollOffset) + " more below ──") | dim | center
        );
    }

    return vbox(lineElements);
}

}  // namespace components
}  // namespace waste
