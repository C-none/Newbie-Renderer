#if defined(_MSC_VER)
#pragma warning(push, 0)
#endif

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>
#undef STB_IMAGE_IMPLEMENTATION

#if defined(_MSC_VER)
#pragma warning(pop)
#endif

#define VMA_IMPLEMENTATION
#include <vk_mem_alloc.h>

namespace flecs
{
namespace _
{
struct placement_new_tag_t
{
};

extern const placement_new_tag_t placement_new_tag{};
} // namespace _
} // namespace flecs
