#include "adc/message_codec.hpp"

#include <cctype>
#include <charconv>
#include <cstdio>
#include <system_error>

namespace adc {

namespace {

constexpr unsigned char kFirstPrintable = 0x20;  // JSON: characters below this must be escaped
constexpr int kUnicodeEscapeDigits = 4;   // XXXX
constexpr int kHexBase = 16;
constexpr unsigned kAsciiLimit = 0x80;
constexpr std::size_t kUnicodeEscapeBuffer = 8;  // "\u00XX" + terminator

void appendField(std::string& out, const std::string& key, const std::string& value, bool raw) {
    out += ",\"" + MessageCodec::escape(key) + "\":";
    out += raw ? value : "\"" + MessageCodec::escape(value) + "\"";
}

// A tiny reader for flat JSON objects.
class Reader {
public:
    explicit Reader(const std::string& text) : s_(text) {}

    void skipSpace() {
        while (pos_ < s_.size() && std::isspace(static_cast<unsigned char>(s_[pos_]))) ++pos_;
    }
    bool take(char c) {
        skipSpace();
        if (pos_ < s_.size() && s_[pos_] == c) {
            ++pos_;
            return true;
        }
        return false;
    }
    bool peek(char c) {
        skipSpace();
        return pos_ < s_.size() && s_[pos_] == c;
    }
    bool atEnd() {
        skipSpace();
        return pos_ == s_.size();
    }
    bool readString(std::string& out) {
        out.clear();
        if (!take('"')) return false;
        while (pos_ < s_.size()) {
            const char c = s_[pos_++];
            if (c == '"') return true;
            if (c != '\\') {
                out += c;
                continue;
            }
            if (pos_ >= s_.size()) return false;
            const char e = s_[pos_++];
            switch (e) {
                case '"': out += '"'; break;
                case '\\': out += '\\'; break;
                case '/': out += '/'; break;
                case 'n': out += '\n'; break;
                case 'r': out += '\r'; break;
                case 't': out += '\t'; break;
                case 'b': out += '\b'; break;
                case 'f': out += '\f'; break;
                case 'u': {
                    if (pos_ + kUnicodeEscapeDigits > s_.size()) return false;
                    unsigned value = 0;
                    const char* first = s_.data() + pos_;
                    const auto r = std::from_chars(first, first + kUnicodeEscapeDigits, value, kHexBase);
                    if (r.ec != std::errc() || r.ptr != first + kUnicodeEscapeDigits) return false;
                    pos_ += kUnicodeEscapeDigits;
                    if (value >= kAsciiLimit) return false;  // only ASCII escapes are produced
                    out += static_cast<char>(value);
                    break;
                }
                default: return false;
            }
        }
        return false;
    }
    bool readBare(std::string& out) {
        skipSpace();
        out.clear();
        while (pos_ < s_.size() && (std::isalnum(static_cast<unsigned char>(s_[pos_])) || s_[pos_] == '-' ||
                                    s_[pos_] == '+' || s_[pos_] == '.')) {
            out += s_[pos_++];
        }
        return !out.empty();
    }

private:
    const std::string& s_;
    std::size_t pos_ = 0;
};

}  // namespace

std::string MessageCodec::escape(const std::string& text) {
    std::string out;
    for (char ch : text) {
        const unsigned char c = static_cast<unsigned char>(ch);
        switch (ch) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            default:
                if (c < kFirstPrintable) {
                    char buf[kUnicodeEscapeBuffer];
                    std::snprintf(buf, sizeof buf, "\\u%04x", static_cast<unsigned>(c));
                    out += buf;
                } else {
                    out += ch;
                }
        }
    }
    return out;
}

std::string MessageCodec::toJson(const std::string& cabinetId, const OutboundMessage& m) {
    std::string out = "{\"cabinet_id\":\"" + escape(cabinetId) + "\"";
    appendField(out, "type", m.type, false);
    appendField(out, "id", m.id, false);
    appendField(out, "timestamp", m.timestamp, false);
    for (const MessageField& f : m.fields) appendField(out, f.key, f.value, f.raw);
    out += "}";
    return out;
}

OutboundMessage MessageCodec::eventMessage(const EventRecord& r) {
    OutboundMessage m;
    m.type = "event";
    m.id = "EVT-" + std::to_string(r.id);
    m.timestamp = r.timestamp;
    m.fields = {{"event_id", std::to_string(r.id), true},
                {"event_type", toString(r.type), false},
                {"user", r.user, false},
                {"medication", r.medication, false},
                {"quantity", std::to_string(r.quantity), true},
                {"outcome", r.outcome, false},
                {"details", r.details, false}};
    return m;
}

std::optional<InboundMessage> MessageCodec::parse(const std::string& text) {
    Reader in(text);
    InboundMessage msg;
    if (!in.take('{')) return std::nullopt;
    if (!in.take('}')) {
        do {
            std::string key;
            if (!in.readString(key) || !in.take(':')) return std::nullopt;
            if (in.peek('"')) {
                std::string value;
                if (!in.readString(value)) return std::nullopt;
                msg.fields[key] = value;
            } else if (in.take('[')) {
                std::vector<std::string> items;
                if (!in.take(']')) {
                    do {
                        std::string item;
                        if (!in.readString(item)) return std::nullopt;
                        items.push_back(item);
                    } while (in.take(','));
                    if (!in.take(']')) return std::nullopt;
                }
                if (key == "ids") msg.ids = items;
            } else {
                std::string value;
                if (!in.readBare(value)) return std::nullopt;
                msg.fields[key] = value;
            }
        } while (in.take(','));
        if (!in.take('}')) return std::nullopt;
    }
    if (!in.atEnd()) return std::nullopt;
    const auto type = msg.fields.find("type");
    if (type == msg.fields.end() || type->second.empty()) return std::nullopt;
    msg.type = type->second;
    return msg;
}

std::string MessageCodec::ackJson(const std::vector<std::string>& ids) {
    std::string out = "{\"type\":\"ack\",\"ids\":[";
    for (std::size_t i = 0; i < ids.size(); ++i) out += (i ? ",\"" : "\"") + escape(ids[i]) + "\"";
    return out + "]}";
}

std::string MessageCodec::configJson(const std::string& id, const std::string& key, const std::string& value) {
    return "{\"type\":\"config\",\"id\":\"" + escape(id) + "\",\"key\":\"" + escape(key) + "\",\"value\":\"" +
           escape(value) + "\"}";
}

}  // namespace adc
