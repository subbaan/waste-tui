#include "modal.h"
#include <ftxui/component/component.hpp>
#include <ftxui/component/component_options.hpp>
#include <ftxui/dom/elements.hpp>

namespace waste {
namespace components {

using namespace ftxui;

Element ModalFrame(const std::string& title, Element content) {
    return window(text(" " + title + " "), content) | clear_under | center;
}

Component ConfirmModal(
    const std::string& title,
    const std::string& message,
    std::function<void()> onConfirm,
    std::function<void()> onCancel
) {
    auto yesButton = Button("  Yes  ", onConfirm, ButtonOption::Animated());
    auto noButton = Button("  No   ", onCancel, ButtonOption::Animated());

    auto buttons = Container::Horizontal({yesButton, noButton});

    return Renderer(buttons, [=] {
        return ModalFrame(title, vbox({
            text(message) | center,
            separator(),
            hbox({
                filler(),
                yesButton->Render(),
                text("  "),
                noButton->Render(),
                filler()
            })
        }) | size(WIDTH, GREATER_THAN, 30));
    });
}

Component InputModal(
    const std::string& title,
    const std::string& label,
    std::string* value,
    std::function<void()> onConfirm,
    std::function<void()> onCancel,
    bool password
) {
    InputOption inputOpt;
    if (password) {
        inputOpt.password = true;
    }
    auto input = Input(value, "", inputOpt);

    auto okButton = Button("  OK  ", onConfirm, ButtonOption::Animated());
    auto cancelButton = Button("Cancel", onCancel, ButtonOption::Animated());

    auto container = Container::Vertical({
        input,
        Container::Horizontal({okButton, cancelButton})
    });

    return Renderer(container, [=] {
        return ModalFrame(title, vbox({
            hbox({text(label + ": "), input->Render() | flex | border}),
            separator(),
            hbox({
                filler(),
                okButton->Render(),
                text("  "),
                cancelButton->Render(),
                filler()
            })
        }) | size(WIDTH, GREATER_THAN, 40));
    }) | CatchEvent([=](Event event) {
        if (event == Event::Return) {
            onConfirm();
            return true;
        }
        if (event == Event::Escape) {
            onCancel();
            return true;
        }
        return false;
    });
}

Component TwoFieldModal(
    const std::string& title,
    const std::string& label1,
    std::string* value1,
    const std::string& label2,
    std::string* value2,
    std::function<void()> onConfirm,
    std::function<void()> onCancel
) {
    auto input1 = Input(value1, "");
    auto input2 = Input(value2, "");

    auto okButton = Button("  OK  ", onConfirm, ButtonOption::Animated());
    auto cancelButton = Button("Cancel", onCancel, ButtonOption::Animated());

    auto container = Container::Vertical({
        input1,
        input2,
        Container::Horizontal({okButton, cancelButton})
    });

    // Find max label width for alignment
    int labelWidth = std::max(label1.length(), label2.length()) + 2;

    return Renderer(container, [=] {
        auto lbl1 = text(label1 + ": ");
        auto lbl2 = text(label2 + ": ");

        return ModalFrame(title, vbox({
            hbox({lbl1 | size(WIDTH, EQUAL, labelWidth), input1->Render() | flex | border}),
            hbox({lbl2 | size(WIDTH, EQUAL, labelWidth), input2->Render() | flex | border}),
            separator(),
            hbox({
                filler(),
                okButton->Render(),
                text("  "),
                cancelButton->Render(),
                filler()
            })
        }) | size(WIDTH, GREATER_THAN, 45));
    }) | CatchEvent([=](Event event) {
        if (event == Event::Return) {
            onConfirm();
            return true;
        }
        if (event == Event::Escape) {
            onCancel();
            return true;
        }
        return false;
    });
}

Component ModalOverlay(
    Component background,
    Component modal,
    bool* showModal
) {
    return Container::Tab({background, modal}, reinterpret_cast<int*>(showModal))
        | Modal(modal, showModal);
}

}  // namespace components
}  // namespace waste
