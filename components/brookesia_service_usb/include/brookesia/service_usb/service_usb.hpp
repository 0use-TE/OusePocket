/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include <cstdint>
#include <expected>
#include <functional>
#include <mutex>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "boost/json.hpp"
#include "brookesia/service_manager/service/base.hpp"
#include "brookesia/service_helper/system/usb.hpp"
#include "brookesia/service_usb/macro_configs.h"

namespace esp_brookesia::service {

/**
 * @brief USB Serial/JTAG service used for host control and file transfer.
 */
class Usb final : public ServiceBase {
public:
    using Helper = helper::Usb;
    using HostCommand = Helper::HostCommand;
    using TransferArtifact = Helper::TransferArtifact;
    using HostCommandHandler = std::function<std::expected<void, std::string>(
                                   HostCommand command, const TransferArtifact &artifact, std::string_view destination
                               )>;
    struct ServiceCallError {
        std::string code;
        std::string message;
    };
    using ServiceCallResult = std::expected<boost::json::value, ServiceCallError>;
    using ServiceCallHandler = std::function<ServiceCallResult(
                                   std::string_view service, std::string_view function, const boost::json::object &args, uint32_t timeout_ms
                               )>;

    /**
     * @brief Get the process-wide service instance.
     */
    static Usb &get_instance();

    /**
     * @brief Register the system-level handler for completed host transfers.
     *
     * The callback is non-owning from the service perspective and must remain
     * valid until clear_host_command_bridge() is called.
     */
    bool register_host_command_bridge(HostCommandHandler handler);

    /**
     * @brief Register the service-call bridge owned by system core.
     */
    bool register_service_call_bridge(ServiceCallHandler handler);

    /**
     * @brief Remove the currently registered system-level transfer handler.
     */
    void clear_host_command_bridge();

    /**
     * @brief Remove the service-call bridge.
     */
    void clear_service_call_bridge();

    /** Optional board hook: mute only the console while retaining log capture.
     * Set before starting the receiver; the callback must outlive the service.
     */
    void set_console_quiet_handler(std::function<void(bool)> handler);

    /** Stop accepting work and join both workers before Core tears down its bridges.
     * A timeout retains all resources; call again after the pending operation finishes.
     * Call from the system lifecycle thread, never from a USB bridge callback.
     */
    std::expected<void, std::string> quiesce(uint32_t timeout_ms = 10000);

private:
    static std::string get_component_version();

    Usb();
    ~Usb() override = default;

    bool on_init() override;
    void on_deinit() override;
    bool on_start() override;
    void on_stop() override;
    std::vector<FunctionSchema> get_function_schemas() override;
    std::vector<EventSchema> get_event_schemas() override;
    FunctionHandlerMap get_function_handlers() override;

    std::expected<boost::json::object, std::string> function_get_status();
    std::expected<boost::json::object, std::string> function_get_transfer_status();
    std::expected<void, std::string> function_abort_transfer(uint32_t request_id);

    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace esp_brookesia::service
