# Integer parsing

In this benchmark I try to figure out which way to extract integers from a string is the fastest. This came up in a side-project I am working on so the benchmark is done on a somewhat real world situation. If you don't care about the context feel free to skip to the benchmarked [implementations](#the-implementations) or directly to the [results](#results).

## The context

Currently I am making a command-line application where I need to parse files containing sparse matrices in matrix market format. Each entry of the matrix is described by a single line in the form:

```
<row-index> <col-index> <float-value>
```

In my application I care only about the indices so I don't need to parse the floating point value and I can just ignore it. I don't even check it is there and it is in the correct format. Additionally, there can be empty lines (containing only whitespace) anywhere in the file.

What is important, there is a whole bunch of these lines - up to about 2 million. Originally, I was using `stringstream` for the parsing but the application was running pretty slow and profiling revealed that about 99 % of the time was spent doing whatever `stringstream` is doing. So it appears I need something better.

## The implementations

We want a function that takes a string and
 - extracts two whitespace separated *positive* integers from the beginning of the string and ignores the rest
 - reports an error if they are not there or if they would not fit into a 64-bit unsigned integer
 - however, if the line contains only whitespace it doesn't report an error

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

Again, this is a pretty simple solution. Apart from one slight issue. There does not seem to be a standard way to check whether an overflow happened (the number in the string didn't fit into `size_t`). The c standard simply states that it is undefined behavior:

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
and failing the conversion itself. So before calling `strtoull` we need to manually skip all the whitespace and check if we reached the end.

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

This function is probably the most promising since *cppreference* says:

> This is intended to allow the fastest possible implementation that is useful in common high-throughput contexts such as text-based interchange (JSON or XML).

Similarly as with `strtoull` we have to check for the empty string ourselves, but this time it has one additional reason - `from_chars` doesn't skip leading whitespace. This means that we need to skip whitespace manually even before reading the scond number.

The call to `from_chars` itself is pretty simple:

```cpp
auto res = std::from_chars(&str[i], str.data() + str.size(), result.row);
if (res.ec == std::errc::invalid_argument || res.ec == std::errc::result_out_of_range) {
    return { 0, 0, ErrCode::error };
}
```

### custom

And finally, we get to the last contender - my own hand-written implementation. I wasn't trying to do anything too fancy. For the conversion of a single number I just used this simple loop:

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

It passed my simple tests and I was proud of how quickly I came up with a working solution. Thankfully, a bit later when I was investigating the performance difference between this and `from_chars` I took a peek at the microsoft c++ standard library. And their overflow handling was quite different. That made me take a more closer look at my own solution and turns out it doesn't work at all.


## Results

|               ns/op |                op/s |    err% |     total | benchmark
|--------------------:|--------------------:|--------:|----------:|:----------
|              594.86 |        1,681,075.26 |    0.2% |      0.01 | `stringstream`
|              176.32 |        5,671,481.90 |    0.3% |      0.01 | `sscanf`
|               56.92 |       17,568,313.80 |    0.6% |      0.01 | `strtoull`
|               46.36 |       21,570,059.64 |    0.1% |      0.01 | `from_chars`
|               31.86 |       31,386,324.55 |    0.2% |      0.01 | `custom`
