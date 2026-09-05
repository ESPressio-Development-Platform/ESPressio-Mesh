#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>

#include "ESPressio_MeshLimits.hpp"

namespace ESPressio::Mesh {

/// <summary>Independent bounded work classes protected by Mesh traffic governance.</summary>
enum class MeshTrafficClass : std::uint8_t {
    InfrastructureResponse,
    ClockControl,
    GeneralControl,
    Application
};

/// <summary>Generation-safe reservation for one admitted unit of Mesh work.</summary>
struct MeshTrafficReservation final {
    MeshTrafficClass Class{MeshTrafficClass::Application};
    std::uint8_t Slot{std::numeric_limits<std::uint8_t>::max()};
    std::uint16_t Generation{0};

    constexpr bool IsValid() const noexcept {
        return Slot != std::numeric_limits<std::uint8_t>::max() && Generation != 0U;
    }
    constexpr explicit operator bool() const noexcept { return IsValid(); }
};

/// <summary>Result of attempting to reserve bounded capacity from one Mesh traffic class.</summary>
enum class MeshTrafficAdmissionResult : std::uint8_t {
    Admitted,
    ResourceUnavailable,
    Invalid
};

/// <summary>Injectable local policy boundary governing admission into independently protected Mesh work classes.</summary>
class IMeshTrafficGovernor {
public:
    virtual ~IMeshTrafficGovernor() = default;

    /// <summary>Attempts to admit one unit of work without borrowing capacity from another traffic class.</summary>
    virtual MeshTrafficAdmissionResult TryAcquire(
        MeshTrafficClass trafficClass,
        MeshTrafficReservation& reservation
    ) noexcept = 0;

    /// <summary>Releases one exact current reservation.</summary>
    virtual bool Release(MeshTrafficReservation reservation) noexcept = 0;

    /// <summary>Returns the number of active reservations in one class.</summary>
    virtual std::size_t Active(MeshTrafficClass trafficClass) const noexcept = 0;

    /// <summary>Returns the configured reservation capacity of one class.</summary>
    virtual std::size_t Capacity(MeshTrafficClass trafficClass) const noexcept = 0;

    /// <summary>Releases every retained local work reservation during controlled Mesh shutdown/reset.</summary>
    /// <remarks>
    /// Owning services must first abandon their exact work records. This final governor reset is local resource
    /// reclamation only; it does not manufacture completion, cancellation or any distributed protocol evidence.
    /// </remarks>
    virtual void ResetForControlledShutdown() noexcept = 0;
};

/// <summary>
/// Default fixed-capacity governor preserving independent reserves for Mesh-survival control work.
/// </summary>
/// <remarks>
/// Application saturation can consume only Application capacity. It therefore cannot consume the protected
/// InfrastructureResponse, ClockControl or GeneralControl reserves. Delivery acknowledgements and other mandatory
/// infrastructure responses should be admitted as InfrastructureResponse work by the owning service.
///
/// The governor controls admission only; it owns no queues, scheduling task or wire semantics. Alternative governors
/// may be injected through IMeshTrafficGovernor provided they retain finite bounded behavior required by the application.
/// </remarks>
class DefaultMeshTrafficGovernor final : public IMeshTrafficGovernor {
    struct Slot final {
        std::uint16_t Generation{0};
        bool Occupied{false};
    };

    std::array<Slot, Limits::InfrastructureResponseCapacity> _infrastructure{};
    std::array<Slot, Limits::ClockControlCapacity> _clock{};
    std::array<Slot, Limits::GeneralControlCapacity> _general{};
    std::array<Slot, Limits::ApplicationTransmissionCapacity> _application{};

    static std::uint16_t NextGeneration(std::uint16_t current) noexcept {
        ++current;
        if (current == 0U) ++current;
        return current;
    }

    template<std::size_t Capacity>
    static MeshTrafficAdmissionResult AcquireFrom(
        std::array<Slot, Capacity>& slots,
        MeshTrafficClass trafficClass,
        MeshTrafficReservation& reservation
    ) noexcept {
        static_assert(Capacity > 0 && Capacity < std::numeric_limits<std::uint8_t>::max(),
                      "Traffic reservation slots must fit the compact handle.");
        reservation = {};
        for (std::size_t index = 0; index < Capacity; ++index) {
            auto& slot = slots[index];
            if (slot.Occupied) continue;
            slot.Generation = NextGeneration(slot.Generation);
            slot.Occupied = true;
            reservation = MeshTrafficReservation{
                trafficClass,
                static_cast<std::uint8_t>(index),
                slot.Generation
            };
            return MeshTrafficAdmissionResult::Admitted;
        }
        return MeshTrafficAdmissionResult::ResourceUnavailable;
    }

    template<std::size_t Capacity>
    static bool ReleaseFrom(
        std::array<Slot, Capacity>& slots,
        MeshTrafficReservation reservation
    ) noexcept {
        if (!reservation || reservation.Slot >= Capacity) return false;
        auto& slot = slots[reservation.Slot];
        if (!slot.Occupied || slot.Generation != reservation.Generation) return false;
        slot.Occupied = false;
        return true;
    }

    template<std::size_t Capacity>
    static std::size_t ActiveIn(const std::array<Slot, Capacity>& slots) noexcept {
        std::size_t active = 0;
        for (const auto& slot : slots) if (slot.Occupied) ++active;
        return active;
    }

    template<std::size_t Capacity>
    static void Reset(std::array<Slot, Capacity>& slots) noexcept {
        for (auto& slot : slots) slot.Occupied = false;
    }

public:
    MeshTrafficAdmissionResult TryAcquire(
        MeshTrafficClass trafficClass,
        MeshTrafficReservation& reservation
    ) noexcept override {
        switch (trafficClass) {
            case MeshTrafficClass::InfrastructureResponse:
                return AcquireFrom(_infrastructure, trafficClass, reservation);
            case MeshTrafficClass::ClockControl:
                return AcquireFrom(_clock, trafficClass, reservation);
            case MeshTrafficClass::GeneralControl:
                return AcquireFrom(_general, trafficClass, reservation);
            case MeshTrafficClass::Application:
                return AcquireFrom(_application, trafficClass, reservation);
        }
        reservation = {};
        return MeshTrafficAdmissionResult::Invalid;
    }

    bool Release(MeshTrafficReservation reservation) noexcept override {
        if (!reservation) return false;
        switch (reservation.Class) {
            case MeshTrafficClass::InfrastructureResponse:
                return ReleaseFrom(_infrastructure, reservation);
            case MeshTrafficClass::ClockControl:
                return ReleaseFrom(_clock, reservation);
            case MeshTrafficClass::GeneralControl:
                return ReleaseFrom(_general, reservation);
            case MeshTrafficClass::Application:
                return ReleaseFrom(_application, reservation);
        }
        return false;
    }

    std::size_t Active(MeshTrafficClass trafficClass) const noexcept override {
        switch (trafficClass) {
            case MeshTrafficClass::InfrastructureResponse: return ActiveIn(_infrastructure);
            case MeshTrafficClass::ClockControl: return ActiveIn(_clock);
            case MeshTrafficClass::GeneralControl: return ActiveIn(_general);
            case MeshTrafficClass::Application: return ActiveIn(_application);
        }
        return 0;
    }

    std::size_t Capacity(MeshTrafficClass trafficClass) const noexcept override {
        switch (trafficClass) {
            case MeshTrafficClass::InfrastructureResponse: return Limits::InfrastructureResponseCapacity;
            case MeshTrafficClass::ClockControl: return Limits::ClockControlCapacity;
            case MeshTrafficClass::GeneralControl: return Limits::GeneralControlCapacity;
            case MeshTrafficClass::Application: return Limits::ApplicationTransmissionCapacity;
        }
        return 0;
    }

    /// <summary>Releases all class reservations while preserving generations so pre-reset handles remain stale.</summary>
    void ResetForControlledShutdown() noexcept override {
        Reset(_infrastructure);
        Reset(_clock);
        Reset(_general);
        Reset(_application);
    }
};

} // namespace ESPressio::Mesh
