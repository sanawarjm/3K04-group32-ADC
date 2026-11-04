#pragma once
// Persistence (not a D1 component; added for D2). Supports NFR-REL-001 (records survive a
// restart), NFR-REL-002 (event records are never lost) and NFR-AVL-002 (unsent information is
// stored while the Monitoring System is unavailable).
//
// ---- Module Interface Specification (MG 1.1.3) --------------------------------
// Responsibility: saves and loads named tables of text rows in one data folder.
// Secret:         the persistence medium and file format: one tab-separated text file per name,
//                 a version header line, escaping of tab/newline/backslash, atomic replace of
//                 snapshots (write ".tmp" then rename) and append-only record files.
// Uses:           nothing (C++ standard library only).
// State invariant(s): INV-STO-1: a snapshot file always holds either the previous complete
//                     snapshot or the new complete snapshot, never a mix.
//
// No other module includes <fstream> or builds file paths.

#include <string>
#include <vector>

namespace adc {

class Storage {
public:
    using Row = std::vector<std::string>;

    // Storage(directory)
    // Pre:         directory names a folder that may or may not exist.
    // Post:        the folder exists if it could be created; no file is read yet.
    // Effects:     may create the folder.
    // Exceptions:  none (a folder that cannot be created makes every later write fail).
    explicit Storage(std::string directory);

    // saveSnapshot(name, rows) -> bool
    // Reqs:        NFR-REL-001, NFR-AVL-002
    // Pre:         name is a plain file stem (letters, digits, '_' or '-').
    // Post:        true: loadSnapshot(name) returns rows. false: the old file is unchanged (INV-STO-1).
    // Effects:     writes "<name>.tsv.tmp", then renames it over "<name>.tsv".
    // Exceptions:  none; every failure (including simulateWriteFailure) returns false.
    bool saveSnapshot(const std::string& name, const std::vector<Row>& rows);

    // loadSnapshot(name) -> rows
    // Reqs:        NFR-REL-001
    // Pre:         none.
    // Post:        the rows of the last successful save; empty if the file is missing.
    //              Malformed lines (bad escape, or a file with a wrong header) are skipped and
    //              counted in malformedLines().
    // Effects:     updates malformedLines().
    // Exceptions:  none.
    std::vector<Row> loadSnapshot(const std::string& name);

    // appendRecord(name, row) -> bool
    // Reqs:        NFR-REL-001, NFR-REL-002, NFR-AUD-005 (append only: no edit or delete exists)
    // Pre:         name as for saveSnapshot.
    // Post:        true: the row is the last row of loadRecords(name). false: the file is unchanged.
    // Effects:     appends one line (writes the header first if the file is new).
    // Exceptions:  none.
    bool appendRecord(const std::string& name, const Row& row);

    // loadRecords(name) -> rows
    // Post:        every appended row, in append order; empty if the file is missing; malformed
    //              lines skipped and counted.
    std::vector<Row> loadRecords(const std::string& name);

    // malformedLines() -> count of lines skipped by the most recent load.
    int malformedLines() const;

    // simulateWriteFailure(on)
    // Pre:         none. Used by tests and the demo harness only.
    // Post:        while on, every save/append returns false and changes nothing on disk.
    void simulateWriteFailure(bool on);
    bool writeFailureSimulated() const;

    // directory() -> the data folder this Storage uses.
    const std::string& directory() const;

    // resetDirectory(directory) -> bool
    // Reqs:        demo support (`adc_app --reset`)
    // Pre:         none.
    // Post:        every ".tsv" / ".tsv.tmp" file in the folder is deleted, then the folder is
    //              removed if it became empty. No other file or folder is touched.
    // Effects:     deletes files.
    // Exceptions:  none; returns false if a file could not be removed.
    static bool resetDirectory(const std::string& directory);

private:
    std::string pathFor(const std::string& name) const;
    std::vector<Row> loadFile(const std::string& name);

    std::string directory_;
    bool failWrites_ = false;
    int malformed_ = 0;
};

}  // namespace adc
