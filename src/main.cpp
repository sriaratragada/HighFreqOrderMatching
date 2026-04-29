#include "replay_formats.h"

#include <hfom/version.hpp>

#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>

namespace {

void usage() {
    std::cerr << "Usage: match_cli [--allocation FIFO|PRORATA] [--file PATH] [--format dsl|csv|jsonl]\n"
              << "       [--json] [--dump-book] [--dump-depth N]\n"
              << "       match_cli --version | -h | --help\n"
              << "Input format defaults from --file extension (.csv, .jsonl, .ndjson); otherwise DSL (text).\n"
              << "Use --format when reading from stdin or when the extension does not match the content.\n"
              << "  CSV: first line is a fixed header: " << hfom::replay::kCsvHeaderLine << "\n"
              << "  JSONL: one JSON object per line; cmd add|cancel|book (schema in README).\n"
              << "DSL: lines starting with # are ignored.\n"
              << "DSL commands:\n"
              << "  ADD BUY|SELL LIMIT [GTC|IOC|FOK] <id> <price> <qty>   (TIF defaults to GTC)\n"
              << "  ADD BUY|SELL MARKET [GTC|IOC|FOK] <id> <qty>\n"
              << "  ADD BUY|SELL STOP [GTC|IOC|FOK] <id> <stop_price> <qty>\n"
              << "  CANCEL <id>\n"
              << "  BOOK [depth]   (print top depth levels per side; default 10)\n"
              << "Each matched fill prints one line (text or JSON when --json).\n";
}

}  // namespace

int main(int argc, char** argv) {
    std::string allocation = "FIFO";
    std::string file_path;
    std::string format_opt;
    bool json_trades = false;
    bool dump_book = false;
    int dump_depth = 10;

    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--allocation" && i + 1 < argc) {
            allocation = argv[++i];
        } else if (a == "--file" && i + 1 < argc) {
            file_path = argv[++i];
        } else if (a == "--format" && i + 1 < argc) {
            format_opt = argv[++i];
        } else if (a == "--json") {
            json_trades = true;
        } else if (a == "--dump-book") {
            dump_book = true;
        } else if (a == "--dump-depth" && i + 1 < argc) {
            dump_depth = std::stoi(argv[++i]);
            if (dump_depth < 1) {
                std::cerr << "--dump-depth must be >= 1\n";
                return 2;
            }
        } else if (a == "--version") {
            std::cout << HFOM_VERSION_STRING << '\n';
            return 0;
        } else if (a == "--help" || a == "-h") {
            usage();
            return 0;
        } else {
            std::cerr << "Unknown argument: " << a << "\n";
            usage();
            return 2;
        }
    }

    hfom::replay::InputFormat fmt = hfom::replay::InputFormat::Dsl;
    if (!format_opt.empty()) {
        if (!hfom::replay::parseFormatArg(format_opt, fmt)) {
            std::cerr << "Unknown --format `" << format_opt << "` (use dsl, csv, or jsonl)\n";
            return 2;
        }
    } else if (!file_path.empty()) {
        hfom::replay::InputFormat inferred{};
        if (hfom::replay::inferFormatFromPath(file_path, inferred)) {
            fmt = inferred;
        }
    }

    TradeCallback sink = [json_trades](int qty, double price, int bid_id, int ask_id) {
        if (!json_trades) {
            std::cout << "TRADE " << qty << ' ' << price << ' ' << bid_id << ' ' << ask_id << '\n';
            return;
        }
        std::ostringstream oss;
        oss << std::setprecision(17) << std::fixed;
        oss << "{\"type\":\"trade\",\"qty\":" << qty << ",\"price\":" << price
            << ",\"bid_order_id\":" << bid_id << ",\"ask_order_id\":" << ask_id << "}\n";
        std::cout << oss.str();
    };
    OrderBook book(allocation, std::move(sink));

    std::istream* in = &std::cin;
    std::ifstream fin;
    if (!file_path.empty()) {
        fin.open(file_path);
        if (!fin) {
            std::cerr << "Cannot open file: " << file_path << '\n';
            return 1;
        }
        in = &fin;
    }

    std::string line;
    int line_no = 0;
    bool csv_header_seen = false;

    while (std::getline(*in, line)) {
        ++line_no;
        std::string err;
        bool ok = false;

        switch (fmt) {
            case hfom::replay::InputFormat::Dsl:
                ok = hfom::replay::processDslLine(line, book, err);
                break;
            case hfom::replay::InputFormat::Csv:
                if (!csv_header_seen) {
                    ok = hfom::replay::validateCsvHeaderRow(line, err);
                    if (ok) {
                        csv_header_seen = true;
                    }
                } else {
                    ok = hfom::replay::processCsvDataLine(line, book, err);
                }
                break;
            case hfom::replay::InputFormat::Jsonl:
                ok = hfom::replay::processJsonlLine(line, book, err);
                break;
        }

        if (!ok) {
            std::cerr << "Error line " << line_no << ": " << err << "\n  " << line << '\n';
            return 1;
        }
    }

    if (fmt == hfom::replay::InputFormat::Csv && !csv_header_seen) {
        std::cerr << "CSV replay requires a header row as the first line.\n";
        return 1;
    }

    if (dump_book) {
        book.printDepth(std::cout, dump_depth);
    }

    return 0;
}
