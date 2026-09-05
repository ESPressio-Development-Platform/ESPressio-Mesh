#include <array>
#include <cassert>

#include "ESPressio_MeshTrafficGovernor.hpp"

using namespace ESPressio::Mesh;

int main() {
    DefaultMeshTrafficGovernor governor;

    assert(governor.Capacity(MeshTrafficClass::InfrastructureResponse) == Limits::InfrastructureResponseCapacity);
    assert(governor.Capacity(MeshTrafficClass::ClockControl) == Limits::ClockControlCapacity);
    assert(governor.Capacity(MeshTrafficClass::GeneralControl) == Limits::GeneralControlCapacity);
    assert(governor.Capacity(MeshTrafficClass::Application) == Limits::ApplicationTransmissionCapacity);

    std::array<MeshTrafficReservation, Limits::ApplicationTransmissionCapacity> application{};
    for (auto& reservation : application) {
        assert(governor.TryAcquire(MeshTrafficClass::Application, reservation) == MeshTrafficAdmissionResult::Admitted);
        assert(reservation);
    }
    MeshTrafficReservation saturated{};
    assert(governor.TryAcquire(MeshTrafficClass::Application, saturated) ==
           MeshTrafficAdmissionResult::ResourceUnavailable);
    assert(!saturated);

    // Application saturation must not consume protected Mesh-survival capacity.
    MeshTrafficReservation infrastructure{};
    MeshTrafficReservation clock{};
    MeshTrafficReservation general{};
    assert(governor.TryAcquire(MeshTrafficClass::InfrastructureResponse, infrastructure) ==
           MeshTrafficAdmissionResult::Admitted);
    assert(governor.TryAcquire(MeshTrafficClass::ClockControl, clock) ==
           MeshTrafficAdmissionResult::Admitted);
    assert(governor.TryAcquire(MeshTrafficClass::GeneralControl, general) ==
           MeshTrafficAdmissionResult::Admitted);

    governor.ResetForControlledShutdown();
    assert(governor.Active(MeshTrafficClass::InfrastructureResponse) == 0);
    assert(governor.Active(MeshTrafficClass::ClockControl) == 0);
    assert(governor.Active(MeshTrafficClass::GeneralControl) == 0);
    assert(governor.Active(MeshTrafficClass::Application) == 0);
    assert(!governor.Release(application.back()));
    assert(!governor.Release(infrastructure));

    MeshTrafficReservation afterReset{};
    assert(governor.TryAcquire(MeshTrafficClass::Application, afterReset) ==
           MeshTrafficAdmissionResult::Admitted);
    assert(afterReset.Slot == application.front().Slot);
    assert(afterReset.Generation != application.front().Generation);

    const auto staleApplication = application.front();
    assert(!governor.Release(application.front()));
    MeshTrafficReservation replacement{};
    assert(governor.Release(afterReset));
    assert(governor.TryAcquire(MeshTrafficClass::Application, replacement) == MeshTrafficAdmissionResult::Admitted);
    assert(replacement.Slot == staleApplication.Slot);
    assert(replacement.Generation != staleApplication.Generation);
    assert(!governor.Release(staleApplication));
    assert(governor.Release(replacement));

    assert(!governor.Release(infrastructure));
    assert(!governor.Release(clock));
    assert(!governor.Release(general));
    assert(governor.Active(MeshTrafficClass::InfrastructureResponse) == 0);
    assert(governor.Active(MeshTrafficClass::ClockControl) == 0);
    assert(governor.Active(MeshTrafficClass::GeneralControl) == 0);

    return 0;
}
