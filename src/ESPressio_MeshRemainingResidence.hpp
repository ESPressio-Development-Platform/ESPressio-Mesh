#pragma once

#include <cstdint>
#include <limits>

namespace ESPressio::Mesh {

inline constexpr std::uint64_t MeshNanosecondsPerMillisecond=1'000'000ULL;

/// <summary>Converts authenticated remaining residence into a local monotonic expiry without extending the budget.</summary>
/// <param name="maximumAdapterAdmissionWaitNanoseconds">
/// Optional current-attempt admission-wait ceiling. Zero means the residence budget is the only bound.
/// </param>
constexpr bool TryEstablishMeshLocalExpiry(
    std::uint64_t monotonicNowNanoseconds,
    std::uint32_t receivedRemainingResidenceMilliseconds,
    std::uint64_t maximumAdapterAdmissionWaitNanoseconds,
    std::uint64_t& localExpiryNanoseconds) noexcept {
    localExpiryNanoseconds=0;
    if(receivedRemainingResidenceMilliseconds==0) return false;
    const auto remaining=static_cast<std::uint64_t>(receivedRemainingResidenceMilliseconds)*MeshNanosecondsPerMillisecond;
    if(monotonicNowNanoseconds>std::numeric_limits<std::uint64_t>::max()-remaining) return false;
    auto expiry=monotonicNowNanoseconds+remaining;
    if(maximumAdapterAdmissionWaitNanoseconds!=0){
        if(monotonicNowNanoseconds>std::numeric_limits<std::uint64_t>::max()-maximumAdapterAdmissionWaitNanoseconds)
            return false;
        const auto admissionExpiry=monotonicNowNanoseconds+maximumAdapterAdmissionWaitNanoseconds;
        if(admissionExpiry<expiry) expiry=admissionExpiry;
    }
    localExpiryNanoseconds=expiry;
    return true;
}

/// <summary>Derives a conservative same-or-lower remaining residence from a retained local monotonic expiry.</summary>
constexpr bool TryEncodeMeshRemainingResidenceMilliseconds(
    std::uint64_t localExpiryNanoseconds,
    std::uint64_t monotonicNowNanoseconds,
    std::uint32_t receivedMaximumMilliseconds,
    std::uint32_t& encodedMilliseconds) noexcept {
    encodedMilliseconds=0;
    if(receivedMaximumMilliseconds==0 || localExpiryNanoseconds<=monotonicNowNanoseconds) return false;
    const auto remaining=localExpiryNanoseconds-monotonicNowNanoseconds;
    const auto milliseconds=remaining/MeshNanosecondsPerMillisecond;
    if(milliseconds==0) return false;
    const auto bounded=milliseconds<receivedMaximumMilliseconds?milliseconds:receivedMaximumMilliseconds;
    if(bounded==0 || bounded>std::numeric_limits<std::uint32_t>::max()) return false;
    encodedMilliseconds=static_cast<std::uint32_t>(bounded);
    return true;
}

/// <summary>Shortens an existing local expiry when a later authenticated representation carries less residence.</summary>
constexpr bool TightenMeshLocalExpiry(
    std::uint64_t monotonicNowNanoseconds,
    std::uint32_t laterRemainingResidenceMilliseconds,
    std::uint64_t& currentExpiryNanoseconds) noexcept {
    if(currentExpiryNanoseconds<=monotonicNowNanoseconds || laterRemainingResidenceMilliseconds==0) return false;
    std::uint64_t candidate=0;
    if(!TryEstablishMeshLocalExpiry(monotonicNowNanoseconds,laterRemainingResidenceMilliseconds,0,candidate)) return false;
    if(candidate<currentExpiryNanoseconds) currentExpiryNanoseconds=candidate;
    return true;
}

} // namespace ESPressio::Mesh
