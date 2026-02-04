#ifndef WASTE_TUI_COMPONENTS_SCROLLTEXT_H
#define WASTE_TUI_COMPONENTS_SCROLLTEXT_H

#include <ftxui/component/component.hpp>
#include <ftxui/dom/elements.hpp>
#include <string>
#include <vector>
#include <functional>

namespace waste {
namespace components {

// Line in scrollable text area
struct TextLine {
    std::string prefix;      // e.g., "[14:30] alice: "
    std::string content;     // The main text
    ftxui::Color prefixColor = ftxui::Color::Default;
    bool dim = false;        // For system messages
};

// Options for scrollable text
struct ScrollTextOptions {
    bool autoScroll = true;  // Auto-scroll to bottom on new content
    int maxLines = 1000;     // Maximum lines to keep in buffer
};

// Create a scrollable text component
// - lines: pointer to lines vector (will be read from)
// - scrollOffset: pointer to scroll offset (will be modified)
// - options: configuration
ftxui::Component ScrollText(
    std::vector<TextLine>* lines,
    int* scrollOffset,
    ScrollTextOptions options = {}
);

// Render scrollable text as Element (non-interactive)
ftxui::Element ScrollTextElement(
    const std::vector<TextLine>& lines,
    int scrollOffset,
    int visibleLines
);

}  // namespace components
}  // namespace waste

#endif  // WASTE_TUI_COMPONENTS_SCROLLTEXT_H
