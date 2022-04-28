#include <nanobench.h>

#include <iostream>
#include <sstream>
#include <cinttypes> // strtoumax
#include <charconv> // from_chars


size_t parse_string_stream(const std::string& str) {
    std::stringstream sstream(str);

    sstream >> std::ws;
    if (sstream.eof()) {
        return 0;
    }

    size_t row, col;
    sstream >> row >> col;

    if (!sstream) {
        return -1;
    }

    return row + col;
}


size_t parse_sscanf(const std::string& str) {
    size_t row, col;
    auto res = sscanf(str.c_str(), "%lu %lu", &row, &col);

    if (res == EOF) {
        return 0;
    }
    
    if (res < 2) {
        return -1;
    }

    return row + col;
}


size_t parse_manual(const std::string& str) {
    size_t i = 0;

    while (i < str.size() && isspace(str[i])) {
        ++i;
    }

    if (i >= str.size()) {
        return 0;
    }

    if (!isdigit(str[i])) {
        return -1;
    }

    size_t row = 0;
    while(i < str.size() && isdigit(str[i])) {
        row = row*10 + (str[i] - '0');
        ++i;
    }

    while (i < str.size() && isspace(str[i])) {
        ++i;
    }

    if (i >= str.size() || !isdigit(str[i])) {
        return -1;
    }

    size_t col = 0;
    while(i < str.size() && isdigit(str[i])) {
        col = col*10 + (str[i] - '0');
        ++i;
    }

    return row + col;
}


size_t parse_strtoull(const std::string& str) {
    size_t i = 0;

    while (i < str.size() && isspace(str[i])) {
        ++i;
    }

    if (i >= str.size()) {
        return 0;
    }

    char* end = nullptr;
    size_t row = std::strtoull(&str[i], &end, 10);
    if (&str[i] == end) {
        return -1;
    }

    char* next_end = nullptr;
    size_t col = std::strtoull(end, &next_end, 10);
    if (next_end == end) {
        return -1;
    }

    return row + col;
}


size_t parse_strtoumax(const std::string& str) {
    size_t i = 0;

    while (i < str.size() && isspace(str[i])) {
        ++i;
    }

    if (i >= str.size()) {
        return 0;
    }

    char* end = nullptr;
    size_t row = std::strtoumax(&str[i], &end, 10);
    if (&str[i] == end) {
        return -1;
    }

    char* next_end = nullptr;
    size_t col = std::strtoumax(end, &next_end, 10);
    if (next_end == end) {
        return -1;
    }

    return row + col;
}


size_t parse_from_chars(const std::string& str) {
    size_t i = 0;

    while (i < str.size() && isspace(str[i])) {
        ++i;
    }
    if (i >= str.size()) {
        return 0;
    }

    size_t row;
    auto res = std::from_chars(&str[i], str.data() + str.size(), row);
    if (res.ec == std::errc::invalid_argument) {
        return -1;
    }

    i = res.ptr - str.data();

    while (i < str.size() && isspace(str[i])) {
        ++i;
    }
    if (i >= str.size()) {
        return -1;
    }

    size_t col;
    res = std::from_chars(&str[i], str.data() + str.size(), col);
    if (res.ec == std::errc::invalid_argument) {
        return -1;
    }

    return row + col;
}


#define CHECK_CASE(expr) \
    if (true) {          \
        if (!(expr)) {   \
            return;      \
        }                \
    } else (void)0


template<typename Func>
void test_case(Func func, const std::string& test_name) {

    auto test_single_input = [&](const std::string& input, size_t expected) {
        size_t actual = func(input);
        if (actual != expected) {
            std::cerr << "TEST FAILED: " << test_name << "\n";
            std::cerr << "    On input: '" << input << "'\n";
            std::cerr << "    Got:      " << actual << "\n";
            std::cerr << "    Expected: " << expected << "\n";
            return false;
        }
        return true;
    };

    CHECK_CASE( test_single_input("25 3568 1.00256", 3593) );
    CHECK_CASE( test_single_input("25 3568 ", 3593) );
    CHECK_CASE( test_single_input("  25  3568  1.00256   ", 3593) );
    CHECK_CASE( test_single_input("", 0) );
    CHECK_CASE( test_single_input("    \t\t   \n", 0) );
    CHECK_CASE( test_single_input(" k  11100 36 ", size_t(-1)) );
    CHECK_CASE( test_single_input(" 11100 ? 36 ", size_t(-1)) );
    CHECK_CASE( test_single_input(" 11100 36 ?", 11136) );

    std::cerr << "TEST PASSED: " << test_name << "\n";
}


int main() {
    test_case(parse_string_stream, "string_stream");
    test_case(parse_sscanf, "sscanf");
    test_case(parse_manual, "custom");
    test_case(parse_strtoull, "strtoull");
    test_case(parse_strtoumax, "strtoumax");
    test_case(parse_from_chars, "from_chars");
    std::cerr << "\n";


    std::string test_str = "2365 15985 25.01564 ";

    ankerl::nanobench::Bench()
        .run("stringstream", [&] {
            auto res = parse_string_stream(test_str);    
            ankerl::nanobench::doNotOptimizeAway(res);
        })
        .run("sscanf", [&] {
            auto res = parse_sscanf(test_str);    
            ankerl::nanobench::doNotOptimizeAway(res);
        })
        .run("manual", [&] {
            auto res = parse_manual(test_str);    
            ankerl::nanobench::doNotOptimizeAway(res);
        })
        .run("strtoull", [&] {
            auto res = parse_strtoull(test_str);    
            ankerl::nanobench::doNotOptimizeAway(res);
        })
        .run("strtoumax", [&] {
            auto res = parse_strtoumax(test_str);    
            ankerl::nanobench::doNotOptimizeAway(res);
        })
        .run("from_chars", [&] {
            auto res = parse_from_chars(test_str);    
            ankerl::nanobench::doNotOptimizeAway(res);
        });
}
