import std;
// import hello;
//import dependency;
import nr.rhi;
import nr.utils;

int main()
{
    using namespace std;
    // hello::helloSlang();
     nr::rhi::rhiTest();
     char p1[] = "abcdc";
     const char *p2 = "abcdc";
     print(cout, "{} {} {} {}\n", sizeof(p1), strlen(p1), sizeof(p2), strlen(p2));
    return 0;
}