#ifndef WASTE_TUI_COMPONENTS_TABLE_H
#define WASTE_TUI_COMPONENTS_TABLE_H

#include <ftxui/component/component.hpp>
#include <ftxui/dom/elements.hpp>
#include <string>
#include <vector>
#include <functional>

namespace waste {
namespace components {

// Column definition for table
struct TableColumn {
    std::string header;
    int width;  // 0 = flexible
    enum class Align { Left, Center, Right } align = Align::Left;
};

// Table options
struct TableOptions {
    std::vector<TableColumn> columns;
    bool showHeader = true;
    bool showBorder = true;
    ftxui::Color accentColor = ftxui::Color::Cyan;
    std::function<void(int)> onSelect = nullptr;  // Called on Enter
    std::function<void(int)> onHighlight = nullptr;  // Called on selection change
};

// Create a table component
// - rows: vector of rows, each row is a vector of cell strings
// - selected: pointer to selected index (will be modified)
// - options: table configuration
ftxui::Component Table(
    std::vector<std::vector<std::string>>* rows,
    int* selected,
    TableOptions options
);

// Render a table as an Element (non-interactive)
ftxui::Element TableElement(
    const std::vector<std::vector<std::string>>& rows,
    const std::vector<TableColumn>& columns,
    int selected = -1,
    bool showHeader = true,
    ftxui::Color accentColor = ftxui::Color::Cyan
);

}  // namespace components
}  // namespace waste

#endif  // WASTE_TUI_COMPONENTS_TABLE_H
