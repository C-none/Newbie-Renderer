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

} // namespace nr