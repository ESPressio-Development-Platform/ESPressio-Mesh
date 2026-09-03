#include <cassert>

#include "../src/ESPressio_DeduplicationWindow.hpp"

int main() {
    using namespace ESPressio::Mesh;

    DeduplicationWindow<> window;
    static_assert(DeduplicationWindow<>::CapacityBits() == 128);
    assert(window.Empty());
    assert(window.Classify(0) == DeduplicationDisposition::Invalid);

    assert(window.Classify(100) == DeduplicationDisposition::Unseen);
    assert(window.Commit(100) == DeduplicationDisposition::Unseen);
    assert(window.HighestObservedSequence() == 100);
    assert(window.Classify(100) == DeduplicationDisposition::Duplicate);
    assert(window.Commit(100) == DeduplicationDisposition::Duplicate);

    // Advancing the high-water mark must not imply skipped values were observed.
    assert(window.Commit(103) == DeduplicationDisposition::Unseen);
    assert(window.HighestObservedSequence() == 103);
    assert(window.Classify(102) == DeduplicationDisposition::Unseen);
    assert(window.Classify(101) == DeduplicationDisposition::Unseen);
    assert(window.Classify(100) == DeduplicationDisposition::Duplicate);

    // Out-of-order unseen traffic inside the retained window remains admissible.
    assert(window.Commit(101) == DeduplicationDisposition::Unseen);
    assert(window.Classify(101) == DeduplicationDisposition::Duplicate);
    assert(window.Classify(102) == DeduplicationDisposition::Unseen);

    // Move exactly to a window where the oldest retained committed sequence falls out.
    assert(window.Commit(228) == DeduplicationDisposition::Unseen);
    assert(window.HighestObservedSequence() == 228);
    assert(window.Classify(101) == DeduplicationDisposition::Duplicate); // offset 127
    assert(window.Classify(100) == DeduplicationDisposition::TooOld);   // offset 128

    // A jump at least one whole window discards all exact older history and those
    // sequences become TooOld rather than being treated as unseen.
    assert(window.Commit(400) == DeduplicationDisposition::Unseen);
    assert(window.HighestObservedSequence() == 400);
    assert(window.Classify(228) == DeduplicationDisposition::TooOld);
    assert(window.Classify(399) == DeduplicationDisposition::Unseen);
    assert(window.Commit(399) == DeduplicationDisposition::Unseen);
    assert(window.Classify(399) == DeduplicationDisposition::Duplicate);

    window.Reset();
    assert(window.Empty());
    assert(window.Classify(1) == DeduplicationDisposition::Unseen);
    assert(window.Commit(1) == DeduplicationDisposition::Unseen);
    assert(window.HighestObservedSequence() == 1);

    return 0;
}
