#include "services/trim/TrimJob.h"

#include "core/log/Log.h"
#include "platform/win/ChildProcess.h"
#include "platform/win/TextConvert.h"

#include <charconv>
#include <format>
#include <stdexcept>

namespace icd::services {

    namespace {

        // The line without surrounding blanks and tabs.
        std::string_view trimmed(std::string_view line) {
            const auto first = line.find_first_not_of(" \t");
            if (first == std::string_view::npos) {
                return {};
            }
            const auto last = line.find_last_not_of(" \t");
            return line.substr(first, last - first + 1);
        }

        // defrag's volume argument: "C:" for a drive root, the path itself for a folder mount.
        std::wstring volumeArgument(const std::string& root) {
            std::wstring arg = platform::fromUtf8(root);
            if (arg.size() == 3 && arg[1] == L':' && arg[2] == L'\\') {
                arg.pop_back();
            }
            return arg;
        }

        // Turns defrag's lines into progress reports and log lines.
        class TrimReporter {
        public:
            TrimReporter(JobContext& context, TrimOutput& output) : context_(context), output_(output) {}

            // Handles one complete line.
            void line(const std::string& raw) {
                const std::string text = platform::oemToUtf8(std::string(trimmed(raw)));
                if (text.empty()) {
                    return;
                }
                if (const auto percent = retrimPercent(text)) {
                    percent_ = static_cast<std::uint64_t>(*percent);
                }
                else {
                    noteLine(text);
                }
                context_.progress.report({.stage = stage_, .done = percent_, .total = 100, .message = text});
            }

            // The last line that was not a progress line, for error messages.
            [[nodiscard]] const std::string& last() const { return last_; }

        private:
            // Keeps a log line; "Performing pass 1:" starts a new stage, since the percentage restarts there.
            void noteLine(const std::string& text) {
                last_ = text;
                output_.push(text);
                if (text.starts_with("Performing ")) {
                    stage_ = text.ends_with(':') ? text.substr(0, text.size() - 1) : text;
                    percent_ = 0;
                }
            }

        private:
            JobContext& context_; // Progress target.
            TrimOutput& output_; // Log lines.
            std::string last_; // Last log line.
            std::string stage_ = "Retrim"; // Current stage name.
            std::uint64_t percent_ = 0; // Last percentage in this stage.
        };

    } // namespace

    std::vector<std::string> LineSplitter::feed(std::string_view chunk) {
        std::vector<std::string> lines;
        for (const char c : chunk) {
            if (c == '\r' || c == '\n') {
                lines.push_back(std::move(pending_));
                pending_.clear();
            }
            else {
                pending_.push_back(c);
            }
        }
        return lines;
    }

    std::optional<std::string> LineSplitter::finish() {
        if (pending_.empty()) {
            return std::nullopt;
        }
        return std::exchange(pending_, {});
    }

    std::optional<int> retrimPercent(std::string_view line) {
        line = trimmed(line);
        constexpr std::string_view prefix = "Retrim:";
        if (!line.starts_with(prefix)) {
            return std::nullopt;
        }
        line = trimmed(line.substr(prefix.size()));
        int percent = 0;
        const auto [end, error] = std::from_chars(line.data(), line.data() + line.size(), percent);
        if (error != std::errc{} || end == line.data() + line.size() || *end != '%' || percent < 0 || percent > 100) {
            return std::nullopt;
        }
        return percent;
    }

    void TrimOutput::push(std::string line) {
        const std::scoped_lock lock(mutex_);
        lines_.push_back(std::move(line));
    }

    std::vector<std::string> TrimOutput::take() {
        const std::scoped_lock lock(mutex_);
        return std::exchange(lines_, {});
    }

    JobFunction makeTrimJob(std::string root, std::shared_ptr<TrimOutput> output) {
        return [root = std::move(root), output = std::move(output)](JobContext& context) {
            const std::wstring command =
                std::format(L"\"{}\" {} /L /U /V", platform::systemProgram(L"defrag.exe"), volumeArgument(root));
            core::logging::info("trim start", {{"root", root}});
            TrimReporter reporter(context, *output);
            LineSplitter splitter;
            const auto onOutput = [&](std::string_view chunk) {
                for (const std::string& line : splitter.feed(chunk)) {
                    reporter.line(line);
                }
            };
            const unsigned long code =
                platform::runCaptured(command, onOutput, [&] { return context.token.cancelled(); });
            if (const auto rest = splitter.finish()) {
                reporter.line(*rest);
            }
            context.token.throwIfCancelled();
            if (code != 0) {
                throw std::runtime_error(std::format("defrag exited with code 0x{:x}: {}", code, reporter.last()));
            }
        };
    }

} // namespace icd::services
