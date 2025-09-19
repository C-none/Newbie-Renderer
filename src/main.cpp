#include <slang.h>

import hello;
import nr.rhi;
import std;

int main()
{
    using namespace std;
    hello::helloSlang();
    nr::rhi::rhiTest();
    // cout<<"Hello, World!";
    char p1[] = "abcdc";
    const char *p2 = "abcdc";
    print("{} {} {} {}", sizeof(p1), strlen(p1), sizeof(p2), strlen(p2));

    return 0;
}