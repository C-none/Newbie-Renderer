import std;
import nr.utils;

int main()
{
    auto invocationCount = std::uint32_t{0};
    nr::nrAssert(false, [&] {
        ++invocationCount;
        return std::format("lazy assertion failure probe invocation={}", invocationCount);
    });
    return 0;
}
