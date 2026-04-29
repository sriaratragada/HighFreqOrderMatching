#include "orderbook.h"

#include <algorithm>
#include <iostream>
#include <limits>
#include <numeric>
#include <vector>

namespace {

/// Largest-remainder allocation: shares `total` across `weights` (integer quantities).
std::vector<int> proportionalAllocate(int total, const std::vector<int>& weights) {
    std::vector<int> out(weights.size(), 0);
    if (total <= 0 || weights.empty()) {
        return out;
    }
    const long long sum_w = std::accumulate(weights.begin(), weights.end(), 0LL);
    if (sum_w == 0) {
        return out;
    }
    std::vector<long long> frac(weights.size());
    int allocated = 0;
    for (size_t i = 0; i < weights.size(); ++i) {
        const long long raw = static_cast<long long>(total) * weights[i] / sum_w;
        out[static_cast<int>(i)] = static_cast<int>(raw);
        frac[i] = static_cast<long long>(total) * weights[i] - raw * sum_w;
        allocated += out[static_cast<int>(i)];
    }
    int rem = total - allocated;
    while (rem > 0) {
        auto it = std::max_element(frac.begin(), frac.end());
        if (*it == 0) {
            break;
        }
        const auto idx = static_cast<int>(std::distance(frac.begin(), it));
        out[idx] += 1;
        *it = 0;
        rem -= 1;
    }
    return out;
}

}  // namespace

int OrderBook::allocateOrderId() { return next_order_id_++; }

OrderBook::OrderBook(std::string allocation, TradeCallback trade_sink)
    : allocation(std::move(allocation)), trade_sink_(std::move(trade_sink)) {}

void OrderBook::ensureOrderId(Order& order) {
    if (order.orderId < 0) {
        order.orderId = allocateOrderId();
    }
    if (order.timestamp == 0) {
        order.timestamp = Order::timeSinceEpochMillisec();
    }
}

void OrderBook::applyMarketPeg(Order& order,
                               const std::map<double, std::deque<Order>>& bids_map,
                               const std::map<double, std::deque<Order>>& asks_map) {
    if (order.type == Order::BUY && order.orderType == Order::MARKET) {
        order.price = (!asks_map.empty()) ? asks_map.begin()->first
                                          : std::numeric_limits<double>::max();
    } else if (order.type == Order::SELL && order.orderType == Order::MARKET) {
        order.price = (!bids_map.empty()) ? bids_map.rbegin()->first
                                          : std::numeric_limits<double>::lowest();
    }
}

bool OrderBook::canFullyFill(const Order& order) const {
    if (order.quantity <= 0) {
        return true;
    }
    if (order.type == Order::BUY) {
        if (order.orderType == Order::MARKET) {
            if (asks.empty()) {
                return false;
            }
            int sum = 0;
            for (const auto& lvl : asks) {
                for (const auto& o : lvl.second) {
                    sum += o.quantity;
                }
                if (sum >= order.quantity) {
                    return true;
                }
            }
            return false;
        }
        int rem = order.quantity;
        for (const auto& lvl : asks) {
            if (lvl.first > order.price) {
                break;
            }
            for (const auto& o : lvl.second) {
                rem -= o.quantity;
                if (rem <= 0) {
                    return true;
                }
            }
        }
        return false;
    }
    if (order.orderType == Order::MARKET) {
        if (bids.empty()) {
            return false;
        }
        int sum = 0;
        for (auto it = bids.rbegin(); it != bids.rend(); ++it) {
            for (const auto& o : it->second) {
                sum += o.quantity;
            }
            if (sum >= order.quantity) {
                return true;
            }
        }
        return false;
    }
    int rem = order.quantity;
    for (auto it = bids.rbegin(); it != bids.rend(); ++it) {
        if (it->first < order.price) {
            break;
        }
        for (const auto& o : it->second) {
            rem -= o.quantity;
            if (rem <= 0) {
                return true;
            }
        }
    }
    return false;
}

bool OrderBook::isOrderIdActive(int orderId) const {
    for (const auto& o : stopOrders) {
        if (o.orderId == orderId) {
            return true;
        }
    }
    for (const auto& level : bids) {
        for (const auto& o : level.second) {
            if (o.orderId == orderId) {
                return true;
            }
        }
    }
    for (const auto& level : asks) {
        for (const auto& o : level.second) {
            if (o.orderId == orderId) {
                return true;
            }
        }
    }
    return false;
}

bool OrderBook::addOrder(Order order) {
    ensureOrderId(order);
    if (isOrderIdActive(order.orderId)) {
        return false;
    }

    if (order.orderType == Order::STOP) {
        stopOrders.push_back(order);
        checkStopOrders();
        return true;
    }

    applyMarketPeg(order, bids, asks);

    if (order.tif == Order::FOK && !canFullyFill(order)) {
        return false;
    }

    const int incoming_id = order.orderId;

    if (order.type == Order::BUY) {
        bids[order.price].push_back(order);
    } else {
        asks[order.price].push_back(order);
    }
    checkStopOrders();
    matchOrders();

    if (order.tif == Order::IOC) {
        cancelOrder(incoming_id);
    }

    return true;
}

void OrderBook::removeOrder(const Order& order) {
    auto& book = (order.type == Order::BUY) ? bids : asks;
    auto it = book.find(order.price);
    if (it != book.end()) {
        auto& orders = it->second;
        orders.erase(std::remove(orders.begin(), orders.end(), order), orders.end());
        if (orders.empty()) {
            book.erase(it);
        }
    }
}

bool OrderBook::cancelOrder(int orderId) {
    for (auto it = stopOrders.begin(); it != stopOrders.end(); ++it) {
        if (it->orderId == orderId) {
            stopOrders.erase(it);
            return true;
        }
    }
    for (auto book_it = bids.begin(); book_it != bids.end();) {
        auto& dq = book_it->second;
        const auto hit =
            std::find_if(dq.begin(), dq.end(),
                         [orderId](const Order& o) { return o.orderId == orderId; });
        if (hit != dq.end()) {
            dq.erase(hit);
            if (dq.empty()) {
                book_it = bids.erase(book_it);
            }
            return true;
        }
        ++book_it;
    }
    for (auto book_it = asks.begin(); book_it != asks.end();) {
        auto& dq = book_it->second;
        const auto hit =
            std::find_if(dq.begin(), dq.end(),
                         [orderId](const Order& o) { return o.orderId == orderId; });
        if (hit != dq.end()) {
            dq.erase(hit);
            if (dq.empty()) {
                book_it = asks.erase(book_it);
            }
            return true;
        }
        ++book_it;
    }
    return false;
}

void OrderBook::checkStopOrders() {
    for (auto it = stopOrders.begin(); it != stopOrders.end();) {
        bool trigger = false;
        if (it->type == Order::BUY && it->stopPrice <= getLowestAskPrice()) {
            trigger = true;
        } else if (it->type == Order::SELL && it->stopPrice >= getHighestBidPrice()) {
            trigger = true;
        }
        if (trigger) {
            Order activated = *it;
            activated.orderType = Order::MARKET;
            it = stopOrders.erase(it);
            addOrder(activated);
        } else {
            ++it;
        }
    }
}

int OrderBook::getQuantity(const Order& order) const { return order.quantity; }

double OrderBook::getLowestAskPrice() const {
    return asks.empty() ? std::numeric_limits<double>::max() : asks.begin()->first;
}

double OrderBook::getHighestBidPrice() const {
    return bids.empty() ? 0.0 : bids.rbegin()->first;
}

void OrderBook::matchOrders() {
    while (!bids.empty() && !asks.empty()) {
        auto highestBid = bids.rbegin();
        auto lowestAsk = asks.begin();

        if (highestBid->first >= lowestAsk->first) {
            if (allocation == "PRORATA") {
                matchOrdersProRata(highestBid->second, lowestAsk->second);
            } else {
                matchOrdersFIFO(highestBid->second, lowestAsk->second);
            }

            if (highestBid->second.empty()) {
                bids.erase(highestBid->first);
            }
            if (lowestAsk->second.empty()) {
                asks.erase(lowestAsk->first);
            }
        } else {
            break;
        }
    }
}

void OrderBook::matchOrdersFIFO(std::deque<Order>& bidOrders, std::deque<Order>& askOrders) {
    while (!bidOrders.empty() && !askOrders.empty()) {
        Order& bidOrder = bidOrders.front();
        Order& askOrder = askOrders.front();

        const int tradeQuantity = std::min(bidOrder.quantity, askOrder.quantity);
        const double tradePrice = askOrder.price;
        executeTrade(bidOrder, askOrder, tradeQuantity, tradePrice);

        bidOrder.quantity -= tradeQuantity;
        askOrder.quantity -= tradeQuantity;

        if (bidOrder.quantity == 0) {
            bidOrders.pop_front();
        }
        if (askOrder.quantity == 0) {
            askOrders.pop_front();
        }
    }
}

void OrderBook::matchOrdersProRata(std::deque<Order>& bidOrders, std::deque<Order>& askOrders) {
    while (!bidOrders.empty() && !askOrders.empty()) {
        int totalBid = 0;
        for (const auto& b : bidOrders) {
            totalBid += b.quantity;
        }
        int totalAsk = 0;
        for (const auto& a : askOrders) {
            totalAsk += a.quantity;
        }
        const int tradeQuantity = std::min(totalBid, totalAsk);
        if (tradeQuantity <= 0) {
            break;
        }

        std::vector<int> bid_w;
        bid_w.reserve(bidOrders.size());
        for (const auto& b : bidOrders) {
            bid_w.push_back(b.quantity);
        }
        std::vector<int> ask_w;
        ask_w.reserve(askOrders.size());
        for (const auto& a : askOrders) {
            ask_w.push_back(a.quantity);
        }

        std::vector<int> bid_alloc = proportionalAllocate(tradeQuantity, bid_w);
        std::vector<int> ask_alloc = proportionalAllocate(tradeQuantity, ask_w);

        size_t bi = 0;
        size_t ai = 0;
        while (bi < bidOrders.size() && ai < askOrders.size()) {
            if (bid_alloc[bi] == 0) {
                ++bi;
                continue;
            }
            if (ask_alloc[ai] == 0) {
                ++ai;
                continue;
            }
            const int t = std::min(bid_alloc[bi], ask_alloc[ai]);
            const double tradePrice = askOrders[ai].price;
            executeTrade(bidOrders[bi], askOrders[ai], t, tradePrice);

            bidOrders[bi].quantity -= t;
            askOrders[ai].quantity -= t;
            bid_alloc[bi] -= t;
            ask_alloc[ai] -= t;
        }

        auto compact = [](std::deque<Order>& d) {
            std::deque<Order> kept;
            for (auto& o : d) {
                if (o.quantity > 0) {
                    kept.push_back(std::move(o));
                }
            }
            d.swap(kept);
        };
        compact(bidOrders);
        compact(askOrders);
    }
}

void OrderBook::executeTrade(const Order& bidOrder, const Order& askOrder, int quantity,
                             double trade_price) {
    if (trade_sink_) {
        trade_sink_(quantity, trade_price, bidOrder.orderId, askOrder.orderId);
    }
}

void OrderBook::printDepth(std::ostream& os, int max_levels) const {
    if (max_levels <= 0) {
        return;
    }
    os << "ASKS (price x qty)\n";
    int a = 0;
    for (const auto& lvl : asks) {
        if (a >= max_levels) {
            break;
        }
        int q = 0;
        for (const auto& o : lvl.second) {
            q += o.quantity;
        }
        os << "  " << lvl.first << " x " << q << '\n';
        ++a;
    }
    os << "BIDS (price x qty)\n";
    int b = 0;
    for (auto it = bids.rbegin(); it != bids.rend() && b < max_levels; ++it, ++b) {
        int q = 0;
        for (const auto& o : it->second) {
            q += o.quantity;
        }
        os << "  " << it->first << " x " << q << '\n';
    }
}

void OrderBook::dispBids() const {
    for (const auto& bid : bids) {
        std::cout << "Price: " << bid.first << "\n";
        for (const auto& order : bid.second) {
            std::cout << " Order ID: " << order.orderId << ", Quantity: " << order.quantity
                      << ", Timestamp: " << order.timestamp << "\n";
        }
    }
}

void OrderBook::dispAsks() const {
    for (const auto& ask : asks) {
        std::cout << "Price: " << ask.first << "\n";
        for (const auto& order : ask.second) {
            std::cout << " Order ID: " << order.orderId << ", Quantity: " << order.quantity
                      << ", Timestamp: " << order.timestamp << "\n";
        }
    }
}
