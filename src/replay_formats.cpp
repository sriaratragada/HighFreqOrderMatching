#include "replay_formats.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <sstream>
#include <stdexcept>

namespace hfom {
namespace replay {

namespace {

void trim(std::string& s) {
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) {
        s.erase(s.begin());
    }
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) {
        s.pop_back();
    }
}

void stripUtf8Bom(std::string& s) {
    if (s.size() >= 3 && static_cast<unsigned char>(s[0]) == 0xEF &&
        static_cast<unsigned char>(s[1]) == 0xBB && static_cast<unsigned char>(s[2]) == 0xBF) {
        s.erase(0, 3);
    }
}

bool iequals(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) {
        return false;
    }
    for (size_t i = 0; i < a.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(a[i])) !=
            std::tolower(static_cast<unsigned char>(b[i]))) {
            return false;
        }
    }
    return true;
}

std::string to_lower(std::string s) {
    for (char& c : s) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return s;
}

bool parse_order_type(const std::string& t, Order::OrderType& out, std::string& err) {
    if (iequals(t, "LIMIT")) {
        out = Order::LIMIT;
        return true;
    }
    if (iequals(t, "MARKET")) {
        out = Order::MARKET;
        return true;
    }
    if (iequals(t, "STOP")) {
        out = Order::STOP;
        return true;
    }
    err = "unknown order type: " + t;
    return false;
}

bool parse_tif_token(const std::string& t, Order::TimeInForce& out, std::string& err) {
    if (t.empty()) {
        out = Order::GTC;
        return true;
    }
    if (iequals(t, "GTC")) {
        out = Order::GTC;
        return true;
    }
    if (iequals(t, "IOC")) {
        out = Order::IOC;
        return true;
    }
    if (iequals(t, "FOK")) {
        out = Order::FOK;
        return true;
    }
    err = "unknown time-in-force: " + t;
    return false;
}

bool add_order_dispatch(OrderBook& book, Order& o, std::string& err) {
    if (!book.addOrder(o)) {
        if (book.isOrderIdActive(o.orderId)) {
            err = "ADD: duplicate order id (already active): " + std::to_string(o.orderId);
        } else if (o.tif == Order::FOK) {
            err = "ADD: FOK order could not be fully filled immediately";
        } else {
            err = "ADD: rejected";
        }
        return false;
    }
    return true;
}

}  // namespace

const char kCsvHeaderLine[] =
    "action,side,order_type,tif,id,price,qty,stop_price,book_depth";

bool inferFormatFromPath(const std::string& path, InputFormat& out) {
    const auto dot = path.rfind('.');
    if (dot == std::string::npos) {
        return false;
    }
    std::string ext = to_lower(path.substr(dot));
    if (ext == ".csv") {
        out = InputFormat::Csv;
        return true;
    }
    if (ext == ".jsonl" || ext == ".ndjson") {
        out = InputFormat::Jsonl;
        return true;
    }
    return false;
}

bool parseFormatArg(const std::string& s, InputFormat& out) {
    if (iequals(s, "dsl") || iequals(s, "text")) {
        out = InputFormat::Dsl;
        return true;
    }
    if (iequals(s, "csv")) {
        out = InputFormat::Csv;
        return true;
    }
    if (iequals(s, "jsonl") || iequals(s, "json")) {
        out = InputFormat::Jsonl;
        return true;
    }
    return false;
}

std::vector<std::string> splitCsvLine(const std::string& line) {
    std::vector<std::string> out;
    std::string cur;
    bool in_quotes = false;
    for (size_t i = 0; i < line.size(); ++i) {
        const char c = line[i];
        if (in_quotes) {
            if (c == '"') {
                if (i + 1 < line.size() && line[i + 1] == '"') {
                    cur += '"';
                    ++i;
                } else {
                    in_quotes = false;
                }
            } else {
                cur += c;
            }
        } else {
            if (c == '"') {
                in_quotes = true;
            } else if (c == ',') {
                trim(cur);
                out.push_back(cur);
                cur.clear();
            } else {
                cur += c;
            }
        }
    }
    trim(cur);
    out.push_back(cur);
    return out;
}

bool validateCsvHeaderRow(const std::string& line, std::string& err) {
    std::string copy = line;
    stripUtf8Bom(copy);
    trim(copy);
    const auto cells = splitCsvLine(copy);
    const std::vector<const char*> expected = {"action", "side",      "order_type", "tif",
                                               "id",     "price",     "qty",        "stop_price",
                                               "book_depth"};
    if (cells.size() != expected.size()) {
        err = "CSV header must have exactly 9 columns";
        return false;
    }
    for (size_t i = 0; i < expected.size(); ++i) {
        if (!iequals(cells[i], expected[i])) {
            err = std::string("CSV header column ") + std::to_string(i + 1) + " must be `" +
                  expected[i] + "`, got `" + cells[i] + "`";
            return false;
        }
    }
    return true;
}

bool processCsvDataLine(const std::string& line, OrderBook& book, std::string& err) {
    std::string copy = line;
    stripUtf8Bom(copy);
    trim(copy);
    if (copy.empty()) {
        return true;
    }
    const auto f = splitCsvLine(copy);
    if (f.size() != 9u) {
        err = "CSV row must have 9 columns";
        return false;
    }

    const std::string& action = f[0];
    if (iequals(action, "BOOK")) {
        int depth = 10;
        if (!f[8].empty()) {
            try {
                depth = std::stoi(f[8]);
            } catch (const std::exception&) {
                err = "BOOK: book_depth must be integer";
                return false;
            }
        }
        if (depth < 1) {
            err = "BOOK: depth must be positive";
            return false;
        }
        book.printDepth(std::cout, depth);
        return true;
    }

    if (iequals(action, "CANCEL")) {
        if (f[4].empty()) {
            err = "CANCEL: id column (5) required";
            return false;
        }
        int id = 0;
        try {
            id = std::stoi(f[4]);
        } catch (const std::exception&) {
            err = "CANCEL: id must be integer";
            return false;
        }
        if (!book.cancelOrder(id)) {
            err = "CANCEL: order not found: " + std::to_string(id);
            return false;
        }
        return true;
    }

    if (!iequals(action, "ADD")) {
        err = "CSV action must be ADD, CANCEL, or BOOK";
        return false;
    }

    Order o{};
    if (iequals(f[1], "BUY")) {
        o.type = Order::BUY;
    } else if (iequals(f[1], "SELL")) {
        o.type = Order::SELL;
    } else {
        err = "ADD: side must be BUY or SELL";
        return false;
    }

    if (!parse_order_type(f[2], o.orderType, err)) {
        return false;
    }
    if (!parse_tif_token(f[3], o.tif, err)) {
        return false;
    }

    if (f[4].empty()) {
        err = "ADD: id required";
        return false;
    }
    try {
        o.orderId = std::stoi(f[4]);
    } catch (const std::exception&) {
        err = "ADD: id must be integer";
        return false;
    }

    if (o.orderType == Order::LIMIT) {
        if (f[5].empty() || f[6].empty()) {
            err = "ADD LIMIT: price and qty required";
            return false;
        }
        try {
            o.price = std::stod(f[5]);
            o.quantity = std::stoi(f[6]);
        } catch (const std::exception&) {
            err = "ADD LIMIT: price/qty must be numeric";
            return false;
        }
    } else if (o.orderType == Order::MARKET) {
        if (f[6].empty()) {
            err = "ADD MARKET: qty required";
            return false;
        }
        try {
            o.quantity = std::stoi(f[6]);
        } catch (const std::exception&) {
            err = "ADD MARKET: qty must be integer";
            return false;
        }
    } else if (o.orderType == Order::STOP) {
        if (f[7].empty() || f[6].empty()) {
            err = "ADD STOP: qty and stop_price required (qty in qty column, stop in stop_price)";
            return false;
        }
        try {
            o.stopPrice = std::stod(f[7]);
            o.quantity = std::stoi(f[6]);
        } catch (const std::exception&) {
            err = "ADD STOP: stop_price/qty must be numeric";
            return false;
        }
    }

    return add_order_dispatch(book, o, err);
}

bool processJsonlLine(const std::string& line_in, OrderBook& book, std::string& err) {
    std::string line = line_in;
    stripUtf8Bom(line);
    trim(line);
    if (line.empty()) {
        return true;
    }

    nlohmann::json j;
    try {
        j = nlohmann::json::parse(line);
    } catch (const std::exception& e) {
        err = std::string("JSON parse error: ") + e.what();
        return false;
    }

    if (!j.is_object()) {
        err = "JSON line must be an object";
        return false;
    }

    const std::string cmd = to_lower(j.value("cmd", ""));
    if (cmd == "book") {
        int depth = j.value("depth", 10);
        if (!j.contains("depth") && j.contains("book_depth")) {
            depth = j.value("book_depth", 10);
        }
        if (depth < 1) {
            err = "book: depth must be positive";
            return false;
        }
        book.printDepth(std::cout, depth);
        return true;
    }

    if (cmd == "cancel") {
        if (!j.contains("id") || !j["id"].is_number_integer()) {
            err = "cancel: id (integer) required";
            return false;
        }
        const int id = j["id"].get<int>();
        if (!book.cancelOrder(id)) {
            err = "CANCEL: order not found: " + std::to_string(id);
            return false;
        }
        return true;
    }

    if (cmd != "add") {
        err = "cmd must be add, cancel, or book";
        return false;
    }

    Order o{};
    const std::string side = to_lower(j.value("side", ""));
    if (side == "buy") {
        o.type = Order::BUY;
    } else if (side == "sell") {
        o.type = Order::SELL;
    } else {
        err = "add: side must be buy or sell";
        return false;
    }

    const std::string ot = j.value("order_type", j.value("type", ""));
    if (!parse_order_type(ot, o.orderType, err)) {
        return false;
    }

    std::string tif_s = j.value("tif", "GTC");
    if (j.contains("time_in_force")) {
        tif_s = j["time_in_force"].get<std::string>();
    }
    if (!parse_tif_token(tif_s, o.tif, err)) {
        return false;
    }

    if (!j.contains("id") || !j["id"].is_number_integer()) {
        err = "add: id (integer) required";
        return false;
    }
    o.orderId = j["id"].get<int>();

    if (o.orderType == Order::LIMIT) {
        if (!j.contains("price") || !j.contains("qty")) {
            err = "add limit: price and qty required";
            return false;
        }
        o.price = j["price"].get<double>();
        o.quantity = j["qty"].get<int>();
    } else if (o.orderType == Order::MARKET) {
        if (!j.contains("qty")) {
            err = "add market: qty required";
            return false;
        }
        o.quantity = j["qty"].get<int>();
    } else if (o.orderType == Order::STOP) {
        if (!j.contains("stop_price") || !j.contains("qty")) {
            err = "add stop: stop_price and qty required";
            return false;
        }
        o.stopPrice = j["stop_price"].get<double>();
        o.quantity = j["qty"].get<int>();
    }

    return add_order_dispatch(book, o, err);
}

bool processDslLine(const std::string& line_in, OrderBook& book, std::string& err) {
    std::string line = line_in;
    trim(line);
    if (line.empty() || line[0] == '#') {
        return true;
    }

    std::istringstream iss(line);
    std::string cmd;
    if (!(iss >> cmd)) {
        return true;
    }

    if (iequals(cmd, "BOOK")) {
        int depth = 10;
        if (iss >> depth) {
            if (depth < 1) {
                err = "BOOK: depth must be positive";
                return false;
            }
        }
        std::string extra;
        if (iss >> extra) {
            err = "BOOK: trailing tokens";
            return false;
        }
        book.printDepth(std::cout, depth);
        return true;
    }

    if (iequals(cmd, "CANCEL")) {
        int id = 0;
        if (!(iss >> id)) {
            err = "CANCEL: need order id";
            return false;
        }
        if (!book.cancelOrder(id)) {
            err = "CANCEL: order not found: " + std::to_string(id);
            return false;
        }
        return true;
    }

    if (!iequals(cmd, "ADD")) {
        err = "unknown command: " + cmd;
        return false;
    }

    std::string side_s;
    std::string type_s;
    if (!(iss >> side_s >> type_s)) {
        err = "ADD: expected ADD <BUY|SELL> <LIMIT|MARKET|STOP> ...";
        return false;
    }

    Order o{};
    if (iequals(side_s, "BUY")) {
        o.type = Order::BUY;
    } else if (iequals(side_s, "SELL")) {
        o.type = Order::SELL;
    } else {
        err = "ADD: side must be BUY or SELL";
        return false;
    }

    if (!parse_order_type(type_s, o.orderType, err)) {
        return false;
    }

    std::string tok;
    if (!(iss >> tok)) {
        err = "ADD: missing id or time-in-force";
        return false;
    }

    Order::TimeInForce tif = Order::GTC;
    int id = 0;
    if (iequals(tok, "GTC") || iequals(tok, "IOC") || iequals(tok, "FOK")) {
        std::string tif_err;
        if (!parse_tif_token(tok, tif, tif_err)) {
            err = tif_err;
            return false;
        }
        o.tif = tif;
        if (!(iss >> id)) {
            err = "ADD: need order id after time-in-force";
            return false;
        }
        o.orderId = id;
    } else {
        try {
            id = std::stoi(tok);
        } catch (const std::exception&) {
            err = "ADD: expected numeric order id or GTC|IOC|FOK";
            return false;
        }
        o.orderId = id;
    }

    if (o.orderType == Order::LIMIT) {
        double price = 0.0;
        int qty = 0;
        if (!(iss >> price >> qty)) {
            err = "ADD LIMIT: need <price> <qty>";
            return false;
        }
        o.price = price;
        o.quantity = qty;
    } else if (o.orderType == Order::MARKET) {
        int qty = 0;
        if (!(iss >> qty)) {
            err = "ADD MARKET: need <qty>";
            return false;
        }
        o.quantity = qty;
    } else if (o.orderType == Order::STOP) {
        double stop_px = 0.0;
        int qty = 0;
        if (!(iss >> stop_px >> qty)) {
            err = "ADD STOP: need <stop_price> <qty>";
            return false;
        }
        o.stopPrice = stop_px;
        o.quantity = qty;
    }

    std::string extra;
    if (iss >> extra) {
        err = "trailing tokens on line";
        return false;
    }

    return add_order_dispatch(book, o, err);
}

}  // namespace replay
}  // namespace hfom
