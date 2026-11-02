#include "services/execute/ExecutionLog.h"

#include "core/format/Json.h"
#include "core/format/JsonReader.h"
#include "platform/win/LogSinks.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <chrono>
#include <format>
#include <sstream>
#include <utility>

namespace icd::services {

    namespace {

        using core::ClusterRun;
        using core::JsonValue;
        using core::JsonWriter;
        using core::execute::ExecutionPlan;
        using core::execute::PlannedMove;

        // Stable names of the effort levels in logs.
        constexpr std::array<std::string_view, 3> effortKeys{"fast", "moderate", "maximum"};

        // A 64-bit value as "0x..." (JSON numbers lose precision above 2^53).
        std::string hex(std::uint64_t value) { return std::format("{:#x}", value); }

        // A 64-bit signed value as decimal text.
        std::string decimal(std::int64_t value) { return std::to_string(value); }

        // The current UTC time as ISO 8601, for event lines.
        std::string utcNow() {
            return std::format("{:%Y-%m-%dT%H:%M:%SZ}",
                               std::chrono::floor<std::chrono::seconds>(std::chrono::system_clock::now()));
        }

        // Writes runs as [[start, length], ...].
        void writeRuns(JsonWriter& json, std::span<const ClusterRun> runs) {
            json.beginArray();
            for (const ClusterRun& r : runs) {
                json.beginArray().value(r.start).value(r.length).endArray();
            }
            json.endArray();
        }

        // Writes stream runs as [[stream, vcn, lcn, length], ...].
        void writeStreamRuns(JsonWriter& json, std::span<const core::execute::StreamRun> runs) {
            json.beginArray();
            for (const auto& r : runs) {
                json.beginArray().value(std::uint64_t{r.stream}).value(r.vcn).value(r.lcn).value(r.length).endArray();
            }
            json.endArray();
        }

        // The plan line: identity and counts.
        std::string planLine(const ExecutionPlan& plan) {
            const auto& id = plan.identity;
            JsonWriter json(true);
            json.beginObject().key("type").value("plan").key("version").value(executionLogVersion);
            json.key("volume").value(id.volumeName).key("serial").value(hex(id.volumeSerial));
            json.key("bytesPerCluster").value(std::uint64_t{id.geometry.bytesPerCluster});
            json.key("totalClusters").value(id.geometry.totalClusters);
            json.key("usnJournal").value(id.usn ? hex(id.usn->journalId) : std::string());
            json.key("usnNext").value(id.usn ? decimal(id.usn->nextUsn) : std::string());
            json.key("madeAt").value(decimal(id.madeAt)).key("strategy").value(id.strategy);
            json.key("level").value(effortKeys.at(static_cast<std::size_t>(id.effort)));
            json.key("moves").value(std::uint64_t{plan.moves.size()}).key("reserved");
            writeRuns(json, plan.reserved);
            json.key("written").value(utcNow()).endObject();
            return json.str();
        }

        // One move line.
        std::string moveLine(const ExecutionPlan& plan, std::uint32_t index) {
            const PlannedMove& m = plan.moves[index];
            JsonWriter json(true);
            json.beginObject().key("type").value("move").key("index").value(std::uint64_t{index});
            json.key("file").value(hex(m.fileReference)).key("reason").value(core::planner::reasonKey(m.reason));
            json.key("fromParking").value(m.fromParking).key("extra").value(m.extra);
            json.key("flags").value(std::uint64_t{m.fileFlags}).key("region");
            writeRuns(json, std::span(&m.region, 1));
            json.key("sources");
            writeRuns(json, plan.sources(m));
            json.key("destinations");
            writeRuns(json, plan.destinations(m));
            json.key("path").value(plan.paths[index]).endObject();
            return json.str();
        }

        // A 64-bit value from "0x..." text.
        std::uint64_t parseHex(const JsonValue& v) {
            const std::string& text = v.asString();
            std::uint64_t out = 0;
            const char* first = text.data() + (text.starts_with("0x") ? 2 : 0);
            const auto [end, error] = std::from_chars(first, text.data() + text.size(), out, 16);
            if (error != std::errc() || end != text.data() + text.size() || first == end) {
                throw ExecutionLogError("bad hexadecimal value \"" + text + "\"");
            }
            return out;
        }

        // A signed 64-bit value from decimal text.
        std::int64_t parseDecimal(const JsonValue& v) {
            const std::string& text = v.asString();
            std::int64_t out = 0;
            const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), out);
            if (error != std::errc() || end != text.data() + text.size()) {
                throw ExecutionLogError("bad number \"" + text + "\"");
            }
            return out;
        }

        // Runs from [[start, length], ...].
        std::vector<ClusterRun> parseRuns(const JsonValue& v) {
            std::vector<ClusterRun> out;
            for (const JsonValue& r : v.asArray()) {
                const auto& pair = r.asArray();
                if (pair.size() != 2) {
                    throw ExecutionLogError("a run needs a start and a length");
                }
                out.push_back({pair[0].asUnsigned(), pair[1].asUnsigned()});
            }
            return out;
        }

        // The move reason named in a log.
        core::planner::MoveReason reasonNamed(const std::string& key) {
            for (std::uint8_t r = 0; r < 6; ++r) {
                const auto reason = static_cast<core::planner::MoveReason>(r);
                if (core::planner::reasonKey(reason) == key) {
                    return reason;
                }
            }
            throw ExecutionLogError("unknown move reason \"" + key + "\"");
        }

        // The effort level named in a log.
        core::strategy::Effort effortNamed(const std::string& key) {
            const auto found = std::ranges::find(effortKeys, key);
            if (found == effortKeys.end()) {
                throw ExecutionLogError("unknown level \"" + key + "\"");
            }
            return static_cast<core::strategy::Effort>(found - effortKeys.begin());
        }

        // Reads the lines of a log one by one into its contents.
        class LogParser {
        public:
            // Takes one parsed line.
            void take(const JsonValue& line) {
                const std::string& type = line.at("type").asString();
                if (!havePlan_ && type != "plan") {
                    throw ExecutionLogError("the log does not start with a plan");
                }
                using Handler = void (LogParser::*)(const JsonValue&);
                constexpr std::array<std::pair<std::string_view, Handler>, 7> handlers{{
                    {"plan", &LogParser::plan},
                    {"move", &LogParser::move},
                    {"dropped", &LogParser::settle},
                    {"outcome", &LogParser::settle},
                    {"begin", &LogParser::begin},
                    {"event", &LogParser::event},
                    {"end", &LogParser::end},
                }};
                const auto found = std::ranges::find(handlers, type, &std::pair<std::string_view, Handler>::first);
                if (found == handlers.end()) {
                    throw ExecutionLogError("unknown line type \"" + type + "\"");
                }
                (this->*(found->second))(line);
            }

            // The contents.
            [[nodiscard]] ExecutionLogContents finish() && {
                if (!havePlan_) {
                    throw ExecutionLogError("the log holds no plan");
                }
                if (out_.plan.moves.size() != expectedMoves_) {
                    throw ExecutionLogError("the log lists fewer moves than its plan");
                }
                return std::move(out_);
            }

            // The contents so far (to mark truncation).
            ExecutionLogContents& contents() { return out_; }

        private:
            // The plan line.
            void plan(const JsonValue& line) {
                if (havePlan_ || line.at("version").asUnsigned() != executionLogVersion) {
                    throw ExecutionLogError("a second plan, or a log of another format version");
                }
                havePlan_ = true;
                auto& id = out_.plan.identity;
                id.volumeName = line.at("volume").asString();
                id.volumeSerial = parseHex(line.at("serial"));
                id.geometry = {static_cast<std::uint32_t>(line.at("bytesPerCluster").asUnsigned()),
                               line.at("totalClusters").asUnsigned()};
                if (!line.at("usnJournal").asString().empty()) {
                    id.usn = core::UsnCursor{parseHex(line.at("usnJournal")), parseDecimal(line.at("usnNext"))};
                }
                id.madeAt = parseDecimal(line.at("madeAt"));
                id.strategy = line.at("strategy").asString();
                id.effort = effortNamed(line.at("level").asString());
                out_.plan.reserved = parseRuns(line.at("reserved"));
                expectedMoves_ = line.at("moves").asUnsigned();
            }

            // A move line; they come in plan order.
            void move(const JsonValue& line) {
                if (line.at("index").asUnsigned() != out_.plan.moves.size()) {
                    throw ExecutionLogError("move lines out of order");
                }
                PlannedMove m;
                m.fileReference = parseHex(line.at("file"));
                m.reason = reasonNamed(line.at("reason").asString());
                m.fromParking = line.at("fromParking").asBool();
                m.extra = line.at("extra").asBool();
                m.fileFlags = static_cast<std::uint16_t>(line.at("flags").asUnsigned());
                const auto region = parseRuns(line.at("region"));
                m.region = region.empty() ? ClusterRun{} : region.front();
                const auto from = parseRuns(line.at("sources"));
                const auto to = parseRuns(line.at("destinations"));
                out_.plan.add(m, from, to, line.at("path").asString());
                out_.settled.push_back(false);
            }

            // The index of an outcome-like line, checked.
            [[nodiscard]] std::uint32_t indexOf(const JsonValue& line) const {
                const std::uint64_t index = line.at("index").asUnsigned();
                if (index >= out_.plan.moves.size()) {
                    throw ExecutionLogError("a line names move " + std::to_string(index) + " the plan does not have");
                }
                return static_cast<std::uint32_t>(index);
            }

            // An outcome or dropped line.
            void settle(const JsonValue& line) {
                const std::uint32_t index = indexOf(line);
                LoggedOutcome o;
                o.move = index;
                const bool dropped = line.at("type").asString() == "dropped";
                o.outcome = dropped ? "dropped" : line.at("outcome").asString();
                o.reason = line.at("reason").asString();
                o.clusters = dropped ? 0 : line.at("clusters").asUnsigned();
                out_.settled[index] = o.outcome != outcomeKey(MoveOutcome::Partial);
                if (out_.inFlight == index) {
                    out_.inFlight.reset();
                }
                out_.outcomes.push_back(std::move(o));
            }

            // A begin line.
            void begin(const JsonValue& line) { out_.inFlight = indexOf(line); }

            // An event line; a new run reopens the log.
            void event(const JsonValue& line) {
                if (line.at("event").asString() == "run-started") {
                    out_.ended = false;
                    out_.inFlight.reset();
                }
            }

            // An end line.
            void end(const JsonValue& line) {
                out_.ended = true;
                out_.cancelled = line.at("cancelled").asBool();
            }

            ExecutionLogContents out_; // Being filled.
            bool havePlan_ = false; // The plan line was read.
            std::uint64_t expectedMoves_ = 0; // Moves the plan line announced.
        };

        // The text lines of a log, empty ones left out.
        std::vector<std::string_view> linesOf(std::string_view text) {
            std::vector<std::string_view> out;
            while (!text.empty()) {
                const std::size_t end = text.find('\n');
                std::string_view line = text.substr(0, end);
                while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) {
                    line.remove_suffix(1);
                }
                if (!line.empty()) {
                    out.push_back(line);
                }
                text = end == std::string_view::npos ? std::string_view() : text.substr(end + 1);
            }
            return out;
        }

        // Where a line is in its log.
        struct LinePosition {
            std::size_t number = 0; // 1-based line number, for messages.
            bool last = false; // The last line, which a crash may have cut short.
        };

        // One line parsed; nullopt for a last line that is not complete JSON (cut short by a crash). Any other bad line
        // throws ExecutionLogError.
        std::optional<JsonValue> parseLine(std::string_view text, LinePosition at) {
            try {
                return core::parseJson(text);
            }
            catch (const core::JsonError& e) {
                if (at.last) {
                    return std::nullopt;
                }
                throw ExecutionLogError(std::format("line {}: {}", at.number, e.what()));
            }
        }

        // A volume name as it appears in log file names: "Volume{GUID}" or a sanitised name.
        std::string fileTag(const std::string& volumeName) {
            std::string tag;
            for (const char c : volumeName) {
                const bool keep = std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '{' || c == '}' || c == '-';
                tag.push_back(keep ? c : '_');
            }
            const std::size_t start = tag.find("Volume{");
            return start == std::string::npos ? tag : tag.substr(start, tag.find('}', start) + 1 - start);
        }

    } // namespace

    std::ofstream ExecutionLogWriter::openFile(const std::filesystem::path& file, bool truncate) {
        std::ofstream out(file, std::ios::binary | std::ios::out | (truncate ? std::ios::trunc : std::ios::app));
        if (!out) {
            throw std::runtime_error("cannot write the execution log " + file.string());
        }
        return out;
    }

    ExecutionLogWriter::ExecutionLogWriter(const std::filesystem::path& file,
                                           const core::execute::ExecutionPlan& plan) : path_(file) {
        if (!file.parent_path().empty()) {
            std::filesystem::create_directories(file.parent_path());
        }
        out_ = openFile(file, true);
        line(planLine(plan));
        for (std::uint32_t i = 0; i < plan.moves.size(); ++i) {
            line(moveLine(plan, i));
        }
    }

    ExecutionLogWriter::ExecutionLogWriter(const std::filesystem::path& file) :
        path_(file), out_(openFile(file, false)) {}

    void ExecutionLogWriter::line(const std::string& json) {
        out_ << json << '\n';
        out_.flush();
        if (!out_) {
            throw std::runtime_error("cannot write the execution log " + path_.string());
        }
    }

    void ExecutionLogWriter::dropped(const core::execute::DroppedMove& move) {
        JsonWriter json(true);
        json.beginObject().key("type").value("dropped").key("index").value(std::uint64_t{move.move});
        json.key("reason").value(core::execute::dropKey(move.reason)).endObject();
        line(json.str());
    }

    void ExecutionLogWriter::begin(std::uint32_t move) {
        JsonWriter json(true);
        json.beginObject().key("type").value("begin").key("index").value(std::uint64_t{move}).endObject();
        line(json.str());
    }

    void ExecutionLogWriter::outcome(const MoveRecord& r) {
        JsonWriter json(true);
        json.beginObject().key("type").value("outcome").key("index").value(std::uint64_t{r.move});
        json.key("outcome").value(outcomeKey(r.outcome)).key("reason").value(r.reason);
        json.key("replaced").value(r.replaced).key("verified").value(r.verified);
        json.key("clusters").value(r.clustersMoved).key("error").value(std::uint64_t{r.win32Error}).key("runs");
        writeStreamRuns(json, r.runsAfter);
        json.endObject();
        line(json.str());
    }

    void ExecutionLogWriter::event(std::string_view what) {
        JsonWriter json(true);
        json.beginObject().key("type").value("event").key("event").value(what).key("time").value(utcNow()).endObject();
        line(json.str());
    }

    void ExecutionLogWriter::end(const ExecutionReport& report) {
        const ExecutionTotals& t = report.totals;
        JsonWriter json(true);
        json.beginObject().key("type").value("end").key("cancelled").value(report.cancelled);
        json.key("moved").value(t.moved).key("alreadyDone").value(t.alreadyDone).key("partial").value(t.partial);
        json.key("skipped").value(t.skipped).key("replaced").value(t.replaced).key("unverified").value(t.unverified);
        json.key("clusters").value(t.clustersMoved).key("seconds").value(report.seconds).endObject();
        line(json.str());
    }

    ExecutionHooks ExecutionLogWriter::hooks() {
        ExecutionHooks h;
        h.onBegin = [this](std::uint32_t move) { begin(move); };
        h.onRecord = [this](const MoveRecord& r) { outcome(r); };
        h.onEvent = [this](std::string_view what) { event(what); };
        return h;
    }

    ExecutionLogContents parseExecutionLog(std::string_view text) {
        LogParser parser;
        const std::vector<std::string_view> lines = linesOf(text);
        for (std::size_t i = 0; i < lines.size(); ++i) {
            const auto value = parseLine(lines[i], {i + 1, i + 1 == lines.size()});
            if (!value) {
                parser.contents().truncated = true; // Cut short by a crash while it was written.
                break;
            }
            try {
                parser.take(*value);
            }
            catch (const core::JsonError& e) {
                throw ExecutionLogError(std::format("line {}: {}", i + 1, e.what()));
            }
        }
        return std::move(parser).finish();
    }

    ExecutionLogContents readExecutionLog(const std::filesystem::path& file) {
        std::ifstream in(file, std::ios::binary);
        if (!in) {
            throw ExecutionLogError("cannot read " + file.string());
        }
        std::ostringstream text;
        text << in.rdbuf();
        return parseExecutionLog(text.str());
    }

    std::vector<std::uint32_t> movesToResume(const ExecutionLogContents& log) {
        std::vector<std::uint32_t> out;
        for (std::uint32_t i = 0; i < log.settled.size(); ++i) {
            if (!log.settled[i]) {
                out.push_back(i);
            }
        }
        return out;
    }

    std::filesystem::path executionLogDirectory() { return platform::defaultLogDirectory() / "execute"; }

    std::filesystem::path newExecutionLogPath(const std::filesystem::path& directory, const std::string& volumeName) {
        const auto now = std::chrono::floor<std::chrono::seconds>(std::chrono::system_clock::now());
        return directory / std::format("{}-{:%Y%m%d-%H%M%S}.jsonl", fileTag(volumeName), now);
    }

    bool resumable(const ExecutionLogContents& log) {
        return (!log.ended || log.cancelled) && !movesToResume(log).empty();
    }
    std::optional<std::filesystem::path> unfinishedExecutionLog(const std::filesystem::path& directory,
                                                                const std::string& volumeName) {
        std::vector<std::filesystem::path> logs;
        std::error_code error;
        const std::string prefix = fileTag(volumeName) + "-";
        for (const auto& entry : std::filesystem::directory_iterator(directory, error)) {
            const std::string name = entry.path().filename().string();
            if (name.starts_with(prefix) && name.ends_with(".jsonl")) {
                logs.push_back(entry.path());
            }
        }
        std::ranges::sort(logs, std::greater<>());
        for (const auto& log : logs) {
            try {
                if (resumable(readExecutionLog(log))) {
                    return log;
                }
            }
            catch (const ExecutionLogError&) {
                continue; // Not a log this version can resume.
            }
        }
        return std::nullopt;
    }

} // namespace icd::services
