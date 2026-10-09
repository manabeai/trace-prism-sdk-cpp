#pragma once

#include <nlohmann/json.hpp>

#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <mutex>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#ifdef _WIN32
#include <process.h>
#else
#include <unistd.h>
#endif

namespace traceprism {

using Json = nlohmann::json;

namespace detail {

inline Json encode(const Json& raw) {
    if (raw.is_null()) return {{"t", "null"}};
    if (raw.is_boolean()) return {{"t", "bool"}, {"v", raw}};
    if (raw.is_number_integer() || raw.is_number_unsigned())
        return {{"t", "int"}, {"v", raw.dump()}};
    if (raw.is_number_float()) {
        if (!std::isfinite(raw.get<double>()))
            throw std::invalid_argument("TracePrism: non-finite number");
        return {{"t", "float"}, {"v", raw.dump()}};
    }
    if (raw.is_string()) return {{"t", "string"}, {"v", raw}};
    if (raw.is_array()) {
        Json items = Json::array();
        for (const auto& item : raw) items.push_back(encode(item));
        return {{"t", "array"}, {"items", std::move(items)}};
    }
    Json fields = Json::array();
    for (auto it = raw.begin(); it != raw.end(); ++it)
        fields.push_back({{"name", it.key()}, {"value", encode(it.value())}});
    return {{"t", "record"}, {"fields", std::move(fields)}};
}

inline Json scalar(const Json& raw) {
    if (raw.is_array() || raw.is_object())
        throw std::invalid_argument("TracePrism: span and map keys must be scalars");
    Json value = encode(raw);
    if (raw.is_null()) value["v"] = nullptr;
    return value;
}

inline unsigned long process_id() {
#ifdef _WIN32
    return static_cast<unsigned long>(_getpid());
#else
    return static_cast<unsigned long>(getpid());
#endif
}

inline std::filesystem::path run_dir() {
    if (const char* explicit_dir = std::getenv("TRACEPRISM_RUN_DIR")) {
        std::filesystem::path path(explicit_dir);
        if (!path.is_absolute())
            throw std::invalid_argument("TracePrism: TRACEPRISM_RUN_DIR must be absolute");
        return path;
    }
#ifdef _WIN32
    const char* base = std::getenv("APPDATA");
    if (!base) base = std::getenv("LOCALAPPDATA");
    if (base) return std::filesystem::path(base) / "traceprism" / "runs";
#elif defined(__APPLE__)
    if (const char* home = std::getenv("HOME"))
        return std::filesystem::path(home) / "Library/Application Support/traceprism/runs";
#else
    if (const char* base = std::getenv("XDG_DATA_HOME")) {
        std::filesystem::path path(base);
        if (path.is_absolute()) return path / "traceprism" / "runs";
    }
    if (const char* home = std::getenv("HOME"))
        return std::filesystem::path(home) / ".local/share/traceprism/runs";
#endif
    throw std::runtime_error("TracePrism: user data directory is unavailable");
}

inline std::string run_id() {
    if (const char* configured = std::getenv("VIZ_RUN_ID")) {
        std::string id(configured);
        if (!id.empty() && id.size() <= 128 && id.find_first_not_of(
            "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-") == std::string::npos)
            return id;
    }
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    return "run-" + std::to_string(ms) + "-" + std::to_string(process_id());
}

} // namespace detail

struct Span { Json segments = Json::array(); };

template <typename... T>
Span span(const T&... segments) {
    Span result;
    (result.segments.push_back(detail::scalar(Json(segments))), ...);
    return result;
}

struct FrameRef { std::uint64_t seq; };
struct Origin { std::optional<FrameRef> frame; Json id = Json::array(); };

inline Origin from(FrameRef frame) { return {.frame = frame}; }
inline Origin from(Span id) { return {.frame = std::nullopt, .id = std::move(id.segments)}; }

struct Field { std::string name; Json value; };

template <typename T>
Field field(std::string name, const T& value) {
    return {std::move(name), detail::encode(Json(value))};
}

template <typename T, typename Compare, typename Allocator>
Field field(std::string name, const std::set<T, Compare, Allocator>& values) {
    Json items = Json::array();
    for (const auto& value : values) items.push_back(detail::encode(Json(value)));
    return {std::move(name), {{"t", "set"}, {"items", std::move(items)}}};
}

template <typename K, typename V, typename Compare, typename Allocator>
Field field(std::string name, const std::map<K, V, Compare, Allocator>& values) {
    Json entries = Json::array();
    for (const auto& [key, value] : values)
        entries.push_back({{"key", detail::scalar(Json(key))}, {"value", detail::encode(Json(value))}});
    return {std::move(name), {{"t", "map"}, {"entries", std::move(entries)}}};
}

namespace detail {

inline void collect(std::vector<Field>& fields, std::optional<Origin>&, Field field) {
    fields.push_back(std::move(field));
}
inline void collect(std::vector<Field>&, std::optional<Origin>& origin, Origin value) {
    if (origin) throw std::invalid_argument("TracePrism: only one from() is allowed");
    origin = std::move(value);
}

class Recorder {
public:
    Recorder() : run_id_(run_id()) {
        try {
            std::filesystem::path path;
            if (const char* explicit_path = std::getenv("VIZ_TRACE_PATH")) path = explicit_path;
            else path = run_dir() / (run_id_ + ".jsonl");
            if (!path.parent_path().empty()) std::filesystem::create_directories(path.parent_path());
            sink_.open(path, std::ios::app);
            if (!sink_) std::cerr << "TracePrism: recording unavailable: " << path << '\n';
        } catch (const std::exception& error) {
            std::cerr << "TracePrism: recording unavailable: " << error.what() << '\n';
        }
    }

    FrameRef write(const char* file, int line, Span span, std::vector<Field> fields,
                   std::optional<Origin> origin) {
        std::lock_guard lock(mutex_);
        if (!sink_) return {UINT64_MAX};
        if (origin && origin->frame && origin->frame->seq >= next_seq_)
            throw std::invalid_argument("TracePrism: from() must refer to an earlier frame");
        std::map<std::string, Json> observed;
        for (auto& field : fields) {
            if (field.name.empty() || field.name.size() > 256)
                throw std::invalid_argument("TracePrism: field name must be 1..256 bytes");
            observed.insert_or_assign(std::move(field.name), std::move(field.value));
        }
        Json ops = Json::array();
        for (auto& [name, value] : observed) {
            if (auto it = values_.find(name); it != values_.end() && it->second == value) continue;
            ops.push_back({{"op", "put"}, {"name", name}, {"value", value}});
            values_.insert_or_assign(name, std::move(value));
        }
        Json event = {{"format", "viz.trace/v2"}, {"kind", next_seq_ == 0 ? "snapshot" : "patch"},
                      {"runId", run_id_}, {"seq", std::to_string(next_seq_)},
                      {"span", std::move(span.segments)}, {"source", {{"file", file}, {"line", line}}},
                      {"producer", "cpp"}, {"pid", process_id()}};
        if (origin) {
            if (origin->frame) event["from"] = std::to_string(origin->frame->seq);
            else event["fromId"] = std::move(origin->id);
        }
        if (next_seq_ == 0) {
            Json values = Json::array();
            for (const auto& [name, value] : values_)
                values.push_back({{"name", name}, {"value", value}});
            event["values"] = std::move(values);
        } else event["ops"] = std::move(ops);
        sink_ << event.dump() << '\n' << std::flush;
        if (!sink_) {
            std::cerr << "TracePrism: recording stopped: write failed\n";
            return {UINT64_MAX};
        }
        return {next_seq_++};
    }

private:
    std::mutex mutex_;
    std::ofstream sink_;
    std::string run_id_;
    std::uint64_t next_seq_ = 0;
    std::map<std::string, Json> values_;
};

inline Recorder& recorder() {
    static Recorder instance;
    return instance;
}

} // namespace detail

template <typename... T>
FrameRef record_at(const char* file, int line, Span span, T... args) {
    std::vector<Field> fields;
    std::optional<Origin> origin;
    (detail::collect(fields, origin, std::move(args)), ...);
    return detail::recorder().write(file, line, std::move(span), std::move(fields), std::move(origin));
}

} // namespace traceprism

#ifdef DEBUG
#define TP_FIELD(name) ::traceprism::field(#name, (name))
#define record(...) ::traceprism::record_at(__FILE__, __LINE__, __VA_ARGS__)
#else
#define record(...) ((void)0)
#endif
