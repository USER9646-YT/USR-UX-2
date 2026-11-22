#include <fcntl.h>
#include <linux/fb.h>
#include <stdint.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>

typedef struct {
    int x, y, w, h;
    uint32_t color;
    int has_close_button;
} Window;

#define MAX_WINDOWS 10
Window windows[MAX_WINDOWS];
int num_windows = 0;
int active_dragging_idx = -1;

const int8_t cursor_bits[16][11] = {
    {0, -1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
    {0,  0,-1,-1,-1,-1,-1,-1,-1,-1,-1},
    {0,  1, 0,-1,-1,-1,-1,-1,-1,-1,-1},
    {0,  1, 1, 0,-1,-1,-1,-1,-1,-1,-1},
    {0,  1, 1, 1, 0,-1,-1,-1,-1,-1,-1},
    {0,  1, 1, 1, 1, 0,-1,-1,-1,-1,-1},
    {0,  1, 1, 1, 1, 1, 0,-1,-1,-1,-1},
    {0,  1, 1, 1, 1, 1, 1, 0,-1,-1,-1},
    {0,  1, 1, 1, 1, 1, 1, 1, 0,-1,-1},
    {0,  1, 1, 1, 1, 1, 0, 0, 0, 0,-1},
    {0,  1, 1, 0, 1, 1, 0,-1,-1,-1,-1},
    {0,  1, 0,-1, 0, 1, 1, 0,-1,-1,-1},
    {0,  0,-1,-1, 0, 1, 1, 0,-1,-1,-1},
    {-1,-1,-1,-1,-1, 0, 1, 1, 0,-1,-1},
    {-1,-1,-1,-1,-1, 0, 1, 1, 0,-1,-1},
    {-1,-1,-1,-1,-1,-1, 0, 0,-1,-1,-1}
};

void draw_rect(uint32_t *buf, int stride, int max_w, int max_h, int rx, int ry, int rw, int rh, uint32_t color) {
    for (int y = ry; y < ry + rh; y++) {
        if (y < 0 || y >= max_h) continue;
        for (int x = rx; x < rx + rw; x++) {
            if (x < 0 || x >= max_w) continue;
            buf[y * stride + x] = color;
        }
    }
}

void draw_cursor(uint32_t *buf, int stride, int max_w, int max_h, int cx, int cy) {
    for (int dy = 0; dy < 16; dy++) {
        for (int dx = 0; dx < 11; dx++) {
            int px = cx + dx;
            int py = cy + dy;
            if (px < 0 || px >= max_w || py < 0 || py >= max_h) continue;
            int8_t val = cursor_bits[dy][dx];
            if (val == 0) buf[py * stride + px] = 0x00000000;
            else if (val == 1) buf[py * stride + px] = 0x00FFFFFF;
        }
    }
}

void add_window(int x, int y, int w, int h, uint32_t color, int has_close) {
    if (num_windows < MAX_WINDOWS) {
        windows[num_windows] = (Window){x, y, w, h, color, has_close};
        num_windows++;
    }
}

void close_window(int index) {
    for (int i = index; i < num_windows - 1; i++) {
        windows[i] = windows[i + 1];
    }
    num_windows--;
}

int main() {
    int fb = open("/dev/fb0", O_RDWR);
    struct fb_var_screeninfo vinfo;
    struct fb_fix_screeninfo finfo;
    ioctl(fb, FBIOGET_VSCREENINFO, &vinfo);
    ioctl(fb, FBIOGET_FSCREENINFO, &finfo);

    size_t screensize = finfo.smem_len;
    int stride = finfo.line_length / 4;
    uint32_t *fbp = mmap(0, screensize, PROT_READ | PROT_WRITE, MAP_SHARED, fb, 0);
    uint32_t *backbuffer = malloc(screensize);

    int mouse = open("/dev/input/mice", O_RDONLY);
    fcntl(mouse, F_SETFL, fcntl(mouse, F_GETFL, 0) | O_NONBLOCK);

    int mx = vinfo.xres / 2, my = vinfo.yres / 2;
    int prev_left_btn = 0;

    // Create Initial Windows:
    // Window 1: Has a red close button
    add_window(150, 100, 380, 260, 0x00E0E0E0, 1);
    // Window 2: NO close button (just a tool panel)
    add_window(580, 180, 300, 220, 0x00D0E8D0, 0);

    uint8_t packet[3];
    while (1) {
        int left_btn = 0;
        int dx = 0, dy = 0;

        // Drain mouse events
        while (read(mouse, packet, 3) == 3) {
            left_btn = packet[0] & 0x1;
            dx = (int8_t)packet[1];
            dy = -(int8_t)packet[2];

            mx += dx;
            my += dy;
            if (mx < 0) mx = 0;
            if (my < 0) my = 0;
            if (mx >= (int)vinfo.xres) mx = vinfo.xres - 1;
            if (my >= (int)vinfo.yres) my = vinfo.yres - 1;
        }

        // Handle Click / Drag logic on mouse down transition
        if (left_btn && !prev_left_btn) {
            active_dragging_idx = -1;
            // Iterate backwards (top-most window first)
            for (int i = num_windows - 1; i >= 0; i--) {
                Window *w = &windows[i];
                // Check titlebar hit
                if (mx >= w->x && mx <= w->x + w->w && my >= w->y && my <= w->y + 24) {
                    
                    // Check if close button was clicked
                    if (w->has_close_button) {
                        int bx = w->x + w->w - 20;
                        int by = w->y + 4;
                        if (mx >= bx && mx <= bx + 16 && my >= by && my <= by + 16) {
                            close_window(i);
                            break;
                        }
                    }

                    // Otherwise, trigger window drag and bring window to front (Z-order shift)
                    active_dragging_idx = i;
                    Window targeted = *w;
                    close_window(i);
                    add_window(targeted.x, targeted.y, targeted.w, targeted.h, targeted.color, targeted.has_close_button);
                    active_dragging_idx = num_windows - 1; // Now at the top
                    break;
                }
            }
        } else if (left_btn && prev_left_btn && active_dragging_idx != -1) {
            // Drag active window
            windows[active_dragging_idx].x += dx;
            windows[active_dragging_idx].y += dy;
        } else if (!left_btn) {
            active_dragging_idx = -1;
        }
        prev_left_btn = left_btn;

        // Exit if all windows are closed
        if (num_windows == 0) break;

        // --- Render Frame ---
        // 1. Desktop Background
        draw_rect(backbuffer, stride, vinfo.xres, vinfo.yres, 0, 0, vinfo.xres, vinfo.yres, 0x00008080);

        // 2. Render All Windows (Back to Front)
        for (int i = 0; i < num_windows; i++) {
            Window *w = &windows[i];

            // Drop Shadow
            draw_rect(backbuffer, stride, vinfo.xres, vinfo.yres, w->x + 3, w->y + 3, w->w, w->h + 24, 0x00000000);
            // Outer Border
            draw_rect(backbuffer, stride, vinfo.xres, vinfo.yres, w->x - 1, w->y - 1, w->w + 2, w->h + 26, 0x00808080);
            // Titlebar
            draw_rect(backbuffer, stride, vinfo.xres, vinfo.yres, w->x, w->y, w->w, 24, 0x00000080);

            // Red Close Button (if enabled)
            if (w->has_close_button) {
                int bx = w->x + w->w - 20;
                int by = w->y + 4;
                // Red box
                draw_rect(backbuffer, stride, vinfo.xres, vinfo.yres, bx, by, 16, 16, 0x00CC0000);
                // White 'X' accent lines inside button
                draw_rect(backbuffer, stride, vinfo.xres, vinfo.yres, bx + 4, by + 7, 8, 2, 0x00FFFFFF);
            }

            // Client Area Body
            draw_rect(backbuffer, stride, vinfo.xres, vinfo.yres, w->x, w->y + 24, w->w, w->h, w->color);
        }

        // 3. Cursor
        draw_cursor(backbuffer, stride, vinfo.xres, vinfo.yres, mx, my);

        // 4. Blit to Screen
        memcpy(fbp, backbuffer, screensize);
    }

    free(backbuffer);
    munmap(fbp, screensize);
    close(mouse);
    close(fb);
    return 0;
}