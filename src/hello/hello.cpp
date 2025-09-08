module hello;

import std;

namespace hello {
    void say(const std::string& message)
    {
        auto foo = [](int x) { return x + 1; };
        std::cout << message << "26" << std::endl;
    }
}
