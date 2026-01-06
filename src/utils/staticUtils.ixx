module;
export module nr.utils:staticUtils;

export namespace nr
{
consteval bool isDebugMode()
{
#if defined(NDEBUG)
    return false;
#else
    return true;
#endif
}

[[nodiscard]] consteval unsigned VK_MAKE_API_VERSION(unsigned variant, unsigned major, unsigned minor, unsigned patch)
{
    // substitue the macros defined in vk_platform.h
    return ((((variant)) << 29U) | (((major)) << 22U) | (((minor)) << 12U) | ((patch)));
}


} // namespace nr