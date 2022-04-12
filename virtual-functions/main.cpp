#include <iostream>
#include <random>
#include <chrono>
#include <vector>


struct Consumer {
    virtual void consume(size_t x) = 0;
    virtual void print_results() const = 0;
};

struct HistogramConsumer : Consumer {
    HistogramConsumer(size_t from, size_t to, size_t bin_size)
        : from(from)
        , to(to)
        , bin_size(bin_size)
        , bins((to - from + 1 + bin_size - 1)/bin_size) { }

    void consume(size_t x) override {
        bins[(x - from)/bin_size]++;
    }

    void print_results() const override {
        std::cout << "HISTOGRAM:\n";
        for (size_t i = 0; i < bins.size(); ++i) {
            std::cout << from + i*bin_size << " - " << from + (i + 1)*bin_size - 1 << ": ";
            std::cout << bins[i] << "\n";
        }
    }

private:
    size_t from;
    size_t to;
    size_t bin_size;

    std::vector<size_t> bins;
};

struct AverageConsumer : Consumer {

    void consume(size_t x) override {
        count++;
        sum += x;
    }

    void print_results() const override {
        std::cout << "AVERAGE: " << ((float)sum)/count << "\n";
    }

private:
    size_t count = 0;
    size_t sum = 0;
};

template<typename TimeUnits>
auto direct(const std::vector<size_t>& data) {
    HistogramConsumer hist(1, 100, 13);
    AverageConsumer avg;

    auto start = std::chrono::steady_clock::now();
    for (auto x : data) {
        hist.consume(x);
        avg.consume(x);
    }
    auto end = std::chrono::steady_clock::now();

    hist.print_results();
    avg.print_results();

    auto ms = std::chrono::duration_cast<TimeUnits>(end - start).count();
    return ms;
}


template<typename TimeUnits>
auto virtual_thing(const std::vector<size_t>& data, Consumer& hist, Consumer& avg) {
    auto start = std::chrono::steady_clock::now();
    for (auto x : data) {
        hist.consume(x);
        avg.consume(x);
    }
    auto end = std::chrono::steady_clock::now();

    hist.print_results();
    avg.print_results();

    auto ms = std::chrono::duration_cast<TimeUnits>(end - start).count();
    return ms;
}

int main() {
    const int n = 1*(1u << 20);

    std::mt19937 mt;
    std::uniform_int_distribution<size_t> dist(1, 100);
    
    std::vector<size_t> data(n);
    for (size_t i = 0; i < data.size(); ++i) {
        data[i] = dist(mt);
    }

    using TimeUnits = std::chrono::microseconds;

    auto res1 = direct<TimeUnits>(data);

    HistogramConsumer hist(1, 100, 13);
    AverageConsumer avg;
    auto res2 = virtual_thing<TimeUnits>(data, hist, avg);

    std::cout << "Non-virtual: " << res1 << "\n";
    std::cout << "virtual:     " << res2 << "\n";
}
