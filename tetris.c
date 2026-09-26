/*
 * tetris.c — Simple falling-block puzzle game for the Hack platform.
 *
 * Controls:
 *   - Left / Right arrows: move
 *   - Up arrow: rotate
 *   - Down arrow: soft drop
 *   - Space: hard drop
 *   - Q: quit
 *
 * Compile:
 *   hack_cc -I include demo/tetris.c -o demo/tetris.hackem
 * Run:
 *   hack_emu demo/tetris.hackem
 */
#define HACK_OUTPUT_SCREEN
#include <hack.h>

#define BOARD_W 10
#define BOARD_H 20
#define CELL 10

#define FIELD_X 120
#define FIELD_Y 24

#define PREVIEW_X 8
#define PREVIEW_Y 36
#define PREVIEW_CELL 8

#define KEY_ENTER 128
#define KEY_LEFT 130
#define KEY_UP 131
#define KEY_RIGHT 132
#define KEY_DOWN 133

#define PIECE_I 0
#define PIECE_O 1
#define PIECE_T 2
#define PIECE_S 3
#define PIECE_Z 4
#define PIECE_J 5
#define PIECE_L 6

int board[200];

int cur_piece;
int cur_rot;
int cur_x;
int cur_y;
int next_piece;

int score;
int total_lines;
int level;
int game_over;

int key_prev;
int key_repeat_wait;
int drop_counter;

int board_index(int x, int y) {
    return y * BOARD_W + x;
}

int board_get(int x, int y) {
    return board[board_index(x, y)];
}

void board_set(int x, int y, int value) {
    board[board_index(x, y)] = value;
}

void clear_board(void) {
    int i;
    i = 0;
    while (i < BOARD_W * BOARD_H) {
        board[i] = 0;
        i = i + 1;
    }
}

int piece_block(int piece, int rot, int x, int y) {
    rot = rot % 4;

    if (piece == PIECE_I) {
        if (rot == 0 || rot == 2) {
            return y == 1 && x >= 0 && x < 4;
        }
        return x == 2 && y >= 0 && y < 4;
    }

    if (piece == PIECE_O) {
        return y >= 0 && y < 2 && x >= 1 && x < 3;
    }

    if (piece == PIECE_T) {
        if (rot == 0) return (y == 1 && x >= 0 && x < 3) || (y == 0 && x == 1);
        if (rot == 1) return (x == 1 && y >= 0 && y < 3) || (x == 2 && y == 1);
        if (rot == 2) return (y == 0 && x >= 0 && x < 3) || (y == 1 && x == 1);
        return (x == 1 && y >= 0 && y < 3) || (x == 0 && y == 1);
    }

    if (piece == PIECE_S) {
        if (rot == 0 || rot == 2) {
            return (y == 0 && (x == 1 || x == 2)) || (y == 1 && (x == 0 || x == 1));
        }
        return (x == 1 && (y == 0 || y == 1)) || (x == 2 && (y == 1 || y == 2));
    }

    if (piece == PIECE_Z) {
        if (rot == 0 || rot == 2) {
            return (y == 0 && (x == 0 || x == 1)) || (y == 1 && (x == 1 || x == 2));
        }
        return (x == 2 && (y == 0 || y == 1)) || (x == 1 && (y == 1 || y == 2));
    }

    if (piece == PIECE_J) {
        if (rot == 0) return (y == 0 && x == 0) || (y == 1 && x >= 0 && x < 3);
        if (rot == 1) return (x == 1 && y >= 0 && y < 3) || (x == 2 && y == 0);
        if (rot == 2) return (y == 0 && x >= 0 && x < 3) || (y == 1 && x == 2);
        return (x == 1 && y >= 0 && y < 3) || (x == 0 && y == 2);
    }

    if (rot == 0) return (y == 0 && x == 2) || (y == 1 && x >= 0 && x < 3);
    if (rot == 1) return (x == 1 && y >= 0 && y < 3) || (x == 2 && y == 2);
    if (rot == 2) return (y == 0 && x >= 0 && x < 3) || (y == 1 && x == 0);
    return (x == 1 && y >= 0 && y < 3) || (x == 0 && y == 0);
}

int can_place(int piece, int rot, int px, int py) {
    int dx;
    int dy;
    int bx;
    int by;

    dy = 0;
    while (dy < 4) {
        dx = 0;
        while (dx < 4) {
            if (piece_block(piece, rot, dx, dy)) {
                bx = px + dx;
                by = py + dy;

                if (bx < 0 || bx >= BOARD_W || by >= BOARD_H) {
                    return 0;
                }
                if (by >= 0 && board_get(bx, by)) {
                    return 0;
                }
            }
            dx = dx + 1;
        }
        dy = dy + 1;
    }

    return 1;
}

int current_drop_delay(void) {
    int d;
    d = 5000 - (level - 1) * 300;
    if (d < 700) d = 700;
    return d;
}

int random_piece(void) {
    return rand() % 7;
}

void update_level(void) {
    level = total_lines / 10 + 1;
}

void clear_lines_if_needed(void) {
    int y;
    int x;
    int row_full;
    int yy;
    int lines_this_drop;

    lines_this_drop = 0;
    y = BOARD_H - 1;
    while (y >= 0) {
        row_full = 1;
        x = 0;
        while (x < BOARD_W) {
            if (!board_get(x, y)) {
                row_full = 0;
            }
            x = x + 1;
        }

        if (row_full) {
            yy = y;
            while (yy > 0) {
                x = 0;
                while (x < BOARD_W) {
                    board_set(x, yy, board_get(x, yy - 1));
                    x = x + 1;
                }
                yy = yy - 1;
            }
            x = 0;
            while (x < BOARD_W) {
                board_set(x, 0, 0);
                x = x + 1;
            }
            lines_this_drop = lines_this_drop + 1;
            total_lines = total_lines + 1;
            update_level();
            y = y + 1;
        }

        y = y - 1;
    }

    if (lines_this_drop == 1) score = score + 40 * level;
    else if (lines_this_drop == 2) score = score + 100 * level;
    else if (lines_this_drop == 3) score = score + 300 * level;
    else if (lines_this_drop >= 4) score = score + 1200 * level;
}

void spawn_piece(void) {
    cur_piece = next_piece;
    next_piece = random_piece();
    cur_rot = 0;
    cur_x = 3;
    cur_y = 0;
    drop_counter = 0;

    if (!can_place(cur_piece, cur_rot, cur_x, cur_y)) {
        game_over = 1;
    }
}

void draw_block_pixels(int x, int y, int cell) {
    fill_rect(x + 1, y + 1, cell - 2, cell - 2);
}

void clear_block_pixels(int x, int y, int cell) {
    clear_rect(x + 1, y + 1, cell - 2, cell - 2);
}

void render_quit_screen(void) {
    clear_screen();
    draw_string(27, 10, "QUITTING...");
}

void draw_static_ui(void) {
    clear_screen();
    draw_rect(FIELD_X - 2, FIELD_Y - 2, BOARD_W * CELL + 4, BOARD_H * CELL + 4);
    draw_string(1, 0, "TETRIS");
    draw_string(1, 2, "NEXT");
    draw_string(1, 10, "SCORE");
    draw_string(1, 13, "LINES");
    draw_string(1, 16, "LEVEL");
    draw_string(1, 18, "UP ROT");
    draw_string(1, 19, "DN DROP");
    draw_string(1, 20, "SPC FALL");
    draw_string(1, 21, "Q QUIT");
}

void draw_piece_pixels(int piece, int rot, int px, int py, int ox, int oy, int cell) {
    int dx;
    int dy;
    int sx;
    int sy;

    dy = 0;
    while (dy < 4) {
        dx = 0;
        while (dx < 4) {
            if (piece_block(piece, rot, dx, dy)) {
                sx = px + dx;
                sy = py + dy;
                if (sy >= 0) {
                    draw_block_pixels(ox + sx * cell, oy + sy * cell, cell);
                }
            }
            dx = dx + 1;
        }
        dy = dy + 1;
    }
}

void erase_piece_pixels(int piece, int rot, int px, int py, int ox, int oy, int cell) {
    int dx;
    int dy;
    int sx;
    int sy;

    dy = 0;
    while (dy < 4) {
        dx = 0;
        while (dx < 4) {
            if (piece_block(piece, rot, dx, dy)) {
                sx = px + dx;
                sy = py + dy;
                if (sy >= 0) {
                    clear_block_pixels(ox + sx * cell, oy + sy * cell, cell);
                }
            }
            dx = dx + 1;
        }
        dy = dy + 1;
    }
}

void draw_current_piece(void) {
    draw_piece_pixels(cur_piece, cur_rot, cur_x, cur_y, FIELD_X, FIELD_Y, CELL);
}

void erase_current_piece(void) {
    erase_piece_pixels(cur_piece, cur_rot, cur_x, cur_y, FIELD_X, FIELD_Y, CELL);
}

void redraw_board_cells(void) {
    int x;
    int y;
    int px;
    int py;

    y = 0;
    while (y < BOARD_H) {
        x = 0;
        while (x < BOARD_W) {
            px = FIELD_X + x * CELL;
            py = FIELD_Y + y * CELL;
            if (board_get(x, y)) {
                draw_block_pixels(px, py, CELL);
            } else {
                clear_block_pixels(px, py, CELL);
            }
            x = x + 1;
        }
        y = y + 1;
    }
}

void update_preview_display(void) {
    clear_rect(PREVIEW_X, PREVIEW_Y, PREVIEW_CELL * 4 + 2, PREVIEW_CELL * 4 + 2);
    draw_piece_pixels(next_piece, 0, 0, 0, PREVIEW_X, PREVIEW_Y, PREVIEW_CELL);
}

void update_stats_display(void) {
    char buf[12];

    draw_string(1, 11, "          ");
    itoa(score, buf);
    draw_string(1, 11, buf);

    draw_string(1, 14, "          ");
    itoa(total_lines, buf);
    draw_string(1, 14, buf);

    draw_string(1, 17, "          ");
    itoa(level, buf);
    draw_string(1, 17, buf);
}

void lock_piece(void) {
    int dx;
    int dy;
    int bx;
    int by;

    dy = 0;
    while (dy < 4) {
        dx = 0;
        while (dx < 4) {
            if (piece_block(cur_piece, cur_rot, dx, dy)) {
                bx = cur_x + dx;
                by = cur_y + dy;
                if (by >= 0 && by < BOARD_H && bx >= 0 && bx < BOARD_W) {
                    board_set(bx, by, cur_piece + 1);
                }
            }
            dx = dx + 1;
        }
        dy = dy + 1;
    }

    clear_lines_if_needed();
    redraw_board_cells();
    spawn_piece();
    update_stats_display();
    update_preview_display();
    if (!game_over) {
        draw_current_piece();
    }
}

int move_piece_and_redraw(int dx, int dy) {
    int new_x;
    int new_y;

    new_x = cur_x + dx;
    new_y = cur_y + dy;
    if (!can_place(cur_piece, cur_rot, new_x, new_y)) {
        return 0;
    }

    erase_current_piece();
    cur_x = new_x;
    cur_y = new_y;
    draw_current_piece();
    return 1;
}

void try_rotate_piece(void) {
    int new_rot;
    int new_x;

    new_rot = (cur_rot + 1) % 4;

    new_x = cur_x;
    if (!can_place(cur_piece, new_rot, new_x, cur_y)) {
        new_x = cur_x - 1;
        if (!can_place(cur_piece, new_rot, new_x, cur_y)) {
            new_x = cur_x + 1;
            if (!can_place(cur_piece, new_rot, new_x, cur_y)) {
                new_x = cur_x - 2;
                if (!can_place(cur_piece, new_rot, new_x, cur_y)) {
                    new_x = cur_x + 2;
                    if (!can_place(cur_piece, new_rot, new_x, cur_y)) {
                        return;
                    }
                }
            }
        }
    }

    erase_current_piece();
    cur_x = new_x;
    cur_rot = new_rot;
    draw_current_piece();
}

void hard_drop_piece(void) {
    erase_current_piece();
    while (can_place(cur_piece, cur_rot, cur_x, cur_y + 1)) {
        cur_y = cur_y + 1;
    }
    score = score + 2;
    update_stats_display();
    lock_piece();
}

void init_game(void) {
    clear_board();
    score = 0;
    total_lines = 0;
    level = 1;
    game_over = 0;
    key_prev = 0;
    key_repeat_wait = 0;
    drop_counter = 0;
    next_piece = random_piece();
    spawn_piece();

    draw_static_ui();
    redraw_board_cells();
    update_preview_display();
    update_stats_display();
    if (!game_over) {
        draw_current_piece();
    }
}

void render_game_over(void) {
    char buf[12];

    clear_screen();
    draw_string(13, 6, "GAME OVER");
    draw_string(11, 9, "SCORE");
    itoa(score, buf);
    draw_string(18, 9, buf);
    draw_string(11, 11, "LINES");
    itoa(total_lines, buf);
    draw_string(18, 11, buf);
    draw_string(8, 15, "ENTER RESTART");
    draw_string(10, 17, "Q TO QUIT");
}

int handle_live_input(void) {
    int key;

    key = read_key();
    if (key == 0) {
        key_prev = 0;
        key_repeat_wait = 0;
        return 1;
    }
    if (key == 'Q' || key == 'q') {
        return 0;
    }

    if (key == KEY_LEFT) {
        if (key != key_prev) {
            move_piece_and_redraw(-1, 0);
            key_repeat_wait = 150;
        } else if (key_repeat_wait > 0) {
            key_repeat_wait = key_repeat_wait - 1;
        } else {
            move_piece_and_redraw(-1, 0);
            key_repeat_wait = 45;
        }
    } else if (key == KEY_RIGHT) {
        if (key != key_prev) {
            move_piece_and_redraw(1, 0);
            key_repeat_wait = 150;
        } else if (key_repeat_wait > 0) {
            key_repeat_wait = key_repeat_wait - 1;
        } else {
            move_piece_and_redraw(1, 0);
            key_repeat_wait = 45;
        }
    } else if (key == KEY_DOWN) {
        if (key != key_prev) {
            if (!move_piece_and_redraw(0, 1)) {
                lock_piece();
            } else {
                score = score + 1;
                update_stats_display();
            }
            key_repeat_wait = 40;
        } else if (key_repeat_wait > 0) {
            key_repeat_wait = key_repeat_wait - 1;
        } else {
            if (!move_piece_and_redraw(0, 1)) {
                lock_piece();
            } else {
                score = score + 1;
                update_stats_display();
            }
            key_repeat_wait = 12;
        }
        if (!game_over) {
            drop_counter = 0;
        }
    } else if (key == KEY_UP) {
        if (key_prev != KEY_UP) {
            try_rotate_piece();
        }
        key_repeat_wait = 0;
    } else if (key == ' ') {
        if (key_prev != ' ') {
            hard_drop_piece();
        }
        key_repeat_wait = 0;
    } else {
        key_repeat_wait = 0;
    }

    key_prev = key;
    return 1;
}

int run_game(void) {
    int running;
    int delay;

    running = 1;
    while (running && !game_over) {
        running = handle_live_input();

        if (!game_over) {
            drop_counter = drop_counter + 1;
            delay = current_drop_delay();
            if (drop_counter >= delay) {
                if (!move_piece_and_redraw(0, 1)) {
                    lock_piece();
                }
                drop_counter = 0;
            }
        }
    }

    return running;
}

int wait_for_restart(void) {
    int key;

    key_prev = 0;
    while (1) {
        key = read_key();
        if (key == 'Q' || key == 'q') {
            return 0;
        }
        if ((key == KEY_ENTER || key == ' ') && key_prev == 0) {
            while (read_key() != 0) {
            }
            return 1;
        }
        key_prev = key;
    }
}

int main(void) {
    srand(1);

    while (1) {
        init_game();
        if (!run_game()) {
            render_quit_screen();
            return 0;
        }
        render_game_over();
        if (!wait_for_restart()) {
            render_quit_screen();
            return 0;
        }
    }

    return 0;
}
