module;
#include <glm/glm.hpp>
export module nr.utils:math;
import std;
export namespace nr
{
    class Extent: public glm::uvec2
{
    public:
        Extent() : glm::uvec2(0, 0) {}
        Extent(unsigned w, unsigned h) : glm::uvec2(w, h) {}
        Extent(glm::uvec2 const &v) : glm::uvec2(v) {}
        unsigned width() const { return x; }
        unsigned height() const { return y; }
        unsigned area() const { return x * y; }
        bool isEmpty() const { return x == 0 || y == 0; }
        Extent operator+(const Extent &other) const { return Extent(x + other.x, y + other.y); }
};
}