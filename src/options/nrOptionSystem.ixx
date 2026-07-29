export module nr.options:system;

export import :model;
import :registration;
import std;

export namespace nr::options
{
enum class CatalogCommitRejectReason : std::uint8_t
{
    none,
    invalidCatalog,
    wrongScope,
    duplicateId,
    invalidMutation,
    notInitialized,
    alreadyInitialized,
    admissionMustBeClosed,
    pendingMutation,
    shutdown,
};

struct CatalogCommitResult
{
    bool committed = false;
    CatalogCommitRejectReason reason = CatalogCommitRejectReason::none;
    std::string detail{};

    [[nodiscard]] static CatalogCommitResult success() noexcept
    {
        return CatalogCommitResult{.committed = true};
    }

    [[nodiscard]] static CatalogCommitResult rejected(CatalogCommitRejectReason reason, std::string detail = {})
    {
        return CatalogCommitResult{
            .reason = reason,
            .detail = std::move(detail),
        };
    }
};

class ScheduledMutation
{
  public:
    ScheduledMutation(const ScheduledMutation &) = delete;
    ScheduledMutation &operator=(const ScheduledMutation &) = delete;
    ScheduledMutation(ScheduledMutation &&other) noexcept;
    ScheduledMutation &operator=(ScheduledMutation &&other) noexcept;

    [[nodiscard]] const OptionMutationRequest &request() const noexcept
    {
        return request_;
    }

    [[nodiscard]] std::uint64_t sequence() const noexcept
    {
        return sequence_;
    }

    [[nodiscard]] std::uint64_t admittedBindingEpoch() const noexcept
    {
        return admittedBindingEpoch_;
    }

    [[nodiscard]] std::uint64_t admittedGraphGeneration() const noexcept
    {
        return admittedGraphGeneration_;
    }

    [[nodiscard]] bool consumed() const noexcept
    {
        return consumed_;
    }

  private:
    friend class OptionSystem;

    ScheduledMutation(OptionMutationRequest request, std::uint64_t sequence, std::uint64_t bindingEpoch, std::uint64_t graphGeneration) noexcept;

    OptionMutationRequest request_{};
    std::uint64_t sequence_ = 0u;
    std::uint64_t admittedBindingEpoch_ = 0u;
    std::uint64_t admittedGraphGeneration_ = 0u;
    bool consumed_ = false;
};

struct RenderableFrameStart
{
    std::uint64_t frameIndex = 0u;
    std::optional<ScheduledMutation> mutation{};
};

struct MutationCommitResult
{
    bool committed = false;
    ScheduleRejectReason reason = ScheduleRejectReason::none;
};

struct EffectMaterializationResult
{
    std::optional<FrameEffect> effect{};
    ScheduleRejectReason reason = ScheduleRejectReason::none;
};

struct CameraResetValues
{
    OptionWireValue::Object pose{};
    std::uint64_t verticalFovDegrees = 60u;
    OptionWireValue::Object clipPlanes{};
};

struct AbandonedMutation
{
    std::uint64_t sequence = 0u;
    OptionId id{};
    MutationOrigin origin = MutationOrigin::imgui;
    std::optional<std::string> requestId{};
};

class OptionSystem
{
  public:
    explicit OptionSystem(AuthorityMode authorityMode = AuthorityMode::human);
    ~OptionSystem() = default;

    OptionSystem(const OptionSystem &) = delete;
    OptionSystem &operator=(const OptionSystem &) = delete;
    OptionSystem(OptionSystem &&) = delete;
    OptionSystem &operator=(OptionSystem &&) = delete;

    [[nodiscard]] CatalogCommitResult initializeSession(std::shared_ptr<const OptionCatalog> sessionCatalog, const OptionAvailabilityMap &initialAvailability = {});
    [[nodiscard]] CatalogCommitResult initializeSession(std::shared_ptr<const OptionCatalog> sessionCatalog, std::shared_ptr<const OptionCatalog> graphCatalog, const OptionAvailabilityMap &initialAvailability = {});
    [[nodiscard]] CatalogCommitResult replaceGraphCatalog(std::shared_ptr<const OptionCatalog> graphCatalog);
    [[nodiscard]] CatalogCommitResult commitGraphReplacement(ScheduledMutation &&mutation, std::shared_ptr<const OptionCatalog> graphCatalog);
    [[nodiscard]] CatalogCommitResult clearGraphCatalog();

    [[nodiscard]] ScheduleResult trySchedule(OptionMutationRequest request);
    [[nodiscard]] std::optional<RenderableFrameStart> beginRenderableFrame();
    [[nodiscard]] ScheduleRejectReason validateForExecution(const ScheduledMutation &mutation) const;
    [[nodiscard]] MutationCommitResult commitCanonical(ScheduledMutation &&mutation);
    [[nodiscard]] MutationCommitResult commitModelAndCameraReset(ScheduledMutation &&mutation, CameraResetValues camera);
    [[nodiscard]] EffectMaterializationResult materializeFrameEffect(ScheduledMutation &&mutation);
    [[nodiscard]] bool discardMutation(ScheduledMutation &&mutation) noexcept;
    [[nodiscard]] std::shared_ptr<const OptionFrameSnapshot> snapshotForCollection(std::optional<FrameEffect> effect = {}) const;
    [[nodiscard]] std::shared_ptr<const OptionFrameSnapshot> publishRenderableFrame(const OptionAvailabilityMap &availability, std::optional<FrameEffect> effect = {});

    [[nodiscard]] std::shared_ptr<const OptionFrameSnapshot> snapshot() const noexcept;
    [[nodiscard]] std::shared_ptr<const OptionCatalog> activeCatalog() const;
    [[nodiscard]] LiveBinding liveBinding() const;
    [[nodiscard]] bool hasPendingMutation() const;
    [[nodiscard]] bool admissionOpen() const;
    [[nodiscard]] AuthorityMode authorityMode() const;
    [[nodiscard]] bool setAuthorityMode(AuthorityMode mode);
    [[nodiscard]] std::optional<AbandonedMutation> shutdown();

  private:
    enum class GateState : std::uint8_t
    {
        closed,
        open,
        shutdown,
    };

    [[nodiscard]] static std::shared_ptr<const OptionCatalog> emptyCatalog();
    [[nodiscard]] CatalogBuildResult combineCatalogs(const OptionCatalog &session, const OptionCatalog &graph) const;
    [[nodiscard]] ScheduleRejectReason validateForExecutionLocked(const ScheduledMutation &mutation) const;
    [[nodiscard]] OptionAvailabilityMap normalizedAvailability(const OptionCatalog &catalog, const OptionAvailabilityMap &availability, const std::optional<FrameEffect> &effect) const;
    [[nodiscard]] std::shared_ptr<const OptionFrameSnapshot> makeSnapshotLocked(const OptionAvailabilityMap &availability, std::optional<FrameEffect> effect) const;
    [[nodiscard]] static std::string makeSessionIdentity();
    [[nodiscard]] std::string makeSnapshotToken(std::uint64_t bindingEpoch, std::uint64_t graphGeneration) const;
    [[nodiscard]] bool originAllowed(MutationOrigin origin) const noexcept;

    mutable std::mutex stateMutex_{};
    std::shared_ptr<const OptionCatalog> sessionCatalog_{};
    std::shared_ptr<const OptionCatalog> graphCatalog_{};
    std::shared_ptr<const OptionCatalog> combinedCatalog_{};
    OptionValueMap canonicalValues_{};
    std::shared_ptr<const OptionFrameSnapshot> publishedSnapshot_{};
    std::optional<ScheduledMutation> pendingMutation_{};
    GateState gateState_ = GateState::closed;
    std::uint64_t bindingEpoch_ = 1u;
    std::uint64_t graphGeneration_ = 0u;
    std::uint64_t frameIndex_ = 0u;
    std::uint64_t revision_ = 0u;
    std::uint64_t nextSequence_ = 1u;
    std::string sessionIdentity_{};
    AuthorityMode authorityMode_ = AuthorityMode::human;
    bool initialized_ = false;
};
} // namespace nr::options
