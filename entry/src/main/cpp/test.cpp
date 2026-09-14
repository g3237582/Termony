// build with:
// clang++ -std=c++17 -O2 -fsanitize=address test.cpp terminal.cpp terminal_session.cpp -I/usr/include/freetype2 -DSTANDALONE -DTESTING -lCatch2Main -lCatch2 -lfreetype -lutf8proc -lGLESv2 -lglfw -o test
#include "terminal.h"
#include <catch2/catch_test_macros.hpp>
#include <cerrno>
#include <cstdio>
#include <fstream>
#include <nlohmann/json.hpp>
using json = nlohmann::json;

TEST_CASE( "FormatErrno includes op and errno", "" ) {
    std::string s = FormatErrno("forkpty", EPERM);
    REQUIRE( s.find("forkpty") != std::string::npos );
    REQUIRE( s.find(std::to_string(EPERM)) != std::string::npos );
}

TEST_CASE( "AppendNotice writes into the buffer without a pty", "" ) {
    terminal_context ctx;
    ctx.ResizeTo(24, 80);
    ctx.AppendNotice("hello");
    REQUIRE( ctx.buffer[0][0].code == 'h' );
    REQUIRE( ctx.row == 1 );
    REQUIRE( ctx.col == 0 );
}

TEST_CASE( "Columns change with input", "" ) {
    terminal_context ctx;

    ctx.ResizeTo(24, 80);
    REQUIRE( ctx.row == 0 );
    REQUIRE( ctx.col == 0 );
    ctx.Parse('a');
    REQUIRE( ctx.row == 0 );
    REQUIRE( ctx.col == 1 );
}

TEST_CASE( "Line feed", "" ) {
    terminal_context ctx;

    ctx.ResizeTo(2, 80);
    REQUIRE( ctx.row == 0 );
    REQUIRE( ctx.col == 0 );

    // a|
    ctx.Parse('a');
    REQUIRE( ctx.row == 0 );
    REQUIRE( ctx.col == 1 );

    ctx.Parse('\x0d');
    REQUIRE( ctx.row == 0 );
    REQUIRE( ctx.col == 0 );

    // a
    // |
    ctx.Parse('\x0a');
    REQUIRE( ctx.row == 1 );
    REQUIRE( ctx.col == 0 );
    REQUIRE( ctx.buffer[0][0].code == 'a' );

    // a
    // b|
    ctx.Parse('b');
    REQUIRE( ctx.row == 1 );
    REQUIRE( ctx.col == 1 );
    REQUIRE( ctx.buffer[1][0].code == 'b' );

    // b
    //  |
    ctx.Parse('\x0a');
    REQUIRE( ctx.row == 1 );
    REQUIRE( ctx.col == 1 );
    REQUIRE( ctx.buffer[0][0].code == 'b' );
    REQUIRE( ctx.buffer[1][0].code == ' ' );
}

TEST_CASE( "Backspace", "" ) {
    terminal_context ctx;

    ctx.ResizeTo(24, 80);
    REQUIRE( ctx.row == 0 );
    REQUIRE( ctx.col == 0 );

    // a|
    ctx.Parse('a');
    REQUIRE( ctx.row == 0 );
    REQUIRE( ctx.col == 1 );
    REQUIRE( ctx.buffer[0][0].code == 'a' );

    ctx.Parse('\x08');
    REQUIRE( ctx.row == 0 );
    REQUIRE( ctx.col == 0 );
    REQUIRE( ctx.buffer[0][0].code == 'a' );

    // b|
    ctx.Parse('b');
    REQUIRE( ctx.row == 0 );
    REQUIRE( ctx.col == 1 );
    REQUIRE( ctx.buffer[0][0].code == 'b' );
}

TEST_CASE( "Tab", "" ) {
    terminal_context ctx;

    ctx.ResizeTo(24, 80);
    REQUIRE( ctx.row == 0 );
    REQUIRE( ctx.col == 0 );

    // a|
    ctx.Parse('a');
    REQUIRE( ctx.row == 0 );
    REQUIRE( ctx.col == 1 );
    REQUIRE( ctx.buffer[0][0].code == 'a' );

    // a       |
    ctx.Parse('\x09');
    REQUIRE( ctx.row == 0 );
    REQUIRE( ctx.col == 8 );
    REQUIRE( ctx.buffer[0][0].code == 'a' );

    // a       b|
    ctx.Parse('b');
    REQUIRE( ctx.row == 0 );
    REQUIRE( ctx.col == 9 );
    REQUIRE( ctx.buffer[0][8].code == 'b' );
}

TEST_CASE( "Insert Characters", "" ) {
    terminal_context ctx;

    ctx.ResizeTo(24, 80);
    REQUIRE( ctx.row == 0 );
    REQUIRE( ctx.col == 0 );

    // a|
    ctx.Parse('a');
    REQUIRE( ctx.row == 0 );
    REQUIRE( ctx.col == 1 );
    REQUIRE( ctx.buffer[0][0].code == 'a' );

    // a
    ctx.Parse('\x0d');
    REQUIRE( ctx.row == 0 );
    REQUIRE( ctx.col == 0 );
    REQUIRE( ctx.buffer[0][0].code == 'a' );

    // |a
    // CSI @
    ctx.Parse('\x1b');
    ctx.Parse('[');
    ctx.Parse('@');
    REQUIRE( ctx.row == 0 );
    REQUIRE( ctx.col == 0 );
    REQUIRE( ctx.buffer[0][0].code == ' ' );
    REQUIRE( ctx.buffer[0][1].code == 'a' );

    // |  a
    // CSI 2 @
    ctx.Parse('\x1b');
    ctx.Parse('[');
    ctx.Parse('2');
    ctx.Parse('@');
    REQUIRE( ctx.row == 0 );
    REQUIRE( ctx.col == 0 );
    REQUIRE( ctx.buffer[0][0].code == ' ' );
    REQUIRE( ctx.buffer[0][1].code == ' ' );
    REQUIRE( ctx.buffer[0][2].code == ' ' );
    REQUIRE( ctx.buffer[0][3].code == 'a' );
}

TEST_CASE( "Cursor Up", "" ) {
    terminal_context ctx;

    ctx.ResizeTo(24, 80);
    REQUIRE( ctx.row == 0 );
    REQUIRE( ctx.col == 0 );

    // a|
    ctx.Parse('a');
    REQUIRE( ctx.row == 0 );
    REQUIRE( ctx.col == 1 );
    REQUIRE( ctx.buffer[0][0].code == 'a' );

    // a|
    ctx.Parse('\x0d');
    ctx.Parse('\x1b');
    ctx.Parse('[');
    ctx.Parse('A');
    REQUIRE( ctx.row == 0 );
    REQUIRE( ctx.col == 0 );
    REQUIRE( ctx.buffer[0][0].code == 'a' );

    // a
    //  |
    ctx.Parse('\x0a');
    REQUIRE( ctx.row == 1 );
    REQUIRE( ctx.col == 0 );
    REQUIRE( ctx.buffer[0][0].code == 'a' );

    // a
    // |
    ctx.Parse('\x0b');
    REQUIRE( ctx.row == 1 );
    REQUIRE( ctx.col == 0 );

    // a
    // CSI A
    ctx.Parse('\x0d');
    ctx.Parse('\x1b');
    ctx.Parse('[');
    ctx.Parse('A');
    REQUIRE( ctx.row == 0 );
    REQUIRE( ctx.col == 0 );
    REQUIRE( ctx.buffer[0][0].code == 'a' );

    // b|
    ctx.Parse('b');
    REQUIRE( ctx.row == 0 );
    REQUIRE( ctx.col == 1 );
    REQUIRE( ctx.buffer[0][0].code == 'b' );

    // b
    // |
    // set scroll margin [2,3]
    ctx.Parse('b');
    ctx.Parse('\x0d');
    ctx.Parse('\x1b');
    ctx.Parse('[');
    ctx.Parse('2');
    ctx.Parse(';');
    ctx.Parse('3');
    ctx.Parse('r');
    REQUIRE( ctx.row == 1 );
    REQUIRE( ctx.col == 0 );

    // b
    // |
    // CSI A
    ctx.Parse('\x0d');
    ctx.Parse('\x1b');
    ctx.Parse('[');
    ctx.Parse('A');
    REQUIRE( ctx.row == 1 );
    REQUIRE( ctx.col == 0 );
}

TEST_CASE( "Erase in Display", "" ) {
    terminal_context ctx;

    ctx.ResizeTo(24, 80);
    REQUIRE( ctx.row == 0 );
    REQUIRE( ctx.col == 0 );

    // a|
    ctx.Parse('a');
    REQUIRE( ctx.row == 0 );
    REQUIRE( ctx.col == 1 );
    REQUIRE( ctx.buffer[0][0].code == 'a' );

    // ab|
    ctx.Parse('b');
    REQUIRE( ctx.row == 0 );
    REQUIRE( ctx.col == 2 );
    REQUIRE( ctx.buffer[0][1].code == 'b' );

    // ab
    ctx.Parse('\x08');
    REQUIRE( ctx.row == 0 );
    REQUIRE( ctx.col == 1 );

    // a|
    // CSI J
    ctx.Parse('\x1b');
    ctx.Parse('[');
    ctx.Parse('J');
    REQUIRE( ctx.row == 0 );
    REQUIRE( ctx.col == 1 );
    REQUIRE( ctx.buffer[0][0].code == 'a' );
    REQUIRE( ctx.buffer[0][1].code == ' ' );
}

TEST_CASE( "Save Cursor", "" ) {
    terminal_context ctx;

    ctx.ResizeTo(24, 80);
    REQUIRE( ctx.row == 0 );
    REQUIRE( ctx.col == 0 );

    // a|
    ctx.Parse('a');
    REQUIRE( ctx.row == 0 );
    REQUIRE( ctx.col == 1 );
    REQUIRE( ctx.buffer[0][0].code == 'a' );

    // a|
    ctx.Parse('\x1b');
    ctx.Parse('7');
    REQUIRE( ctx.row == 0 );
    REQUIRE( ctx.col == 1 );
    REQUIRE( ctx.buffer[0][0].code == 'a' );

    // a
    ctx.Parse('\x08');
    REQUIRE( ctx.row == 0 );
    REQUIRE( ctx.col == 0 );

    // a|
    ctx.Parse('\x1b');
    ctx.Parse('8');
    REQUIRE( ctx.row == 0 );
    REQUIRE( ctx.col == 1 );
    REQUIRE( ctx.buffer[0][0].code == 'a' );

    // ab|
    ctx.Parse('b');
    REQUIRE( ctx.row == 0 );
    REQUIRE( ctx.col == 2 );
    REQUIRE( ctx.buffer[0][1].code == 'b' );
}

TEST_CASE( "Tab Clear", "" ) {
    terminal_context ctx;

    ctx.ResizeTo(24, 80);
    REQUIRE( ctx.row == 0 );
    REQUIRE( ctx.col == 0 );

    // a|
    ctx.Parse('a');
    REQUIRE( ctx.row == 0 );
    REQUIRE( ctx.col == 1 );
    REQUIRE( ctx.buffer[0][0].code == 'a' );

    // a       |
    ctx.Parse('\x09');
    REQUIRE( ctx.row == 0 );
    REQUIRE( ctx.col == 8 );
    REQUIRE( ctx.buffer[0][0].code == 'a' );

    // a       b|
    ctx.Parse('b');
    REQUIRE( ctx.row == 0 );
    REQUIRE( ctx.col == 9 );
    REQUIRE( ctx.buffer[0][8].code == 'b' );

    // a       b|
    ctx.Parse('\x1b');
    ctx.Parse('[');
    ctx.Parse('3');
    ctx.Parse('g');
    REQUIRE( ctx.row == 0 );
    REQUIRE( ctx.col == 9 );

    // got to last column
    ctx.Parse('\x09');
    REQUIRE( ctx.row == 0 );
    REQUIRE( ctx.col == 79 );
}

void TestAlacritty(std::string name) {
    terminal_context ctx;
    std::string ref = "alacritty/alacritty_terminal/tests/ref";

    std::ifstream size_f(ref + "/" + name + "/size.json");
    json size_json = json::parse(size_f);

    ctx.ResizeTo(size_json["screen_lines"].template get<int>(), size_json["columns"].template get<int>());

    FILE *fp = fopen((ref + "/" + name + "/alacritty.recording").c_str(), "rb");
    int ch;
    while ((ch = fgetc(fp)) != EOF) {
        ctx.Parse(ch);
    }

    // compare result
    std::ifstream grid_f(ref + "/" + name + "/grid.json");
    json grid_json = json::parse(grid_f);

    for (int i = 0;i < ctx.num_rows;i++) {
        for (int j = 0;j < ctx.num_cols;j++) {
            // order is inverted!
            json expected_json = grid_json["raw"]["inner"][ctx.num_rows - i - 1]["inner"][j];
            std::string expected_str = expected_json["c"].template get<std::string>();
            REQUIRE (expected_str.size() == 1 );

            // alacritty stores vanilla \t instead of SP
            if (expected_str[0] == '\t') {
                expected_str[0] = ' ';
            }

            // TODO: validate style
            if (ctx.buffer[i][j].code != expected_str[0]) {
                // print diff
                fprintf(stderr, "Diff:\n");
                for (int ii = 0;ii < ctx.num_rows;ii++) {
                    bool equal = true;
                    for (int jj = 0;jj < ctx.num_cols;jj++) {
                        json expected_json = grid_json["raw"]["inner"][ctx.num_rows - ii - 1]["inner"][jj];
                        std::string expected_str = expected_json["c"].template get<std::string>();
                        REQUIRE (expected_str.size() == 1 );

                        // alacritty stores vanilla \t instead of SP
                        if (expected_str[0] == '\t') {
                            expected_str[0] = ' ';
                        }

                        if (ctx.buffer[ii][jj].code != expected_str[0]) {
                            equal = false;
                        }
                    }

                    if (equal) {
                        fprintf(stderr, "%02d=", ii);
                        for (int jj = 0;jj < ctx.num_cols;jj++) {
                            fprintf(stderr, "%c", ctx.buffer[ii][jj].code);
                        }
                        fprintf(stderr, "\n");
                    } else {
                        fprintf(stderr, "%02d-", ii);
                        for (int jj = 0;jj < ctx.num_cols;jj++) {
                            fprintf(stderr, "%c", ctx.buffer[ii][jj].code);
                        }
                        fprintf(stderr, "\n");

                        fprintf(stderr, "%02d+", ii);
                        for (int jj = 0;jj < ctx.num_cols;jj++) {
                            json expected = grid_json["raw"]["inner"][ctx.num_rows - ii - 1]["inner"][jj];
                            std::string c = expected["c"].template get<std::string>();
                            REQUIRE( c.size() == 1 );
                            fprintf(stderr, "%c", c[0]);
                        }
                        fprintf(stderr, "\n");
                    }
                }
            }
            REQUIRE( ctx.buffer[i][j].code == expected_str[0] );
        }
    }
}

#define TEST_ALACRITTY(name) \
    TEST_CASE( "Alacritty test " name, "" ) { \
        TestAlacritty(name); \
    }

// TODO: pass more tests
// TODO: alternate screen
// TEST_ALACRITTY("alt_reset");
TEST_ALACRITTY("clear_underline");
TEST_ALACRITTY("colored_reset");
TEST_ALACRITTY("decaln_reset");
// TEST_ALACRITTY("deccolm_reset");
TEST_ALACRITTY("delete_chars_reset");
TEST_ALACRITTY("delete_lines");
TEST_ALACRITTY("erase_chars_reset");
TEST_ALACRITTY("erase_in_line");
TEST_ALACRITTY("hyperlinks");
TEST_ALACRITTY("insert_blank_reset");
TEST_ALACRITTY("newline_with_cursor_beyond_scroll_region");
TEST_ALACRITTY("row_reset");
TEST_ALACRITTY("scroll_in_region_up_preserves_history");
TEST_ALACRITTY("scroll_up_reset");
TEST_ALACRITTY("selective_erasure");
TEST_ALACRITTY("sgr");
TEST_ALACRITTY("tmux_git_log");
TEST_ALACRITTY("tmux_htop");
TEST_ALACRITTY("underline");
// TEST_ALACRITTY("vim_large_window_scroll");
TEST_ALACRITTY("vim_simple_edit");
// TEST_ALACRITTY("vttest_cursor_movement_1");
TEST_ALACRITTY("vttest_insert");
TEST_ALACRITTY("vttest_origin_mode_1");
TEST_ALACRITTY("vttest_origin_mode_2");
TEST_ALACRITTY("vttest_scroll");
TEST_ALACRITTY("vttest_tab_clear_set");

// TODO: multibyte character
// TEST_ALACRITTY("colored_underline");
// TEST_ALACRITTY("csi_rep");
// TEST_ALACRITTY("fish_cc");
// TEST_ALACRITTY("grid_reset");
// TEST_ALACRITTY("history");
// TEST_ALACRITTY("indexed_256_coIors");
// TEST_ALACRITTY("issue_855");
// TEST_ALACRITTY("ll");
// TEST_ALACRITTY("region_scroll_down");
// TEST_ALACRITTY("saved_cursor");
// TEST_ALACRITTY("saved_cursor_alt");
// TEST_ALACRITTY("tab_rendering");
// TEST_ALACRITTY("vim_24bitcolors_bce");
// TEST_ALACRITTY("wrapline_alt_toggle");
// TEST_ALACRITTY("zerowidth");
// TEST_ALACRITTY("zsh_tab_completion");
