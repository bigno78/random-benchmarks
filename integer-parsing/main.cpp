#include <nanobench.h>

#include <iostream>
#include <sstream>
#include <charconv> // from_chars
#include <random>
#include <climits>


// On windows size_t is long long unsigned int and on linux it is long unsigned int.
#ifdef _WIN32
    #define FMT "%llu %llu"
#else
    #define FMT "%lu %lu"
#endif


enum class ErrCode {
    success,
    empty,
    error
};

struct Result {
    size_t row;
    size_t col;
    ErrCode err;
};


////////////////////////////////////////////////////////////////////////////////////
//                                                                                // 
//                          BENCHMARKED IMPLEMENTATIONS                           //
//                                                                                //
////////////////////////////////////////////////////////////////////////////////////

Result parse_string_stream(const std::string& str) {
    Result result;
    std::stringstream sstream(str);

    sstream >> std::ws;
    
    if (sstream.eof()) {
        result.err = ErrCode::empty;
        return result;
    }

    sstream >> result.row >> result.col;
    
    if (!sstream) {
        result.err = ErrCode::error;
        return result;
    }
    
    result.err = ErrCode::success;

    return result;
}


Result parse_sscanf(const std::string& str) {
    Result res;

    // clear errno so we don't get leftover ERANGE from other calls
    errno = 0;

    auto count = sscanf(str.c_str(), FMT, &res.row, &res.col);

    if (count == EOF) {
        res.err = ErrCode::empty;
        return res;
    }
    
    if (count < 2 || errno == ERANGE) {
        res.err = ErrCode::error;
        return res;
    }

    res.err = ErrCode::success;
    return res;
}


Result parse_strtoull(const std::string& str) {
    Result res;
    size_t i = 0;

    while (i < str.size() && isspace(str[i])) {
        ++i;
    }

    if (i >= str.size()) {
        res.err = ErrCode::empty;
        return res;
    }

    // clear errno so we don't get leftover ERANGE from other calls
    errno = 0;

    char* end = nullptr;
    res.row = std::strtoull(&str[i], &end, 10);
    if (&str[i] == end || (res.row == ULLONG_MAX && errno == ERANGE)) {
        res.err = ErrCode::error;
        return res;
    }

    char* next_end = nullptr;
    res.col = std::strtoull(end, &next_end, 10);
    if (next_end == end || (res.col == ULLONG_MAX && errno == ERANGE)) {
        res.err = ErrCode::error;
        return res;
    }

    res.err = ErrCode::success;
    return res;
}


Result parse_from_chars(const std::string& str) {
    Result result;
    size_t i = 0;

    while (i < str.size() && isspace(str[i])) {
        ++i;
    }
    if (i >= str.size()) {
        result.err = ErrCode::empty;
        return result;
    }

    auto res = std::from_chars(&str[i], str.data() + str.size(), result.row);
    if (res.ec == std::errc::invalid_argument || res.ec == std::errc::result_out_of_range) {
        result.err = ErrCode::error;
        return result;
    }

    i = res.ptr - str.data();

    while (i < str.size() && isspace(str[i])) {
        ++i;
    }
    if (i >= str.size()) {
        result.err = ErrCode::error;
        return result;
    }

    res = std::from_chars(&str[i], str.data() + str.size(), result.col);
    if (res.ec == std::errc::invalid_argument || res.ec == std::errc::result_out_of_range) {
        result.err = ErrCode::error;
        return result;
    }

    result.err = ErrCode::success;
    return result;
}

bool parse_single(const std::string& str, size_t i, size_t& end, size_t& val) {
    // DISCLAIMER!!!!!
    // the implementation of overflow handling is taken from the Microsoft C++ Standard Library
    
    constexpr size_t max_val = size_t(-1);
    constexpr size_t risky_val = max_val/10;
    constexpr size_t max_digit = max_val % 10;
    
    size_t res = 0;
    while(i < str.size() && isdigit(str[i])) {
        size_t d = str[i] - '0';
        if (res < risky_val || (res == risky_val && d <= max_digit)) {
            res = res*10 + d;
        } else {
            return false;
        }
        ++i;
    }

    end = i;
    val = res;

    return true;
}

Result parse_custom(const std::string& str) {
    Result res;
    size_t i = 0;

    while (i < str.size() && isspace(str[i])) {
        ++i;
    }

    if (i >= str.size()) {
        res.err = ErrCode::empty;
        return res;
    }

    if (!isdigit(str[i])) {
        res.err = ErrCode::error;
        return res;
    }

    size_t end;
    if (!parse_single(str, i, end, res.row)) {
        res.err = ErrCode::error;
        return res;
    }
    i = end;

    while (i < str.size() && isspace(str[i])) {
        ++i;
    }

    if (i >= str.size() || !isdigit(str[i])) {
        res.err = ErrCode::error;
        return res;
    }

    if (!parse_single(str, i, end, res.col)) {
        res.err = ErrCode::error;
        return res;
    }

    res.err = ErrCode::success;
    return res;
}


////////////////////////////////////////////////////////////////////////////////////
//                                                                                // 
//                                TEST STUFF                                      //
//                                                                                //
////////////////////////////////////////////////////////////////////////////////////

bool operator==(Result a, Result b) {
    if (a.err != b.err) {
        return false;
    }

    if (a.err != ErrCode::success) {
        return true;
    }

    return a.col == b.col && a.row == b.row;
}

bool operator!=(Result a, Result b) {
    return !(a == b);
}

std::ostream& operator<<(std::ostream& out, Result res) {
    switch (res.err) {
        case ErrCode::empty:
            out << "empty string";
            break;
        case ErrCode::error:
            out << "error";
            break;
        case ErrCode::success:
            out << res.row << " " << res.col;
            break;
        default:
            out << "unknown";
    }
    return out;
}

#define EARLY_RETURN(expr) \
    if (true) {          \
        if (!(expr)) {   \
            return;      \
        }                \
    } else (void)0


std::mt19937 mt;

template<typename Func>
void test_overflow(Func func, const std::string& test_name) {
    size_t max_val = size_t(-1);
    size_t critical_val = max_val/10;

    std::uniform_int_distribution<size_t> dist(critical_val + 1, max_val);

    for (size_t i = 0; i < 100'000; ++i) {
        std::string str = std::to_string(dist(mt)) + "0 10";
        auto res = func(str);
        if (res.err != ErrCode::error) {
            std::cout << "OVERFLOW TEST FAILED: " << test_name << "\n";
            std::cout << "    Input: " << str << "\n";
            return;
        }
    }
}

template<typename Func>
void test_parse_func(Func func, const std::string& test_name) {

    auto test_single_input = [&] (const std::string& input, Result expected) {
        Result actual = func(input);
        if (actual != expected) {
            std::cerr << "TEST FAILED: " << test_name << "\n";
            std::cerr << "    On input: '" << input << "'\n";
            std::cerr << "    Got:      " << actual << "\n";
            std::cerr << "    Expected: " << expected << "\n";
            return false;
        }
        return true;
    };

    EARLY_RETURN( test_single_input("252165 1682156", { 252165, 1682156, ErrCode::success }) );
    EARLY_RETURN( test_single_input("252165 1682156 1.00256", { 252165, 1682156, ErrCode::success }) );
    EARLY_RETURN( test_single_input("252165 1682156 ???", { 252165, 1682156, ErrCode::success }) );
    EARLY_RETURN( test_single_input(" \t 252165 \t 1682156 \t ", { 252165, 1682156, ErrCode::success }) );

    EARLY_RETURN( test_single_input("", { 0, 0, ErrCode::empty }) );
    EARLY_RETURN( test_single_input("    \t\t   \n", { 0, 0, ErrCode::empty }) );

    EARLY_RETURN( test_single_input(" k  11100 36 ", { 0, 0, ErrCode::error }) );
    EARLY_RETURN( test_single_input(" 11100 ? 36 ", { 0, 0, ErrCode::error }) );
    EARLY_RETURN( test_single_input("18446744073709551616 36", { 0, 0, ErrCode::error }) );
    EARLY_RETURN( test_single_input("26 18446744073709551616", { 0, 0, ErrCode::error }) );
    EARLY_RETURN( test_single_input("26 184467440737095516111", { 0, 0, ErrCode::error }) );
    EARLY_RETURN( test_single_input("26 53197085087656854960", { 0, 0, ErrCode::error }) );

    std::cerr << "TEST PASSED: " << test_name << "\n";
}


////////////////////////////////////////////////////////////////////////////////////
//                                                                                // 
//                                    MAIN                                        //
//                                                                                //
////////////////////////////////////////////////////////////////////////////////////

int main() {
    test_parse_func(parse_string_stream, "stringstream");
    test_parse_func(parse_custom, "custom");
    test_parse_func(parse_sscanf, "sscanf");
    test_parse_func(parse_strtoull, "strtoull");
    test_parse_func(parse_from_chars, "from_chars");

    // test overflows in a bit more detailed way for my implementation
    test_overflow(parse_custom, "custom");

    std::cerr << "\n";

    std::string test_str = "236514 159854 25.01564 ";

    ankerl::nanobench::Bench()
        .run("stringstream", [&] {
            auto res = parse_string_stream(test_str);    
            ankerl::nanobench::doNotOptimizeAway(res);
        })
        .run("sscanf", [&] {
            auto res = parse_sscanf(test_str);    
            ankerl::nanobench::doNotOptimizeAway(res);
        })
        .run("strtoull", [&] {
            auto res = parse_strtoull(test_str);    
            ankerl::nanobench::doNotOptimizeAway(res);
        })
        .run("from_chars", [&] {
            auto res = parse_from_chars(test_str);    
            ankerl::nanobench::doNotOptimizeAway(res);
        })
        .run("custom", [&] {
            auto res = parse_custom(test_str);    
            ankerl::nanobench::doNotOptimizeAway(res);
        });
}
