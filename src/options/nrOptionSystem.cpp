module nr.options;

import :model;
import :registration;
import :system;
import nr.utils;
import std;

namespace nr::options
{
namespace
{
[[nodiscard]] OptionAvailability unavailable(std::string reason)
{
    return OptionAvailability{
        .reason = std::move(reason),
    };
}

[[nodiscard]] bool catalogHasOnlyScope(const OptionCatalog &catalog, OptionScope scope) noexcept
{
    return std::ranges::all_of(catalog.definitions(), [&](auto const &entry) { return entry.second.scope == scope; });
}

[[nodiscard]] OptionValueMap defaultValues(const OptionCatalog &catalog)
{
    auto values = OptionValueMap{};
    std::ranges::for_each(catalog.definitions(),
                          [&](auto const &entry) { values.emplace(entry.first, entry.second.defaultValue); });
    return values;
}
} // namespace

ScheduledMutation::ScheduledMutation(OptionMutationRequest request, std::uint64_t sequence, std::uint64_t bindingEpoch,
                                     std::uint64_t graphGeneration) noexcept
    : request_(std::move(request)), sequence_(sequence), admittedBindingEpoch_(bindingEpoch),
      admittedGraphGeneration_(graphGeneration)
{
}

ScheduledMutation::ScheduledMutation(ScheduledMutation &&other) noexcept
    : request_(std::move(other.request_)), sequence_(other.sequence_),
      admittedBindingEpoch_(other.admittedBindingEpoch_), admittedGraphGeneration_(other.admittedGraphGeneration_),
      consumed_(other.consumed_)
{
    other.consumed_ = true;
}

ScheduledMutation &ScheduledMutation::operator=(ScheduledMutation &&other) noexcept
{
    if (this != std::addressof(other))
    {
        request_ = std::move(other.request_);
        sequence_ = other.sequence_;
        admittedBindingEpoch_ = other.admittedBindingEpoch_;
        admittedGraphGeneration_ = other.admittedGraphGeneration_;
        consumed_ = other.consumed_;
        other.consumed_ = true;
    }
    return *this;
}

OptionSystem::OptionSystem(AuthorityMode authorityMode)
    : sessionIdentity_(makeSessionIdentity()), authorityMode_(authorityMode)
{
    sessionCatalog_ = emptyCatalog();
    graphCatalog_ = emptyCatalog();
    combinedCatalog_ = emptyCatalog();
    auto initial = std::make_shared<OptionFrameSnapshot>();
    initial->catalog = combinedCatalog_;
    initial->bindingEpoch = bindingEpoch_;
    initial->snapshotToken = makeSnapshotToken(bindingEpoch_, graphGeneration_);
    auto publishedInitial = std::shared_ptr<const OptionFrameSnapshot>{std::move(initial)};
    std::atomic_store_explicit(&publishedSnapshot_, std::move(publishedInitial), std::memory_order_release);
}

std::shared_ptr<const OptionCatalog> OptionSystem::emptyCatalog()
{
    auto builder = OptionCatalogBuilder{};
    return builder.build().catalog;
}

CatalogBuildResult OptionSystem::combineCatalogs(const OptionCatalog &session, const OptionCatalog &graph) const
{
    auto builder = OptionCatalogBuilder{};
    std::ranges::for_each(session.definitions(), [&](auto const &entry) { (void)builder.add(entry.second); });
    std::ranges::for_each(graph.definitions(), [&](auto const &entry) { (void)builder.add(entry.second); });
    return builder.build();
}

CatalogCommitResult OptionSystem::initializeSession(std::shared_ptr<const OptionCatalog> sessionCatalog,
                                                    const OptionAvailabilityMap &initialAvailability)
{
    return initializeSession(std::move(sessionCatalog), emptyCatalog(), initialAvailability);
}

CatalogCommitResult OptionSystem::initializeSession(std::shared_ptr<const OptionCatalog> sessionCatalog,
                                                    std::shared_ptr<const OptionCatalog> graphCatalog,
                                                    const OptionAvailabilityMap &initialAvailability)
{
    auto lock = std::scoped_lock{stateMutex_};
    if (gateState_ == GateState::shutdown)
    {
        return CatalogCommitResult::rejected(CatalogCommitRejectReason::shutdown);
    }
    if (initialized_)
    {
        return CatalogCommitResult::rejected(CatalogCommitRejectReason::alreadyInitialized);
    }
    if (!sessionCatalog || !graphCatalog)
    {
        return CatalogCommitResult::rejected(CatalogCommitRejectReason::invalidCatalog);
    }
    if (!catalogHasOnlyScope(*sessionCatalog, OptionScope::session))
    {
        return CatalogCommitResult::rejected(CatalogCommitRejectReason::wrongScope);
    }
    if (!catalogHasOnlyScope(*graphCatalog, OptionScope::graph))
    {
        return CatalogCommitResult::rejected(CatalogCommitRejectReason::wrongScope);
    }

    auto combined = combineCatalogs(*sessionCatalog, *graphCatalog);
    if (!combined.valid())
    {
        return CatalogCommitResult::rejected(CatalogCommitRejectReason::invalidCatalog, "catalogs cannot be combined");
    }

    sessionCatalog_ = std::move(sessionCatalog);
    graphCatalog_ = std::move(graphCatalog);
    combinedCatalog_ = std::move(combined.catalog);
    canonicalValues_ = defaultValues(*combinedCatalog_);
    graphGeneration_ = graphCatalog_->definitions().empty() ? 0u : 1u;
    initialized_ = true;
    gateState_ = GateState::open;

    auto snapshot = makeSnapshotLocked(initialAvailability, {});
    std::atomic_store_explicit(&publishedSnapshot_, std::move(snapshot), std::memory_order_release);
    return CatalogCommitResult::success();
}

CatalogCommitResult OptionSystem::replaceGraphCatalog(std::shared_ptr<const OptionCatalog> graphCatalog)
{
    auto lock = std::scoped_lock{stateMutex_};
    if (gateState_ == GateState::shutdown)
    {
        return CatalogCommitResult::rejected(CatalogCommitRejectReason::shutdown);
    }
    if (!initialized_)
    {
        return CatalogCommitResult::rejected(CatalogCommitRejectReason::notInitialized);
    }
    if (gateState_ != GateState::closed)
    {
        return CatalogCommitResult::rejected(CatalogCommitRejectReason::admissionMustBeClosed);
    }
    if (pendingMutation_)
    {
        return CatalogCommitResult::rejected(CatalogCommitRejectReason::pendingMutation);
    }
    if (!graphCatalog)
    {
        return CatalogCommitResult::rejected(CatalogCommitRejectReason::invalidCatalog);
    }
    if (!catalogHasOnlyScope(*graphCatalog, OptionScope::graph))
    {
        return CatalogCommitResult::rejected(CatalogCommitRejectReason::wrongScope);
    }

    auto combined = combineCatalogs(*sessionCatalog_, *graphCatalog);
    if (!combined.valid())
    {
        auto const duplicate = std::ranges::any_of(
            combined.issues, [](auto const &issue) { return issue.code == CatalogIssueCode::duplicateId; });
        return CatalogCommitResult::rejected(duplicate ? CatalogCommitRejectReason::duplicateId
                                                       : CatalogCommitRejectReason::invalidCatalog,
                                             "candidate graph catalog cannot be combined with the session catalog");
    }

    auto nextValues = OptionValueMap{};
    std::ranges::for_each(sessionCatalog_->definitions(), [&](auto const &entry) {
        auto const existing = canonicalValues_.find(entry.first);
        nextValues.emplace(entry.first,
                           existing != canonicalValues_.end() ? existing->second : entry.second.defaultValue);
    });
    std::ranges::for_each(graphCatalog->definitions(),
                          [&](auto const &entry) { nextValues.emplace(entry.first, entry.second.defaultValue); });

    nrAssert(bindingEpoch_ != std::numeric_limits<std::uint64_t>::max(), "Option binding epoch exhausted");
    nrAssert(graphGeneration_ != std::numeric_limits<std::uint64_t>::max(), "Option graph generation exhausted");
    ++bindingEpoch_;
    ++graphGeneration_;
    graphCatalog_ = std::move(graphCatalog);
    combinedCatalog_ = std::move(combined.catalog);
    canonicalValues_ = std::move(nextValues);
    return CatalogCommitResult::success();
}

CatalogCommitResult OptionSystem::commitGraphReplacement(ScheduledMutation &&mutation,
                                                         std::shared_ptr<const OptionCatalog> graphCatalog)
{
    auto lock = std::scoped_lock{stateMutex_};
    auto const consumeAndReject = [&](CatalogCommitRejectReason reason, std::string detail = {}) {
        mutation.consumed_ = true;
        return CatalogCommitResult::rejected(reason, std::move(detail));
    };

    auto const mutationValidation = validateForExecutionLocked(mutation);
    if (mutationValidation != ScheduleRejectReason::none)
    {
        return consumeAndReject(CatalogCommitRejectReason::invalidMutation, std::string{wireName(mutationValidation)});
    }
    if (gateState_ != GateState::closed)
    {
        return consumeAndReject(CatalogCommitRejectReason::admissionMustBeClosed);
    }
    auto const *mutationDefinition = combinedCatalog_->find(mutation.request_.id);
    if (mutationDefinition == nullptr || mutationDefinition->scope != OptionScope::session ||
        mutationDefinition->lifetime != OptionValueLifetime::canonical)
    {
        return consumeAndReject(CatalogCommitRejectReason::invalidMutation,
                                "graph replacement must be committed by a canonical session option");
    }
    if (!graphCatalog)
    {
        return consumeAndReject(CatalogCommitRejectReason::invalidCatalog);
    }
    if (!catalogHasOnlyScope(*graphCatalog, OptionScope::graph))
    {
        return consumeAndReject(CatalogCommitRejectReason::wrongScope);
    }

    auto combined = combineCatalogs(*sessionCatalog_, *graphCatalog);
    if (!combined.valid())
    {
        auto const duplicate = std::ranges::any_of(
            combined.issues, [](auto const &issue) { return issue.code == CatalogIssueCode::duplicateId; });
        return consumeAndReject(duplicate ? CatalogCommitRejectReason::duplicateId
                                          : CatalogCommitRejectReason::invalidCatalog,
                                "candidate graph catalog cannot be combined with the session catalog");
    }

    auto nextValues = OptionValueMap{};
    std::ranges::for_each(sessionCatalog_->definitions(), [&](auto const &entry) {
        auto const existing = canonicalValues_.find(entry.first);
        nextValues.emplace(entry.first,
                           existing != canonicalValues_.end() ? existing->second : entry.second.defaultValue);
    });
    nextValues.insert_or_assign(mutation.request_.id, mutation.request_.value);
    std::ranges::for_each(graphCatalog->definitions(),
                          [&](auto const &entry) { nextValues.emplace(entry.first, entry.second.defaultValue); });

    nrAssert(bindingEpoch_ != std::numeric_limits<std::uint64_t>::max(), "Option binding epoch exhausted");
    nrAssert(graphGeneration_ != std::numeric_limits<std::uint64_t>::max(), "Option graph generation exhausted");
    ++bindingEpoch_;
    ++graphGeneration_;
    graphCatalog_ = std::move(graphCatalog);
    combinedCatalog_ = std::move(combined.catalog);
    canonicalValues_ = std::move(nextValues);
    mutation.consumed_ = true;
    return CatalogCommitResult::success();
}

CatalogCommitResult OptionSystem::clearGraphCatalog()
{
    return replaceGraphCatalog(emptyCatalog());
}

ScheduleResult OptionSystem::trySchedule(OptionMutationRequest request)
{
    auto lock = std::scoped_lock{stateMutex_};
    if (gateState_ == GateState::shutdown)
    {
        return ScheduleResult::rejected(ScheduleRejectReason::shutdown);
    }
    if (gateState_ != GateState::open)
    {
        return ScheduleResult::rejected(ScheduleRejectReason::admissionClosed);
    }
    if (!originAllowed(request.origin))
    {
        return ScheduleResult::rejected(ScheduleRejectReason::unauthorizedOrigin);
    }
    if (!request.id.valid() || (!request.binding.bindingEpoch && !request.binding.snapshotToken))
    {
        return ScheduleResult::rejected(ScheduleRejectReason::invalidParams);
    }
    if (request.requestId && request.requestId->size() > 128u)
    {
        return ScheduleResult::rejected(ScheduleRejectReason::invalidParams);
    }

    auto const epochMatches = !request.binding.bindingEpoch || *request.binding.bindingEpoch == bindingEpoch_;
    auto const token = makeSnapshotToken(bindingEpoch_, graphGeneration_);
    auto const tokenMatches = !request.binding.snapshotToken || *request.binding.snapshotToken == token;
    if (request.binding.bindingEpoch && request.binding.snapshotToken && (!epochMatches || !tokenMatches))
    {
        return ScheduleResult::rejected(ScheduleRejectReason::bindingProofMismatch);
    }
    if (!epochMatches)
    {
        return ScheduleResult::rejected(ScheduleRejectReason::staleBinding);
    }
    if (!tokenMatches)
    {
        return ScheduleResult::rejected(ScheduleRejectReason::staleSnapshot);
    }
    if (pendingMutation_)
    {
        return ScheduleResult::rejected(ScheduleRejectReason::busy);
    }

    auto const *definition = combinedCatalog_->find(request.id);
    if (definition == nullptr)
    {
        return ScheduleResult::rejected(ScheduleRejectReason::unknownOption);
    }
    auto const validation = definition->schema.validate(request.value);
    if (!validation.valid)
    {
        return ScheduleResult::rejected(ScheduleRejectReason::invalidValue);
    }
    auto currentSnapshot = std::atomic_load_explicit(&publishedSnapshot_, std::memory_order_acquire);
    auto const *availability = currentSnapshot != nullptr ? currentSnapshot->findAvailability(request.id) : nullptr;
    if (availability == nullptr || !availability->available)
    {
        return ScheduleResult::rejected(ScheduleRejectReason::unavailable);
    }
    if (definition->admissionValidator)
    {
        if (definition->admissionValidator(request.value, canonicalValues_))
        {
            return ScheduleResult::rejected(ScheduleRejectReason::invalidValue);
        }
    }

    nrAssert(nextSequence_ != 0u && nextSequence_ != std::numeric_limits<std::uint64_t>::max(),
             "Option log sequence exhausted");
    auto const sequence = nextSequence_++;
    pendingMutation_ = ScheduledMutation{std::move(request), sequence, bindingEpoch_, graphGeneration_};
    return ScheduleResult::accepted(sequence);
}

std::optional<RenderableFrameStart> OptionSystem::beginRenderableFrame()
{
    auto lock = std::scoped_lock{stateMutex_};
    if (gateState_ != GateState::open)
    {
        return std::nullopt;
    }
    nrAssert(frameIndex_ != std::numeric_limits<std::uint64_t>::max(), "Option frame index exhausted");
    ++frameIndex_;
    gateState_ = GateState::closed;

    auto result = RenderableFrameStart{.frameIndex = frameIndex_};
    if (pendingMutation_)
    {
        result.mutation.emplace(std::move(*pendingMutation_));
        pendingMutation_.reset();
    }
    return result;
}

ScheduleRejectReason OptionSystem::validateForExecutionLocked(const ScheduledMutation &mutation) const
{
    if (mutation.consumed_)
    {
        return ScheduleRejectReason::invalidParams;
    }
    if (gateState_ == GateState::shutdown)
    {
        return ScheduleRejectReason::shutdown;
    }
    if (mutation.admittedBindingEpoch_ != bindingEpoch_ || mutation.admittedGraphGeneration_ != graphGeneration_)
    {
        return ScheduleRejectReason::staleBinding;
    }
    auto const *definition = combinedCatalog_->find(mutation.request_.id);
    if (definition == nullptr)
    {
        return ScheduleRejectReason::unknownOption;
    }
    auto const validation = definition->schema.validate(mutation.request_.value);
    return validation.valid ? ScheduleRejectReason::none : ScheduleRejectReason::invalidValue;
}

ScheduleRejectReason OptionSystem::validateForExecution(const ScheduledMutation &mutation) const
{
    auto lock = std::scoped_lock{stateMutex_};
    return validateForExecutionLocked(mutation);
}

MutationCommitResult OptionSystem::commitCanonical(ScheduledMutation &&mutation)
{
    auto lock = std::scoped_lock{stateMutex_};
    auto const validation = validateForExecutionLocked(mutation);
    if (validation != ScheduleRejectReason::none)
    {
        mutation.consumed_ = true;
        return MutationCommitResult{.reason = validation};
    }

    auto const *definition = combinedCatalog_->find(mutation.request_.id);
    if (definition->lifetime != OptionValueLifetime::canonical)
    {
        mutation.consumed_ = true;
        return MutationCommitResult{.reason = ScheduleRejectReason::invalidParams};
    }
    canonicalValues_.insert_or_assign(mutation.request_.id, mutation.request_.value);
    mutation.consumed_ = true;
    return MutationCommitResult{.committed = true};
}

MutationCommitResult OptionSystem::commitModelAndCameraReset(ScheduledMutation &&mutation, CameraResetValues camera)
{
    auto lock = std::scoped_lock{stateMutex_};
    auto const validation = validateForExecutionLocked(mutation);
    if (validation != ScheduleRejectReason::none)
    {
        mutation.consumed_ = true;
        return MutationCommitResult{.reason = validation};
    }
    if (mutation.request_.id != optionId(keys::viewerModelSource))
    {
        mutation.consumed_ = true;
        return MutationCommitResult{.reason = ScheduleRejectReason::invalidParams};
    }

    auto derived = OptionValueMap{
        {optionId(keys::viewerCameraPose), OptionWireValue{std::move(camera.pose)}},
        {optionId(keys::viewerCameraVerticalFovDegrees), OptionWireValue{camera.verticalFovDegrees}},
        {optionId(keys::viewerCameraClipPlanes), OptionWireValue{std::move(camera.clipPlanes)}},
    };
    auto const validDerived = std::ranges::all_of(derived, [&](auto const &entry) {
        auto const *definition = combinedCatalog_->find(entry.first);
        return definition != nullptr && definition->scope == OptionScope::session &&
               definition->lifetime == OptionValueLifetime::canonical &&
               definition->schema.validate(entry.second).valid;
    });
    if (!validDerived)
    {
        mutation.consumed_ = true;
        return MutationCommitResult{.reason = ScheduleRejectReason::invalidValue};
    }

    canonicalValues_.insert_or_assign(mutation.request_.id, mutation.request_.value);
    std::ranges::for_each(derived,
                          [&](auto const &entry) { canonicalValues_.insert_or_assign(entry.first, entry.second); });
    mutation.consumed_ = true;
    return MutationCommitResult{.committed = true};
}

EffectMaterializationResult OptionSystem::materializeFrameEffect(ScheduledMutation &&mutation)
{
    auto lock = std::scoped_lock{stateMutex_};
    auto const validation = validateForExecutionLocked(mutation);
    if (validation != ScheduleRejectReason::none)
    {
        mutation.consumed_ = true;
        return EffectMaterializationResult{.reason = validation};
    }

    auto const *definition = combinedCatalog_->find(mutation.request_.id);
    if (definition->lifetime != OptionValueLifetime::frameEffect)
    {
        mutation.consumed_ = true;
        return EffectMaterializationResult{.reason = ScheduleRejectReason::invalidParams};
    }

    auto effect = FrameEffect{
        .sequence = mutation.sequence_,
        .id = mutation.request_.id,
        .input = mutation.request_.value,
        .origin = mutation.request_.origin,
        .requestId = mutation.request_.requestId,
    };
    mutation.consumed_ = true;
    return EffectMaterializationResult{.effect = std::move(effect)};
}

bool OptionSystem::discardMutation(ScheduledMutation &&mutation) noexcept
{
    if (mutation.consumed_)
    {
        return false;
    }
    mutation.consumed_ = true;
    return true;
}

std::shared_ptr<const OptionFrameSnapshot> OptionSystem::snapshotForCollection(std::optional<FrameEffect> effect) const
{
    auto lock = std::scoped_lock{stateMutex_};
    if (gateState_ != GateState::closed || !initialized_)
    {
        return {};
    }
    return makeSnapshotLocked({}, std::move(effect));
}

OptionAvailabilityMap OptionSystem::normalizedAvailability(const OptionCatalog &catalog,
                                                           const OptionAvailabilityMap &availability,
                                                           const std::optional<FrameEffect> &effect) const
{
    static const auto reasonSchema = OptionSchema::string(maximumAvailabilityReasonBytes);
    auto normalizeReason = [](OptionAvailability value) {
        if (!reasonSchema.validate(OptionWireValue{value.reason}).valid)
        {
            value.reason = "invalid_availability_reason";
        }
        return value;
    };
    auto normalized = OptionAvailabilityMap{};
    std::ranges::for_each(catalog.definitions(), [&](auto const &entry) {
        auto const provided = availability.find(entry.first);
        normalized.emplace(entry.first, normalizeReason(provided != availability.end() ? provided->second
                                                                                       : unavailable("not_collected")));
    });
    if (effect)
    {
        normalized.insert_or_assign(effect->id, unavailable("effect_in_flight"));
    }
    return normalized;
}

std::shared_ptr<const OptionFrameSnapshot> OptionSystem::makeSnapshotLocked(const OptionAvailabilityMap &availability,
                                                                            std::optional<FrameEffect> effect) const
{
    return std::make_shared<const OptionFrameSnapshot>(OptionFrameSnapshot{
        .catalog = combinedCatalog_,
        .values = canonicalValues_,
        .availability = normalizedAvailability(*combinedCatalog_, availability, effect),
        .frameIndex = frameIndex_,
        .revision = revision_,
        .graphGeneration = graphGeneration_,
        .bindingEpoch = bindingEpoch_,
        .snapshotToken = makeSnapshotToken(bindingEpoch_, graphGeneration_),
        .effect = std::move(effect),
    });
}

std::shared_ptr<const OptionFrameSnapshot> OptionSystem::publishRenderableFrame(
    const OptionAvailabilityMap &availability, std::optional<FrameEffect> effect)
{
    auto lock = std::scoped_lock{stateMutex_};
    if (gateState_ != GateState::closed || !initialized_)
    {
        return {};
    }
    nrAssert(revision_ != std::numeric_limits<std::uint64_t>::max(), "Option snapshot revision exhausted");
    ++revision_;
    auto next = makeSnapshotLocked(availability, std::move(effect));
    std::atomic_store_explicit(&publishedSnapshot_, next, std::memory_order_release);
    gateState_ = GateState::open;
    return next;
}

std::shared_ptr<const OptionFrameSnapshot> OptionSystem::snapshot() const noexcept
{
    return std::atomic_load_explicit(&publishedSnapshot_, std::memory_order_acquire);
}

std::shared_ptr<const OptionCatalog> OptionSystem::activeCatalog() const
{
    auto lock = std::scoped_lock{stateMutex_};
    return combinedCatalog_;
}

LiveBinding OptionSystem::liveBinding() const
{
    auto lock = std::scoped_lock{stateMutex_};
    return LiveBinding{
        .graphGeneration = graphGeneration_,
        .bindingEpoch = bindingEpoch_,
        .snapshotToken = makeSnapshotToken(bindingEpoch_, graphGeneration_),
    };
}

bool OptionSystem::hasPendingMutation() const
{
    auto lock = std::scoped_lock{stateMutex_};
    return pendingMutation_.has_value();
}

bool OptionSystem::admissionOpen() const
{
    auto lock = std::scoped_lock{stateMutex_};
    return gateState_ == GateState::open;
}

AuthorityMode OptionSystem::authorityMode() const
{
    auto lock = std::scoped_lock{stateMutex_};
    return authorityMode_;
}

bool OptionSystem::setAuthorityMode(AuthorityMode mode)
{
    auto lock = std::scoped_lock{stateMutex_};
    if (gateState_ == GateState::shutdown || pendingMutation_)
    {
        return false;
    }
    authorityMode_ = mode;
    return true;
}

std::optional<AbandonedMutation> OptionSystem::shutdown()
{
    auto lock = std::scoped_lock{stateMutex_};
    if (gateState_ == GateState::shutdown)
    {
        return {};
    }
    gateState_ = GateState::shutdown;
    if (!pendingMutation_)
    {
        return {};
    }

    auto abandoned = AbandonedMutation{
        .sequence = pendingMutation_->sequence_,
        .id = pendingMutation_->request_.id,
        .origin = pendingMutation_->request_.origin,
        .requestId = pendingMutation_->request_.requestId,
    };
    pendingMutation_.reset();
    return abandoned;
}

std::string OptionSystem::makeSessionIdentity()
{
    static auto nextSession = std::atomic<std::uint64_t>{1u};
    static auto const processNonce = [] {
        auto const wallClock = static_cast<std::uint64_t>(std::chrono::system_clock::now().time_since_epoch().count());
        auto const monotonicClock =
            static_cast<std::uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count());
        return std::rotl(wallClock, 23) ^ monotonicClock ^ 0x9e3779b97f4a7c15ull;
    }();
    auto const session = nextSession.fetch_add(1u, std::memory_order_relaxed);
    return std::format("{:016x}-{:016x}", processNonce, session);
}

std::string OptionSystem::makeSnapshotToken(std::uint64_t bindingEpoch, std::uint64_t graphGeneration) const
{
    return std::format("session-{}-binding-{}-graph-{}", sessionIdentity_, bindingEpoch, graphGeneration);
}

bool OptionSystem::originAllowed(MutationOrigin origin) const noexcept
{
    switch (authorityMode_)
    {
    case AuthorityMode::human:
        return origin == MutationOrigin::imgui || origin == MutationOrigin::camera;
    case AuthorityMode::agent:
        return origin == MutationOrigin::websocket;
    case AuthorityMode::offlineLua:
        return origin == MutationOrigin::lua;
    }
    std::unreachable();
}
} // namespace nr::options
