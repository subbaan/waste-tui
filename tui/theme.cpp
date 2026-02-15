#include "theme.h"

namespace waste {

const std::vector<ColorTheme>& builtinThemes() {
    using ftxui::Color;

    static const std::vector<ColorTheme> themes = {
        // Default — ANSI terminal colors (matches original hardcoded look)
        {
            "Default",
            Color::Default,   // bg: inherit terminal background
            Color::Default,   // fg: inherit terminal foreground
            Color::Cyan,      // accent
            Color::Blue,      // primary
            Color::White,     // primaryFg
            Color::Green,     // success
            Color::Yellow,    // warning
            Color::Red,       // error
            Color::Magenta,   // notification
            Color::GrayDark,  // bgDark
            Color::Black,     // contrastFg
        },
        // Dracula
        {
            "Dracula",
            Color::RGB(40, 42, 54),     // bg
            Color::RGB(248, 248, 242),  // fg
            Color::RGB(139, 233, 253),  // accent: cyan
            Color::RGB(98, 114, 164),   // primary: comment
            Color::RGB(248, 248, 242),  // primaryFg: fg
            Color::RGB(80, 250, 123),   // success: green
            Color::RGB(241, 250, 140),  // warning: yellow
            Color::RGB(255, 85, 85),    // error: red
            Color::RGB(255, 121, 198),  // notification: pink
            Color::RGB(68, 71, 90),     // bgDark: current-line
            Color::RGB(40, 42, 54),     // contrastFg: bg
        },
        // Gruvbox Dark
        {
            "Gruvbox Dark",
            Color::RGB(40, 40, 40),     // bg: bg0
            Color::RGB(235, 219, 178),  // fg: light-fg
            Color::RGB(131, 165, 152),  // accent: aqua
            Color::RGB(69, 133, 136),   // primary: dark-aqua
            Color::RGB(235, 219, 178),  // primaryFg: light-fg
            Color::RGB(184, 187, 38),   // success: green
            Color::RGB(250, 189, 47),   // warning: yellow
            Color::RGB(251, 73, 52),    // error: red
            Color::RGB(211, 134, 155),  // notification: purple
            Color::RGB(60, 56, 54),     // bgDark: bg1
            Color::RGB(40, 40, 40),     // contrastFg: bg0
        },
        // Nord
        {
            "Nord",
            Color::RGB(46, 52, 64),     // bg: nord0
            Color::RGB(216, 222, 233),  // fg: nord4
            Color::RGB(136, 192, 208),  // accent: frost8
            Color::RGB(94, 129, 172),   // primary: frost10
            Color::RGB(236, 239, 244),  // primaryFg: snow6
            Color::RGB(163, 190, 140),  // success: green
            Color::RGB(235, 203, 139),  // warning: yellow
            Color::RGB(191, 97, 106),   // error: red
            Color::RGB(180, 142, 173),  // notification: purple
            Color::RGB(59, 66, 82),     // bgDark: nord1
            Color::RGB(46, 52, 64),     // contrastFg: nord0
        },
        // Solarized Dark
        {
            "Solarized Dark",
            Color::RGB(0, 43, 54),      // bg: base03
            Color::RGB(131, 148, 150),  // fg: base0
            Color::RGB(42, 161, 152),   // accent: cyan
            Color::RGB(38, 139, 210),   // primary: blue
            Color::RGB(238, 232, 213),  // primaryFg: base2
            Color::RGB(133, 153, 0),    // success: green
            Color::RGB(181, 137, 0),    // warning: yellow
            Color::RGB(220, 50, 47),    // error: red
            Color::RGB(211, 54, 130),   // notification: magenta
            Color::RGB(7, 54, 66),      // bgDark: base02
            Color::RGB(0, 43, 54),      // contrastFg: base03
        },
        // Monokai
        {
            "Monokai",
            Color::RGB(39, 40, 34),     // bg
            Color::RGB(248, 248, 242),  // fg
            Color::RGB(102, 217, 239),  // accent: cyan
            Color::RGB(117, 113, 94),   // primary: comment
            Color::RGB(248, 248, 242),  // primaryFg: fg
            Color::RGB(166, 226, 46),   // success: green
            Color::RGB(230, 219, 116),  // warning: yellow
            Color::RGB(249, 38, 114),   // error: red
            Color::RGB(174, 129, 255),  // notification: purple
            Color::RGB(53, 53, 53),     // bgDark: line
            Color::RGB(39, 40, 34),     // contrastFg: bg
        },
        // Catppuccin Mocha
        {
            "Catppuccin",
            Color::RGB(30, 30, 46),     // bg: base
            Color::RGB(205, 214, 244),  // fg: text
            Color::RGB(137, 220, 235),  // accent: sky
            Color::RGB(137, 180, 250),  // primary: blue
            Color::RGB(205, 214, 244),  // primaryFg: text
            Color::RGB(166, 227, 161),  // success: green
            Color::RGB(249, 226, 175),  // warning: yellow
            Color::RGB(243, 139, 168),  // error: red
            Color::RGB(245, 194, 231),  // notification: pink
            Color::RGB(49, 50, 68),     // bgDark: surface0
            Color::RGB(30, 30, 46),     // contrastFg: base
        },
        // Tokyo Night
        {
            "Tokyo Night",
            Color::RGB(26, 27, 38),     // bg
            Color::RGB(169, 177, 214),  // fg
            Color::RGB(125, 207, 255),  // accent: cyan
            Color::RGB(122, 162, 247),  // primary: blue
            Color::RGB(192, 202, 245),  // primaryFg: fg-bright
            Color::RGB(158, 206, 106),  // success: green
            Color::RGB(224, 175, 104),  // warning: orange
            Color::RGB(247, 118, 142),  // error: red
            Color::RGB(187, 154, 247),  // notification: purple
            Color::RGB(41, 46, 66),     // bgDark: bg-dark
            Color::RGB(26, 27, 38),     // contrastFg: bg
        },
        // One Dark
        {
            "One Dark",
            Color::RGB(40, 44, 52),     // bg
            Color::RGB(171, 178, 191),  // fg
            Color::RGB(86, 182, 194),   // accent: cyan
            Color::RGB(97, 175, 239),   // primary: blue
            Color::RGB(220, 223, 228),  // primaryFg: fg-bright
            Color::RGB(152, 195, 121),  // success: green
            Color::RGB(229, 192, 123),  // warning: yellow
            Color::RGB(224, 108, 117),  // error: red
            Color::RGB(198, 120, 221),  // notification: purple
            Color::RGB(53, 59, 69),     // bgDark: gutter
            Color::RGB(40, 44, 52),     // contrastFg: bg
        },
        // Everforest Dark
        {
            "Everforest",
            Color::RGB(47, 53, 55),     // bg: bg0
            Color::RGB(211, 198, 170),  // fg
            Color::RGB(131, 194, 159),  // accent: aqua
            Color::RGB(127, 187, 179),  // primary: blue
            Color::RGB(211, 198, 170),  // primaryFg: fg
            Color::RGB(167, 192, 128),  // success: green
            Color::RGB(219, 188, 127),  // warning: yellow
            Color::RGB(230, 126, 128),  // error: red
            Color::RGB(214, 153, 182),  // notification: purple
            Color::RGB(55, 63, 65),     // bgDark: bg1
            Color::RGB(47, 53, 55),     // contrastFg: bg0
        },
        // Kanagawa
        {
            "Kanagawa",
            Color::RGB(31, 31, 40),     // bg: sumiInk3
            Color::RGB(220, 215, 186),  // fg: fujiWhite
            Color::RGB(106, 149, 137),  // accent: waveAqua2
            Color::RGB(126, 156, 216),  // primary: crystalBlue
            Color::RGB(220, 215, 186),  // primaryFg: fujiWhite
            Color::RGB(152, 187, 108),  // success: springGreen
            Color::RGB(226, 194, 93),   // warning: carpYellow
            Color::RGB(195, 64, 67),    // error: autumnRed
            Color::RGB(216, 166, 227),  // notification: oniViolet
            Color::RGB(54, 54, 70),     // bgDark: sumiInk4
            Color::RGB(31, 31, 40),     // contrastFg: sumiInk3
        },
        // Rose Pine
        {
            "Rose Pine",
            Color::RGB(25, 23, 36),     // bg: base
            Color::RGB(224, 222, 244),  // fg: text
            Color::RGB(156, 207, 216),  // accent: foam
            Color::RGB(196, 167, 231),  // primary: iris
            Color::RGB(224, 222, 244),  // primaryFg: text
            Color::RGB(62, 143, 176),   // success: pine
            Color::RGB(246, 193, 119),  // warning: gold
            Color::RGB(235, 111, 146),  // error: love
            Color::RGB(234, 154, 151),  // notification: rose
            Color::RGB(38, 35, 53),     // bgDark: surface
            Color::RGB(25, 23, 36),     // contrastFg: base
        },
    };

    return themes;
}

int findThemeIndex(const std::string& name) {
    const auto& themes = builtinThemes();
    for (size_t i = 0; i < themes.size(); ++i) {
        if (themes[i].name == name) {
            return static_cast<int>(i);
        }
    }
    return 0;  // Default
}

}  // namespace waste
