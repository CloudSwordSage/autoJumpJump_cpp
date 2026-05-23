#include "play_runner/internal/jump_adaptive.h"

#include "play_runner/config_ui_state.h"
#include "play_runner/logging.h"

#include <algorithm>
#include <cmath>
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <filesystem>
#include <fstream>
#include <limits>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace play_runner {

    namespace {

        struct SamplePair {
                double x;
                double y;
        };

        struct PrefixSums {
                std::vector<double> sum_x;
                std::vector<double> sum_y;
                std::vector<double> sum_xx;
                std::vector<double> sum_xy;
                std::vector<double> sum_yy;
        };

        PrefixSums BuildPrefixSums(
            const std::vector<double> & x,
            const std::vector<double> & y
        ) {
            const std::size_t n = x.size();
            PrefixSums sums;
            sums.sum_x.assign(n + 1, 0.0);
            sums.sum_y.assign(n + 1, 0.0);
            sums.sum_xx.assign(n + 1, 0.0);
            sums.sum_xy.assign(n + 1, 0.0);
            sums.sum_yy.assign(n + 1, 0.0);

            for (std::size_t i = 0; i < n; ++i) {
                sums.sum_x[i + 1] = sums.sum_x[i] + x[i];
                sums.sum_y[i + 1] = sums.sum_y[i] + y[i];
                sums.sum_xx[i + 1] = sums.sum_xx[i] + x[i] * x[i];
                sums.sum_xy[i + 1] = sums.sum_xy[i] + x[i] * y[i];
                sums.sum_yy[i + 1] = sums.sum_yy[i] + y[i] * y[i];
            }
            return sums;
        }

        double GetIntervalSum(
            const std::vector<double> & prefix,
            int start,
            int end
        ) {
            return prefix[static_cast<std::size_t>(end + 1)] -
                   prefix[static_cast<std::size_t>(start)];
        }

        struct FitResult {
                double a;
                double b;
                double rss;
        };

        FitResult FitInterval(const PrefixSums & sums, int start, int end) {
            const int n = end - start + 1;
            const double Sx = GetIntervalSum(sums.sum_x, start, end);
            const double Sy = GetIntervalSum(sums.sum_y, start, end);
            const double Sxx = GetIntervalSum(sums.sum_xx, start, end);
            const double Sxy = GetIntervalSum(sums.sum_xy, start, end);
            const double Syy = GetIntervalSum(sums.sum_yy, start, end);

            const double n_d = static_cast<double>(n);
            const double denom = n_d * Sxx - Sx * Sx;

            FitResult result{};
            if (std::abs(denom) <= 1e-12) {
                result.a = 0.0;
                result.b = Sy / n_d;
                double rss =
                    Syy - 2.0 * result.b * Sy + result.b * result.b * n_d;
                if (rss < 0.0) {
                    rss = 0.0;
                }
                result.rss = rss;
                return result;
            }

            result.a = (n_d * Sxy - Sx * Sy) / denom;
            result.b = (Sy - result.a * Sx) / n_d;

            double rss = Syy - 2.0 * result.a * Sxy - 2.0 * result.b * Sy +
                         result.a * result.a * Sxx +
                         2.0 * result.a * result.b * Sx +
                         result.b * result.b * n_d;
            if (rss < 0.0) {
                rss = 0.0;
            }
            result.rss = rss;
            return result;
        }

        bool ComputeSegments(
            const std::vector<SamplePair> & samples,
            int max_segments,
            int min_len,
            std::vector<JumpSegment> & out_segments,
            int & out_best_k
        ) {
            const int n = static_cast<int>(samples.size());
            if (n <= 0 || min_len < 2 || max_segments <= 0) {
                return false;
            }
            if (n < min_len * 3) {
                return false;
            }

            std::vector<double> x;
            std::vector<double> y;
            x.reserve(samples.size());
            y.reserve(samples.size());
            for (const auto & s : samples) {
                x.push_back(s.x);
                y.push_back(s.y);
            }

            PrefixSums sums = BuildPrefixSums(x, y);
            const double INF = 1e300;

            std::vector<std::vector<double>> cost(
                static_cast<std::size_t>(n),
                std::vector<double>(static_cast<std::size_t>(n), INF)
            );

            for (int i = 0; i < n; ++i) {
                for (int j = i + min_len - 1; j < n; ++j) {
                    cost[static_cast<std::size_t>(i)]
                        [static_cast<std::size_t>(j)] =
                            FitInterval(sums, i, j).rss;
                }
            }

            std::vector<std::vector<double>> dp(
                static_cast<std::size_t>(max_segments + 1),
                std::vector<double>(static_cast<std::size_t>(n), INF)
            );
            std::vector<std::vector<int>> split(
                static_cast<std::size_t>(max_segments + 1),
                std::vector<int>(static_cast<std::size_t>(n), -1)
            );

            for (int j = min_len - 1; j < n; ++j) {
                dp[1][static_cast<std::size_t>(j)] =
                    cost[0][static_cast<std::size_t>(j)];
            }

            for (int k = 2; k <= max_segments; ++k) {
                const int j_begin = k * min_len - 1;
                if (j_begin >= n) {
                    break;
                }
                for (int j = j_begin; j < n; ++j) {
                    const int i_begin = (k - 1) * min_len - 1;
                    const int i_end = j - min_len;
                    for (int i = i_begin; i <= i_end; ++i) {
                        const double prev =
                            dp[k - 1][static_cast<std::size_t>(i)];
                        if (prev >= INF / 2) {
                            continue;
                        }
                        const double cur_cost =
                            cost[static_cast<std::size_t>(i + 1)]
                                [static_cast<std::size_t>(j)];
                        if (cur_cost >= INF / 2) {
                            continue;
                        }
                        const double total = prev + cur_cost;
                        double & best = dp[k][static_cast<std::size_t>(j)];
                        if (total < best) {
                            best = total;
                            split[k][static_cast<std::size_t>(j)] = i;
                        }
                    }
                }
            }

            int best_k = 1;
            double best_bic = std::numeric_limits<double>::infinity();
            for (int k = 1; k <= max_segments; ++k) {
                const double total_rss = dp[k][static_cast<std::size_t>(n - 1)];
                if (total_rss >= INF / 2) {
                    continue;
                }
                const double rss = total_rss <= 1e-12 ? 1e-12 : total_rss;
                const int n_params = 2 * k + (k - 1);
                const double bic = static_cast<double>(n) *
                                       std::log(rss / static_cast<double>(n)) +
                                   static_cast<double>(n_params) *
                                       std::log(static_cast<double>(n));
                if (bic < best_bic) {
                    best_bic = bic;
                    best_k = k;
                }
            }

            std::vector<int> ends(static_cast<std::size_t>(best_k), -1);
            int j = n - 1;
            for (int k = best_k; k >= 2; --k) {
                ends[static_cast<std::size_t>(k - 1)] = j;
                int i = split[k][static_cast<std::size_t>(j)];
                if (i < 0) {
                    return false;
                }
                j = i;
            }
            ends[0] = j;

            std::vector<JumpSegment> segments;
            segments.reserve(static_cast<std::size_t>(best_k));
            int prev_end = -1;
            for (int seg_index = 0; seg_index < best_k; ++seg_index) {
                const int start = prev_end + 1;
                const int end = ends[static_cast<std::size_t>(seg_index)];
                if (end < start) {
                    return false;
                }
                FitResult fit = FitInterval(sums, start, end);
                JumpSegment seg{};
                seg.a = fit.a;
                seg.b = fit.b;
                seg.x_start = x[static_cast<std::size_t>(start)];
                seg.x_end = x[static_cast<std::size_t>(end)];
                segments.push_back(seg);
                prev_end = end;
            }

            if (segments.empty()) {
                return false;
            }

            out_segments = std::move(segments);
            out_best_k = best_k;
            return true;
        }

    } // namespace

    class JumpAdaptiveFitter::Impl {
        public:
            Impl(
                std::shared_ptr<ConfigUiState> ui_state,
                std::string config_path
            )
                : ui_state_(std::move(ui_state)),
                  config_path_(std::move(config_path)) {
                worker_ = std::thread([this]() { WorkerLoop(); });
            }

            ~Impl() {
                {
                    std::lock_guard<std::mutex> lock(mutex_);
                    stop_ = true;
                    dirty_ = true;
                }
                cv_.notify_all();
                if (worker_.joinable()) {
                    worker_.join();
                }
            }

            void PushSample(double x, double y) {
                AppConfig config = ui_state_->GetConfigSnapshot();
                const int history_size = config.jump.params.history_size;
                {
                    std::lock_guard<std::mutex> lock(mutex_);
                    samples_.push_back(SamplePair{x, y});
                    while (history_size > 0 &&
                           static_cast<int>(samples_.size()) > history_size) {
                        samples_.pop_front();
                    }
                    dirty_ = true;
                }
                cv_.notify_one();
            }

            void DiscardRecent(int count) {
                if (count <= 0) {
                    return;
                }
                {
                    std::lock_guard<std::mutex> lock(mutex_);
                    while (!samples_.empty() && count > 0) {
                        samples_.pop_back();
                        count -= 1;
                    }
                    dirty_ = true;
                }
                cv_.notify_one();
            }

            bool ExportSamplesCsv() {
                std::vector<SamplePair> snapshot;
                {
                    std::lock_guard<std::mutex> lock(mutex_);
                    snapshot.assign(samples_.begin(), samples_.end());
                }
                std::filesystem::path config_path(config_path_);
                std::filesystem::path base =
                    config_path.has_parent_path()
                        ? config_path.parent_path().parent_path()
                        : std::filesystem::current_path();
                std::filesystem::path out_path =
                    base / "logs" / "jump_samples.csv";
                try {
                    if (out_path.has_parent_path()) {
                        std::filesystem::create_directories(
                            out_path.parent_path()
                        );
                    }
                } catch (...) {
                }

                std::ofstream out(out_path);
                if (!out.is_open()) {
                    return false;
                }
                out << "x,y\n";
                for (const auto & s : snapshot) {
                    out << s.x << "," << s.y << "\n";
                }
                return true;
            }

        private:
            void WorkerLoop() {
                while (true) {
                    std::vector<SamplePair> snapshot;
                    {
                        std::unique_lock<std::mutex> lock(mutex_);
                        cv_.wait(lock, [&]() { return stop_ || dirty_; });
                        if (stop_) {
                            return;
                        }
                        dirty_ = false;
                        snapshot.assign(samples_.begin(), samples_.end());
                    }

                    AppConfig config = ui_state_->GetConfigSnapshot();
                    if (!config.jump.enable_adaptive_adjustment) {
                        continue;
                    }
                    const int max_segments = config.jump.params.max_segments;
                    const int min_len = config.jump.params.min_len;

                    std::sort(
                        snapshot.begin(),
                        snapshot.end(),
                        [](const SamplePair & left, const SamplePair & right) {
                            return left.x < right.x;
                        }
                    );

                    std::vector<JumpSegment> new_segments;
                    int best_k = 1;
                    if (!ComputeSegments(
                            snapshot,
                            max_segments,
                            min_len,
                            new_segments,
                            best_k
                        )) {
                        continue;
                    }

                    Logger::Instance().Info(
                        std::string("Adaptive fit: segments=") +
                        std::to_string(static_cast<int>(new_segments.size())) +
                        ", best_split=" + std::to_string(best_k) +
                        ", samples=" +
                        std::to_string(static_cast<int>(snapshot.size()))
                    );

                    AppConfig updated = ui_state_->GetConfigSnapshot();
                    updated.jump.params.segments = std::move(new_segments);
                    updated.jump.params.best_split = best_k;
                    ui_state_->ApplyConfig(updated);
                    if (!SaveConfig(config_path_, updated)) {
                        Logger::Instance().Info(
                            "Failed to save updated jump_params"
                        );
                    }
                }
            }

            std::shared_ptr<ConfigUiState> ui_state_;
            std::string config_path_;

            std::mutex mutex_;
            std::condition_variable cv_;
            std::deque<SamplePair> samples_;
            bool stop_ = false;
            bool dirty_ = false;
            std::thread worker_;
    };

    JumpAdaptiveFitter::JumpAdaptiveFitter(
        std::shared_ptr<ConfigUiState> ui_state,
        std::string config_path
    )
        : impl_(
              std::make_unique<Impl>(
                  std::move(ui_state),
                  std::move(config_path)
              )
          ) {
    }

    JumpAdaptiveFitter::~JumpAdaptiveFitter() = default;

    void JumpAdaptiveFitter::PushSample(double x, double y) {
        impl_->PushSample(x, y);
    }

    void JumpAdaptiveFitter::DiscardRecent(int count) {
        impl_->DiscardRecent(count);
    }

    bool JumpAdaptiveFitter::ExportSamplesCsv() {
        return impl_->ExportSamplesCsv();
    }

} // namespace play_runner
