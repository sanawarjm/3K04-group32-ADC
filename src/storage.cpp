#include "adc/storage.hpp"

#include <filesystem>
#include <fstream>
#include <system_error>
#include <utility>

namespace adc {

namespace fs = std::filesystem;

namespace {

const char* const kHeader = "#ADC-STORAGE\tv1";
const char* const kExtension = ".tsv";
const char* const kTempSuffix = ".tmp";

std::string escapeField(const std::string& field) {
    std::string out;
    for (char c : field) {
        switch (c) {
            case '\\': out += "\\\\"; break;
            case '\t': out += "\\t"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            default: out += c; break;
        }
    }
    return out;
}

std::string encodeRow(const Storage::Row& row) {
    std::string line;
    for (std::size_t i = 0; i < row.size(); ++i) {
        if (i > 0) line += '\t';
        line += escapeField(row[i]);
    }
    return line;
}

// Splits one line into fields and undoes the escaping. Returns false for a bad escape.
bool decodeRow(const std::string& line, Storage::Row& row) {
    row.clear();
    std::string field;
    for (std::size_t i = 0; i < line.size(); ++i) {
        const char c = line[i];
        if (c == '\t') {
            row.push_back(field);
            field.clear();
        } else if (c == '\\') {
            if (i + 1 >= line.size()) return false;
            const char next = line[++i];
            if (next == '\\') field += '\\';
            else if (next == 't') field += '\t';
            else if (next == 'n') field += '\n';
            else if (next == 'r') field += '\r';
            else return false;
        } else {
            field += c;
        }
    }
    row.push_back(field);
    return true;
}

}  // namespace

Storage::Storage(std::string directory) : directory_(std::move(directory)) {
    std::error_code ec;
    fs::create_directories(directory_, ec);
}

std::string Storage::pathFor(const std::string& name) const {
    return (fs::path(directory_) / (name + kExtension)).string();
}

bool Storage::saveSnapshot(const std::string& name, const std::vector<Row>& rows) {
    if (failWrites_) return false;
    std::error_code ec;
    fs::create_directories(directory_, ec);
    const std::string target = pathFor(name);
    const std::string temp = target + kTempSuffix;
    {
        std::ofstream out(temp, std::ios::binary | std::ios::trunc);
        if (!out) return false;
        out << kHeader << '\n';
        for (const Row& row : rows) out << encodeRow(row) << '\n';
        out.flush();
        if (!out) {
            out.close();
            fs::remove(temp, ec);
            return false;
        }
    }
    fs::rename(temp, target, ec);
    if (ec) {
        // Windows cannot rename over an existing file in every case: remove it, then retry.
        std::error_code removeEc;
        fs::remove(target, removeEc);
        ec.clear();
        fs::rename(temp, target, ec);
        if (ec) {
            fs::remove(temp, removeEc);
            return false;
        }
    }
    return true;
}

std::vector<Storage::Row> Storage::loadFile(const std::string& name) {
    malformed_ = 0;
    std::vector<Row> rows;
    std::ifstream in(pathFor(name), std::ios::binary);
    if (!in) return rows;  // a missing file loads as empty
    std::string line;
    bool headerSeen = false;
    bool headerValid = false;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (!headerSeen) {
            headerSeen = true;
            headerValid = (line == kHeader);
            if (!headerValid) ++malformed_;
            continue;
        }
        if (!headerValid) {
            ++malformed_;
            continue;
        }
        if (line.empty()) continue;
        Row row;
        if (decodeRow(line, row)) {
            rows.push_back(row);
        } else {
            ++malformed_;
        }
    }
    return rows;
}

std::vector<Storage::Row> Storage::loadSnapshot(const std::string& name) { return loadFile(name); }

std::vector<Storage::Row> Storage::loadRecords(const std::string& name) { return loadFile(name); }

bool Storage::appendRecord(const std::string& name, const Row& row) {
    if (failWrites_) return false;
    std::error_code ec;
    fs::create_directories(directory_, ec);
    const std::string target = pathFor(name);
    const bool isNew = !fs::exists(target, ec);
    std::ofstream out(target, std::ios::binary | std::ios::app);
    if (!out) return false;
    if (isNew) out << kHeader << '\n';
    out << encodeRow(row) << '\n';
    out.flush();
    return static_cast<bool>(out);
}

int Storage::malformedLines() const { return malformed_; }

void Storage::simulateWriteFailure(bool on) { failWrites_ = on; }

bool Storage::writeFailureSimulated() const { return failWrites_; }

const std::string& Storage::directory() const { return directory_; }

bool Storage::resetDirectory(const std::string& directory) {
    std::error_code ec;
    if (!fs::is_directory(directory, ec)) return true;
    bool allRemoved = true;
    for (const auto& entry : fs::directory_iterator(directory, ec)) {
        const bool ours = entry.is_regular_file(ec) &&
                          (entry.path().extension() == kExtension ||
                           (entry.path().extension() == kTempSuffix &&
                            entry.path().stem().extension() == kExtension));
        if (!ours) continue;
        std::error_code removeEc;
        if (!fs::remove(entry.path(), removeEc)) allRemoved = false;
    }
    if (fs::is_empty(directory, ec)) fs::remove(directory, ec);
    return allRemoved;
}

}  // namespace adc
