#include <gtest/gtest.h>
#include <limits>
#include <sstream>
#include <string>
#include <vector>

#include "orderbook.h"

TEST(OrderBookTest, AddBuyOrder) {
    OrderBook orderBook("FIFO");
    ASSERT_TRUE(orderBook.addOrder(Order{Order::BUY, Order::LIMIT, 10, 100.5}));

    EXPECT_DOUBLE_EQ(orderBook.getHighestBidPrice(), 100.5);
    EXPECT_DOUBLE_EQ(orderBook.getLowestAskPrice(), std::numeric_limits<double>::max());
}

TEST(OrderBookTest, AddAskOrder) {
    OrderBook orderBook("FIFO");
    ASSERT_TRUE(orderBook.addOrder(Order{Order::SELL, Order::LIMIT, 10, 100.5, 0.0, 0, -1}));
    ASSERT_TRUE(orderBook.addOrder(Order{Order::SELL, Order::LIMIT, 10, 90.2, 0.0, 0, -1}));
    ASSERT_TRUE(orderBook.addOrder(Order{Order::SELL, Order::LIMIT, 10, 95.3, 0.0, 0, -1}));

    EXPECT_DOUBLE_EQ(orderBook.getLowestAskPrice(), 90.2);
}

TEST(OrderBookTest, MatchProrata) {
    OrderBook orderBook("PRORATA");
    Order sellOrder1{Order::SELL, Order::LIMIT, 15, 100.5, 0.0, 0, 42};
    Order buyOrder1{Order::BUY, Order::LIMIT, 10, 101.5, 0.0, 0, 43};

    ASSERT_TRUE(orderBook.addOrder(sellOrder1));
    ASSERT_TRUE(orderBook.addOrder(buyOrder1));

    const auto& bids = orderBook.getBids();
    const auto& asks = orderBook.getAsks();

    EXPECT_TRUE(bids.empty());

    ASSERT_EQ(asks.size(), 1u);
    EXPECT_DOUBLE_EQ(asks.begin()->first, 100.5);
    ASSERT_EQ(asks.begin()->second.size(), 1u);
    EXPECT_EQ(asks.begin()->second.front().quantity, 5);
    EXPECT_EQ(asks.begin()->second.front().orderId, 42);
}

TEST(OrderBookTest, LimitOrderFIFO) {
    OrderBook orderBook("FIFO");
    ASSERT_TRUE(orderBook.addOrder({Order::SELL, Order::LIMIT, 10, 100.5}));
    ASSERT_TRUE(orderBook.addOrder({Order::SELL, Order::LIMIT, 5, 90.2}));
    ASSERT_TRUE(orderBook.addOrder({Order::SELL, Order::LIMIT, 10, 95.3}));
    ASSERT_TRUE(orderBook.addOrder({Order::SELL, Order::LIMIT, 5, 100.5}));
    ASSERT_TRUE(orderBook.addOrder({Order::BUY, Order::LIMIT, 5, 100.0}));
    ASSERT_TRUE(orderBook.addOrder({Order::BUY, Order::LIMIT, 15, 50.4}));

    EXPECT_DOUBLE_EQ(orderBook.getLowestAskPrice(), 95.3);
    EXPECT_DOUBLE_EQ(orderBook.getHighestBidPrice(), 50.4);
}

TEST(OrderBookTest, MatchOrderMarket) {
    OrderBook orderBook("FIFO");
    ASSERT_TRUE(orderBook.addOrder({Order::BUY, Order::MARKET, 20}));
    EXPECT_DOUBLE_EQ(orderBook.getHighestBidPrice(), std::numeric_limits<double>::max());

    ASSERT_TRUE(orderBook.addOrder({Order::SELL, Order::LIMIT, 10, 100.5}));
    ASSERT_TRUE(orderBook.addOrder({Order::SELL, Order::LIMIT, 10, 90.2}));
    ASSERT_TRUE(orderBook.addOrder({Order::SELL, Order::LIMIT, 10, 95.3}));

    EXPECT_DOUBLE_EQ(orderBook.getLowestAskPrice(), 95.3);
}

TEST(OrderBookTest, FIFOSamePriceLevel) {
    std::vector<std::pair<int, int>> trades;
    OrderBook book2("FIFO", [&trades](int qty, double, int, int ask_id) {
        trades.push_back({qty, ask_id});
    });
    ASSERT_TRUE(book2.addOrder({Order::SELL, Order::LIMIT, 5, 100.0, 0.0, 0, 1}));
    ASSERT_TRUE(book2.addOrder({Order::SELL, Order::LIMIT, 5, 100.0, 0.0, 0, 2}));
    ASSERT_TRUE(book2.addOrder({Order::BUY, Order::LIMIT, 7, 100.0, 0.0, 0, 99}));

    ASSERT_EQ(trades.size(), 2u);
    EXPECT_EQ(trades[0].first, 5);
    EXPECT_EQ(trades[0].second, 1);
    EXPECT_EQ(trades[1].first, 2);
    EXPECT_EQ(trades[1].second, 2);
}

TEST(OrderBookTest, CancelOrder) {
    OrderBook book("FIFO");
    ASSERT_TRUE(book.addOrder({Order::BUY, Order::LIMIT, 10, 50.0, 0.0, 0, 7}));
    EXPECT_TRUE(book.cancelOrder(7));
    EXPECT_TRUE(book.getBids().empty());
}

TEST(OrderBookTest, DuplicateOrderIdRejected) {
    OrderBook book("FIFO");
    ASSERT_TRUE(book.addOrder({Order::BUY, Order::LIMIT, 1, 50.0, 0.0, 0, 9}));
    EXPECT_FALSE(book.addOrder({Order::BUY, Order::LIMIT, 1, 51.0, 0.0, 0, 9}));
}

TEST(OrderBookTest, MarketBuyPegDoesNotWalkMultipleAskLevels) {
    OrderBook book("FIFO");
    ASSERT_TRUE(book.addOrder({Order::SELL, Order::LIMIT, 10, 90.0, 0.0, 0, 1}));
    ASSERT_TRUE(book.addOrder({Order::SELL, Order::LIMIT, 10, 100.0, 0.0, 0, 2}));
    ASSERT_TRUE(book.addOrder({Order::BUY, Order::MARKET, 20, 0.0, 0.0, 0, 3}));

    EXPECT_DOUBLE_EQ(book.getLowestAskPrice(), 100.0);
    ASSERT_FALSE(book.getBids().empty());
    const double bid_px = book.getHighestBidPrice();
    ASSERT_FALSE(book.getBids().find(bid_px) == book.getBids().end());
    EXPECT_EQ(book.getBids().find(bid_px)->second.front().quantity, 10);
}

TEST(OrderBookTest, StopBuyRestsWhenStopPriceAboveLowestAsk) {
    OrderBook book("FIFO");
    ASSERT_TRUE(book.addOrder({Order::SELL, Order::LIMIT, 5, 120.0, 0.0, 0, 1}));
    ASSERT_TRUE(book.addOrder({Order::BUY, Order::STOP, 1, 0.0, 125.0, 0, 2}));
    EXPECT_EQ(book.getBids().size(), 0u);
    EXPECT_TRUE(book.cancelOrder(2));
}

TEST(OrderBookTest, StopBuyActivatesWhenLowestAskMeetsStop) {
    OrderBook book("FIFO");
    ASSERT_TRUE(book.addOrder({Order::SELL, Order::LIMIT, 3, 100.0, 0.0, 0, 11}));
    ASSERT_TRUE(book.addOrder({Order::BUY, Order::STOP, 3, 0.0, 100.0, 0, 10}));
    EXPECT_TRUE(book.getBids().empty());
    EXPECT_TRUE(book.getAsks().empty());
}

TEST(OrderBookTest, ProRataConservesVolumeAcrossMultipleOrders) {
    int traded = 0;
    OrderBook book("PRORATA", [&traded](int qty, double, int, int) { traded += qty; });

    ASSERT_TRUE(book.addOrder({Order::SELL, Order::LIMIT, 3, 100.0, 0.0, 0, 1}));
    ASSERT_TRUE(book.addOrder({Order::SELL, Order::LIMIT, 3, 100.0, 0.0, 0, 2}));
    ASSERT_TRUE(book.addOrder({Order::SELL, Order::LIMIT, 4, 100.0, 0.0, 0, 3}));
    ASSERT_TRUE(book.addOrder({Order::BUY, Order::LIMIT, 4, 101.0, 0.0, 0, 4}));
    ASSERT_TRUE(book.addOrder({Order::BUY, Order::LIMIT, 4, 101.0, 0.0, 0, 5}));

    EXPECT_EQ(traded, 8);

    int rest = 0;
    for (const auto& lvl : book.getAsks()) {
        for (const auto& o : lvl.second) {
            EXPECT_GT(o.quantity, 0);
            rest += o.quantity;
        }
    }
    for (const auto& lvl : book.getBids()) {
        for (const auto& o : lvl.second) {
            EXPECT_GT(o.quantity, 0);
            rest += o.quantity;
        }
    }
    EXPECT_EQ(rest, 2);
}

TEST(OrderBookTest, StopBuyOnEmptyBookActivatesImmediately) {
    OrderBook book("FIFO");
    ASSERT_TRUE(book.addOrder({Order::BUY, Order::STOP, 2, 0.0, 50.0, 0, 1}));
    EXPECT_FALSE(book.getBids().empty());
}

TEST(OrderBookTest, IocLimitDropsUnfilledRemainder) {
    OrderBook book("FIFO");
    ASSERT_TRUE(book.addOrder({Order::SELL, Order::LIMIT, 3, 100.0, 0.0, 0, 1}));
    Order ioc{Order::BUY, Order::LIMIT, 10, 101.0, 0.0, 0, 2};
    ioc.tif = Order::IOC;
    ASSERT_TRUE(book.addOrder(ioc));
    EXPECT_TRUE(book.getBids().empty());
    ASSERT_TRUE(book.getAsks().empty());
}

TEST(OrderBookTest, IocDoesNotRestMarketBuyRemainder) {
    OrderBook book("FIFO");
    ASSERT_TRUE(book.addOrder({Order::SELL, Order::LIMIT, 5, 90.0, 0.0, 0, 1}));
    Order ioc{Order::BUY, Order::MARKET, 12, 0.0, 0.0, 0, 2};
    ioc.tif = Order::IOC;
    ASSERT_TRUE(book.addOrder(ioc));
    EXPECT_TRUE(book.getBids().empty());
}

TEST(OrderBookTest, FokRejectsWhenNotFullyFillable) {
    OrderBook book("FIFO");
    ASSERT_TRUE(book.addOrder({Order::SELL, Order::LIMIT, 3, 100.0, 0.0, 0, 1}));
    Order fok{Order::BUY, Order::LIMIT, 10, 101.0, 0.0, 0, 2};
    fok.tif = Order::FOK;
    EXPECT_FALSE(book.addOrder(fok));
    ASSERT_EQ(book.getAsks().size(), 1u);
    EXPECT_EQ(book.getAsks().begin()->second.front().quantity, 3);
}

TEST(OrderBookTest, FokAcceptsWhenFullyFillable) {
    OrderBook book("FIFO");
    ASSERT_TRUE(book.addOrder({Order::SELL, Order::LIMIT, 10, 100.0, 0.0, 0, 1}));
    Order fok{Order::BUY, Order::LIMIT, 10, 100.0, 0.0, 0, 2};
    fok.tif = Order::FOK;
    ASSERT_TRUE(book.addOrder(fok));
    EXPECT_TRUE(book.getBids().empty());
    EXPECT_TRUE(book.getAsks().empty());
}

TEST(OrderBookTest, PerBookOrderIdCounters) {
    OrderBook a("FIFO");
    OrderBook b("FIFO");
    ASSERT_TRUE(a.addOrder({Order::BUY, Order::LIMIT, 1, 10.0, 0.0, 0, -1}));
    ASSERT_TRUE(b.addOrder({Order::BUY, Order::LIMIT, 1, 10.0, 0.0, 0, -1}));
    ASSERT_EQ(a.getBids().begin()->second.front().orderId, 0);
    ASSERT_EQ(b.getBids().begin()->second.front().orderId, 0);
}

TEST(OrderBookTest, PrintDepthAggregatesLevels) {
    OrderBook book("FIFO");
    ASSERT_TRUE(book.addOrder({Order::SELL, Order::LIMIT, 3, 100.0, 0.0, 0, 1}));
    ASSERT_TRUE(book.addOrder({Order::SELL, Order::LIMIT, 2, 100.0, 0.0, 0, 2}));
    ASSERT_TRUE(book.addOrder({Order::BUY, Order::LIMIT, 1, 99.0, 0.0, 0, 3}));
    std::ostringstream os;
    book.printDepth(os, 5);
    const std::string s = os.str();
    EXPECT_NE(s.find("100"), std::string::npos);
    EXPECT_NE(s.find("x 5"), std::string::npos);
}
