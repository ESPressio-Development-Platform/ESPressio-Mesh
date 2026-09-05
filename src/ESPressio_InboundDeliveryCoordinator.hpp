#pragma once

#include <cstddef>
#include <cstdint>

#include "ESPressio_AuthenticatedMembershipTable.hpp"
#include "ESPressio_InboundDeliveryReservations.hpp"

namespace ESPressio::Mesh {

/// <summary>Outcome of beginning one authenticated inbound Mesh delivery.</summary>
enum class InboundDeliveryBeginResult : std::uint8_t {
    Reserved,
    Duplicate,
    TooOld,
    AlreadyInProgress,
    ResourceUnavailable,
    UnknownAuthenticatedMembership,
    Invalid
};

/// <summary>Outcome of definitively committing one previously reserved delivery.</summary>
enum class InboundDeliveryCommitResult : std::uint8_t {
    Committed,
    AlreadyCommitted,
    TooOld,
    MembershipUnavailable,
    NotReserved,
    Invalid
};

/// <summary>Whether a committed duplicate previously reached destination-framework acceptance.</summary>
enum class InboundDeliveryAcceptanceResult : std::uint8_t {
    Accepted,
    NotAccepted,
    TooOld,
    MembershipUnavailable,
    Invalid
};

/// <summary>
/// Composes authenticated membership-scoped deduplication with bounded InProgress reservation.
/// </summary>
/// <remarks>
/// This coordinator does not authenticate traffic itself. The caller may invoke TryBegin only after
/// sufficient end-to-end source authentication and MembershipIncarnation validation. The exact
/// authenticated source/incarnation must already exist in AuthenticatedMembershipTable. Therefore
/// an unauthenticated identity claim cannot create, reserve or advance deduplication state.
///
/// A Reserved result grants temporary exclusive semantic-handoff ownership. The receiver must then
/// either CommitDefinitive after a definitive local receive disposition, or ReleaseRetryable when the
/// local receiver reports TemporarilyUnavailable/ResourceUnavailable. This preserves legitimate later
/// retry while preventing concurrent duplicate upper-layer delivery.
/// </remarks>
template<
    std::size_t MembershipCapacity = Limits::MaxMeshNodes,
    std::size_t InProgressCapacity = Limits::MaxActiveInboundDeliveries
>
class InboundDeliveryCoordinator final {
public:
    using MembershipTable = AuthenticatedMembershipTable<MembershipCapacity>;
    using ReservationTable = InboundDeliveryReservationTable<InProgressCapacity>;

private:
    MembershipTable& _memberships;
    ReservationTable& _reservations;

public:
    InboundDeliveryCoordinator(
        MembershipTable& memberships,
        ReservationTable& reservations
    ) noexcept :
        _memberships(memberships),
        _reservations(reservations) {
    }

    /// <summary>
    /// Classifies committed history and reserves an unseen authenticated delivery before family dispatch.
    /// </summary>
    InboundDeliveryBeginResult TryBegin(
        const InboundDeliveryIdentity& identity
    ) noexcept {
        if (!identity.IsValid()) return InboundDeliveryBeginResult::Invalid;

        auto* membership = _memberships.FindExact(identity.Source, identity.Incarnation);
        if (membership == nullptr) {
            return InboundDeliveryBeginResult::UnknownAuthenticatedMembership;
        }

        switch (membership->DeliveryDeduplication.Classify(identity.MessageId)) {
            case DeduplicationDisposition::Duplicate:
                return InboundDeliveryBeginResult::Duplicate;
            case DeduplicationDisposition::TooOld:
                return InboundDeliveryBeginResult::TooOld;
            case DeduplicationDisposition::Invalid:
                return InboundDeliveryBeginResult::Invalid;
            case DeduplicationDisposition::Unseen:
                break;
        }

        switch (_reservations.TryReserve(identity)) {
            case InboundDeliveryReservationResult::Reserved:
                return InboundDeliveryBeginResult::Reserved;
            case InboundDeliveryReservationResult::AlreadyInProgress:
                return InboundDeliveryBeginResult::AlreadyInProgress;
            case InboundDeliveryReservationResult::ResourceUnavailable:
                return InboundDeliveryBeginResult::ResourceUnavailable;
            case InboundDeliveryReservationResult::Invalid:
                return InboundDeliveryBeginResult::Invalid;
        }
        return InboundDeliveryBeginResult::Invalid;
    }

    /// <summary>
    /// Commits definitive semantic handoff to the exact membership's deduplication window, then releases InProgress state.
    /// </summary>
    InboundDeliveryCommitResult CommitDefinitive(
        const InboundDeliveryIdentity& identity
    ) noexcept {
        if (!identity.IsValid()) return InboundDeliveryCommitResult::Invalid;
        if (!_reservations.Contains(identity)) return InboundDeliveryCommitResult::NotReserved;

        auto* membership = _memberships.FindExact(identity.Source, identity.Incarnation);
        if (membership == nullptr) {
            _reservations.Release(identity);
            return InboundDeliveryCommitResult::MembershipUnavailable;
        }

        const auto disposition = membership->DeliveryDeduplication.Commit(identity.MessageId);
        _reservations.Release(identity);

        switch (disposition) {
            case DeduplicationDisposition::Unseen:
                return InboundDeliveryCommitResult::Committed;
            case DeduplicationDisposition::Duplicate:
                return InboundDeliveryCommitResult::AlreadyCommitted;
            case DeduplicationDisposition::TooOld:
                return InboundDeliveryCommitResult::TooOld;
            case DeduplicationDisposition::Invalid:
                return InboundDeliveryCommitResult::Invalid;
        }
        return InboundDeliveryCommitResult::Invalid;
    }

    /// <summary>
    /// Commits definitive deduplication and records that this exact delivery reached destination-framework acceptance.
    /// </summary>
    InboundDeliveryCommitResult CommitAccepted(
        const InboundDeliveryIdentity& identity
    ) noexcept {
        const auto committed = CommitDefinitive(identity);
        if (committed != InboundDeliveryCommitResult::Committed &&
            committed != InboundDeliveryCommitResult::AlreadyCommitted) return committed;
        auto* membership = _memberships.FindExact(identity.Source, identity.Incarnation);
        if (membership == nullptr) return InboundDeliveryCommitResult::MembershipUnavailable;
        const auto acceptance = membership->AcceptedDeliveryDeduplication.Commit(identity.MessageId);
        return acceptance == DeduplicationDisposition::Unseen ||
                       acceptance == DeduplicationDisposition::Duplicate
            ? committed
            : InboundDeliveryCommitResult::Invalid;
    }

    /// <summary>Classifies whether an exact committed duplicate belongs to the bounded accepted-delivery subset.</summary>
    InboundDeliveryAcceptanceResult WasAccepted(
        const InboundDeliveryIdentity& identity
    ) const noexcept {
        if (!identity.IsValid()) return InboundDeliveryAcceptanceResult::Invalid;
        const auto* membership = _memberships.FindExact(identity.Source, identity.Incarnation);
        if (membership == nullptr) return InboundDeliveryAcceptanceResult::MembershipUnavailable;
        switch (membership->AcceptedDeliveryDeduplication.Classify(identity.MessageId)) {
            case DeduplicationDisposition::Duplicate: return InboundDeliveryAcceptanceResult::Accepted;
            case DeduplicationDisposition::Unseen: return InboundDeliveryAcceptanceResult::NotAccepted;
            case DeduplicationDisposition::TooOld: return InboundDeliveryAcceptanceResult::TooOld;
            case DeduplicationDisposition::Invalid: return InboundDeliveryAcceptanceResult::Invalid;
        }
        return InboundDeliveryAcceptanceResult::Invalid;
    }

    /// <summary>
    /// Releases a reservation without committing deduplication so a legitimate later retry may be accepted.
    /// </summary>
    bool ReleaseRetryable(const InboundDeliveryIdentity& identity) noexcept {
        return _reservations.Release(identity);
    }
};

} // namespace ESPressio::Mesh
