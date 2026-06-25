export module nr.utils:staticUtils;
export import :staticUtilsConstants;
import std;

export namespace nr
{
[[nodiscard]] consteval unsigned VK_MAKE_API_VERSION(unsigned variant, unsigned major, unsigned minor, unsigned patch)
{
    return ((((variant)) << 29U) | (((major)) << 22U) | (((minor)) << 12U) | ((patch)));
}

} // namespace nr
