#include "table.h"
#include <ftxui/component/component_base.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/dom/elements.hpp>
#include <algorithm>

namespace waste {
namespace components {

using namespace ftxui;

namespace {

// Truncate or pad string to width
std::string fitToWidth(const std::string& str, int width, TableColumn::Align align) {
    if (width <= 0) return str;

    if ((int)str.length() > width) {
        if (width > 3) {
            return str.substr(0, width - 3) + "...";
        }
        return str.substr(0, width);
    }

    int padding = width - str.length();
    switch (align) {
        case TableColumn::Align::Left:
            return str + std::string(padding, ' ');
        case TableColumn::Align::Right:
            return std::string(padding, ' ') + str;
        case TableColumn::Align::Center: {
            int left = padding / 2;
            int right = padding - left;
            return std::string(left, ' ') + str + std::string(right, ' ');
        }
    }
    return str;
}

// Table component implementation
class TableComponent : public ComponentBase {
public:
    TableComponent(
        std::vector<std::vector<std::string>>* rows,
        int* selected,
        TableOptions options
    ) : rows_(rows), selected_(selected), options_(std::move(options)) {}

    Element Render() override {
        Elements rowElements;

        // Header
        if (options_.showHeader && !options_.columns.empty()) {
            Elements headerCells;
            for (const auto& col : options_.columns) {
                std::string headerText = fitToWidth(col.header, col.width, col.align);
                headerCells.push_back(text(headerText) | bold);
                headerCells.push_back(text(" "));
            }
            rowElements.push_back(hbox(headerCells));

            // Separator line
            std::string sep;
            for (const auto& col : options_.columns) {
                int w = col.width > 0 ? col.width : col.header.length();
                sep += std::string(w, '-') + " ";
            }
            rowElements.push_back(text(sep) | dim);
        }

        // Data rows
        if (rows_) {
            for (size_t i = 0; i < rows_->size(); ++i) {
                const auto& row = (*rows_)[i];
                Elements cells;

                // Selection indicator
                if ((int)i == *selected_) {
                    cells.push_back(text("▸ ") | color(Color::Cyan));
                } else {
                    cells.push_back(text("  "));
                }

                // Cells
                for (size_t j = 0; j < row.size() && j < options_.columns.size(); ++j) {
                    std::string cellText = fitToWidth(row[j], options_.columns[j].width, options_.columns[j].align);
                    auto cell = text(cellText);
                    cells.push_back(cell);
                    cells.push_back(text(" "));
                }

                Element rowElem = hbox(cells);
                if ((int)i == *selected_) {
                    rowElem = rowElem | inverted;
                }
                rowElements.push_back(rowElem);
            }
        }

        // Empty state
        if (!rows_ || rows_->empty()) {
            rowElements.push_back(text("  (empty)") | dim);
        }

        Element content = vbox(rowElements);

        if (options_.showBorder) {
            content = content | border;
        }

        return content | reflect(box_);
    }

    bool OnEvent(Event event) override {
        if (!rows_ || rows_->empty()) return false;

        int maxIndex = (int)rows_->size() - 1;

        if (event == Event::ArrowUp || event == Event::Character('k')) {
            if (*selected_ > 0) {
                (*selected_)--;
                if (options_.onHighlight) options_.onHighlight(*selected_);
            }
            return true;
        }

        if (event == Event::ArrowDown || event == Event::Character('j')) {
            if (*selected_ < maxIndex) {
                (*selected_)++;
                if (options_.onHighlight) options_.onHighlight(*selected_);
            }
            return true;
        }

        if (event == Event::Home) {
            *selected_ = 0;
            if (options_.onHighlight) options_.onHighlight(*selected_);
            return true;
        }

        if (event == Event::End) {
            *selected_ = maxIndex;
            if (options_.onHighlight) options_.onHighlight(*selected_);
            return true;
        }

        if (event == Event::Return) {
            if (options_.onSelect) {
                options_.onSelect(*selected_);
            }
            return true;
        }

        // Page up/down
        if (event == Event::PageUp) {
            *selected_ = std::max(0, *selected_ - 10);
            if (options_.onHighlight) options_.onHighlight(*selected_);
            return true;
        }

        if (event == Event::PageDown) {
            *selected_ = std::min(maxIndex, *selected_ + 10);
            if (options_.onHighlight) options_.onHighlight(*selected_);
            return true;
        }

        return false;
    }

    bool Focusable() const override { return true; }

private:
    std::vector<std::vector<std::string>>* rows_;
    int* selected_;
    TableOptions options_;
    Box box_;
};

}  // namespace

Component Table(
    std::vector<std::vector<std::string>>* rows,
    int* selected,
    TableOptions options
) {
    return Make<TableComponent>(rows, selected, std::move(options));
}

Element TableElement(
    const std::vector<std::vector<std::string>>& rows,
    const std::vector<TableColumn>& columns,
    int selected,
    bool showHeader
) {
    Elements rowElements;

    // Header
    if (showHeader && !columns.empty()) {
        Elements headerCells;
        for (const auto& col : columns) {
            std::string headerText = fitToWidth(col.header, col.width, col.align);
            headerCells.push_back(text(headerText) | bold);
            headerCells.push_back(text(" "));
        }
        rowElements.push_back(hbox(headerCells));

        // Separator
        std::string sep;
        for (const auto& col : columns) {
            int w = col.width > 0 ? col.width : col.header.length();
            sep += std::string(w, '-') + " ";
        }
        rowElements.push_back(text(sep) | dim);
    }

    // Data rows
    for (size_t i = 0; i < rows.size(); ++i) {
        const auto& row = rows[i];
        Elements cells;

        // Selection indicator
        if ((int)i == selected) {
            cells.push_back(text("▸ ") | color(Color::Cyan));
        } else {
            cells.push_back(text("  "));
        }

        for (size_t j = 0; j < row.size() && j < columns.size(); ++j) {
            std::string cellText = fitToWidth(row[j], columns[j].width, columns[j].align);
            cells.push_back(text(cellText));
            cells.push_back(text(" "));
        }

        Element rowElem = hbox(cells);
        if ((int)i == selected) {
            rowElem = rowElem | inverted | focus;  // focus enables auto-scroll in yframe
        }
        rowElements.push_back(rowElem);
    }

    if (rows.empty()) {
        rowElements.push_back(text("  (empty)") | dim);
    }

    return vbox(rowElements) | vscroll_indicator | yframe;
}

}  // namespace components
}  // namespace waste
