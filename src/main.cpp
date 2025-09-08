#include <vulkan/vulkan.hpp>

import hello;
import std;

int main()
{
    using namespace std;
    hello::say("Hello, World!");
    // cout<<"Hello, World!";
    char p1[] = "abcdc";
    const char *p2 = "abcdc";
    print("{} {} {} {}", sizeof(p1), strlen(p1), sizeof(p2), strlen(p2));

    return 0;
}