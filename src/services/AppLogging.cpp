#include "services/AppLogging.h"

#include "core/log/Log.h"
#include "platform/win/LogSinks.h"
#include "services/AppInfo.h"

#include <exception>
#include <memory>
#include <string>

namespace icd::services {

    namespace {

        // Path as UTF-8 for log fields; path::string() would use the ANSI code page and can throw.
        std::string pathUtf8(const std::filesystem::path& path) {
            const std::u8string text = path.u8string();
            return {text.begin(), text.end()};
        }

    } // namespace

    std::filesystem::path initAppLogging(std::string_view executableName) {
        auto& logger = core::logging::globalLogger();
        logger.addSink(std::make_shared<platform::DebugOutputSink>());
        std::filesystem::path logFile;
        std::string fileError;
        try {
            auto sink = std::make_shared<platform::FileSink>(
                platform::FileSink::Options{platform::defaultLogDirectory(), std::string(executableName)});
            logFile = sink->path();
            logger.addSink(std::move(sink));
        }
        catch (const std::exception& e) {
            fileError = e.what(); // Keep running: a missing log file must never stop the app.
        }
        core::logging::info("startup",
                            {{"app", executableName},
                             {"version", appVersion()},
                             {"log", logFile.empty() ? std::string("-") : pathUtf8(logFile)}});
        if (!fileError.empty()) {
            core::logging::warning("log file unavailable", {{"reason", fileError}});
        }
        return logFile;
    }

    void shutdownAppLogging() {
        core::logging::info("shutdown");
        core::logging::globalLogger().flush();
    }

} // namespace icd::services
