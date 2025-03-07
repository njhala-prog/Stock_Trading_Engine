#include <iostream>
#include <thread>
#include <atomic>
#include <random>
#include <chrono>
#include <mutex>
#include <vector>

std::mutex print_mutex;

const int MAX_TICKERS = 1024;
const int MAX_ORDERS = 50000;

enum OrderType
{               
    BUY,
    SELL
};

struct Order
{
    int orderId;
    OrderType type;
    int ticker;
    std::atomic<int> quantity;
    double price;
    std::atomic<bool> active;
};

Order orders[MAX_ORDERS];
std::atomic<int> orderCount(0);
std::atomic<int> globalOrderId(0);

//findBestOrders function is used to find the best buy and sell orders in the order book we are using a pair to return the best buy and sell orders by again rescanning the order book
std::pair<int, int> findBestOrders(int ticker, double &bestBuyPrice, double &bestSellPrice)
{
    int bestBuy= -1, bestSell = -1;
    bestBuyPrice = -1.0;
    bestSellPrice = 1e9;
    int currentCount = orderCount.load();

    for (int i = 0; i < currentCount; i++)
    {
        if (!orders[i].active.load() || orders[i].ticker != ticker)
            continue;
        if (orders[i].type == BUY)
        {
            int qty = orders[i].quantity.load();
            if (qty > 0 && orders[i].price > bestBuyPrice)
            {
                bestBuyPrice = orders[i].price;
                bestBuy = i;
            }
        }
        else
        {
            int qty = orders[i].quantity.load();
            if (qty > 0 && orders[i].price < bestSellPrice)
            {
                bestSellPrice = orders[i].price;
                bestSell = i;
            }
        }
    }
    return {bestBuy, bestSell};
}
//matchOrder function is used to match the orders in the order book
void matchOrder(int ticker)
{
    int currentCount = orderCount.load();
    int bestBuy = -1, bestSell = -1;
    double bestBuyPrice = -1.0, bestSellPrice = 1e9;

    for (int i = 0; i < currentCount; i++)
    {
        if (!orders[i].active.load() || orders[i].ticker != ticker)
            continue;
        if (orders[i].type == BUY)
        {
            int qty = orders[i].quantity.load();
            if (qty > 0 && orders[i].price > bestBuyPrice)
            {
                bestBuyPrice = orders[i].price;
                bestBuy = i;
            }
        }
        else
        {
            int qty = orders[i].quantity.load();
            if (qty > 0 && orders[i].price < bestSellPrice)
            {
                bestSellPrice = orders[i].price;
                bestSell = i;
            }
        }
    }

    while (bestBuy != -1 && bestSell != -1 && bestBuyPrice >= bestSellPrice)
    {
        Order &buyOrder = orders[bestBuy];
        Order &sellOrder = orders[bestSell];

        int buyQty = buyOrder.quantity.load();
        int sellQty = sellOrder.quantity.load();
        if (buyQty <= 0 || sellQty <= 0)
            break;
        int tradeQty = (buyQty < sellQty) ? buyQty : sellQty;

        buyOrder.quantity.fetch_sub(tradeQty);
        sellOrder.quantity.fetch_sub(tradeQty);

        {
            std::lock_guard<std::mutex> lock(print_mutex);
            std::cout << "Matched Ticker " << ticker
                      << ": Trade Qty = " << tradeQty
                      << " at Price = " << sellOrder.price << "\n";
        }

        if (buyOrder.quantity.load() <= 0)
            buyOrder.active.store(false);
        if (sellOrder.quantity.load() <= 0)
            sellOrder.active.store(false);
// rescanning the order book to find the best buy and sell orders for next match
        std::tie(bestBuy, bestSell) = findBestOrders(ticker, bestBuyPrice, bestSellPrice);
    }
}
//addOrder function is used to add the orders to the order book
void addOrder(OrderType type, int ticker, int quantity, double price)
{
    int idx = orderCount.fetch_add(1);
    if (idx >= MAX_ORDERS)
    {
        std::lock_guard<std::mutex> lock(print_mutex);
        std::cout << "Order book full. Cannot add new order.\n";
        return;
    }
    orders[idx].orderId = globalOrderId.fetch_add(1);
    orders[idx].type = type;
    orders[idx].ticker = ticker;
    orders[idx].quantity.store(quantity);
    orders[idx].price = price;
    orders[idx].active.store(true);

    {
        std::lock_guard<std::mutex> lock(print_mutex);
        std::cout << "Added Order: ID " << orders[idx].orderId
                  << ", " << (type == BUY ? "BUY" : "SELL")
                  << ", Ticker " << ticker
                  << ", Qty " << quantity
                  << ", Price " << price << "\n";
    }

    matchOrder(ticker);
}

void simulateOrders(int threadId)
{
    std::default_random_engine generator(
        threadId + std::chrono::system_clock::now().time_since_epoch().count());
    std::uniform_int_distribution<int> orderTypeDist(0, 1);
    std::uniform_int_distribution<int> tickerDist(0, MAX_TICKERS - 1);
    std::uniform_int_distribution<int> quantityDist(1, 100);
    std::uniform_real_distribution<double> priceDist(10.0, 1000.0);

    for (int i = 0; i < 500; i++)
    {
        OrderType type = (orderTypeDist(generator) == 0) ? BUY : SELL;
        int ticker = tickerDist(generator);
        int quantity = quantityDist(generator);
        double price = priceDist(generator);
        addOrder(type, ticker, quantity, price);
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}

int main()
{
    const int numThreads = 6;
    std::thread threads[numThreads];

    for (int i = 0; i < numThreads; i++)
    {
        threads[i] = std::thread(simulateOrders, i);
    }
    for (int i = 0; i < numThreads; i++)
    {
        threads[i].join();
    }
    return 0;
}
