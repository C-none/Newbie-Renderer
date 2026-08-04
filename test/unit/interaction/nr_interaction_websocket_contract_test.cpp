import dependency.json;
import dependency.network;
import dependency.network.test;
import nr.interaction;
import nr.options;
import nr.test;
import std;

namespace
{
namespace json = dependency::json;
namespace network = dependency::network;
namespace networkTest = dependency::network::test;
namespace interaction = nr::interaction;
namespace options = nr::options;

[[nodiscard]] std::shared_ptr<const options::OptionCatalog> sessionCatalog()
{
    auto builder = options::OptionCatalogBuilder{};
    auto definitions = options::makeSessionDefinitions(options::SessionDefinitionSeed{
        .environmentName = "test_environment",
        .environmentNames = {"test_environment"},
    });
    std::ranges::for_each(definitions, [&](auto definition) { nr::test::require(builder.add(std::move(definition))); });
    auto result = builder.build();
    nr::test::require(result.valid());
    return result.catalog;
}

[[nodiscard]] options::OptionAvailabilityMap allAvailable(const options::OptionCatalog &catalog)
{
    auto result = options::OptionAvailabilityMap{};
    std::ranges::for_each(catalog.definitions(), [&](auto const &entry) {
        result.emplace(entry.first, options::OptionAvailability{.available = true, .reason = {}});
    });
    return result;
}

[[nodiscard]] std::unique_ptr<options::OptionSystem> agentSystem()
{
    auto system = std::make_unique<options::OptionSystem>(options::AuthorityMode::agent);
    auto catalog = sessionCatalog();
    nr::test::require(system->initializeSession(catalog, allAvailable(*catalog)).committed);
    return system;
}

[[nodiscard]] const json::JsonValue::Object &jsonObject(const json::JsonValue &value)
{
    auto const *object = std::get_if<json::JsonValue::Object>(&value.storage);
    nr::test::require(object != nullptr, "JSON value should be an object");
    return *object;
}

[[nodiscard]] const json::JsonValue &jsonField(const json::JsonValue::Object &object, std::string_view name)
{
    auto const found = object.find(name);
    nr::test::require(found != object.end(), "JSON object is missing an expected field");
    return found->second;
}

[[nodiscard]] json::JsonValue response(std::string_view text)
{
    auto parsed = json::parseJson(text);
    nr::test::require(parsed.valid(), "protocol response should be valid JSON");
    return std::move(*parsed.value);
}

[[nodiscard]] std::string serializeFixture(json::JsonValue value)
{
    auto output = std::string{};
    nr::test::requireEqual(json::serializeJson(value, output, 4u * 1024u), json::JsonError::none);
    return output;
}

[[nodiscard]] std::string applyRequest(std::uint64_t epoch, std::string_view requestId = "apply-1",
                                       bool includeUnexpectedField = false,
                                       std::string_view optionId = "viewer.window.fullscreen",
                                       json::JsonValue value = json::JsonValue{true})
{
    using Json = json::JsonValue;
    auto params = Json::Object{
        {"binding_epoch", Json{epoch}},
        {"id", Json{optionId}},
        {"value", std::move(value)},
    };
    if (includeUnexpectedField)
    {
        params.emplace("unexpected", Json{true});
    }
    return serializeFixture(Json{Json::Object{
        {"id", Json{requestId}},
        {"jsonrpc", Json{"2.0"}},
        {"method", Json{"option.apply"}},
        {"params", Json{std::move(params)}},
    }});
}

const nr::test::CaseRegistrar jsonBoundaryCase{
    "dependency JSON stays strict bounded and round-trippable", [] {
        auto parsed = json::parseJson(R"({"message":"hello","values":[1,2,3]})", 4u);
        nr::test::require(parsed.valid());

        auto serialized = std::string{};
        nr::test::requireEqual(json::serializeJson(*parsed.value, serialized, 256u), json::JsonError::none);
        nr::test::require(json::parseJson(serialized, 4u).valid());

        nr::test::require(!json::parseJson(R"({"trailing":true,})").valid());
        nr::test::requireEqual(json::parseJson(R"([[[[[]]]]])", 3u).error, json::JsonError::maximumDepth);

        auto tooSmall = std::string{};
        nr::test::requireEqual(json::serializeJson(*parsed.value, tooSmall, 4u), json::JsonError::responseTooLarge);
        nr::test::require(tooSmall.empty());

        auto invalidUtf8 = std::string{"invalid-"};
        invalidUtf8.push_back(static_cast<char>(0xff));
        auto invalidOutput = std::string{};
        nr::test::requireEqual(json::serializeJson(json::JsonValue{std::move(invalidUtf8)}, invalidOutput, 256u),
                               json::JsonError::invalidUtf8);
        nr::test::require(invalidOutput.empty());
    }};

const nr::test::CaseRegistrar handshakeCase{
    "WebSocket handshake policy enforces loopback endpoint auth Origin and one controller", [] {
        auto const token = std::string{"AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA"};
        auto valid = network::HandshakeRequest{
            .target = network::optionWebSocketTarget,
            .authorization = std::format("Bearer {}", token),
        };
        nr::test::requireEqual(network::evaluateHandshake(valid, token), network::HandshakeRejectReason::none);

        auto origin = valid;
        origin.originPresent = true;
        nr::test::requireEqual(network::evaluateHandshake(origin, token),
                               network::HandshakeRejectReason::originForbidden);

        auto wrongPath = valid;
        wrongPath.target = "/v1/options/";
        nr::test::requireEqual(network::evaluateHandshake(wrongPath, token),
                               network::HandshakeRejectReason::wrongTarget);

        auto query = valid;
        query.target = "/v1/options?token=secret";
        nr::test::requireEqual(network::evaluateHandshake(query, token), network::HandshakeRejectReason::wrongTarget);

        auto wrongToken = valid;
        wrongToken.authorization = "Bearer wrong";
        nr::test::requireEqual(network::evaluateHandshake(wrongToken, token),
                               network::HandshakeRejectReason::unauthorized);

        auto busy = valid;
        busy.controllerActive = true;
        nr::test::requireEqual(network::evaluateHandshake(busy, token), network::HandshakeRejectReason::controllerBusy);

        auto headers = valid;
        headers.headerBytes = 8193u;
        nr::test::requireEqual(network::evaluateHandshake(headers, token),
                               network::HandshakeRejectReason::headersTooLarge);
    }};

const nr::test::CaseRegistrar payloadCase{
    "WebSocket payload policy uses RFC close codes after message reassembly", [] {
        nr::test::require(network::evaluatePayload(network::PayloadKind::text, true, 256u * 1024u).accepted);
        nr::test::requireEqual(*network::evaluatePayload(network::PayloadKind::binary, true, 1u).closeCode,
                               network::WebSocketCloseCode::unsupportedData);
        nr::test::requireEqual(*network::evaluatePayload(network::PayloadKind::text, false, 1u).closeCode,
                               network::WebSocketCloseCode::invalidPayload);
        nr::test::requireEqual(*network::evaluatePayload(network::PayloadKind::text, true, 256u * 1024u + 1u).closeCode,
                               network::WebSocketCloseCode::messageTooBig);
    }};

const nr::test::CaseRegistrar readMethodsCase{
    "WebSocket protocol exposes only snapshot-backed read methods", [] {
        auto system = agentSystem();
        auto protocol = interaction::OptionRpcProtocol{*system};
        auto slot = std::string{};

        auto describe = protocol.handleText(
            R"({"jsonrpc":"2.0","id":"describe","method":"session.describe","params":{}})", {}, slot);
        nr::test::require(describe.responseReady && !describe.mutationStarted);
        auto describeJson = response(slot);
        auto const &describeResult = jsonObject(jsonField(jsonObject(describeJson), "result"));
        nr::test::requireEqual(std::get<std::string>(jsonField(describeResult, "authority_mode").storage),
                               std::string{"agent"});
        nr::test::require(!std::get<bool>(jsonField(describeResult, "tasks").storage));
        nr::test::requireEqual(std::get<std::string>(jsonField(describeResult, "final_result_channel").storage),
                               std::string{"rotating-ndjson-file"});
        auto const finalResultPath = std::get<std::string>(jsonField(describeResult, "final_result_path").storage);
        nr::test::require(finalResultPath.ends_with("build/app/logs/options.ndjson"),
                          "final result path should identify the active option NDJSON log");
        nr::test::requireEqual(std::get<std::string>(jsonField(describeResult, "final_result_schema").storage),
                               std::string{"NR_OPTION_V1"});

        auto snapshot = protocol.handleText(
            R"({"jsonrpc":"2.0","id":"snapshot","method":"option.snapshot","params":{}})", {}, slot);
        nr::test::require(snapshot.responseReady);
        auto snapshotJson = response(slot);
        auto const &snapshotResult = jsonObject(jsonField(jsonObject(snapshotJson), "result"));
        auto const &records = std::get<json::JsonValue::Array>(jsonField(snapshotResult, "options").storage);
        nr::test::require(!records.empty());
        nr::test::requireEqual(std::get<std::uint64_t>(jsonField(snapshotResult, "schema_version").storage),
                               std::uint64_t{1u});
        auto const exitRecord = std::ranges::find_if(records, [](auto const &candidate) {
            auto const &record = jsonObject(candidate);
            return std::get<std::string>(jsonField(record, "id").storage) == "viewer.exit";
        });
        nr::test::require(exitRecord != records.end(), "WebSocket discovery must expose viewer.exit");
        auto const &exitObject = jsonObject(*exitRecord);
        nr::test::requireEqual(std::get<std::string>(jsonField(exitObject, "control").storage), std::string{"button"});
        nr::test::requireEqual(std::get<std::string>(jsonField(exitObject, "group").storage), std::string{"Viewer"});
        nr::test::requireEqual(std::get<std::string>(jsonField(exitObject, "title").storage), std::string{"Exit"});
        nr::test::require(std::get<json::JsonValue::Object>(jsonField(exitObject, "value").storage).empty());

        auto get = protocol.handleText(
            R"({"jsonrpc":"2.0","id":"get","method":"option.get","params":{"id":"viewer.window.fullscreen"}})", {},
            slot);
        nr::test::require(get.responseReady);
        auto getJson = response(slot);
        auto const &getResult = jsonObject(jsonField(jsonObject(getJson), "result"));
        auto const &record = jsonObject(jsonField(getResult, "option"));
        nr::test::requireEqual(std::get<std::string>(jsonField(record, "id").storage),
                               std::string{"viewer.window.fullscreen"});
    }};

const nr::test::CaseRegistrar admissionCase{
    "started response is prepared before admission and transport loss never rolls back", [] {
        auto system = agentSystem();
        auto protocol = interaction::OptionRpcProtocol{*system};
        auto slot = std::string{};
        slot.reserve(256u * 1024u);
        auto const originalCapacity = slot.capacity();

        auto result = protocol.handleText(applyRequest(system->liveBinding().bindingEpoch), {}, slot);
        nr::test::require(result.responseReady && result.mutationStarted);
        nr::test::requireEqual(slot.capacity(), originalCapacity, "started response must use reserved capacity");
        auto startedJson = response(slot);
        auto const &startedResult = jsonObject(jsonField(jsonObject(startedJson), "result"));
        nr::test::requireEqual(std::get<std::string>(jsonField(startedResult, "status").storage),
                               std::string{"started"});
        nr::test::require(system->hasPendingMutation());

        slot.clear();
        auto reconnectedProtocol = interaction::OptionRpcProtocol{*system};
        auto duplicate =
            reconnectedProtocol.handleText(applyRequest(system->liveBinding().bindingEpoch, "apply-2"), {}, slot);
        nr::test::require(duplicate.responseReady && !duplicate.mutationStarted);
        auto duplicateJson = response(slot);
        auto const &error = jsonObject(jsonField(jsonObject(duplicateJson), "error"));
        auto const &data = jsonObject(jsonField(error, "data"));
        nr::test::requireEqual(std::get<std::string>(jsonField(data, "reason").storage), std::string{"operation_busy"});
        nr::test::require(system->hasPendingMutation(), "disconnect/reconnect must not cancel the admitted mutation");
    }};

const nr::test::CaseRegistrar exitAdmissionCase{
    "WebSocket exit uses the shared empty-object frame-effect option", [] {
        auto system = agentSystem();
        auto protocol = interaction::OptionRpcProtocol{*system};
        auto slot = std::string{};
        auto started = protocol.handleText(applyRequest(system->liveBinding().bindingEpoch, "exit-1", false,
                                                        "viewer.exit", json::JsonValue{json::JsonValue::Object{}}),
                                           {}, slot);
        nr::test::require(started.responseReady && started.mutationStarted);

        auto frame = system->beginRenderableFrame();
        nr::test::require(frame && frame->mutation);
        auto materialized = system->materializeFrameEffect(std::move(*frame->mutation));
        nr::test::require(materialized.effect.has_value());
        nr::test::requireEqual(materialized.effect->id, options::optionId(options::keys::viewerExit));
        nr::test::requireEqual(materialized.effect->origin, options::MutationOrigin::websocket);
        nr::test::require(system->publishRenderableFrame(allAvailable(*system->activeCatalog())) != nullptr);
    }};

const nr::test::CaseRegistrar fragmentedAndProfileCase{
    "reassembled fragmented requests work while batch notification and extra fields never schedule", [] {
        auto system = agentSystem();
        auto protocol = interaction::OptionRpcProtocol{*system};
        auto slot = std::string{};
        auto complete = applyRequest(system->liveBinding().bindingEpoch);
        auto firstFragment = complete.substr(0u, complete.size() / 2u);
        auto secondFragment = complete.substr(complete.size() / 2u);
        auto reassembled = firstFragment + secondFragment;
        auto started = protocol.handleText(reassembled, {}, slot);
        nr::test::require(started.mutationStarted);

        auto frame = system->beginRenderableFrame();
        nr::test::require(frame && frame->mutation);
        nr::test::require(system->discardMutation(std::move(*frame->mutation)));
        nr::test::require(system->publishRenderableFrame(allAvailable(*system->activeCatalog())) != nullptr);

        auto batch =
            protocol.handleText(R"([{"jsonrpc":"2.0","id":"batch","method":"option.snapshot","params":{}}])", {}, slot);
        nr::test::require(batch.responseReady && !batch.mutationStarted);
        nr::test::require(!system->hasPendingMutation());

        auto notification = protocol.handleText(
            R"({"jsonrpc":"2.0","method":"option.apply","params":{"id":"viewer.window.fullscreen","value":true,"binding_epoch":1}})",
            {}, slot);
        nr::test::require(!notification.responseReady && !notification.mutationStarted);
        nr::test::require(!system->hasPendingMutation());

        auto extra = protocol.handleText(applyRequest(system->liveBinding().bindingEpoch, "extra", true), {}, slot);
        nr::test::require(extra.responseReady && !extra.mutationStarted);
        nr::test::require(!system->hasPendingMutation());
    }};

const nr::test::CaseRegistrar rateLimitCase{
    "transport rate rejection is explicit and cannot reserve the option slot", [] {
        auto system = agentSystem();
        auto protocol = interaction::OptionRpcProtocol{*system};
        auto slot = std::string{};
        auto result = protocol.handleText(applyRequest(system->liveBinding().bindingEpoch),
                                          network::MessageContext{.rateLimited = true}, slot);
        nr::test::require(result.responseReady && !result.mutationStarted);
        nr::test::require(!system->hasPendingMutation());

        auto json = response(slot);
        auto const &error = jsonObject(jsonField(jsonObject(json), "error"));
        auto const &data = jsonObject(jsonField(error, "data"));
        nr::test::requireEqual(std::get<std::string>(jsonField(data, "reason").storage), std::string{"rate_limited"});
    }};

const nr::test::CaseRegistrar liveTransportHandshakeAndLockstepCase{
    "live WebSocket transport enforces handshake ownership lockstep fragmentation and reconnect", [] {
        auto const token = std::string{"AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA"};
        auto system = agentSystem();
        auto protocol = interaction::OptionRpcProtocol{*system};
        auto server = network::LoopbackWebSocketServer{};
        auto started = server.start(
            network::WebSocketServerConfig{.bearerToken = token},
            [&](std::string_view payload, const network::MessageContext &context, std::string &responseSlot) {
                return protocol.handleText(payload, context, responseSlot);
            });
        nr::test::require(started.started, started.detail);
        nr::test::requireEqual(started.endpoint.address, std::string{"127.0.0.1"});
        nr::test::require(started.endpoint.port != 0u);

        {
            auto unauthorized = networkTest::WebSocketClient{};
            auto connected = unauthorized.connect(started.endpoint, "wrong-token");
            nr::test::require(!connected.connected);
            nr::test::requireEqual(connected.httpStatus, std::uint16_t{401u}, connected.detail);
        }
        {
            auto origin = networkTest::WebSocketClient{};
            auto connected = origin.connect(started.endpoint, token, std::string{"https://example.invalid"});
            nr::test::require(!connected.connected);
            nr::test::requireEqual(connected.httpStatus, std::uint16_t{403u}, connected.detail);
        }

        auto controller = networkTest::WebSocketClient{};
        auto connected = controller.connect(started.endpoint, token);
        nr::test::require(connected.connected, connected.detail);
        nr::test::requireEqual(connected.httpStatus, std::uint16_t{101u});

        {
            auto secondController = networkTest::WebSocketClient{};
            auto secondConnected = secondController.connect(started.endpoint, token);
            nr::test::require(!secondConnected.connected);
            nr::test::requireEqual(secondConnected.httpStatus, std::uint16_t{503u}, secondConnected.detail);
        }

        auto firstRequest =
            std::string{R"({"jsonrpc":"2.0","id":"live-describe","method":"session.describe","params":{}})"};
        auto firstWrite = controller.writeText(firstRequest);
        nr::test::require(firstWrite.written, firstWrite.detail);
        auto firstRead = controller.read();
        nr::test::require(firstRead.messageReceived, firstRead.detail);
        auto firstResponse = response(firstRead.text);
        nr::test::requireEqual(std::get<std::string>(jsonField(jsonObject(firstResponse), "id").storage),
                               std::string{"live-describe"});

        auto fragmentedRequest =
            std::string{R"({"jsonrpc":"2.0","id":"live-snapshot","method":"option.snapshot","params":{}})"};
        auto fragments = std::array<std::string_view, 3u>{
            std::string_view{fragmentedRequest}.substr(0u, 19u),
            std::string_view{fragmentedRequest}.substr(19u, 23u),
            std::string_view{fragmentedRequest}.substr(42u),
        };
        auto fragmentedWrite = controller.writeTextFragments(fragments);
        nr::test::require(fragmentedWrite.written, fragmentedWrite.detail);
        auto fragmentedRead = controller.read();
        nr::test::require(fragmentedRead.messageReceived, fragmentedRead.detail);
        auto fragmentedResponse = response(fragmentedRead.text);
        nr::test::requireEqual(std::get<std::string>(jsonField(jsonObject(fragmentedResponse), "id").storage),
                               std::string{"live-snapshot"});
        auto const &snapshotResult = jsonObject(jsonField(jsonObject(fragmentedResponse), "result"));
        nr::test::require(!std::get<json::JsonValue::Array>(jsonField(snapshotResult, "options").storage).empty());

        controller.close();
        auto reconnected = networkTest::WebSocketClient{};
        auto reconnectResult = reconnected.connect(started.endpoint, token);
        nr::test::require(reconnectResult.connected, reconnectResult.detail);
        auto reconnectWrite = reconnected.writeText(firstRequest);
        nr::test::require(reconnectWrite.written, reconnectWrite.detail);
        auto reconnectRead = reconnected.read();
        nr::test::require(reconnectRead.messageReceived, reconnectRead.detail);
        reconnected.close();
        server.stop();
    }};

const nr::test::CaseRegistrar liveTransportLostStartedResponseCase{
    "live transport loss after admission preserves the pending mutation and reports the lost started response once",
    [] {
        auto const token = std::string{"AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA"};
        auto system = agentSystem();
        auto protocol = interaction::OptionRpcProtocol{*system};
        auto events = std::vector<network::TransportEvent>{};
        auto eventsMutex = std::mutex{};
        auto eventsChanged = std::condition_variable{};
        auto config = network::WebSocketServerConfig{.bearerToken = token};
        config.limits.outboundBytesPerSecond = 512.0;
        config.limits.outboundBurstBytes = static_cast<double>(config.limits.maximumResponseBytes);
        auto const maximumResponseBytes = config.limits.maximumResponseBytes;

        auto server = network::LoopbackWebSocketServer{};
        auto started = server.start(
            std::move(config),
            [&, maximumResponseBytes](std::string_view payload, const network::MessageContext &context,
                                      std::string &responseSlot) {
                if (payload == "drain-outbound-burst")
                {
                    responseSlot.assign(maximumResponseBytes, 'x');
                    return network::TextMessageResult{.responseReady = true};
                }
                return protocol.handleText(payload, context, responseSlot);
            },
            [&](const network::TransportEvent &event) {
                {
                    auto lock = std::scoped_lock{eventsMutex};
                    events.push_back(event);
                }
                eventsChanged.notify_all();
            });
        nr::test::require(started.started, started.detail);

        auto controller = networkTest::WebSocketClient{};
        auto connected = controller.connect(started.endpoint, token);
        nr::test::require(connected.connected, connected.detail);
        auto drainWritten = controller.writeText("drain-outbound-burst");
        nr::test::require(drainWritten.written, drainWritten.detail);
        auto drainRead = controller.read();
        nr::test::require(drainRead.messageReceived, drainRead.detail);
        nr::test::requireEqual(drainRead.text.size(), maximumResponseBytes);
        auto written = controller.writeText(applyRequest(system->liveBinding().bindingEpoch, "lost-started"));
        nr::test::require(written.written, written.detail);

        auto const pendingDeadline = std::chrono::steady_clock::now() + std::chrono::seconds{2};
        while (!system->hasPendingMutation() && std::chrono::steady_clock::now() < pendingDeadline)
        {
            std::this_thread::yield();
        }
        nr::test::require(system->hasPendingMutation(),
                          "the apply request should reach admission before the transport is dropped");
        controller.abort();

        {
            auto lock = std::unique_lock{eventsMutex};
            auto const completed = eventsChanged.wait_for(lock, std::chrono::seconds{3}, [&] {
                auto const lostResponse = std::ranges::any_of(events, [](auto const &event) {
                    return event.kind == network::TransportEventKind::responseWriteFailed && event.startedResponseLost;
                });
                auto const connectionClosed = std::ranges::any_of(events, [](auto const &event) {
                    return event.kind == network::TransportEventKind::connectionClosed;
                });
                return lostResponse && connectionClosed;
            });
            nr::test::require(completed, "the aborted transport should produce terminal transport events");

            auto const lostEvents =
                std::ranges::count_if(events, [](auto const &event) { return event.startedResponseLost; });
            auto const duplicateClosedEvents = std::ranges::count_if(events, [](auto const &event) {
                return event.kind == network::TransportEventKind::connectionClosed && event.startedResponseLost;
            });
            nr::test::requireEqual(lostEvents, std::ptrdiff_t{1},
                                   "the lost started response must be reported exactly once");
            nr::test::requireEqual(duplicateClosedEvents, std::ptrdiff_t{0},
                                   "connection close must not repeat a consumed lost-response event");
        }
        nr::test::require(system->hasPendingMutation(), "transport loss must not cancel an admitted mutation");

        auto reconnected = networkTest::WebSocketClient{};
        auto reconnectResult = reconnected.connect(started.endpoint, token);
        nr::test::require(reconnectResult.connected, reconnectResult.detail);
        auto retryWrite = reconnected.writeText(applyRequest(system->liveBinding().bindingEpoch, "busy-after-loss"));
        nr::test::require(retryWrite.written, retryWrite.detail);
        auto retryRead = reconnected.read();
        nr::test::require(retryRead.messageReceived, retryRead.detail);
        auto retryJson = response(retryRead.text);
        auto const &retryError = jsonObject(jsonField(jsonObject(retryJson), "error"));
        auto const &retryData = jsonObject(jsonField(retryError, "data"));
        nr::test::requireEqual(std::get<std::string>(jsonField(retryData, "reason").storage),
                               std::string{"operation_busy"});
        nr::test::require(system->hasPendingMutation(),
                          "the original admitted mutation should remain pending after reconnect");
        reconnected.close();
        server.stop();
    }};

const nr::test::CaseRegistrar liveTransportCloseCodeCase{
    "live WebSocket transport returns RFC close codes for binary invalid UTF-8 and oversized text", [] {
        auto const token = std::string{"AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA"};
        auto server = network::LoopbackWebSocketServer{};
        auto started = server.start(network::WebSocketServerConfig{.bearerToken = token},
                                    [](std::string_view, const network::MessageContext &, std::string &) {
                                        return network::TextMessageResult{};
                                    });
        nr::test::require(started.started, started.detail);

        {
            auto binary = networkTest::WebSocketClient{};
            auto connected = binary.connect(started.endpoint, token);
            nr::test::require(connected.connected, connected.detail);
            auto written = binary.writeBinary("binary");
            nr::test::require(written.written, written.detail);
            auto closed = binary.read();
            nr::test::require(closed.closeCode.has_value(), closed.detail);
            nr::test::requireEqual(*closed.closeCode,
                                   static_cast<std::uint16_t>(network::WebSocketCloseCode::unsupportedData));
        }

        {
            auto invalidUtf8 = networkTest::WebSocketClient{};
            auto connected = invalidUtf8.connect(started.endpoint, token);
            nr::test::require(connected.connected, connected.detail);
            auto payload = std::string(1u, static_cast<char>(0xffu));
            auto written = invalidUtf8.writeText(payload);
            nr::test::require(written.written, written.detail);
            auto closed = invalidUtf8.read();
            nr::test::require(closed.closeCode.has_value(), closed.detail);
            nr::test::requireEqual(*closed.closeCode,
                                   static_cast<std::uint16_t>(network::WebSocketCloseCode::invalidPayload));
        }

        {
            auto oversized = networkTest::WebSocketClient{};
            auto connected = oversized.connect(started.endpoint, token);
            nr::test::require(connected.connected, connected.detail);
            auto prefix = std::string(256u * 1024u, 'x');
            auto fragments = std::array<std::string_view, 2u>{
                prefix,
                std::string_view{"x"},
            };
            auto written = oversized.writeTextFragments(fragments);
            nr::test::require(written.written, written.detail);
            auto closed = oversized.read();
            nr::test::require(closed.closeCode.has_value(), closed.detail);
            nr::test::requireEqual(*closed.closeCode,
                                   static_cast<std::uint16_t>(network::WebSocketCloseCode::messageTooBig));
        }
        server.stop();
    }};
} // namespace
