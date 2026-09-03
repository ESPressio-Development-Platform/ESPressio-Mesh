#include <array>
#include <cassert>
#include <cstdint>

#include "ESPressio_RouteCache.hpp"

using namespace ESPressio;

static System::DeviceIdentifier Device(std::uint8_t tail) {
    std::array<std::uint8_t, 16> bytes{};
    bytes[15] = tail;
    return System::DeviceIdentifier{bytes};
}

static Mesh::TopologyLinkIdentity Edge(
    const System::DeviceIdentifier& from,
    std::uint8_t localRadio,
    const System::DeviceIdentifier& to,
    std::uint8_t remoteRadio = 0
) {
    return {from, localRadio, to, remoteRadio};
}

int main() {
    const auto a = Device(1);
    const auto b = Device(2);
    const auto c = Device(3);
    const auto d = Device(4);

    Mesh::ResolvedRoute<3> route;
    const std::array hops{Edge(a, 1, b, 1), Edge(b, 2, c, 1)};
    assert(route.Assign(a, c, hops.data(), hops.size()));
    assert(route.HopCount() == 2);
    assert(route.NextHop() != nullptr);
    assert(route.NextHop()->Neighbour == b);

    // Route is a local plan, but it must still be contiguous and loop-free.
    const std::array disconnected{Edge(a, 1, b), Edge(d, 1, c)};
    assert(!route.Assign(a, c, disconnected.data(), disconnected.size()));
    const std::array loop{Edge(a, 1, b), Edge(b, 1, a)};
    assert(!route.Assign(a, a, loop.data(), loop.size()));
    const std::array tooLong{Edge(a, 1, b), Edge(b, 1, c), Edge(c, 1, d), Edge(d, 1, a)};
    assert(!route.Assign(a, a, tooLong.data(), tooLong.size()));

    Mesh::ResolvedRoute<3> local;
    assert(local.Assign(a, a, nullptr, 0));
    assert(local.Empty());

    Mesh::ResolvedRoute<3> routeAC;
    assert(routeAC.Assign(a, c, hops.data(), hops.size()));
    Mesh::ResolvedRoute<3> routeAD;
    const std::array ad{Edge(a, 3, d)};
    assert(routeAD.Assign(a, d, ad.data(), ad.size()));
    Mesh::ResolvedRoute<3> routeBA;
    const std::array ba{Edge(b, 4, a)};
    assert(routeBA.Assign(b, a, ba.data(), ba.size()));

    Mesh::RouteCache<2, 3> cache;
    Mesh::RouteCacheHandle acHandle{};
    assert(cache.Store(routeAC, acHandle) == Mesh::RouteCacheStoreResult::Stored);
    assert(acHandle);
    assert(cache.Find(a, c) != nullptr);
    assert(cache.Resolve(acHandle) != nullptr);

    Mesh::RouteCacheHandle adHandle{};
    assert(cache.Store(routeAD, adHandle) == Mesh::RouteCacheStoreResult::Stored);
    assert(cache.Size() == 2);

    // No hidden eviction policy when full.
    Mesh::RouteCacheHandle rejected{};
    assert(cache.Store(routeBA, rejected) == Mesh::RouteCacheStoreResult::ResourceUnavailable);
    assert(!rejected);
    assert(cache.Size() == 2);

    // Same endpoint pair replaces in place and invalidates its old generation-safe handle.
    Mesh::ResolvedRoute<3> alternateAC;
    const std::array alternate{Edge(a, 5, d), Edge(d, 6, c)};
    assert(alternateAC.Assign(a, c, alternate.data(), alternate.size()));
    Mesh::RouteCacheHandle replacement{};
    assert(cache.Store(alternateAC, replacement) == Mesh::RouteCacheStoreResult::Replaced);
    assert(replacement.Slot == acHandle.Slot);
    assert(replacement.Generation != acHandle.Generation);
    assert(cache.Resolve(acHandle) == nullptr);
    assert(cache.Resolve(replacement) != nullptr);

    // Topology authority invalidation removes every route whose planned path uses that authority.
    assert(cache.InvalidateAuthority(d) == 1);
    assert(cache.Find(a, c) == nullptr);
    assert(cache.Find(a, d) != nullptr);

    assert(cache.InvalidateEndpoint(d) == 1);
    assert(cache.Empty());

    // Freed slot reuse advances generation and does not revive stale handles.
    Mesh::RouteCacheHandle baHandle{};
    assert(cache.Store(routeBA, baHandle) == Mesh::RouteCacheStoreResult::Stored);
    assert(cache.Resolve(adHandle) == nullptr);
    assert(cache.Resolve(baHandle) != nullptr);
    assert(cache.Invalidate(baHandle));
    assert(cache.Empty());

    return 0;
}
