#include <cassert>
#include <cstdint>
#include <limits>

#include "../src/ESPressio_MeshMessageIdGenerator.hpp"

int main() {
    using namespace ESPressio::Mesh;

    MeshMessageIdGenerator generator;
    assert(generator.LastIssued() == 0);
    assert(!generator.IsExhausted());

    MeshMessageId identifier = 99;
    assert(generator.TryIssue(identifier));
    assert(identifier == 1);
    assert(generator.LastIssued() == 1);

    assert(generator.TryIssue(identifier));
    assert(identifier == 2);
    assert(generator.LastIssued() == 2);

    const auto maximum = std::numeric_limits<MeshMessageId>::max();
    assert(generator.RestoreHighWater(maximum - 1));
    assert(!generator.IsExhausted());
    assert(generator.TryIssue(identifier));
    assert(identifier == maximum);
    assert(generator.LastIssued() == maximum);

    // An older persistence snapshot must not move the high-water mark backwards and permit identifier reuse.
    assert(!generator.RestoreHighWater(1));
    assert(generator.LastIssued() == maximum);
    assert(generator.IsExhausted());

    // Exhaustion must never wrap or overwrite the caller's output value.
    identifier = 123;
    assert(!generator.TryIssue(identifier));
    assert(identifier == 123);
    assert(generator.LastIssued() == maximum);

    generator.ResetForNewIncarnation();
    assert(generator.LastIssued() == 0);
    assert(!generator.IsExhausted());
    assert(generator.TryIssue(identifier));
    assert(identifier == 1);

    return 0;
}
