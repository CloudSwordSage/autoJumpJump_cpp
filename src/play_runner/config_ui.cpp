#include "play_runner/config_ui.h"

#include "play_runner/config.h"
#include "play_runner/config_ui_state.h"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <string>
#include <utility>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#ifdef _WIN32
#include <windows.h>
#endif
#include <GL/gl.h>

#ifndef GL_CLAMP_TO_EDGE
#define GL_CLAMP_TO_EDGE 0x812F
#endif

#include "imgui.h"
#include "backends/imgui_impl_glfw.h"
#include "backends/imgui_impl_opengl3.h"
#include "misc/cpp/imgui_stdlib.h"

namespace play_runner {

    namespace {

        void GlfwErrorCallback(int error, const char * description) {
            Logger::Instance().Error(
                std::string("GLFW error ") + std::to_string(error) + ": " +
                (description ? description : "")
            );
        }

        bool SliderIntWithInput(
            const char * label_cn,
            int & value,
            int min_value,
            int max_value
        ) {
            bool changed = false;
            ImGui::PushID(label_cn);
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(label_cn);

            float full_width = ImGui::GetContentRegionAvail().x;
            float input_width = 100.0f;
            float slider_width =
                std::max(120.0f, full_width - input_width - 8.0f);

            ImGui::SetNextItemWidth(slider_width);
            changed |=
                ImGui::SliderInt("##slider", &value, min_value, max_value);
            ImGui::SameLine();
            ImGui::SetNextItemWidth(input_width);
            changed |= ImGui::InputInt("##input", &value);
            ImGui::PopID();
            return changed;
        }

        bool SliderFloatWithInput(
            const char * label_cn,
            float & value,
            float min_value,
            float max_value,
            const char * format
        ) {
            bool changed = false;
            ImGui::PushID(label_cn);
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(label_cn);

            float full_width = ImGui::GetContentRegionAvail().x;
            float input_width = 100.0f;
            float slider_width =
                std::max(120.0f, full_width - input_width - 8.0f);

            ImGui::SetNextItemWidth(slider_width);
            changed |= ImGui::SliderFloat(
                "##slider",
                &value,
                min_value,
                max_value,
                format
            );
            ImGui::SameLine();
            ImGui::SetNextItemWidth(input_width);
            changed |= ImGui::InputFloat("##input", &value, 0.0f, 0.0f, format);
            ImGui::PopID();
            return changed;
        }

        class ScopedImGuiDisabled {
            public:
                explicit ScopedImGuiDisabled(bool disabled)
                    : disabled_(disabled) {
                    if (!disabled_) {
                        return;
                    }
                    ImGui::PushItemFlag(ImGuiItemFlags_Disabled, true);
                    ImGui::PushStyleVar(
                        ImGuiStyleVar_Alpha,
                        ImGui::GetStyle().Alpha * 0.5f
                    );
                }

                ~ScopedImGuiDisabled() {
                    if (!disabled_) {
                        return;
                    }
                    ImGui::PopStyleVar();
                    ImGui::PopItemFlag();
                }

            private:
                bool disabled_;
        };

        ImU32 ToColorU32(unsigned int rgba) {
            unsigned int r = (rgba >> 24) & 0xff;
            unsigned int g = (rgba >> 16) & 0xff;
            unsigned int b = (rgba >> 8) & 0xff;
            unsigned int a = rgba & 0xff;
            return IM_COL32(r, g, b, a);
        }

        std::filesystem::path GetWindowsFontDir() {
#ifdef _WIN32
            wchar_t buffer[MAX_PATH];
            UINT len = ::GetWindowsDirectoryW(buffer, MAX_PATH);
            if (len == 0 || len >= MAX_PATH) {
                return {};
            }
            return std::filesystem::path(buffer) / "Fonts";
#else
            return {};
#endif
        }

        void AddFontCandidateIfExists(
            std::vector<ConfigUi::FontCandidate> & out,
            const std::filesystem::path & font_dir,
            const char * file_name,
            const char * label,
            int font_no
        ) {
            if (font_dir.empty()) {
                return;
            }
            std::filesystem::path p = font_dir / file_name;
            if (!std::filesystem::exists(p)) {
                return;
            }
            out.push_back(ConfigUi::FontCandidate{label, p.string(), font_no});
        }

    } // namespace

    ConfigUi::ConfigUi(
        std::shared_ptr<ConfigUiState> ui_state,
        std::string config_path
    )
        : ui_state_(std::move(ui_state)), config_path_(std::move(config_path)),
          category_(Category::Global),
          working_config_(LoadConfigOrDefault(config_path_)),
          last_frame_seq_(0), last_log_seq_(0), frame_texture_(0),
          frame_texture_w_(0), frame_texture_h_(0),
          right_preview_column_width_(360.0f), auto_scroll_logs_(true),
          selected_font_index_(0), font_size_px_(20.0f), font_dirty_(true),
          open_success_popup_(false), success_popup_message_(""),
          open_calibration_popup_(false), last_calibration_result_id_(0),
          pending_calibration_request_id_(0) {
        last_log_seq_ = Logger::Instance().GetLastSeq();

        std::filesystem::path font_dir = GetWindowsFontDir();
        AddFontCandidateIfExists(
            font_candidates_,
            font_dir,
            "simhei.ttf",
            "黑体 (SimHei)",
            0
        );
        AddFontCandidateIfExists(
            font_candidates_,
            font_dir,
            "msyh.ttc",
            "微软雅黑 (Microsoft YaHei)",
            0
        );
        AddFontCandidateIfExists(
            font_candidates_,
            font_dir,
            "simsun.ttc",
            "宋体 (SimSun)",
            0
        );
        AddFontCandidateIfExists(
            font_candidates_,
            font_dir,
            "simkai.ttf",
            "楷体 (KaiTi)",
            0
        );
        AddFontCandidateIfExists(
            font_candidates_,
            font_dir,
            "simfang.ttf",
            "仿宋 (FangSong)",
            0
        );

        for (std::size_t i = 0; i < font_candidates_.size(); ++i) {
            if (font_candidates_[i].label.find("黑体") != std::string::npos) {
                selected_font_index_ = static_cast<int>(i);
                break;
            }
        }
    }

    int ConfigUi::Run() {
        glfwSetErrorCallback(GlfwErrorCallback);
        if (!glfwInit()) {
            Logger::Instance().Error("Failed to initialize GLFW");
            return 0;
        }

        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);

        GLFWwindow * window = glfwCreateWindow(
            1280,
            800,
            "AutoJumpJump Config",
            nullptr,
            nullptr
        );
        if (window == nullptr) {
            Logger::Instance().Error("Failed to create GLFW window");
            glfwTerminate();
            return 0;
        }

        glfwMakeContextCurrent(window);
        glfwSwapInterval(1);

        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGui::StyleColorsDark();

        ImGuiIO & io = ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

        ImGui_ImplGlfw_InitForOpenGL(window, true);
        ImGui_ImplOpenGL3_Init("#version 130");

        RebuildFonts();

        while (!ui_state_->exit_requested.load()) {
            glfwPollEvents();
            if (glfwWindowShouldClose(window)) {
                ui_state_->exit_requested.store(true);
                break;
            }

            if (font_dirty_) {
                RebuildFonts();
            }

            ImGui_ImplOpenGL3_NewFrame();
            ImGui_ImplGlfw_NewFrame();
            ImGui::NewFrame();

            UpdateFrameTexture();
            DrawUi();

            ImGui::Render();

            int display_w = 0;
            int display_h = 0;
            glfwGetFramebufferSize(window, &display_w, &display_h);
            glViewport(0, 0, display_w, display_h);
            glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT);
            ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
            glfwSwapBuffers(window);
        }

        if (frame_texture_ != 0) {
            glDeleteTextures(1, &frame_texture_);
            frame_texture_ = 0;
        }

        ImGui_ImplOpenGL3_Shutdown();
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();

        glfwDestroyWindow(window);
        glfwTerminate();
        return 0;
    }

    void ConfigUi::DrawUi() {
        ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize, ImGuiCond_Always);
        ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration |
                                 ImGuiWindowFlags_NoMove |
                                 ImGuiWindowFlags_NoSavedSettings;
        if (!ImGui::Begin("##root", nullptr, flags)) {
            ImGui::End();
            return;
        }

        int calib_x = 0;
        int calib_y = 0;
        if (ui_state_->ConsumeFootCalibrationResult(
                last_calibration_result_id_,
                calib_x,
                calib_y
            )) {
            working_config_.jump.foot_center_offset_x = calib_x;
            working_config_.jump.foot_center_offset_y = calib_y;
            status_text_ = "校准完成并已写入配置";
            pending_calibration_request_id_ = 0;
        }

        ImGui::BeginChild("##toolbar", ImVec2(0, 42.0f), false);
        bool target_window_found = ui_state_->IsTargetWindowFound();
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("字体");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(260.0f);
        const char * preview = font_candidates_.empty()
                                   ? "默认"
                                   : font_candidates_[static_cast<std::size_t>(
                                                          selected_font_index_
                                                      )]
                                         .label.c_str();
        if (ImGui::BeginCombo("##font_combo", preview)) {
            if (font_candidates_.empty()) {
                ImGui::Selectable("默认", true);
            } else {
                for (std::size_t i = 0; i < font_candidates_.size(); ++i) {
                    bool selected =
                        (selected_font_index_ == static_cast<int>(i));
                    if (ImGui::Selectable(
                            font_candidates_[i].label.c_str(),
                            selected
                        )) {
                        selected_font_index_ = static_cast<int>(i);
                        font_dirty_ = true;
                    }
                    if (selected) {
                        ImGui::SetItemDefaultFocus();
                    }
                }
            }
            ImGui::EndCombo();
        }

        ImGui::SameLine();
        ImGui::TextUnformatted("字号");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(140.0f);
        if (ImGui::SliderFloat(
                "##font_size",
                &font_size_px_,
                14.0f,
                32.0f,
                "%.0f"
            )) {
            font_dirty_ = true;
        }

        if (!target_window_found) {
            const char * msg = "未找到窗口";
            float text_w = ImGui::CalcTextSize(msg).x;
            float center_x =
                (ImGui::GetWindowContentRegionMax().x - text_w) * 0.5f;
            ImGui::SameLine();
            ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX(), center_x));
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "%s", msg);
        }

        ImGui::SameLine();
        ImGui::SetCursorPosX(ImGui::GetWindowContentRegionMax().x - 90.0f);
        if (ImGui::Button("校准")) {
            open_calibration_popup_ = true;
        }
        ImGui::EndChild();

        if (open_calibration_popup_) {
            ImGui::OpenPopup("##calibration_popup");
            open_calibration_popup_ = false;
        }
        if (ImGui::BeginPopupModal(
                "##calibration_popup",
                nullptr,
                ImGuiWindowFlags_AlwaysAutoResize
            )) {
            ImGui::TextUnformatted(
                "请将游戏重启, 在0分的界面进行校准\n(Tips: "
                "按下开始校准之后鼠标右键游戏窗口以获取前台焦点)"
            );
            ImGui::Separator();
            if (!target_window_found) {
                ImGui::TextColored(
                    ImVec4(1.0f, 0.4f, 0.4f, 1.0f),
                    "%s",
                    "未找到窗口，无法校准"
                );
            }
            if (pending_calibration_request_id_ != 0) {
                ImGui::TextUnformatted("校准中... 请保持角色静止");
            }
            {
                ScopedImGuiDisabled disable(!target_window_found);
                if (ImGui::Button("开始校准")) {
                    pending_calibration_request_id_ =
                        ui_state_->RequestFootCalibration();
                    status_text_ = "已发起校准请求";
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("关闭")) {
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }

        ImGui::Separator();

        float log_height = 260.0f;
        ImVec2 avail = ImGui::GetContentRegionAvail();
        float top_height = std::max(100.0f, avail.y - log_height - 8.0f);

        ImGui::BeginChild(
            "##top",
            ImVec2(0, top_height),
            false,
            ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse
        );
        {
            ImVec2 top_avail = ImGui::GetContentRegionAvail();
            if (frame_texture_ != 0 && frame_texture_w_ > 0 &&
                frame_texture_h_ > 0) {
                const ImGuiStyle & style = ImGui::GetStyle();

                float cell_h = std::max(1.0f, top_avail.y);
                float image_h = cell_h - style.CellPadding.y * 2.0f -
                                style.WindowPadding.y * 2.0f -
                                style.ChildBorderSize * 2.0f;
                image_h = std::max(1.0f, image_h);

                float scale = image_h / static_cast<float>(frame_texture_h_);
                scale = std::max(0.01f, scale);
                float image_w = static_cast<float>(frame_texture_w_) * scale;

                float desired = image_w + style.CellPadding.x * 2.0f +
                                style.WindowPadding.x * 2.0f +
                                style.ChildBorderSize * 2.0f;

                float total_w = std::max(1.0f, top_avail.x);
                float min_center_w = 420.0f;
                float max_allowed =
                    std::max(200.0f, total_w - 160.0f - min_center_w);
                desired = std::clamp(desired, 200.0f, max_allowed);
                right_preview_column_width_ = desired;
            }
        }

        if (ImGui::BeginTable("##layout", 3, ImGuiTableFlags_Resizable)) {
            ImGui::TableSetupColumn(
                "##left",
                ImGuiTableColumnFlags_WidthFixed,
                160.0f
            );
            ImGui::TableSetupColumn(
                "##center",
                ImGuiTableColumnFlags_WidthStretch,
                0.0f
            );
            ImGui::TableSetupColumn(
                "##right",
                ImGuiTableColumnFlags_WidthFixed |
                    ImGuiTableColumnFlags_NoResize,
                right_preview_column_width_
            );

            ImGui::TableNextRow();

            ImGui::TableSetColumnIndex(0);
            DrawLeftCategory();

            ImGui::TableSetColumnIndex(1);
            DrawCenterEditor();

            ImGui::TableSetColumnIndex(2);
            DrawRightPreview();

            ImGui::EndTable();
        }
        ImGui::EndChild();

        DrawBottomLogs(log_height);

        ImGui::End();
    }

    void ConfigUi::DrawLeftCategory() {
        ImGui::BeginChild("##cat", ImVec2(0, 0), true);
        auto draw = [&](Category c, const char * name) {
            bool selected = (category_ == c);
            if (ImGui::Selectable(name, selected)) {
                category_ = c;
            }
        };
        draw(Category::Global, "全局");
        draw(Category::RoleLab, "角色Lab");
        draw(Category::Jump, "跳跃参数");
        draw(Category::Model, "模型信息");
        draw(Category::FailDetect, "失败检测");
        draw(Category::Log, "日志");
        ImGui::EndChild();
    }

    void ConfigUi::DrawCenterEditor() {
        float footer_height = 54.0f;
        ImVec2 avail = ImGui::GetContentRegionAvail();
        float body_height = std::max(0.0f, avail.y - footer_height);

        ImGui::BeginChild("##editor_body", ImVec2(0, body_height), true);

        if (!status_text_.empty()) {
            ImGui::TextUnformatted(status_text_.c_str());
        }

        if (category_ == Category::Global) {
            ImGui::TextUnformatted("全局");
            ImGui::Separator();

            ImGui::TextUnformatted("进程名");
            ImGui::InputText(
                "##process_name",
                &working_config_.capture.process_name
            );

            ImGui::Checkbox("调试", &working_config_.debug);
            ImGui::SameLine();
            bool adaptive_toggle_changed = ImGui::Checkbox(
                "启用自适应调整",
                &working_config_.jump.enable_adaptive_adjustment
            );
            if (adaptive_toggle_changed &&
                working_config_.jump.enable_adaptive_adjustment) {
                working_config_.jump.press_duration_mode = 0;
            }
            ImGui::SameLine();
            ImGui::Checkbox("自动重启", &working_config_.auto_restart);

            {
                ScopedImGuiDisabled disable_mode_select(
                    working_config_.jump.enable_adaptive_adjustment
                );
                ImGui::RadioButton(
                    "使用分段函数",
                    &working_config_.jump.press_duration_mode,
                    0
                );
                ImGui::SameLine();
                ImGui::RadioButton(
                    "使用基础参数",
                    &working_config_.jump.press_duration_mode,
                    1
                );
            }

            ImGui::Checkbox(
                "启用监控窗口",
                &working_config_.display.enable_monitor_window
            );

            ImGui::Separator();
            SliderIntWithInput(
                "上裁剪",
                working_config_.capture.crop_top,
                0,
                2000
            );
            SliderIntWithInput(
                "下裁剪",
                working_config_.capture.crop_bottom,
                0,
                2000
            );
            SliderIntWithInput(
                "左裁剪",
                working_config_.capture.crop_left,
                0,
                2000
            );
            SliderIntWithInput(
                "右裁剪",
                working_config_.capture.crop_right,
                0,
                2000
            );
            SliderIntWithInput(
                "ROI 顶部边距",
                working_config_.capture.roi_top_margin,
                0,
                4000
            );
            SliderIntWithInput(
                "ROI 底部边距",
                working_config_.capture.roi_bottom_margin,
                0,
                4000
            );
        }

        if (category_ == Category::RoleLab) {
            ImGui::TextUnformatted("角色Lab");
            ImGui::Separator();

            SliderIntWithInput("Lab L", working_config_.lab.target_l, 0, 255);
            SliderIntWithInput("Lab A", working_config_.lab.target_a, 0, 255);
            SliderIntWithInput("Lab B", working_config_.lab.target_b, 0, 255);
            SliderIntWithInput(
                "颜色距离阈值",
                working_config_.lab.distance_threshold,
                0,
                200
            );

            float weight_l = static_cast<float>(working_config_.lab.weight_l);
            float weight_a = static_cast<float>(working_config_.lab.weight_a);
            float weight_b = static_cast<float>(working_config_.lab.weight_b);

            if (SliderFloatWithInput("权重 L", weight_l, 0.0f, 5.0f, "%.3f")) {
                working_config_.lab.weight_l = static_cast<double>(weight_l);
            }
            if (SliderFloatWithInput("权重 A", weight_a, 0.0f, 5.0f, "%.3f")) {
                working_config_.lab.weight_a = static_cast<double>(weight_a);
            }
            if (SliderFloatWithInput("权重 B", weight_b, 0.0f, 5.0f, "%.3f")) {
                working_config_.lab.weight_b = static_cast<double>(weight_b);
            }
        }

        if (category_ == Category::Jump) {
            ImGui::TextUnformatted("跳跃参数");
            ImGui::Separator();

            float jump_alpha =
                static_cast<float>(working_config_.jump.jump_alpha);
            float jump_beta =
                static_cast<float>(working_config_.jump.jump_beta);
            if (SliderFloatWithInput(
                    "基础系数 alpha",
                    jump_alpha,
                    0.0f,
                    10.0f,
                    "%.3f"
                )) {
                working_config_.jump.jump_alpha =
                    static_cast<double>(jump_alpha);
            }
            if (SliderFloatWithInput(
                    "基础偏置 beta",
                    jump_beta,
                    -2000.0f,
                    2000.0f,
                    "%.3f"
                )) {
                working_config_.jump.jump_beta = static_cast<double>(jump_beta);
            }

            SliderIntWithInput(
                "脚底校准偏移 X",
                working_config_.jump.foot_center_offset_x,
                -200,
                200
            );
            SliderIntWithInput(
                "脚底校准偏移 Y",
                working_config_.jump.foot_center_offset_y,
                -200,
                200
            );

            SliderIntWithInput(
                "历史样本容量",
                working_config_.jump.params.history_size,
                1,
                5000
            );
            SliderIntWithInput(
                "失败丢弃条数",
                working_config_.jump.params.fail_discard_count,
                0,
                100
            );
            SliderIntWithInput(
                "最大分段数",
                working_config_.jump.params.max_segments,
                1,
                20
            );
            SliderIntWithInput(
                "最小段长度",
                working_config_.jump.params.min_len,
                2,
                100
            );

            ImGui::Separator();
            ImGui::TextUnformatted("自适应分段（只读）");
            AppConfig applied_config = ui_state_->GetConfigSnapshot();
            const JumpParamsConfig & view_params = applied_config.jump.params;
            int seg_count = static_cast<int>(view_params.segments.size());
            ImGui::Text("当前分段数: %d", seg_count);
            ImGui::Text("最佳分段数: %d", view_params.best_split);

            if (seg_count > 0 &&
                ImGui::BeginTable(
                    "##jump_segments_table",
                    5,
                    ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                        ImGuiTableFlags_SizingFixedFit
                )) {
                ImGui::TableSetupColumn("段");
                ImGui::TableSetupColumn("a");
                ImGui::TableSetupColumn("b");
                ImGui::TableSetupColumn("x_start");
                ImGui::TableSetupColumn("x_end");
                ImGui::TableHeadersRow();

                for (int i = 0; i < seg_count; ++i) {
                    const JumpSegment & seg =
                        view_params.segments[static_cast<std::size_t>(i)];
                    ImGui::TableNextRow();

                    ImGui::TableSetColumnIndex(0);
                    ImGui::Text("%d", i + 1);
                    ImGui::TableSetColumnIndex(1);
                    ImGui::Text("%.6f", seg.a);
                    ImGui::TableSetColumnIndex(2);
                    ImGui::Text("%.6f", seg.b);
                    ImGui::TableSetColumnIndex(3);
                    ImGui::Text("%.2f", seg.x_start);
                    ImGui::TableSetColumnIndex(4);
                    if (seg.x_end == -1.0) {
                        ImGui::TextUnformatted("-1");
                    } else {
                        ImGui::Text("%.2f", seg.x_end);
                    }
                }
                ImGui::EndTable();
            }

            SliderIntWithInput(
                "稳定帧数",
                working_config_.jump.stable_min_frames,
                0,
                60
            );
            SliderIntWithInput(
                "位置容差",
                working_config_.jump.stable_pos_eps,
                0,
                50
            );
        }

        if (category_ == Category::Model) {
            ImGui::TextUnformatted("模型信息");
            ImGui::Separator();

            ImGui::TextUnformatted("模型路径");
            ImGui::InputText(
                "##onnx_model_path",
                &working_config_.onnx.model_path
            );
            SliderIntWithInput(
                "输入宽度",
                working_config_.onnx.input_width,
                1,
                4096
            );
            SliderIntWithInput(
                "输入高度",
                working_config_.onnx.input_height,
                1,
                4096
            );

            float score = working_config_.onnx.score_threshold;
            if (SliderFloatWithInput("置信度阈值", score, 0.0f, 1.0f, "%.3f")) {
                working_config_.onnx.score_threshold = score;
            }
            float nms = working_config_.onnx.nms_iou_threshold;
            if (SliderFloatWithInput("NMS IoU 阈值", nms, 0.0f, 1.0f, "%.3f")) {
                working_config_.onnx.nms_iou_threshold = nms;
            }
        }

        if (category_ == Category::FailDetect) {
            ImGui::TextUnformatted("失败检测");
            ImGui::Separator();

            ImGui::TextUnformatted("模板目录");
            ImGui::InputText(
                "##fail_template_path",
                &working_config_.fail_template.template_path
            );

            if (ImGui::TreeNode("模板文件列表")) {
                for (std::size_t i = 0;
                     i < working_config_.fail_template.template_names.size();
                     ++i) {
                    ImGui::PushID(static_cast<int>(i));
                    ImGui::TextUnformatted("文件名");
                    ImGui::InputText(
                        "##name",
                        &working_config_.fail_template.template_names[i]
                    );
                    ImGui::SameLine();
                    if (ImGui::Button("删除")) {
                        working_config_.fail_template.template_names.erase(
                            working_config_.fail_template.template_names
                                .begin() +
                            i
                        );
                        ImGui::PopID();
                        break;
                    }
                    ImGui::PopID();
                }
                if (ImGui::Button("新增")) {
                    working_config_.fail_template.template_names.push_back("");
                }
                ImGui::TreePop();
            }

            float match = static_cast<float>(
                working_config_.fail_template.match_threshold
            );
            if (SliderFloatWithInput("匹配阈值", match, 0.0f, 1.0f, "%.3f")) {
                working_config_.fail_template.match_threshold =
                    static_cast<double>(match);
            }

            SliderIntWithInput(
                "横向分块数",
                working_config_.fail_template.search_region_x_parts,
                1,
                20
            );
            SliderIntWithInput(
                "横向起始块",
                working_config_.fail_template.search_region_x_start_part,
                0,
                20
            );
            SliderIntWithInput(
                "横向结束块",
                working_config_.fail_template.search_region_x_end_part,
                0,
                20
            );
            SliderIntWithInput(
                "纵向分块数",
                working_config_.fail_template.search_region_y_parts,
                1,
                20
            );
            SliderIntWithInput(
                "纵向起始块",
                working_config_.fail_template.search_region_y_start_part,
                0,
                20
            );
            SliderIntWithInput(
                "纵向结束块",
                working_config_.fail_template.search_region_y_end_part,
                0,
                20
            );
            SliderIntWithInput(
                "快速未命中回退阈值",
                working_config_.fail_template.fast_miss_fallback_threshold,
                0,
                200
            );
        }

        if (category_ == Category::Log) {
            ImGui::TextUnformatted("日志");
            ImGui::Separator();

            const char * levels[] = {"debug", "info", "warn", "error"};
            int current = 1;
            if (working_config_.logging.level == LogLevel::Debug)
                current = 0;
            if (working_config_.logging.level == LogLevel::Info)
                current = 1;
            if (working_config_.logging.level == LogLevel::Warn)
                current = 2;
            if (working_config_.logging.level == LogLevel::Error)
                current = 3;

            ImGui::TextUnformatted("日志等级");
            if (ImGui::Combo("##log_level", &current, levels, 4)) {
                if (current == 0)
                    working_config_.logging.level = LogLevel::Debug;
                if (current == 1)
                    working_config_.logging.level = LogLevel::Info;
                if (current == 2)
                    working_config_.logging.level = LogLevel::Warn;
                if (current == 3)
                    working_config_.logging.level = LogLevel::Error;
            }

            ImGui::TextUnformatted("日志文件路径");
            ImGui::InputText(
                "##log_file_path",
                &working_config_.logging.file_path
            );
        }

        ImGui::EndChild();

        ImGui::BeginChild(
            "##editor_footer",
            ImVec2(0, footer_height),
            false,
            ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse
        );
        float button_y = (footer_height - ImGui::GetFrameHeight()) * 0.5f;
        if (button_y > 0.0f) {
            ImGui::SetCursorPosY(button_y);
        }
        if (ImGui::Button("取消更改", ImVec2(120, 0))) {
            ReloadFromFile();
        }
        ImGui::SameLine();
        if (ImGui::Button("保存", ImVec2(120, 0))) {
            SaveToFile();
        }
        ImGui::SameLine();
        if (ImGui::Button("保存并应用", ImVec2(140, 0))) {
            SaveToFileAndApply();
        }
        ImGui::EndChild();

        if (open_success_popup_) {
            ImGui::OpenPopup("提示");
            open_success_popup_ = false;
        }
        if (ImGui::BeginPopupModal(
                "提示",
                nullptr,
                ImGuiWindowFlags_AlwaysAutoResize
            )) {
            ImGui::TextUnformatted(success_popup_message_.c_str());
            if (ImGui::Button("确定", ImVec2(120, 0))) {
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
    }

    void ConfigUi::DrawRightPreview() {
        ImGui::BeginChild(
            "##preview",
            ImVec2(0, 0),
            true,
            ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse
        );
        UiFrameSnapshot frame = ui_state_->GetFrameSnapshot();

        if (frame_texture_ == 0 || frame.width <= 0 || frame.height <= 0 ||
            frame_texture_w_ != frame.width ||
            frame_texture_h_ != frame.height) {
            ImGui::TextUnformatted("等待截屏...");
            ImGui::EndChild();
            return;
        }

        ImVec2 avail = ImGui::GetContentRegionAvail();
        float iw = static_cast<float>(frame_texture_w_);
        float ih = static_cast<float>(frame_texture_h_);
        float scale = std::max(0.01f, avail.y / ih);
        if (iw * scale > avail.x) {
            scale = std::max(0.01f, avail.x / iw);
        }
        scale = std::max(0.01f, scale);
        ImVec2 size(iw * scale, ih * scale);

        ImGui::Image(
            static_cast<ImTextureID>(
                static_cast<std::uintptr_t>(frame_texture_)
            ),
            size
        );

        ImVec2 img_min = ImGui::GetItemRectMin();
        ImVec2 img_max = ImGui::GetItemRectMax();
        ImDrawList * draw = ImGui::GetWindowDrawList();

        float sx = (img_max.x - img_min.x) / iw;
        float sy = (img_max.y - img_min.y) / ih;
        auto to_pos = [&](float x, float y) {
            return ImVec2(img_min.x + x * sx, img_min.y + y * sy);
        };

        float w = iw;
        float h = ih;

        int crop_left = std::max(0, working_config_.capture.crop_left);
        int crop_right = std::max(0, working_config_.capture.crop_right);
        int crop_top = std::max(0, working_config_.capture.crop_top);
        int crop_bottom = std::max(0, working_config_.capture.crop_bottom);

        float crop_x1 =
            static_cast<float>(std::min<int>(crop_left, frame_texture_w_));
        float crop_y1 =
            static_cast<float>(std::min<int>(crop_top, frame_texture_h_));
        float crop_x2 =
            static_cast<float>(std::max(0, frame_texture_w_ - crop_right));
        float crop_y2 =
            static_cast<float>(std::max(0, frame_texture_h_ - crop_bottom));

        draw->AddRect(
            to_pos(crop_x1, crop_y1),
            to_pos(crop_x2, crop_y2),
            ToColorU32(0x3d7cffcc),
            0.0f,
            0,
            2.0f
        );

        float roi_x1 = crop_x1;
        float roi_x2 = crop_x2;
        float roi_y1 =
            crop_y1 +
            static_cast<float>(working_config_.capture.roi_top_margin);
        float roi_y2 =
            crop_y2 -
            static_cast<float>(working_config_.capture.roi_bottom_margin);
        roi_y1 = std::clamp(roi_y1, 0.0f, h);
        roi_y2 = std::clamp(roi_y2, 0.0f, h);
        if (roi_y2 > roi_y1 && roi_x2 > roi_x1) {
            draw->AddRect(
                to_pos(roi_x1, roi_y1),
                to_pos(roi_x2, roi_y2),
                ToColorU32(0x2ecc71cc),
                0.0f,
                0,
                2.0f
            );
        }

        int cropped_w = std::max(0, frame_texture_w_ - crop_left - crop_right);
        int cropped_h = std::max(0, frame_texture_h_ - crop_top - crop_bottom);
        if (cropped_w > 0 && cropped_h > 0) {
            int x_parts = std::max(
                1,
                working_config_.fail_template.search_region_x_parts
            );
            int y_parts = std::max(
                1,
                working_config_.fail_template.search_region_y_parts
            );

            int x_start_part = std::clamp(
                working_config_.fail_template.search_region_x_start_part,
                0,
                x_parts
            );
            int x_end_part = std::clamp(
                working_config_.fail_template.search_region_x_end_part,
                0,
                x_parts
            );
            int y_start_part = std::clamp(
                working_config_.fail_template.search_region_y_start_part,
                0,
                y_parts
            );
            int y_end_part = std::clamp(
                working_config_.fail_template.search_region_y_end_part,
                0,
                y_parts
            );

            if (x_end_part < x_start_part)
                std::swap(x_end_part, x_start_part);
            if (y_end_part < y_start_part)
                std::swap(y_end_part, y_start_part);

            float fx1 = crop_x1 + (static_cast<float>(cropped_w) *
                                   x_start_part / x_parts);
            float fx2 = crop_x1 +
                        (static_cast<float>(cropped_w) * x_end_part / x_parts);
            float fy1 = crop_y1 + (static_cast<float>(cropped_h) *
                                   y_start_part / y_parts);
            float fy2 = crop_y1 +
                        (static_cast<float>(cropped_h) * y_end_part / y_parts);

            draw->AddRect(
                to_pos(fx1, fy1),
                to_pos(fx2, fy2),
                ToColorU32(0xff3b30cc),
                0.0f,
                0,
                2.0f
            );
        }

        ImGui::EndChild();
    }

    void ConfigUi::DrawBottomLogs(float height) {
        ImGui::BeginChild("##logs", ImVec2(0, height), true);

        ImGui::Checkbox("自动滚动", &auto_scroll_logs_);
        ImGui::SameLine();
        if (ImGui::Button("清空")) {
            log_entries_.clear();
            last_log_seq_ = Logger::Instance().GetLastSeq();
        }

        float scroll_before = ImGui::GetScrollY();
        float scroll_max_before = ImGui::GetScrollMaxY();
        bool was_at_bottom = (scroll_before >= scroll_max_before - 1.0f);

        std::vector<Logger::Entry> new_entries =
            Logger::Instance().GetEntriesSince(last_log_seq_, 2000);
        for (const auto & e : new_entries) {
            last_log_seq_ = std::max(last_log_seq_, e.seq);
            log_entries_.push_back(e);
        }
        if (log_entries_.size() > 8000) {
            log_entries_.erase(
                log_entries_.begin(),
                log_entries_.begin() + 2000
            );
        }

        ImGui::Separator();
        ImGui::BeginChild(
            "##log_lines",
            ImVec2(0, 0),
            false,
            ImGuiWindowFlags_HorizontalScrollbar
        );
        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(log_entries_.size()));
        while (clipper.Step()) {
            for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
                ImGui::TextUnformatted(
                    log_entries_[static_cast<std::size_t>(i)].line.c_str()
                );
            }
        }
        if (auto_scroll_logs_ && was_at_bottom) {
            ImGui::SetScrollHereY(1.0f);
        }
        ImGui::EndChild();

        ImGui::EndChild();
    }

    void ConfigUi::ReloadFromFile() {
        working_config_ = LoadConfigOrDefault(config_path_);
        status_text_ = "已从配置文件恢复";
    }

    void ConfigUi::SaveToFile() {
        bool ok = SaveConfig(config_path_, working_config_);
        status_text_ = ok ? "保存成功" : "保存失败";
        if (!ok) {
            Logger::Instance().Error("SaveConfig failed: " + config_path_);
            return;
        }
        working_config_ = LoadConfigOrDefault(config_path_);
        success_popup_message_ = "保存成功";
        open_success_popup_ = true;
    }

    void ConfigUi::SaveToFileAndApply() {
        bool ok = SaveConfig(config_path_, working_config_);
        if (!ok) {
            status_text_ = "保存失败，未应用";
            Logger::Instance().Error("SaveConfig failed: " + config_path_);
            return;
        }
        ui_state_->ApplyConfig(working_config_);
        working_config_ = ui_state_->GetConfigSnapshot();
        status_text_ = "保存并应用成功";
        success_popup_message_ = "保存并应用成功";
        open_success_popup_ = true;
    }

    void ConfigUi::UpdateFrameTexture() {
        UiFrameSnapshot frame = ui_state_->GetFrameSnapshot();
        if (frame.seq == last_frame_seq_) {
            return;
        }
        last_frame_seq_ = frame.seq;

        if (frame.width <= 0 || frame.height <= 0 || frame.bgra.empty()) {
            return;
        }

        if (frame_texture_ == 0) {
            glGenTextures(1, &frame_texture_);
            glBindTexture(GL_TEXTURE_2D, frame_texture_);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        }

        frame_texture_w_ = frame.width;
        frame_texture_h_ = frame.height;

        std::size_t pixel_count =
            static_cast<std::size_t>(frame.width) * frame.height;
        frame_rgba_.resize(pixel_count * 4);
        const std::uint8_t * src = frame.bgra.data();
        std::uint8_t * dst = frame_rgba_.data();
        for (std::size_t i = 0; i < pixel_count; ++i) {
            dst[i * 4 + 0] = src[i * 4 + 2];
            dst[i * 4 + 1] = src[i * 4 + 1];
            dst[i * 4 + 2] = src[i * 4 + 0];
            dst[i * 4 + 3] = src[i * 4 + 3];
        }

        glBindTexture(GL_TEXTURE_2D, frame_texture_);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexImage2D(
            GL_TEXTURE_2D,
            0,
            GL_RGBA,
            frame.width,
            frame.height,
            0,
            GL_RGBA,
            GL_UNSIGNED_BYTE,
            frame_rgba_.data()
        );
    }

    void ConfigUi::RebuildFonts() {
        font_dirty_ = false;

        ImGuiIO & io = ImGui::GetIO();
        ImGui_ImplOpenGL3_DestroyDeviceObjects();
        io.Fonts->Clear();

        ImFont * font = nullptr;
        if (!font_candidates_.empty() && selected_font_index_ >= 0 &&
            selected_font_index_ < static_cast<int>(font_candidates_.size())) {
            const FontCandidate & c = font_candidates_[static_cast<std::size_t>(
                selected_font_index_
            )];
            ImFontConfig cfg{};
            cfg.FontNo = c.font_no;
            font = io.Fonts->AddFontFromFileTTF(
                c.path.c_str(),
                font_size_px_,
                &cfg,
                io.Fonts->GetGlyphRangesChineseFull()
            );
        }
        if (font == nullptr) {
            ImFontConfig cfg{};
            cfg.SizePixels = font_size_px_;
            font = io.Fonts->AddFontDefault(&cfg);
        }
        io.FontDefault = font;
        io.Fonts->Build();
        ImGui_ImplOpenGL3_CreateDeviceObjects();
    }

} // namespace play_runner
