module hello;

import std;

namespace hello
{
void helloSlang()
{
    auto foo = [](int x) { return x + 1; };
    std::print("Hello, Slang! {}\n", foo(1));
}
} // namespace hello
