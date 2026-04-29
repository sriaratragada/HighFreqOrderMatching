#include "replay_formats.h"

#include <gtest/gtest.h>

TEST(ReplayFormats, InferCsvFromPath) {
    hfom::replay::InputFormat f{};
    EXPECT_TRUE(hfom::replay::inferFormatFromPath("/tmp/x.CSV", f));
    EXPECT_EQ(f, hfom::replay::InputFormat::Csv);
}

TEST(ReplayFormats, InferJsonlExtensions) {
    hfom::replay::InputFormat f{};
    EXPECT_TRUE(hfom::replay::inferFormatFromPath("replay.jsonl", f));
    EXPECT_EQ(f, hfom::replay::InputFormat::Jsonl);
    EXPECT_TRUE(hfom::replay::inferFormatFromPath("out.ndjson", f));
    EXPECT_EQ(f, hfom::replay::InputFormat::Jsonl);
}

TEST(ReplayFormats, InferUnknownExtension) {
    hfom::replay::InputFormat f = hfom::replay::InputFormat::Jsonl;
    EXPECT_FALSE(hfom::replay::inferFormatFromPath("notes.txt", f));
}

TEST(ReplayFormats, CsvHeaderExact) {
    std::string err;
    EXPECT_TRUE(hfom::replay::validateCsvHeaderRow(hfom::replay::kCsvHeaderLine, err)) << err;
}

TEST(ReplayFormats, CsvHeaderWithBom) {
    std::string err;
    const std::string line = std::string("\xEF\xBB\xBF") + hfom::replay::kCsvHeaderLine;
    EXPECT_TRUE(hfom::replay::validateCsvHeaderRow(line, err)) << err;
}

TEST(ReplayFormats, CsvHeaderWrongColumn) {
    std::string err;
    EXPECT_FALSE(
        hfom::replay::validateCsvHeaderRow("action,side,order_type,tif,id,price,qty,stop,book_depth", err));
    EXPECT_FALSE(err.empty());
}

TEST(ReplayFormats, SplitCsvQuoted) {
    const auto cells = hfom::replay::splitCsvLine(R"(ADD,BUY,LIMIT,,"1","10,5",3,,)");
    ASSERT_EQ(cells.size(), 9u);
    EXPECT_EQ(cells[4], "1");
    EXPECT_EQ(cells[5], "10,5");
    EXPECT_EQ(cells[6], "3");
}

TEST(ReplayFormats, ParseFormatArg) {
    hfom::replay::InputFormat f{};
    EXPECT_TRUE(hfom::replay::parseFormatArg("DSL", f));
    EXPECT_EQ(f, hfom::replay::InputFormat::Dsl);
    EXPECT_TRUE(hfom::replay::parseFormatArg("JSONL", f));
    EXPECT_EQ(f, hfom::replay::InputFormat::Jsonl);
    EXPECT_FALSE(hfom::replay::parseFormatArg("xml", f));
}
