// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2025 Pionix GmbH and Contributors to EVerest

#include "power_supply_DCImpl.hpp"
#include <cmath>
#include <iomanip>

namespace module {
namespace main {

namespace {
constexpr double voltage_log_threshold_v = 0.5;
constexpr double current_log_threshold_a = 0.2;

bool should_log_setpoint(std::optional<double>& last_voltage, std::optional<double>& last_current, double voltage,
                         double current) {
    if (!last_voltage.has_value() || !last_current.has_value() ||
        std::abs(voltage - *last_voltage) >= voltage_log_threshold_v ||
        std::abs(current - *last_current) >= current_log_threshold_a) {
        last_voltage = voltage;
        last_current = current;
        return true;
    }

    return false;
}
} // namespace

void power_supply_DCImpl::init() {
    mod->acdc.signalVoltageCurrent.connect([this](InfyCanDevice::TelemetryMap telemetries) {
        types::power_supply_DC::VoltageCurrent vc{};
        const auto inverter_mode = last_publish_mode == types::power_supply_DC::Mode::Import;

        for (const auto& [address, telemetry] : telemetries) {
            if (inverter_mode) {
                vc.voltage_V += telemetry.bus_voltage.volt;
                vc.current_A += telemetry.bus_current.ampere;
            } else {
                vc.voltage_V += telemetry.battery_voltage.volt;
                vc.current_A += telemetry.battery_current.ampere;
            }
        }

        // Get the average voltage across all modules.
        if (!telemetries.empty())
            vc.voltage_V /= telemetries.size();

        if (last_publish_mode == types::power_supply_DC::Mode::Import) {
            // According to ISO 15118-20 V2G20-1034 / V2G20-1035,
            // negative current indicates EV -> EVSE power transfer (discharging).
            vc.current_A = -vc.current_A;
        }

        publish_voltage_current(vc);
    });

    mod->acdc.signalModuleStatus.connect(
        [this](can_packet_acdc::PowerModuleStatus status, can_packet_acdc::InverterStatus inverter_status) {
            static bool firsttime = true;

            // Publish mode changes
            types::power_supply_DC::Mode mode;

            if (status.fault_alarm) {
                mode = types::power_supply_DC::Mode::Fault;
            } else if (status.dc_side_off) {
                mode = types::power_supply_DC::Mode::Off;
            } else if (inverter_status.invert_mode) {
                mode = types::power_supply_DC::Mode::Import;
            } else {
                mode = types::power_supply_DC::Mode::Export;
            }

            if (last_publish_mode != mode || firsttime) {
                publish_mode(mode);
                last_publish_mode = mode;
                firsttime = false;
            }
        });

    mod->acdc.signalCapabilitiesUpdate.connect([this](InfyCanDevice::TelemetryMap telemetries) {
        types::power_supply_DC::Capabilities new_caps;
        new_caps.bidirectional = true;

        new_caps.current_regulation_tolerance_A = mod->config.current_regulation_tolerance_A;
        new_caps.peak_current_ripple_A = mod->config.peak_current_ripple_A;
        new_caps.conversion_efficiency_import = mod->config.conversion_efficiency_import;
        new_caps.conversion_efficiency_export = mod->config.conversion_efficiency_export;

        if (telemetries.empty()) {
            // No modules are connected, we cannot import/export anything.
            new_caps.max_import_current_A = 0;
            new_caps.min_import_current_A = 0;
            new_caps.max_import_power_W = 0;
            new_caps.min_import_voltage_V = 0;
            new_caps.max_import_voltage_V = 0;

            new_caps.min_export_current_A = 0.0;
            new_caps.max_export_current_A = 0.0;
            new_caps.min_export_voltage_V = 0.0;
            new_caps.max_export_voltage_V = 0.0;
            new_caps.max_export_power_W = 0.0;

            caps = new_caps;
            publish_capabilities(new_caps);
            return;
        }

        // There is no way to query the import limits from the power supply itself,
        // so we use statically defined config values instead.
        new_caps.max_import_current_A = mod->config.max_import_current_A;
        new_caps.min_import_current_A = mod->config.min_import_current_A;
        new_caps.min_import_voltage_V = mod->config.min_import_voltage_V;
        new_caps.max_import_voltage_V = mod->config.max_import_voltage_V;
        new_caps.max_import_power_W = mod->config.max_import_power_W;

        new_caps.min_export_current_A = mod->config.min_export_current_A;
        new_caps.min_export_voltage_V = std::numeric_limits<float>::min();
        new_caps.max_export_voltage_V = std::numeric_limits<float>::max();
        new_caps.max_export_current_A = 0.0;
        new_caps.max_export_power_W = 0.0;

        for (const auto& [address, telemetry] : telemetries) {
            if (telemetry.dc_min_output_voltage.has_value())
                new_caps.min_export_voltage_V =
                    std::max(new_caps.min_export_voltage_V, telemetry.dc_min_output_voltage.value().volt);

            if (telemetry.dc_max_output_voltage.has_value())
                new_caps.max_export_voltage_V =
                    std::min(new_caps.max_export_voltage_V, telemetry.dc_max_output_voltage.value().volt);

            if (telemetry.dc_max_output_current.has_value())
                new_caps.max_export_current_A += telemetry.dc_max_output_current.value().ampere;
            if (telemetry.dc_rated_output_power.has_value())
                new_caps.max_export_power_W += telemetry.dc_rated_output_power.value().watt;
        }

        caps = new_caps;
        mod->acdc.max_export_current_A = new_caps.max_export_current_A;
        mod->acdc.max_import_current_A = new_caps.max_import_current_A.value();

        EVLOG_info << "Infy: Capabilities updated: export = " << new_caps.min_export_voltage_V << "V / "
                   << new_caps.max_export_voltage_V << "V, " << new_caps.min_export_current_A << "A / "
                   << new_caps.max_export_current_A << "A, " << new_caps.max_export_power_W << "W; "
                   << "import = " << new_caps.min_import_voltage_V.value() << "V / "
                   << new_caps.max_import_voltage_V.value() << "V, " << new_caps.min_import_current_A.value() << "A / "
                   << new_caps.max_import_current_A.value() << "A, " << new_caps.max_import_power_W.value() << "W";
        publish_capabilities(new_caps);
    });

    mod->acdc.switch_on_off(false);
    mod->acdc.adjust_power_factor(1.0);
    mod->acdc.set_output_mode(InfyCanDevice::OutputMode::Automatic);
}

void power_supply_DCImpl::ready() {
}

void power_supply_DCImpl::handle_setMode(types::power_supply_DC::Mode& mode,
                                         types::power_supply_DC::ChargingPhase& phase) {
    std::scoped_lock lock(settings_mutex);

    if (mode != last_publish_mode) {
        last_logged_export_voltage.reset();
        last_logged_export_current.reset();
        last_logged_import_voltage.reset();
        last_logged_import_current.reset();
    }

    if (mode == types::power_supply_DC::Mode::Off) {
        mod->acdc.switch_on_off(false);
        mod->acdc.set_inverter_mode(false);
    } else if (mode == types::power_supply_DC::Mode::Export) {
        mod->acdc.set_inverter_mode(false);
        mod->acdc.switch_on_off(true);
    } else if (mode == types::power_supply_DC::Mode::Import) {
        mod->acdc.set_inverter_mode(true);
        mod->acdc.switch_on_off(true);
    } else if (mode == types::power_supply_DC::Mode::Fault) {
        mod->acdc.switch_on_off(false);
        mod->acdc.set_inverter_mode(false);
    }
};

void power_supply_DCImpl::handle_setExportVoltageCurrent(double& voltage, double& current) {

    if (voltage > caps.max_export_voltage_V)
        voltage = caps.max_export_voltage_V;
    else if (voltage < caps.min_export_voltage_V)
        voltage = caps.min_export_voltage_V;

    if (current > caps.max_export_current_A)
        current = caps.max_export_current_A;
    else if (current < caps.min_export_current_A)
        current = caps.min_export_current_A;

    std::scoped_lock lock(settings_mutex);

    exportVoltage = voltage;
    exportCurrentLimit = current;

    if (should_log_setpoint(this->last_logged_export_voltage, this->last_logged_export_current, exportVoltage,
                            exportCurrentLimit)) {
        EVLOG_info << std::fixed << std::setprecision(2) << "Updating voltage/current via CAN: " << exportVoltage
                   << "V / " << exportCurrentLimit << "A";
    }
    mod->acdc.set_voltage_current(exportVoltage, exportCurrentLimit, true);
};

void power_supply_DCImpl::handle_setImportVoltageCurrent(double& voltage, double& current) {

    if (caps.min_import_voltage_V.has_value() && caps.max_import_current_A.has_value()) {

        if (voltage > caps.max_import_voltage_V.value())
            voltage = caps.max_import_voltage_V.value();
        else if (voltage < caps.min_import_voltage_V.value())
            voltage = caps.min_import_voltage_V.value();

        if (current > caps.max_import_current_A.value())
            current = caps.max_import_current_A.value();
        else if (current < caps.min_import_current_A.value())
            current = caps.min_import_current_A.value();

        std::scoped_lock lock(settings_mutex);
        minImportVoltage = voltage;
        importCurrentLimit = current;

        if (should_log_setpoint(this->last_logged_import_voltage, this->last_logged_import_current, minImportVoltage,
                                importCurrentLimit)) {
            EVLOG_info << std::fixed << std::setprecision(2) << "Updating voltage/current via CAN: " << minImportVoltage
                       << "V / " << importCurrentLimit << "A";
        }
        mod->acdc.set_voltage_current(minImportVoltage, importCurrentLimit, false);
    }
}

} // namespace main
} // namespace module
