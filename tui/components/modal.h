#ifndef WASTE_TUI_COMPONENTS_MODAL_H
#define WASTE_TUI_COMPONENTS_MODAL_H

#include <ftxui/component/component.hpp>
#include <ftxui/dom/elements.hpp>
#include <string>
#include <functional>

namespace waste {
namespace components {

// Simple confirmation modal
ftxui::Component ConfirmModal(
    const std::string& title,
    const std::string& message,
    std::function<void()> onConfirm,
    std::function<void()> onCancel
);

// Input modal (single text field)
ftxui::Component InputModal(
    const std::string& title,
    const std::string& label,
    std::string* value,
    std::function<void()> onConfirm,
    std::function<void()> onCancel,
    bool password = false
);

// Two-field input modal (e.g., address + port)
ftxui::Component TwoFieldModal(
    const std::string& title,
    const std::string& label1,
    std::string* value1,
    const std::string& label2,
    std::string* value2,
    std::function<void()> onConfirm,
    std::function<void()> onCancel
);

// Wrap any component in a modal overlay
ftxui::Component ModalOverlay(
    ftxui::Component background,
    ftxui::Component modal,
    bool* showModal
);

// Helper to create a modal frame
ftxui::Element ModalFrame(
    const std::string& title,
    ftxui::Element content
);

}  // namespace components
}  // namespace waste

#endif  // WASTE_TUI_COMPONENTS_MODAL_H
