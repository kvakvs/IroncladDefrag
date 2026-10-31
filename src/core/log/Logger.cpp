#include "core/log/Log.h"

#include <utility>

namespace icd::core::logging {

    void Logger::addSink(std::shared_ptr<Sink> sink) {
        if (!sink) {
            return;
        }
        const std::scoped_lock lock(mutex_);
        sinks_.push_back(std::move(sink));
    }

    void Logger::clearSinks() {
        const std::scoped_lock lock(mutex_);
        sinks_.clear();
    }

    void Logger::setMinLevel(Level level) { minLevel_.store(level, std::memory_order_relaxed); }

    bool Logger::enabled(Level level) const { return level >= minLevel_.load(std::memory_order_relaxed); }

    void Logger::log(Level level, std::string_view message, std::initializer_list<Field> fields) {
        if (!enabled(level)) {
            return;
        }
        Record record{std::chrono::system_clock::now(), level, std::this_thread::get_id(), std::string(message),
                      std::vector<Field>(fields)};
        write(record);
    }

    void Logger::write(const Record& record) {
        if (!enabled(record.level)) {
            return;
        }
        const std::string line = formatRecord(record); // Formatted outside the lock.
        const std::scoped_lock lock(mutex_);
        for (const auto& sink : sinks_) {
            sink->write(record, line);
        }
    }

    void Logger::flush() {
        const std::scoped_lock lock(mutex_);
        for (const auto& sink : sinks_) {
            sink->flush();
        }
    }

    Logger& globalLogger() {
        static Logger logger;
        return logger;
    }

    void trace(std::string_view message, std::initializer_list<Field> fields) {
        globalLogger().log(Level::Trace, message, fields);
    }

    void debug(std::string_view message, std::initializer_list<Field> fields) {
        globalLogger().log(Level::Debug, message, fields);
    }

    void info(std::string_view message, std::initializer_list<Field> fields) {
        globalLogger().log(Level::Info, message, fields);
    }

    void warning(std::string_view message, std::initializer_list<Field> fields) {
        globalLogger().log(Level::Warning, message, fields);
    }

    void error(std::string_view message, std::initializer_list<Field> fields) {
        globalLogger().log(Level::Error, message, fields);
    }

} // namespace icd::core::logging
