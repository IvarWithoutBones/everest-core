// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2025 Pionix GmbH and Contributors to EVerest

#ifndef INFY_CAN_DEVICE_HPP
#define INFY_CAN_DEVICE_HPP

#include "CanDevice.hpp"
#include <atomic>
#include <linux/can.h>
#include <map>
#include <mutex>
#include <optional>
#include <sigslot/signal.hpp>
#include <thread>

class InfyCanDevice : public CanDevice {
public:
    InfyCanDevice();
    ~InfyCanDevice();

    /// Configures the device driver. This must only be called once before the device is used, as it is not thread-safe.
    void set_config(uint8_t group_address, uint8_t controller_address);

    enum class OutputMode {
        Parallel = 0xA0,
        Series = 0xA1,
        Automatic = 0xA2
    };

    // Commands

    bool switch_on_off(bool on);
    bool set_walkin_enabled(bool on);
    bool set_inverter_mode(bool inverter);
    bool set_voltage_current(float voltage, float current, bool mode_export);
    bool set_output_mode(OutputMode mode);
    bool adjust_power_factor(float pf);
    bool set_generic_setting(uint8_t byte0, uint8_t byte1, uint32_t value);

    bool request_rx(const uint8_t device_number, const std::vector<uint8_t>& addresses,
                    const std::vector<uint8_t>& payload);

    struct Telemetry {
        float ac_ab_line_voltage{0.};
        float ac_bc_line_voltage{0.};
        float ac_ca_line_voltage{0.};
        float ambient_temperature{0.};
        float ac_phase_a_current{0.};
        float ac_phase_b_current{0.};
        float ac_phase_c_current{0.};
        float ac_phase_a_voltage{0.};
        float ac_phase_b_voltage{0.};
        float ac_phase_c_voltage{0.};
        float ac_frequency{0.};
        float ac_phase_a_active_power{0.};
        float ac_phase_b_active_power{0.};
        float ac_phase_c_active_power{0.};
        float ac_total_active_ower{0.};

        float ac_phase_a_reactive_power{0.};
        float ac_phase_b_reactive_power{0.};
        float ac_phase_c_reactive_power{0.};
        float ac_total_reactive_ower{0.};

        float ac_phase_a_apparent_power{0.};
        float ac_phase_b_apparent_power{0.};
        float ac_phase_c_apparent_power{0.};
        float ac_total_apparent_ower{0.};

        std::optional<can_packet_acdc::DcMinOutputVoltage> dc_min_output_voltage{};
        std::optional<can_packet_acdc::DcMaxOutputVoltage> dc_max_output_voltage{};
        std::optional<can_packet_acdc::DcMaxOutputCurrent> dc_max_output_current{};
        std::optional<can_packet_acdc::DcRatedOutputPower> dc_rated_output_power{};

        can_packet_acdc::BusDCVoltage bus_voltage;
        can_packet_acdc::BusDCCurrent bus_current;
        can_packet_acdc::BatteryDCVoltage battery_voltage;
        can_packet_acdc::BatteryDCCurrent battery_current;
        can_packet_acdc::PowerModuleStatus status;

        std::chrono::time_point<std::chrono::steady_clock> last_update;

        bool has_all_limits() const;
    };

    typedef std::map<uint8_t, Telemetry> TelemetryMap;

    /// A map of module addresses to their corresponding telemetry data.
    TelemetryMap telemetries{};
    /// A mutex to synchronize access to `telemetries`.
    std::mutex telemetries_mutex{};

    // Capabilities. Used to configure the battery side limits when controlling the bus side (and vice versa).
    std::atomic<float> max_export_current_A{};
    std::atomic<float> max_import_current_A{};

    // Data out
    sigslot::signal<can_packet_acdc::PowerModuleStatus, can_packet_acdc::InverterStatus> signalModuleStatus;
    sigslot::signal<TelemetryMap> signalVoltageCurrent;
    sigslot::signal<TelemetryMap> signalCapabilitiesUpdate;

    friend std::ostream& operator<<(std::ostream& out, const Telemetry& self);

protected:
    virtual void rx_handler(uint32_t can_id, const std::vector<uint8_t>& payload);

private:
    std::atomic_bool exitTxThread;
    std::thread txThreadHandle;
    void txThread();

    bool tx(const uint8_t device_number, const std::vector<uint8_t>& addresses, const std::vector<uint8_t>& payload);

    void handle_module_packet(Telemetry& telemetry, const std::vector<uint8_t>& payload, uint16_t packet_type);
    void handle_group_packet(const uint8_t source, const std::vector<uint8_t>& payload, const uint16_t packet_type);

    // Static configuration, safe to access from multiple threads as it is only set once during initialization.
    uint8_t group_address{};
    uint8_t controller_address{};

    /// The time we last received error 0x07 (in start processing). Automatic address allocation finishes ~1s later.
    /// Only accessed from the RX thread, so no synchronization is needed.
    std::optional<std::chrono::steady_clock::time_point> last_in_start_processing_error{std::nullopt};

    // Dynamic configuration, will be changed at runtime.
    std::atomic<float> setpoint_export_voltage{0}, setpoint_export_current{0};
    std::atomic<float> setpoint_import_voltage{0}, setpoint_import_current{0};
    std::atomic_bool on{false};
    std::atomic_bool walkin_enable{false};
    std::atomic_bool inverter_mode{false};

    std::mutex settingsMutex{};
};

#endif // INFY_CAN_DEVICE_HPP
