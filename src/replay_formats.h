#ifndef HFOM_REPLAY_FORMATS_H
#define HFOM_REPLAY_FORMATS_H

#include "orderbook.h"

#include <string>
#include <vector>

namespace hfom {
namespace replay {

enum class InputFormat { Dsl, Csv, Jsonl };

/// Infer csv/jsonl from extension (.csv, .jsonl, .ndjson). Returns false if unrecognized.
bool inferFormatFromPath(const std::string& path, InputFormat& out);

/// Parse --format value: dsl, csv, jsonl (case-insensitive).
bool parseFormatArg(const std::string& s, InputFormat& out);

/// Exact header row required as the first line of every CSV replay (Excel columns A–I).
extern const char kCsvHeaderLine[];

bool validateCsvHeaderRow(const std::string& line, std::string& err);

std::vector<std::string> splitCsvLine(const std::string& line);

bool processDslLine(const std::string& line, OrderBook& book, std::string& err);

/// One CSV data row (9 columns), after header validated.
bool processCsvDataLine(const std::string& line, OrderBook& book, std::string& err);

/// One JSON object per line. Schema: cmd "add"|"cancel"|"book" (case-insensitive).
bool processJsonlLine(const std::string& line, OrderBook& book, std::string& err);

}  // namespace replay
}  // namespace hfom

#endif
