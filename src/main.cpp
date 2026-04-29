#include "orderbook.h"

#include <hfom/version.hpp>

#include <cctype>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>

namespace {

void trim(std::string& s) {
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) {
        s.erase(s.begin());
    }
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) {
        s.pop_back();
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

void usage() {
    std::cerr << "Usage: match_cli [--allocation FIFO|PRORATA] [--file PATH] [--json]\n"
              << "       match_cli --version | -h | --help\n"
              << "Reads commands from PATH or stdin. Lines starting with # are ignored.\n"
              << "Commands:\n"
              << "  ADD BUY|SELL LIMIT <id> <price> <qty>\n"
              << "  ADD BUY|SELL MARKET <id> <qty>\n"
              << "  ADD BUY|SELL STOP <id> <stop_price> <qty>\n"
              << "  CANCEL <id>\n"
              << "Each matched fill prints one line (text or JSON when --json).\n";
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

bool process_line(const std::string& line_in, OrderBook& book, std::string& err) {
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
    int id = 0;
    if (!(iss >> side_s >> type_s >> id)) {
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
    o.orderId = id;

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

    if (!book.addOrder(o)) {
        err = "ADD: duplicate order id (already active): " + std::to_string(o.orderId);
        return false;
    }
    return true;
}

}  // namespace

int main(int argc, char** argv) {
    std::string allocation = "FIFO";
    std::string file_path;
    bool json_trades = false;

    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--allocation" && i + 1 < argc) {
            allocation = argv[++i];
        } else if (a == "--file" && i + 1 < argc) {
            file_path = argv[++i];
        } else if (a == "--json") {
            json_trades = true;
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
    while (std::getline(*in, line)) {
        ++line_no;
        std::string err;
        if (!process_line(line, book, err)) {
            std::cerr << "Error line " << line_no << ": " << err << "\n  " << line << '\n';
            return 1;
        }
    }

    return 0;
}
