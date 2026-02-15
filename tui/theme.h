#ifndef WASTE_TUI_THEME_H
#define WASTE_TUI_THEME_H

#include <ftxui/screen/color.hpp>
#include <string>
#include <vector>

namespace waste {

struct ColorTheme {
    std::string name;
    ftxui::Color bg;           // main background
    ftxui::Color fg;           // default text color
    ftxui::Color accent;       // selection highlight, progress gauge, chat sender, fingerprint
    ftxui::Color primary;      // active tabs bg, hyperlinks, queued transfers, selected rows bg
    ftxui::Color primaryFg;    // active tab text, selected row text
    ftxui::Color success;      // upload speed, active/completed transfers, RSA loaded
    ftxui::Color warning;      // download speed, paused transfers, pending keys tab bg
    ftxui::Color error;        // failed transfers, error messages
    ftxui::Color notification; // unread message count
    ftxui::Color bgDark;       // status bar bg, footer bg, selected row bg
    ftxui::Color contrastFg;   // pending keys tab text
};

const std::vector<ColorTheme>& builtinThemes();
int findThemeIndex(const std::string& name);

}  // namespace waste

#endif  // WASTE_TUI_THEME_H
