#pragma once

#include <cstddef>
#include <cstdint>

namespace ESPressio::Mesh {

/// <summary>Borrowed immutable bytes whose storage remains stable for the complete accepted transmission lifetime.</summary>
struct BorrowedStablePayload final {
    const std::uint8_t* Data{nullptr};
    std::size_t Size{0};

    constexpr bool IsValid() const noexcept { return Data != nullptr && Size != 0U; }
    constexpr explicit operator bool() const noexcept { return IsValid(); }
};

/// <summary>
/// Repeatable immutable serialized source used when a caller can reproduce exactly the same logical bytes on demand.
/// </summary>
/// <remarks>
/// Length is finite and known before admission. Read must return exactly the requested byte range from the same immutable
/// logical payload for the complete transmission lifetime. Mesh does not own the source object and never assumes that
/// repeated serialization of mutable application state is stable.
/// </remarks>
class IRepeatableSerializedPayloadSource {
public:
    virtual ~IRepeatableSerializedPayloadSource() = default;
    virtual std::size_t Size() const noexcept = 0;
    virtual bool Read(std::size_t offset, std::uint8_t* destination, std::size_t length) const noexcept = 0;
};

/// <summary>Non-owning reference to one repeatable serialized payload source.</summary>
struct RepeatableSerializedPayload final {
    const IRepeatableSerializedPayloadSource* Source{nullptr};
    std::size_t Size{0};

    bool IsValid() const noexcept {
        return Source != nullptr && Size != 0U && Source->Size() == Size;
    }
    explicit operator bool() const noexcept { return IsValid(); }
};

/// <summary>
/// Immutable application payload reference shared by every recipient in one sender-local transmission aggregate.
/// </summary>
/// <remarks>
/// This type owns no bytes and introduces no payload-capacity constant. Borrowed storage/source lifetime is therefore an
/// explicit caller contract: it must outlive the aggregate or be released only after all recipient deliveries are
/// terminal. A future bounded-owned backing may satisfy the same aggregate contract once an explicit byte capacity is
/// approved; no such capacity is invented here.
/// </remarks>
class ApplicationPayload final {
public:
    enum class Kind : std::uint8_t { None, BorrowedStable, RepeatableSerialized };

private:
    Kind _kind{Kind::None};
    BorrowedStablePayload _borrowed{};
    RepeatableSerializedPayload _repeatable{};

public:
    static ApplicationPayload Borrowed(const std::uint8_t* data, std::size_t size) noexcept {
        ApplicationPayload payload;
        payload._kind = Kind::BorrowedStable;
        payload._borrowed = {data, size};
        if (!payload._borrowed) payload = {};
        return payload;
    }

    static ApplicationPayload Repeatable(const IRepeatableSerializedPayloadSource& source) noexcept {
        ApplicationPayload payload;
        payload._kind = Kind::RepeatableSerialized;
        payload._repeatable = {&source, source.Size()};
        if (!payload._repeatable) payload = {};
        return payload;
    }

    constexpr Kind Type() const noexcept { return _kind; }
    bool IsValid() const noexcept {
        return (_kind == Kind::BorrowedStable && static_cast<bool>(_borrowed)) ||
               (_kind == Kind::RepeatableSerialized && static_cast<bool>(_repeatable));
    }
    explicit operator bool() const noexcept { return IsValid(); }

    std::size_t Size() const noexcept {
        if (_kind == Kind::BorrowedStable) return _borrowed.Size;
        if (_kind == Kind::RepeatableSerialized) return _repeatable.Size;
        return 0U;
    }

    const std::uint8_t* StableData() const noexcept {
        return _kind == Kind::BorrowedStable && _borrowed ? _borrowed.Data : nullptr;
    }

    bool Read(std::size_t offset, std::uint8_t* destination, std::size_t length) const noexcept {
        if (!IsValid() || destination == nullptr || offset > Size() || length > Size() - offset) return false;
        if (_kind == Kind::BorrowedStable) {
            for (std::size_t i = 0; i < length; ++i) destination[i] = _borrowed.Data[offset + i];
            return true;
        }
        return _repeatable.Source->Read(offset, destination, length);
    }
};

} // namespace ESPressio::Mesh
