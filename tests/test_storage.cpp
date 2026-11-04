#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>

#include "adc/storage.hpp"
#include "test_helpers.hpp"

using namespace adc;

class StorageTest : public ::testing::Test {
protected:
    adc_test::TempDir dir;
    std::string file(const std::string& name) const {
        return (std::filesystem::path(dir.path()) / (name + ".tsv")).string();
    }
};

TEST_F(StorageTest, SnapshotSaveAndReload) {
    Storage s(dir.path());
    const std::vector<Storage::Row> rows{{"M01", "Acetaminophen", "D1", "20"}, {"M02", "x", "D2", "15"}};
    ASSERT_TRUE(s.saveSnapshot("inventory", rows));
    Storage again(dir.path());
    EXPECT_EQ(again.loadSnapshot("inventory"), rows);
    EXPECT_EQ(again.malformedLines(), 0);
}

TEST_F(StorageTest, SnapshotReplacesPreviousContent) {
    Storage s(dir.path());
    ASSERT_TRUE(s.saveSnapshot("cabinet", {{"a"}, {"b"}}));
    ASSERT_TRUE(s.saveSnapshot("cabinet", {{"c"}}));
    EXPECT_EQ(s.loadSnapshot("cabinet"), (std::vector<Storage::Row>{{"c"}}));
    EXPECT_FALSE(std::filesystem::exists(file("cabinet") + ".tmp"));
}

TEST_F(StorageTest, EachSnapshotNameIsIndependent) {
    Storage s(dir.path());
    ASSERT_TRUE(s.saveSnapshot("inventory", {{"inv"}}));
    ASSERT_TRUE(s.saveSnapshot("cabinet", {{"cab"}}));
    ASSERT_TRUE(s.saveSnapshot("comm", {{"cursor", "5"}}));
    ASSERT_TRUE(s.saveSnapshot("session", {{"Dispense", "N100"}}));
    EXPECT_EQ(s.loadSnapshot("inventory")[0][0], "inv");
    EXPECT_EQ(s.loadSnapshot("cabinet")[0][0], "cab");
    EXPECT_EQ(s.loadSnapshot("comm")[0][1], "5");
    EXPECT_EQ(s.loadSnapshot("session")[0][1], "N100");
}

TEST_F(StorageTest, EscapingSurvivesTabsNewlinesBackslashes) {
    Storage s(dir.path());
    const std::vector<Storage::Row> rows{{"a\tb", "line1\nline2", "back\\slash", "", "cr\r"}};
    ASSERT_TRUE(s.saveSnapshot("esc", rows));
    EXPECT_EQ(s.loadSnapshot("esc"), rows);
}

TEST_F(StorageTest, AppendAndReloadKeepsOrder) {
    Storage s(dir.path());
    for (int i = 1; i <= 5; ++i) ASSERT_TRUE(s.appendRecord("events", {std::to_string(i)}));
    Storage again(dir.path());
    const auto rows = again.loadRecords("events");
    ASSERT_EQ(rows.size(), 5u);
    for (int i = 0; i < 5; ++i) EXPECT_EQ(rows[static_cast<std::size_t>(i)][0], std::to_string(i + 1));
}

TEST_F(StorageTest, MissingFileLoadsEmpty) {
    Storage s(dir.path());
    EXPECT_TRUE(s.loadSnapshot("nothing").empty());
    EXPECT_TRUE(s.loadRecords("nothing").empty());
    EXPECT_EQ(s.malformedLines(), 0);
}

TEST_F(StorageTest, MalformedLinesSkippedAndCounted) {
    {
        std::ofstream out(file("events"), std::ios::binary);
        out << "#ADC-STORAGE\tv1\n" << "good\t1\n" << "bad\\q\n" << "trailing\\\n" << "good\t2\n";
    }
    Storage s(dir.path());
    const auto rows = s.loadRecords("events");
    ASSERT_EQ(rows.size(), 2u);
    EXPECT_EQ(rows[1][1], "2");
    EXPECT_EQ(s.malformedLines(), 2);
}

TEST_F(StorageTest, WrongHeaderMeansWholeFileMalformed) {
    {
        std::ofstream out(file("inventory"), std::ios::binary);
        out << "something else\n" << "M01\t20\n";
    }
    Storage s(dir.path());
    EXPECT_TRUE(s.loadSnapshot("inventory").empty());
    EXPECT_EQ(s.malformedLines(), 2);
}

TEST_F(StorageTest, SimulatedWriteFailureLeavesOldFileIntact) {
    Storage s(dir.path());
    ASSERT_TRUE(s.saveSnapshot("inventory", {{"old"}}));
    ASSERT_TRUE(s.appendRecord("events", {"1"}));
    s.simulateWriteFailure(true);
    EXPECT_TRUE(s.writeFailureSimulated());
    EXPECT_FALSE(s.saveSnapshot("inventory", {{"new"}}));
    EXPECT_FALSE(s.appendRecord("events", {"2"}));
    EXPECT_EQ(s.loadSnapshot("inventory"), (std::vector<Storage::Row>{{"old"}}));
    EXPECT_EQ(s.loadRecords("events").size(), 1u);
    s.simulateWriteFailure(false);
    EXPECT_TRUE(s.saveSnapshot("inventory", {{"new"}}));
}

TEST_F(StorageTest, RealWriteFailureLeavesOldFileIntact) {
    Storage s(dir.path());
    ASSERT_TRUE(s.saveSnapshot("inventory", {{"old"}}));
    // Block the temporary file by putting a folder where it would be written.
    std::filesystem::create_directory(file("inventory") + ".tmp");
    EXPECT_FALSE(s.saveSnapshot("inventory", {{"new"}}));
    EXPECT_EQ(s.loadSnapshot("inventory"), (std::vector<Storage::Row>{{"old"}}));
}

TEST_F(StorageTest, ResetDeletesOnlyOurFiles) {
    {
        Storage s(dir.path());
        ASSERT_TRUE(s.saveSnapshot("inventory", {{"x"}}));
        ASSERT_TRUE(s.appendRecord("events", {"1"}));
    }
    const std::string other = (std::filesystem::path(dir.path()) / "notes.txt").string();
    { std::ofstream(other) << "keep me"; }
    EXPECT_TRUE(Storage::resetDirectory(dir.path()));
    EXPECT_FALSE(std::filesystem::exists(file("inventory")));
    EXPECT_FALSE(std::filesystem::exists(file("events")));
    EXPECT_TRUE(std::filesystem::exists(other));
}
