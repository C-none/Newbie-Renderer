import std;
import nr.renderer;
import nr.test;

namespace
{
static_assert(!std::copy_constructible<nr::renderer::Renderer>);
static_assert(!std::is_copy_assignable_v<nr::renderer::Renderer>);
static_assert(!std::move_constructible<nr::renderer::Renderer>);
static_assert(!std::is_move_assignable_v<nr::renderer::Renderer>);
static_assert(std::is_nothrow_destructible_v<nr::renderer::Renderer>);

const nr::test::CaseRegistrar defaultShutdownCase{
    "default renderer shutdown is idempotent",
    [] {
        auto renderer = nr::renderer::Renderer{};

        nr::test::require(!renderer.initialized(), "default renderer should not be initialized");
        renderer.shutdown();
        renderer.shutdown();
        nr::test::require(!renderer.initialized(), "repeated default shutdown should preserve the empty state");
    }};
} // namespace
