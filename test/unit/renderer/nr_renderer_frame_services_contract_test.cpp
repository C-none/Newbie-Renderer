import std;
import nr.renderer;
import nr.test;

namespace
{
struct ServiceA
{
    int value = 0;
};

struct ServiceB
{
    std::string name{};
};

const nr::test::CaseRegistrar frameServicesCase{
    "renderer frame services store typed non-owning references",
    [] {
        auto services = nr::renderer::FrameServices{};
        auto a = ServiceA{.value = 7};
        auto b = ServiceB{.name = "ui"};

        nr::test::require(!services.tryGet<ServiceA>().has_value(), "missing service should return empty optional");
        services.set(std::ref(a));
        services.set(std::ref(b));

        auto resolvedA = services.tryGet<ServiceA>();
        auto resolvedB = std::as_const(services).tryGet<ServiceB>();
        nr::test::require(resolvedA.has_value(), "ServiceA should resolve");
        nr::test::require(resolvedB.has_value(), "const ServiceB should resolve");
        nr::test::requireEqual(resolvedA->get().value, 7);
        nr::test::requireEqual(resolvedB->get().name, std::string{"ui"});

        resolvedA->get().value = 11;
        nr::test::requireEqual(a.value, 11, "resolved reference should alias original service");

        services.clear();
        nr::test::require(!services.tryGet<ServiceA>().has_value(), "clear should remove services");
        nr::test::require(!std::as_const(services).tryGet<ServiceB>().has_value(), "clear should remove const services");
    }};
} // namespace
