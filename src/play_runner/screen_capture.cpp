#include "play_runner/screen_capture.h"
#include <algorithm>
#include <cstddef>
#include <stdexcept>
#include <string>
#include <unordered_map>

#include <tlhelp32.h>

namespace play_runner {

    namespace {

        std::wstring Utf8ToWide(const std::string & s) {
            if (s.empty()) {
                return std::wstring();
            }
            int size_needed =
                ::MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
            if (size_needed <= 0) {
                return std::wstring();
            }
            std::wstring result(static_cast<std::size_t>(size_needed), L'\0');
            int written = ::MultiByteToWideChar(
                CP_UTF8,
                0,
                s.c_str(),
                -1,
                &result[0],
                size_needed
            );
            if (written <= 0) {
                return std::wstring();
            }
            if (!result.empty() && result.back() == L'\0') {
                result.pop_back();
            }
            return result;
        }

        std::string WideToUtf8(const std::wstring & w) {
            if (w.empty()) {
                return std::string();
            }
            int size_needed = ::WideCharToMultiByte(
                CP_UTF8,
                0,
                w.c_str(),
                static_cast<int>(w.size()),
                nullptr,
                0,
                nullptr,
                nullptr
            );
            if (size_needed <= 0) {
                return std::string();
            }
            std::string result(static_cast<std::size_t>(size_needed), '\0');
            ::WideCharToMultiByte(
                CP_UTF8,
                0,
                w.c_str(),
                static_cast<int>(w.size()),
                &result[0],
                size_needed,
                nullptr,
                nullptr
            );
            return result;
        }

        std::string ToLowerAscii(const std::string & s) {
            std::string out = s;
            for (char & ch : out) {
                if (ch >= 'A' && ch <= 'Z') {
                    ch = static_cast<char>(ch - 'A' + 'a');
                }
            }
            return out;
        }

        std::unordered_map<unsigned long, std::string> BuildProcessNameMap() {
            std::unordered_map<unsigned long, std::string> result;

            HANDLE snapshot = ::CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
            if (snapshot == INVALID_HANDLE_VALUE) {
                return result;
            }

            PROCESSENTRY32W entry{};
            entry.dwSize = sizeof(entry);

            if (!::Process32FirstW(snapshot, &entry)) {
                ::CloseHandle(snapshot);
                return result;
            }

            do {
                std::wstring name_w(entry.szExeFile);
                std::string name = WideToUtf8(name_w);
                result[entry.th32ProcessID] = ToLowerAscii(name);
            } while (::Process32NextW(snapshot, &entry));

            ::CloseHandle(snapshot);
            return result;
        }

        struct WindowEnumContext {
                std::unordered_map<unsigned long, std::string> * process_names;
                std::vector<WindowInfo> * windows;
        };

        BOOL CALLBACK EnumWindowsProc(HWND hwnd, LPARAM lparam) {
            auto * ctx = reinterpret_cast<WindowEnumContext *>(lparam);

            if (!::IsWindowVisible(hwnd)) {
                return TRUE;
            }

            RECT rect{};
            if (!::GetWindowRect(hwnd, &rect)) {
                return TRUE;
            }
            if (rect.right <= rect.left || rect.bottom <= rect.top) {
                return TRUE;
            }

            wchar_t title_buf[512];
            int title_len = ::GetWindowTextW(
                hwnd,
                title_buf,
                static_cast<int>(sizeof(title_buf) / sizeof(title_buf[0]))
            );
            std::wstring wtitle;
            if (title_len > 0) {
                wtitle.assign(title_buf, title_buf + title_len);
            }

            wchar_t class_buf[256];
            int class_len = ::GetClassNameW(
                hwnd,
                class_buf,
                static_cast<int>(sizeof(class_buf) / sizeof(class_buf[0]))
            );
            std::wstring wclass;
            if (class_len > 0) {
                wclass.assign(class_buf, class_buf + class_len);
            }

            DWORD pid = 0;
            ::GetWindowThreadProcessId(hwnd, &pid);

            WindowInfo info{};
            info.title = WideToUtf8(wtitle);
            info.class_name = WideToUtf8(wclass);
            info.process_id = static_cast<unsigned long>(pid);
            info.rect = rect;
            info.handle =
                reinterpret_cast<std::uint64_t>(reinterpret_cast<void *>(hwnd));

            auto it = ctx->process_names->find(info.process_id);
            if (it != ctx->process_names->end()) {
                info.process_name = it->second;
            }

            ctx->windows->push_back(info);
            return TRUE;
        }

        double GetDpiScaleForWindow(HWND hwnd, bool x_axis) {
            // 优化：缓存 HMODULE，避免每帧 LoadLibrary/FreeLibrary
            static HMODULE shcore = ::LoadLibraryW(L"Shcore.dll");

            using GetDpiForMonitorFunc =
                HRESULT(WINAPI *)(HMONITOR, int, UINT *, UINT *);
            static GetDpiForMonitorFunc get_dpi = nullptr;
            static bool initialized = false;
            if (!initialized) {
                if (shcore) {
                    get_dpi = reinterpret_cast<GetDpiForMonitorFunc>(
                        ::GetProcAddress(shcore, "GetDpiForMonitor")
                    );
                }
                initialized = true;
            }

            if (get_dpi) {
                HMONITOR monitor =
                    ::MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
                UINT dpi_x = 96;
                UINT dpi_y = 96;
                if (SUCCEEDED(get_dpi(monitor, 0, &dpi_x, &dpi_y))) {
                    return x_axis ? static_cast<double>(dpi_x) / 96.0
                                  : static_cast<double>(dpi_y) / 96.0;
                }
            }

            HDC hdc = ::GetDC(hwnd);
            if (!hdc) {
                return 1.0;
            }
            int dpi = ::GetDeviceCaps(hdc, x_axis ? LOGPIXELSX : LOGPIXELSY);
            ::ReleaseDC(hwnd, hdc);
            if (dpi <= 0) {
                return 1.0;
            }
            return static_cast<double>(dpi) / 96.0;
        }

        const char * const kCaptureMethodPrintWindow = "PrintWindow";
        const char * const kCaptureMethodGdiBlt = "GDI BitBlt";
        static const char * g_last_capture_method = kCaptureMethodGdiBlt;

        CapturedFrame CaptureRectWithGdi(const RECT & rect) {
            int width = rect.right - rect.left;
            int height = rect.bottom - rect.top;
            if (width <= 0 || height <= 0) {
                throw std::runtime_error(
                    "CaptureRectWithGdi received empty rect"
                );
            }

            HDC screen_dc = ::GetDC(nullptr);
            if (!screen_dc) {
                throw std::runtime_error("GetDC failed");
            }

            HDC mem_dc = ::CreateCompatibleDC(screen_dc);
            if (!mem_dc) {
                ::ReleaseDC(nullptr, screen_dc);
                throw std::runtime_error("CreateCompatibleDC failed");
            }

            BITMAPINFO bmi{};
            bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
            bmi.bmiHeader.biWidth = width;
            bmi.bmiHeader.biHeight = -height;
            bmi.bmiHeader.biPlanes = 1;
            bmi.bmiHeader.biBitCount = 32;
            bmi.bmiHeader.biCompression = BI_RGB;

            void * bits = nullptr;
            HBITMAP hbm = ::CreateDIBSection(
                screen_dc,
                &bmi,
                DIB_RGB_COLORS,
                &bits,
                nullptr,
                0
            );
            if (!hbm || !bits) {
                if (hbm) {
                    ::DeleteObject(hbm);
                }
                ::DeleteDC(mem_dc);
                ::ReleaseDC(nullptr, screen_dc);
                throw std::runtime_error("CreateDIBSection failed");
            }

            HGDIOBJ old_obj = ::SelectObject(mem_dc, hbm);
            if (!old_obj) {
                ::DeleteObject(hbm);
                ::DeleteDC(mem_dc);
                ::ReleaseDC(nullptr, screen_dc);
                throw std::runtime_error("SelectObject failed");
            }

            BOOL blt_ok = ::BitBlt(
                mem_dc,
                0,
                0,
                width,
                height,
                screen_dc,
                rect.left,
                rect.top,
                SRCCOPY | CAPTUREBLT
            );
            if (!blt_ok) {
                ::SelectObject(mem_dc, old_obj);
                ::DeleteObject(hbm);
                ::DeleteDC(mem_dc);
                ::ReleaseDC(nullptr, screen_dc);
                throw std::runtime_error("BitBlt failed");
            }

            CapturedFrame frame{};
            frame.width = width;
            frame.height = height;
            frame.window_rect = rect;
            frame.bgra.resize(
                static_cast<std::size_t>(width) *
                static_cast<std::size_t>(height) * 4
            );

            auto * src_bytes = static_cast<std::uint8_t *>(bits);
            std::copy(
                src_bytes,
                src_bytes + frame.bgra.size(),
                frame.bgra.data()
            );

            ::SelectObject(mem_dc, old_obj);
            ::DeleteObject(hbm);
            ::DeleteDC(mem_dc);
            ::ReleaseDC(nullptr, screen_dc);

            return frame;
        }

    } // namespace

    std::vector<WindowInfo> EnumerateWindows() {
        auto process_names = BuildProcessNameMap();

        std::vector<WindowInfo> windows;
        WindowEnumContext ctx{};
        ctx.process_names = &process_names;
        ctx.windows = &windows;

        if (!::EnumWindows(EnumWindowsProc, reinterpret_cast<LPARAM>(&ctx))) {
            throw std::runtime_error("EnumWindows failed");
        }

        return windows;
    }

    bool FindWindow(
        const std::string & process_name,
        const std::string & window_title,
        WindowInfo & out
    ) {
        const std::string process_lower = ToLowerAscii(process_name);
        const std::string title_lower = ToLowerAscii(window_title);

        std::vector<WindowInfo> windows = EnumerateWindows();

        for (const auto & win : windows) {
            std::string win_process = ToLowerAscii(win.process_name);
            std::string win_title = ToLowerAscii(win.title);

            bool process_match =
                process_lower.empty() ||
                (!win_process.empty() &&
                 win_process.find(process_lower) != std::string::npos);
            bool title_match =
                title_lower.empty() ||
                (!win_title.empty() &&
                 win_title.find(title_lower) != std::string::npos);

            if (process_match && title_match) {
                out = win;
                return true;
            }
        }

        return false;
    }

    CapturedFrame CaptureWindowFrame(const WindowInfo & window) {
        HWND hwnd =
            reinterpret_cast<HWND>(reinterpret_cast<void *>(window.handle));
        if (!hwnd || !::IsWindow(hwnd)) {
            throw std::invalid_argument("Invalid window handle");
        }

        RECT logical_rect{};
        if (!::GetWindowRect(hwnd, &logical_rect)) {
            throw std::runtime_error("GetWindowRect failed");
        }

        double dpi_scale_x = GetDpiScaleForWindow(hwnd, true);
        double dpi_scale_y = GetDpiScaleForWindow(hwnd, false);

        int logical_width = logical_rect.right - logical_rect.left;
        int logical_height = logical_rect.bottom - logical_rect.top;
        if (logical_width <= 0 || logical_height <= 0) {
            throw std::runtime_error("CaptureWindowFrame got empty rect");
        }

        int width = logical_width;
        int height = logical_height;

        HDC window_dc = ::GetWindowDC(hwnd);
        if (window_dc) {
            HDC mem_dc = ::CreateCompatibleDC(window_dc);
            if (mem_dc) {
                BITMAPINFO bmi{};
                bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
                bmi.bmiHeader.biWidth = width;
                bmi.bmiHeader.biHeight = -height;
                bmi.bmiHeader.biPlanes = 1;
                bmi.bmiHeader.biBitCount = 32;
                bmi.bmiHeader.biCompression = BI_RGB;

                void * bits = nullptr;
                HBITMAP hbm = ::CreateDIBSection(
                    window_dc,
                    &bmi,
                    DIB_RGB_COLORS,
                    &bits,
                    nullptr,
                    0
                );

                if (hbm && bits) {
                    HGDIOBJ old_obj = ::SelectObject(mem_dc, hbm);
                    if (old_obj) {
                        BOOL ok =
                            ::PrintWindow(hwnd, mem_dc, PW_RENDERFULLCONTENT);
                        if (ok) {
                            CapturedFrame frame{};
                            frame.width = width;
                            frame.height = height;
                            frame.dpi_scale_x = dpi_scale_x;
                            frame.dpi_scale_y = dpi_scale_y;
                            frame.window_rect = logical_rect;
                            frame.bgra.resize(
                                static_cast<std::size_t>(width) *
                                static_cast<std::size_t>(height) * 4
                            );

                            auto * src_bytes =
                                static_cast<std::uint8_t *>(bits);
                            std::copy(
                                src_bytes,
                                src_bytes + frame.bgra.size(),
                                frame.bgra.data()
                            );

                            ::SelectObject(mem_dc, old_obj);
                            ::DeleteObject(hbm);
                            ::DeleteDC(mem_dc);
                            ::ReleaseDC(hwnd, window_dc);

                            g_last_capture_method = kCaptureMethodPrintWindow;
                            return frame;
                        }

                        ::SelectObject(mem_dc, old_obj);
                    }

                    ::DeleteObject(hbm);
                }

                ::DeleteDC(mem_dc);
            }

            ::ReleaseDC(hwnd, window_dc);
        }

        g_last_capture_method = kCaptureMethodGdiBlt;

        POINT logical_tl{};
        logical_tl.x = logical_rect.left;
        logical_tl.y = logical_rect.top;
        POINT logical_br{};
        logical_br.x = logical_rect.right;
        logical_br.y = logical_rect.bottom;

        POINT physical_tl =
            LogicalToPhysicalPoint(logical_tl, dpi_scale_x, dpi_scale_y);
        POINT physical_br =
            LogicalToPhysicalPoint(logical_br, dpi_scale_x, dpi_scale_y);

        RECT physical_rect{};
        physical_rect.left = physical_tl.x;
        physical_rect.top = physical_tl.y;
        physical_rect.right = physical_br.x;
        physical_rect.bottom = physical_br.y;

        CapturedFrame frame = CaptureRectWithGdi(physical_rect);

        frame.dpi_scale_x = dpi_scale_x;
        frame.dpi_scale_y = dpi_scale_y;
        frame.window_rect = logical_rect;

        return frame;
    }

    const char * GetLastCaptureMethod() {
        return g_last_capture_method;
    }

    POINT LogicalToPhysicalPoint(
        const POINT & logical,
        double dpi_scale_x,
        double dpi_scale_y
    ) {
        POINT p{};
        p.x = static_cast<LONG>(logical.x * dpi_scale_x);
        p.y = static_cast<LONG>(logical.y * dpi_scale_y);
        return p;
    }

    POINT PhysicalToLogicalPoint(
        const POINT & physical,
        double dpi_scale_x,
        double dpi_scale_y
    ) {
        POINT p{};
        p.x = static_cast<LONG>(physical.x / dpi_scale_x);
        p.y = static_cast<LONG>(physical.y / dpi_scale_y);
        return p;
    }

} // namespace play_runner
