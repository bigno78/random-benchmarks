# Integer parsing

In this benchmark I try to figure out which way to extract integers from a string is the fastest. This came up in a side-project I am working on so the benchmark is done on a somewhat real world situation. If you don't care about the context feel free to skip to the benchmarked [implementations](#the-implementations) or directly to the [results](#results).

## The context

Currently I am making a command-line application where I need to parse files containing sparse matrices in matrix market format. Each entry of the matrix is described by a single line in the form:

```
<row-index> <col-index> <float-value>
```

In my application I care only about the indices so I don't need to parse the floating point value and I can just ignore it. Additionally, there can be empty lines (containing only whitespace) anywhere in the file.

Importantly, there is a whole bunch of these lines - up to about 2 million. Originally, I was using `stringstream` for the parsing but the application was running pretty slow and profiling revealed that about 99 % of the time was spent doing whatever `stringstream` is doing. So it appears I need something better.

## The implementations

We want a function that takes a string and
 - extracts two whitespace separated *positive* integers from the beginning of the string and ignores the rest
 - reports an error if they are not there or if they would not fit into a 64-bit unsigned integer
 - if the line contains only whitespace it reports a different kind of error

To realize these requirements all the implementations have the following signature:

```
Result parse(const std::string& str);
```

Where the return type is

```cpp
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
```

So without further ado let's introduce the contenders.

### stringstream

This is a pretty straightforward solution and there aren't really any surprises. The only tricky thing was figuring out when to use `stream.eof()`, `stream.bad()`, `stream.fail()` or the implicit conversion to `bool`.

```cpp
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
```

### sscanf

Again, this is a pretty simple solution. Apart from one slight issue. There does not seem to be a standard way to check whether an overflow happened (the number in the string didn't fit into `size_t`). The c standard simply states that it is undefined behavior.

> the result of the conversion is placed in the object pointed to by the first argument following the format argument that has not already received a conversion result. If this object does not have an appropriate type, *or if the result of the conversion cannot be represented in the object, the behavior is undefined.*

However, it seems that all the implementations of the standard library I encountered set `errno` to `ERANGE` when overflow would happen, since this code worked for me both on windows and linux.

```cpp
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
```

### strtoull

Starting with this implementation I won't show the full code since it gets quite long and repetitive.

With `strtoull` there is no way to distinguish between reading the whole string without finding a non-whitespace character
and failing the conversion because the first character encountered wasn't legal for a number. So before calling `strtoull` we need to manually skip all the whitespace and check if we reached the end.

```cpp
while (i < str.size() && isspace(str[i])) {
    ++i;
}
if (i >= str.size()) {
    return { 0, 0, ErrCode::empty };
}
```

Then we can finally use the function itself.

```cpp
char* end = nullptr;
res.row = std::strtoull(&str[i], &end, 10);
if (&str[i] == end || (res.row == ULLONG_MAX && errno == ERANGE)) {
    return  { 0, 0, ErrCode::error };
}
```

The code to read the other number is basically a copy-paste of this except that we pass `end` as the first argument since it points to the first character past the first number.

One important disclaimer here is that I am not sure I implemented the error checking correctly. It is possible that I missed some cases or that I do something unnecessary. This is one disadvantage of `strtoull`. The error handling is quite convoluted and confusing.

With that said, it passes my simple tests and I don't think it would change the benchmark results much if it wasn't completely correct.

### from_chars

This function is probably the most promising since [*cppreference*](https://en.cppreference.com/w/cpp/utility/from_chars) says:

> This is intended to allow the fastest possible implementation that is useful in common high-throughput contexts such as text-based interchange (JSON or XML).

Similarly as with `strtoull` we have to check for the empty string ourselves, but this time it has one additional reason - `from_chars` doesn't skip leading whitespace. This means that we need to skip whitespace manually even before reading the second number.

Then call to `from_chars` itself is pretty simple:

```cpp
auto res = std::from_chars(&str[i], str.data() + str.size(), result.row);
if (res.ec == std::errc::invalid_argument || res.ec == std::errc::result_out_of_range) {
    return { 0, 0, ErrCode::error };
}
```

### custom

And finally, we get to the last contender - my own hand-written implementation. I wasn't trying to do anything too fancy. For the conversion of one number I just used this simple loop:

```cpp
size_t res = 0;
while(i < str.size() && isdigit(str[i])) {
    res = 10*res + (str[i] - '0');
    ++i;
}
```

Well, maybe not this exact loop, since it is missing overflow handling. And surprise surprise, that turned out to be quite tricky.

Originally, I did something like this:

```cpp
size_t new_res = 10*res + (str[i] - '0');
if (res > new_res) {
    // overflow!!!
}
```

This passed my simple tests and I was proud of how quickly I came up with a working solution. However, a bit later when I was investigating the performance difference between this implementation and `from_chars` I took a peek at the microsoft c++ standard library and their overflow handling was quite different. That made me go back to my own solution and turns out it doesn't work at all.

If `res` is a reasonably large value and we multiply it by 10 the result most likely wraps around multiple times and it is basically lottery where it lands - it could be larger then before or smaller the before, but no one knows which. So I needed something better. Thankfully, I already had something better - the standard library way of handling overflows. Which looks something like this:

```cpp
constexpr size_t max_val = size_t(-1);
constexpr size_t risky_val = max_val/10;
constexpr size_t max_digit = max_val % 10;
    
// ...

size_t d = str[i] - '0';
if (res < risky_val || (res == risky_val && d <= max_digit)) {
    res = res*10 + d;
} else {
    // overflow!!!!
}
```

Here, we first precalculate some useful values - `risky_val` is the largest number we can safely multiply by 10 without an overflow and `max_digit` is the largest digit we can safely add after this multiplication. So later, when processing a new digit, if the value accumulated so far is less then `risky_val` we know we are safe. If it is equal to `risky_val` we must be a bit cautious and check that the next digit doesn't exceed `max_digit`. In all other cases we have an overflow. It is as simple as that.

## Results

Finally, we get to the most important part, the results. I built and ran the benchmark both on Windows 10 and Ubuntu 20.04.3 on my laptop with Intel Core i7-7700HQ 2.80GHz cpu.

### Linux

The benchmark was built with `GCC 9.4.0`. Furthermore, I used [pyperf](https://github.com/psf/pyperf) to tune the system parameters with  `pyperf system tune` to make the benchmark more stable (as was suggested by the `nanobench` output).

When I built the benchmark using `-O2` I got the following results.

|               ns/op |                op/s |    err% |     total | benchmark
|--------------------:|--------------------:|--------:|----------:|:----------
|              595.64 |        1,678,855.72 |    0.5% |      0.01 | `stringstream`
|              176.13 |        5,677,539.35 |    1.5% |      0.01 | `sscanf`
|               57.18 |       17,489,920.67 |    1.3% |      0.01 | `strtoull`
|               46.57 |       21,474,598.39 |    0.4% |      0.01 | `from_chars`
|               31.93 |       31,323,390.26 |    1.3% |      0.01 | `custom`

We can see that when *cppreference* says that `from_chars` is intended to be "the fastest possible implementation" it means it, since it outperforms all the other standard library solutions. However, what came as a surprise to me is that the custom implementation is even faster. That was quite suspicious and it led me to discover the overflow bug. But even after the fix, it is still faster.

Interestingly, when built using `-O3` the results are a bit different.


|               ns/op |                op/s |    err% |     total | benchmark
|--------------------:|--------------------:|--------:|----------:|:----------
|              589.78 |        1,695,558.48 |    0.1% |      0.01 | `stringstream`
|              172.84 |        5,785,821.23 |    0.2% |      0.01 | `sscanf`
|               58.55 |       17,078,590.75 |    1.0% |      0.01 | `strtoull`
|               27.61 |       36,217,604.04 |    0.0% |      0.01 | `from_chars`
|               30.09 |       33,238,469.22 |    0.0% |      0.01 | `custom`

Now `from_chars` is slightly faster! I guess its main loop is easier for the compiler to unroll.

### Windows

The benchmark was built using `MSVC 19.31.31104.0`. However, on windows [pyperf](https://github.com/psf/pyperf) can't help you, so to improve the stability of the benchmark I at least set the minimum number of iterations to 200 000. And here are the results.

|               ns/op |                op/s |    err% |     total | benchmark
|--------------------:|--------------------:|--------:|----------:|:----------
|            1,145.31 |          873,125.13 |    1.1% |      2.75 | `stringstream`
|              243.68 |        4,103,770.64 |    0.7% |      0.59 | `sscanf`
|               85.78 |       11,657,458.83 |    1.3% |      0.21 | `strtoull`
|               55.32 |       18,075,662.85 |    1.3% |      0.13 | `from_chars`
|               53.94 |       18,538,602.00 |    1.4% |      0.13 | `custom`

One curious thing is that everything is slower than on linux. I don't really have an idea why that is. The other noteworthy thing is that now the custom implementation and `from_chars` are basically the same.

### Conclusions

All in all, it seems that the custom solution is the best. On linux with `-O3` it was slightly slower then `from_chars` but the margin was quite low and in all other cases it was faster or equal.

If I had to say why that is, my first guess would be I overlooked something and my implementation is not correct. My second guess would be that `from_chars` has to handle more cases that I do. Sure, the requirements the standard places on it are quite cutdown compared to the other standard library facilities - it doesn't need to care about the locale, doesn't throw, doesn't need to handle the `+` prefix and `-` only for signed types. But still, it needs to work for all integer types and all bases. That would be a lot of overloads, so it all has to be in a single or just couple of implementations. And that is my advantage. I didn't need to care about that and focused only on my special case.

With that said, for production code I would go with `from_chars` since I trust the standard library implementors a bit more then myself. However, for my side-project I will live on the edge and happily use my own custom mess of a code. 
