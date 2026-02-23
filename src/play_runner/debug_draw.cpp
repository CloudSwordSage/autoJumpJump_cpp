#include "play_runner/debug_draw.h"

#include "play_runner/image_backend.h"
#include "play_runner/inference.h"

#include <windows.h>

namespace play_runner {

    namespace {

        const COLORREF kColorKey = RGB(1, 0, 1);

        HWND g_overlay_hwnd = nullptr;
        HDC g_overlay_mem_dc = nullptr;
        HBITMAP g_overlay_bmp = nullptr;
        HBITMAP g_overlay_old_bmp = nullptr;
        int g_overlay_width = 0;
        int g_overlay_height = 0;
        void * g_overlay_bits = nullptr;

        HPEN g_pen_roi = nullptr;
        HPEN g_pen_body = nullptr;
        HPEN g_pen_foot = nullptr;
        HPEN g_pen_platform = nullptr;
        HPEN g_pen_target_rect = nullptr;
        HPEN g_pen_target_center = nullptr;
        HPEN g_pen_target_line = nullptr;
        HPEN g_pen_candidates = nullptr;
        HBRUSH g_brush_foot = nullptr;
        HBRUSH g_brush_target_center = nullptr;

        void Utf8ToWide(const std::string & src, std::wstring & dst) {
            if (src.empty()) {
                dst.clear();
                return;
            }
            int len = MultiByteToWideChar(
                CP_UTF8,
                0,
                src.c_str(),
                static_cast<int>(src.size()),
                nullptr,
                0
            );
            if (len <= 0) {
                dst.clear();
                return;
            }
            dst.resize(static_cast<std::size_t>(len));
            MultiByteToWideChar(
                CP_UTF8,
                0,
                src.c_str(),
                static_cast<int>(src.size()),
                dst.data(),
                len
            );
        }

        void EnsureOverlayWindow(const std::string & title) {
            if (g_overlay_hwnd) {
                return;
            }

            WNDCLASSEXW wc{};
            wc.cbSize = sizeof(WNDCLASSEXW);
            wc.style = CS_HREDRAW | CS_VREDRAW;
            wc.lpfnWndProc = DefWindowProcW;
            wc.hInstance = GetModuleHandleW(nullptr);
            wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
            wc.hbrBackground = nullptr;
            wc.lpszClassName = L"AutoJumpOverlayWindow";

            static ATOM atom = RegisterClassExW(&wc);
            if (!atom) {
                return;
            }

            std::wstring wtitle;
            Utf8ToWide(title, wtitle);
            g_overlay_hwnd = CreateWindowExW(
                WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TOOLWINDOW |
                    WS_EX_TRANSPARENT,
                wc.lpszClassName,
                wtitle.c_str(),
                WS_POPUP,
                0,
                0,
                100,
                100,
                nullptr,
                nullptr,
                wc.hInstance,
                nullptr
            );
            if (!g_overlay_hwnd) {
                return;
            }

            ShowWindow(g_overlay_hwnd, SW_SHOW);
            SetWindowPos(
                g_overlay_hwnd,
                HWND_TOPMOST,
                0,
                0,
                0,
                0,
                SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE
            );
        }

        void EnsureGdiObjects() {
            if (!g_pen_roi) {
                g_pen_roi = CreatePen(PS_SOLID, 1, RGB(255, 255, 0));
            }
            if (!g_pen_body) {
                g_pen_body = CreatePen(PS_SOLID, 2, RGB(0, 255, 0));
            }
            if (!g_pen_foot) {
                g_pen_foot = CreatePen(PS_SOLID, 1, RGB(0, 0, 255));
            }
            if (!g_pen_platform) {
                g_pen_platform = CreatePen(PS_SOLID, 2, RGB(255, 0, 0));
            }
            if (!g_pen_target_rect) {
                g_pen_target_rect = CreatePen(PS_SOLID, 3, RGB(0, 255, 255));
            }
            if (!g_pen_target_center) {
                g_pen_target_center = CreatePen(PS_SOLID, 1, RGB(0, 255, 255));
            }
            if (!g_pen_target_line) {
                g_pen_target_line = CreatePen(PS_SOLID, 2, RGB(255, 0, 255));
            }
            if (!g_pen_candidates) {
                g_pen_candidates = CreatePen(PS_SOLID, 1, RGB(100, 100, 100));
            }
            if (!g_brush_foot) {
                g_brush_foot = CreateSolidBrush(RGB(0, 0, 255));
            }
            if (!g_brush_target_center) {
                g_brush_target_center = CreateSolidBrush(RGB(0, 255, 255));
            }
        }

        void DrawTextLine(
            HDC hdc,
            int x,
            int y,
            COLORREF color,
            const std::string & text
        ) {
            std::wstring w;
            Utf8ToWide(text, w);
            if (w.empty()) {
                return;
            }
            SetTextColor(hdc, color);
            SetBkMode(hdc, TRANSPARENT);
            TextOutW(hdc, x, y, w.c_str(), static_cast<int>(w.size()));
        }

        void DrawRoi(HDC hdc, int width, int roi_y_min, int roi_y_max) {
            if (roi_y_max <= roi_y_min) {
                return;
            }
            if (width <= 0) {
                return;
            }
            HPEN pen = g_pen_roi ? g_pen_roi
                                 : static_cast<HPEN>(GetStockObject(WHITE_PEN));
            HGDIOBJ old_pen = SelectObject(hdc, pen);
            HGDIOBJ old_brush = SelectObject(hdc, GetStockObject(HOLLOW_BRUSH));
            Rectangle(hdc, 0, roi_y_min, width - 1, roi_y_max - 1);
            SelectObject(hdc, old_brush);
            SelectObject(hdc, old_pen);

            DrawTextLine(
                hdc,
                10,
                roi_y_min + 10,
                RGB(255, 255, 0),
                "Detection ROI"
            );
        }

        void DrawCharacter(HDC hdc, const CharacterDetection & character) {
            if (character.has_body) {
                HPEN pen = g_pen_body
                               ? g_pen_body
                               : static_cast<HPEN>(GetStockObject(WHITE_PEN));
                HGDIOBJ old_pen = SelectObject(hdc, pen);
                HGDIOBJ old_brush =
                    SelectObject(hdc, GetStockObject(HOLLOW_BRUSH));
                Rectangle(
                    hdc,
                    character.body.x,
                    character.body.y,
                    character.body.x + character.body.width,
                    character.body.y + character.body.height
                );
                SelectObject(hdc, old_brush);
                SelectObject(hdc, old_pen);

                DrawTextLine(
                    hdc,
                    character.body.x,
                    character.body.y - 15,
                    RGB(0, 255, 0),
                    "Body"
                );
            }
            if (character.foot_x >= 0 && character.foot_y >= 0) {
                HPEN pen = g_pen_foot
                               ? g_pen_foot
                               : static_cast<HPEN>(GetStockObject(WHITE_PEN));
                HBRUSH brush =
                    g_brush_foot
                        ? g_brush_foot
                        : static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
                HGDIOBJ old_pen = SelectObject(hdc, pen);
                HGDIOBJ old_brush = SelectObject(hdc, brush);
                Ellipse(
                    hdc,
                    character.foot_x - 4,
                    character.foot_y - 4,
                    character.foot_x + 4,
                    character.foot_y + 4
                );
                SelectObject(hdc, old_brush);
                SelectObject(hdc, old_pen);

                DrawTextLine(
                    hdc,
                    character.foot_x - 20,
                    character.foot_y + 10,
                    RGB(0, 0, 255),
                    "Foot"
                );
            }
            if (character.has_platform) {
                HPEN pen = g_pen_platform
                               ? g_pen_platform
                               : static_cast<HPEN>(GetStockObject(WHITE_PEN));
                HGDIOBJ old_pen = SelectObject(hdc, pen);
                HGDIOBJ old_brush =
                    SelectObject(hdc, GetStockObject(HOLLOW_BRUSH));
                Rectangle(
                    hdc,
                    character.platform.x,
                    character.platform.y,
                    character.platform.x + character.platform.width,
                    character.platform.y + character.platform.height
                );
                SelectObject(hdc, old_brush);
                SelectObject(hdc, old_pen);

                DrawTextLine(
                    hdc,
                    character.platform.x,
                    character.platform.y - 15,
                    RGB(255, 0, 0),
                    "Platform"
                );
            }
        }

        void DrawCandidateBlocks(
            HDC hdc,
            const std::vector<BlockCandidate> & candidates
        ) {
            HPEN pen = g_pen_candidates
                           ? g_pen_candidates
                           : static_cast<HPEN>(GetStockObject(WHITE_PEN));
            HGDIOBJ old_pen = SelectObject(hdc, pen);
            HGDIOBJ old_brush = SelectObject(hdc, GetStockObject(HOLLOW_BRUSH));

            for (std::size_t i = 0; i < candidates.size(); ++i) {
                const auto & c = candidates[i];
                Rectangle(hdc, c.x, c.y, c.x + c.width, c.y + c.height);
                std::string label = "#" + std::to_string(i + 1);
                DrawTextLine(hdc, c.x, c.y - 12, RGB(100, 100, 100), label);
            }

            SelectObject(hdc, old_brush);
            SelectObject(hdc, old_pen);
        }

        void DrawTarget(
            HDC hdc,
            const TargetBlock & target,
            int foot_x,
            int foot_y
        ) {
            if (!target.has_target) {
                return;
            }

            HPEN pen_rect = g_pen_target_rect
                                ? g_pen_target_rect
                                : static_cast<HPEN>(GetStockObject(WHITE_PEN));
            HGDIOBJ old_pen = SelectObject(hdc, pen_rect);
            HGDIOBJ old_brush = SelectObject(hdc, GetStockObject(HOLLOW_BRUSH));

            Rectangle(
                hdc,
                target.block.x,
                target.block.y,
                target.block.x + target.block.width,
                target.block.y + target.block.height
            );

            SelectObject(hdc, old_brush);
            SelectObject(hdc, old_pen);
            HPEN pen_center =
                g_pen_target_center
                    ? g_pen_target_center
                    : static_cast<HPEN>(GetStockObject(WHITE_PEN));
            HBRUSH brush_center =
                g_brush_target_center
                    ? g_brush_target_center
                    : static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
            old_pen = SelectObject(hdc, pen_center);
            old_brush = SelectObject(hdc, brush_center);

            int cx = target.block.center_x;
            int cy = target.block.center_y;
            Ellipse(hdc, cx - 4, cy - 4, cx + 4, cy + 4);

            SelectObject(hdc, old_brush);
            SelectObject(hdc, old_pen);

            DrawTextLine(hdc, cx - 35, cy - 20, RGB(0, 255, 255), "TARGET");

            if (foot_x >= 0 && foot_y >= 0) {
                HPEN pen_line =
                    g_pen_target_line
                        ? g_pen_target_line
                        : static_cast<HPEN>(GetStockObject(WHITE_PEN));
                old_pen = SelectObject(hdc, pen_line);
                MoveToEx(hdc, foot_x, foot_y, nullptr);
                LineTo(hdc, cx, cy);
                SelectObject(hdc, old_pen);

                int mid_x = (foot_x + cx) / 2;
                int mid_y = (foot_y + cy) / 2;
                int dist = static_cast<int>(static_cast<int>(target.distance));
                std::string text = "Dist: " + std::to_string(dist) + "px";
                DrawTextLine(hdc, mid_x, mid_y, RGB(255, 0, 255), text);
            }
        }

        void DrawCandidatesCount(HDC hdc, int count) {
            std::string text = "Candidates: " + std::to_string(count);
            DrawTextLine(hdc, 10, 80, RGB(0, 0, 0), text);
        }

        void DrawFps(HDC hdc, double fps) {
            int v = static_cast<int>(fps * 10.0);
            double rounded = static_cast<double>(v) / 10.0;
            std::string text = "FPS: " + std::to_string(rounded);
            DrawTextLine(hdc, 10, 25, RGB(0, 0, 0), text);
        }

        void DrawStatus(HDC hdc, const std::string & text) {
            DrawTextLine(hdc, 10, 45, RGB(0, 0, 0), text);
        }

        void DrawMode(HDC hdc, const std::string & text) {
            DrawTextLine(hdc, 10, 65, RGB(0, 0, 0), text);
        }

        void DrawControls(HDC hdc, int height, const std::string & text) {
            if (height <= 0) {
                return;
            }
            DrawTextLine(hdc, 10, height - 20, RGB(0, 0, 0), text);
        }

        void DrawCaptureMethod(HDC hdc, const std::string & method) {
            std::string text = "Capture: " + method;
            DrawTextLine(hdc, 10, 5, RGB(0, 0, 0), text);
        }

    } // namespace

    void InitDebugWindow(
        const std::string & title,
        bool enabled,
        double & out_scale
    ) {
        out_scale = 1.0;
        if (!enabled) {
            return;
        }

        HDC hdc = GetDC(nullptr);
        if (hdc) {
            int dpi = GetDeviceCaps(hdc, LOGPIXELSX);
            ReleaseDC(nullptr, hdc);
            if (dpi > 0) {
                out_scale = static_cast<double>(dpi) / 96.0;
            }
        }
        if (out_scale <= 0.0) {
            out_scale = 1.0;
        }

        EnsureOverlayWindow(title);
    }

    void SetOverlayVisible(bool visible) {
        if (!g_overlay_hwnd) {
            return;
        }
        ShowWindow(g_overlay_hwnd, visible ? SW_SHOWNOACTIVATE : SW_HIDE);
    }

    void RenderDebugFrame(
        bool enabled,
        const std::string & title,
        double monitor_scale,
        const std::vector<std::uint8_t> & cropped_bgr,
        int cropped_w,
        int cropped_h,
        int overlay_x,
        int overlay_y,
        int roi_y_min,
        int roi_y_max,
        const CharacterDetection & character,
        const std::vector<BlockCandidate> & candidates,
        const TargetBlock & target,
        double fps,
        const std::string & capture_method,
        const std::string & status_text
    ) {
        (void)title;
        (void)monitor_scale;
        (void)cropped_bgr;

        if (!enabled) {
            return;
        }
        if (!g_overlay_hwnd) {
            EnsureOverlayWindow("Overlay");
            if (!g_overlay_hwnd) {
                return;
            }
        }
        if (cropped_w <= 0 || cropped_h <= 0) {
            return;
        }

        if (!g_overlay_mem_dc) {
            HDC screen_dc = GetDC(nullptr);
            if (!screen_dc) {
                return;
            }
            g_overlay_mem_dc = CreateCompatibleDC(screen_dc);
            ReleaseDC(nullptr, screen_dc);
        }

        if (!g_overlay_mem_dc) {
            return;
        }

        if (!g_overlay_bmp || g_overlay_width != cropped_w ||
            g_overlay_height != cropped_h) {
            if (g_overlay_bmp) {
                if (g_overlay_old_bmp) {
                    SelectObject(g_overlay_mem_dc, g_overlay_old_bmp);
                    g_overlay_old_bmp = nullptr;
                }
                DeleteObject(g_overlay_bmp);
                g_overlay_bmp = nullptr;
                g_overlay_bits = nullptr;
            }

            HDC screen_dc = GetDC(nullptr);
            if (!screen_dc) {
                return;
            }

            BITMAPINFO bmi{};
            bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
            bmi.bmiHeader.biWidth = cropped_w;
            bmi.bmiHeader.biHeight = -cropped_h;
            bmi.bmiHeader.biPlanes = 1;
            bmi.bmiHeader.biBitCount = 32;
            bmi.bmiHeader.biCompression = BI_RGB;

            void * bits = nullptr;
            g_overlay_bmp = CreateDIBSection(
                screen_dc,
                &bmi,
                DIB_RGB_COLORS,
                &bits,
                nullptr,
                0
            );

            ReleaseDC(nullptr, screen_dc);

            if (!g_overlay_bmp || !bits) {
                g_overlay_bits = nullptr;
                return;
            }

            g_overlay_bits = bits;
            g_overlay_old_bmp = static_cast<HBITMAP>(
                SelectObject(g_overlay_mem_dc, g_overlay_bmp)
            );
            g_overlay_width = cropped_w;
            g_overlay_height = cropped_h;
        }

        EnsureGdiObjects();

        HDC hdc = g_overlay_mem_dc;

        if (g_overlay_bits && g_overlay_width == cropped_w &&
            g_overlay_height == cropped_h) {
            unsigned char * base = static_cast<unsigned char *>(g_overlay_bits);
            int stride = g_overlay_width * 4;
            for (int y = 0; y < g_overlay_height; ++y) {
                unsigned char * row = base + y * stride;
                for (int x = 0; x < g_overlay_width; ++x) {
                    unsigned char * px = row + x * 4;
                    px[0] = 1;
                    px[1] = 0;
                    px[2] = 1;
                    px[3] = 0;
                }
            }
        }

        DrawCaptureMethod(hdc, capture_method);
        DrawRoi(hdc, cropped_w, roi_y_min, roi_y_max);
        DrawCharacter(hdc, character);
        DrawCandidateBlocks(hdc, candidates);
        if (target.has_target) {
            DrawTarget(hdc, target, character.foot_x, character.foot_y);
        }
        DrawCandidatesCount(hdc, static_cast<int>(candidates.size()));
        DrawFps(hdc, fps);
        DrawStatus(hdc, status_text);
        DrawMode(hdc, "Mode: LAB + YOLO Top");
        DrawControls(
            hdc,
            cropped_h,
            "ESC/Q:Quit | S:Start Auto | D:Stop Auto | SPACE:Jump | P:Capture"
        );

        if (g_overlay_bits && g_overlay_width == cropped_w &&
            g_overlay_height == cropped_h) {
            unsigned char * base = static_cast<unsigned char *>(g_overlay_bits);
            int stride = g_overlay_width * 4;
            for (int y = 0; y < g_overlay_height; ++y) {
                unsigned char * row = base + y * stride;
                for (int x = 0; x < g_overlay_width; ++x) {
                    unsigned char * px = row + x * 4;
                    if (px[0] == 1 && px[1] == 0 && px[2] == 1) {
                        px[3] = 0;
                    } else {
                        px[3] = 255;
                    }
                }
            }
        }

        POINT dst_pos;
        dst_pos.x = overlay_x;
        dst_pos.y = overlay_y;
        SIZE dst_size;
        dst_size.cx = cropped_w;
        dst_size.cy = cropped_h;
        POINT src_pos;
        src_pos.x = 0;
        src_pos.y = 0;

        BLENDFUNCTION blend{};
        blend.BlendOp = AC_SRC_OVER;
        blend.BlendFlags = 0;
        blend.SourceConstantAlpha = 255;
        blend.AlphaFormat = AC_SRC_ALPHA;

        UpdateLayeredWindow(
            g_overlay_hwnd,
            nullptr,
            &dst_pos,
            &dst_size,
            g_overlay_mem_dc,
            &src_pos,
            0,
            &blend,
            ULW_ALPHA
        );
    }

} // namespace play_runner
