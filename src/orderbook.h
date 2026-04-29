#ifndef ORDER_BOOK_H
#define ORDER_BOOK_H

#include <chrono>
#include <cstdint>
#include <deque>
#include <functional>
#include <iosfwd>
#include <limits>
#include <map>
#include <string>
#include <utility>

struct Order {
    enum Type { BUY, SELL };
    enum OrderType { MARKET, LIMIT, STOP };
    enum TimeInForce { GTC, IOC, FOK };

    Type type = BUY;
    OrderType orderType = LIMIT;
    int quantity = 0;
    double price = 0.0;
    double stopPrice = 0.0;
    uint64_t timestamp = 0;
    int orderId = -1;
    TimeInForce tif = GTC;

    bool operator==(const Order& other) const { return orderId == other.orderId; }

    static uint64_t timeSinceEpochMillisec() {
        using namespace std::chrono;
        return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
    }
};

using TradeCallback = std::function<void(int qty, double price, int bid_order_id, int ask_order_id)>;

class OrderBook {
public:
    explicit OrderBook(std::string allocation, TradeCallback trade_sink = TradeCallback());

    /// \return false if duplicate active id, or FOK could not be fully filled immediately.
    bool addOrder(Order order);
    int getQuantity(const Order& order) const;
    void removeOrder(const Order& order);
    bool cancelOrder(int orderId);
    bool isOrderIdActive(int orderId) const;
    void matchOrders();
    double getLowestAskPrice() const;
    double getHighestBidPrice() const;
    void dispBids() const;
    void dispAsks() const;

    /// Aggregated quantity per price level, up to \p max_levels per side (asks best-first, bids best-first).
    void printDepth(std::ostream& os, int max_levels = 10) const;

    const std::map<double, std::deque<Order>>& getBids() const { return bids; }
    const std::map<double, std::deque<Order>>& getAsks() const { return asks; }

private:
    std::string allocation;
    std::map<double, std::deque<Order>> bids;
    std::map<double, std::deque<Order>> asks;
    std::deque<Order> stopOrders;
    TradeCallback trade_sink_;
    int next_order_id_ = 0;

    int allocateOrderId();
    void matchOrdersFIFO(std::deque<Order>& bidOrders, std::deque<Order>& askOrders);
    void matchOrdersProRata(std::deque<Order>& bidOrders, std::deque<Order>& askOrders);
    void executeTrade(const Order& bidOrder, const Order& askOrder, int quantity, double trade_price);
    void checkStopOrders();
    void ensureOrderId(Order& order);
    static void applyMarketPeg(Order& order, const std::map<double, std::deque<Order>>& bids,
                               const std::map<double, std::deque<Order>>& asks);
    bool canFullyFill(const Order& order) const;
};

#endif
