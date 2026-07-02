// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2025 Pionix GmbH and Contributors to EVerest

#include "InfyCanDevice.hpp"
#include "CanPackets.hpp"
#include <iostream>
#include <unistd.h>

#include <everest/logging.hpp>

InfyCanDevice::InfyCanDevice() {
    // spawn thread that requests some data periodically to keep the connection alive
    exitTxThread = false;
    txThreadHandle = std::thread(&InfyCanDevice::txThread, this);
}

InfyCanDevice::~InfyCanDevice() {
    exitTxThread = true;
}

void InfyCanDevice::set_config(uint8_t group_address, uint8_t controller_address) {
    this->group_address = group_address;
    this->controller_address = controller_address;
    EVLOG_info << "Configured InfyCanDevice with group 0x" << std::hex << std::uppercase
               << static_cast<int>(group_address) << " controller address: 0x" << std::hex << std::uppercase
               << static_cast<int>(controller_address);
}

void InfyCanDevice::rx_handler(uint32_t can_id, const std::vector<uint8_t>& payload) {
    EVLOG_debug << "Infy: CAN frame received. ID: 0x" << std::hex << can_id;

    // We only use extended frames here
    if (!(can_id & CAN_EFF_FLAG)) {
        EVLOG_debug << "Infy: Ignoring, not extended protocol.";
        return;
    }

    // is it for our controller address?
    if (can_packet_acdc::destination_address_from_can_id(can_id) != controller_address) {
        return;
    }

    if (can_packet_acdc::command_number_from_can_id(can_id) == can_packet_acdc::CMD_WRITE) {
        switch (can_packet_acdc::error_code_from_can_id(can_id)) {
        case 0x02:
            EVLOG_error << "Infy: ERROR: Command invalid.";
            break;
        case 0x03:
            EVLOG_error << "Infy: ERROR: Data invalid.";
            break;
        case 0x07:
            EVLOG_error << "Infy: ERROR: In start processing.";

            // Module addresses aren't stable during startup, clear the list.
            std::lock_guard<std::mutex> lock(module_addresses_mutex);
            module_addresses.clear();
            last_in_start_processing = std::chrono::steady_clock::now();
            break;
        }
        return;
    }

    // is it a reply to a read command?
    if (can_packet_acdc::command_number_from_can_id(can_id) != can_packet_acdc::CMD_READ) {
        return;
    }

    uint16_t packet_type = payload[0] << 8 | payload[1];

    // is it from our group address?
    const auto source_address = can_packet_acdc::source_address_from_can_id(can_id);
    if (packet_type == 0x1120) {
        // This queries which module addresses are in the group, the CAN ID's source address is the module address,
        // and the group address is encoded in the payload. Used to discover which modules are in the group.
        can_packet_acdc::GenericSetting s(payload);
        if (s.value != group_address) {
            return;
        }

        // Check if we haven't received the "In Start Processing" error in a while, which occurs during system startup.
        // During this (and ~1s afterwards) module addresses aren't assigned yet, and this packets always reads group 0.
        std::lock_guard<std::mutex> lock(module_addresses_mutex);
        if (last_in_start_processing.has_value() &&
            std::chrono::steady_clock::now() - last_in_start_processing.value() < std::chrono::seconds(1)) {
            EVLOG_debug << "Infy: Ignoring module address discovery, still in start processing.";
            return;
        }

        // Add the module address to the list of known modules in the group (if it is not already present).
        if (std::find(module_addresses.begin(), module_addresses.end(), source_address) == module_addresses.end()) {
            EVLOG_info << "Infy: Discovered module address 0x" << std::hex << std::uppercase
                       << static_cast<int>(source_address);

            module_addresses.push_back(source_address);
        }
    } else {
        if (can_packet_acdc::device_number_from_can_id(can_id) == can_packet_acdc::DEV_GROUP) {
            if (source_address != group_address)
                return;
        } else {
            // The message came from a single module, check if our group manages it.
            std::lock_guard<std::mutex> lock(module_addresses_mutex);
            if (std::find(module_addresses.begin(), module_addresses.end(), source_address) == module_addresses.end()) {
                EVLOG_debug << "Infy: Ignoring packet from unknown module address 0x" << std::hex << std::uppercase
                            << static_cast<int>(source_address);
                return;
            }
        }
    }

    if (can_packet_acdc::error_code_from_can_id(can_id) > 0) {
        EVLOG_debug << "Infy: Parsing CAN packet type: " << std::hex << packet_type << " Error code:" << std::hex
                    << (int)can_packet_acdc::error_code_from_can_id(can_id);
    }

    switch (packet_type) {
    case 0x1001: {
        telemetry.battery_voltage = payload;
    } break;

    case 0x1002: {
        telemetry.battery_current = payload;
        if (!inverter_mode.load()) {
            signalVoltageCurrent(telemetry.battery_voltage.volt, telemetry.battery_current.ampere);
        }
    } break;

    case 0x1010: {
        can_packet_acdc::PowerModuleNumber n(payload);
    } break;

    case 0x1110: {
        can_packet_acdc::PowerModuleStatus s(payload);
        telemetry.status = s;
    } break;

    case 0x1111: {
        can_packet_acdc::InverterStatus s(payload);
        signalModuleStatus(telemetry.status, s);
    } break;

    case 0x1103: {
        can_packet_acdc::GenericSetting s(payload);
        telemetry.ac_ab_line_voltage = s.value / 1000.;
    } break;

    case 0x1104: {
        can_packet_acdc::GenericSetting s(payload);
        telemetry.ac_bc_line_voltage = s.value / 1000.;
    } break;

    case 0x1105: {
        can_packet_acdc::GenericSetting s(payload);
        telemetry.ac_ca_line_voltage = s.value / 1000.;
    } break;

    case 0x1106: {
        can_packet_acdc::GenericSetting s(payload);
        telemetry.ambient_temperature = s.value / 1000.;
    } break;

        // 0x1120: handled above

    case 0x1130: {
        can_packet_acdc::GenericSetting s(payload);
        telemetry.dc_max_output_voltage = s.value / 1000.;
    } break;

    case 0x1131: {
        can_packet_acdc::GenericSetting s(payload);
        telemetry.dc_min_output_voltage = s.value / 1000.;
    } break;

    case 0x1132: {
        can_packet_acdc::GenericSetting s(payload);
        telemetry.dc_max_output_current = s.value / 1000.;
    } break;

    case 0x1133: {
        can_packet_acdc::GenericSetting s(payload);
        telemetry.dc_rated_output_power = s.value / 1000.;
    } break;

    case 0x2101: {
        can_packet_acdc::GenericSetting s(payload);
        telemetry.ac_phase_a_voltage = s.value / 1000.;
    } break;

    case 0x2102: {
        can_packet_acdc::GenericSetting s(payload);
        telemetry.ac_phase_b_voltage = s.value / 1000.;
    } break;

    case 0x2103: {
        can_packet_acdc::GenericSetting s(payload);
        telemetry.ac_phase_c_voltage = s.value / 1000.;
    } break;

    case 0x2104: {
        can_packet_acdc::GenericSetting s(payload);
        telemetry.ac_phase_a_current = s.value / 1000.;
    } break;

    case 0x2105: {
        can_packet_acdc::GenericSetting s(payload);
        telemetry.ac_phase_b_current = s.value / 1000.;
    } break;

    case 0x2106: {
        can_packet_acdc::GenericSetting s(payload);
        telemetry.ac_phase_c_current = s.value / 1000.;
    } break;

    case 0x2107: {
        can_packet_acdc::GenericSetting s(payload);
        telemetry.ac_frequency = s.value / 1000.;
    } break;

    case 0x2109: {
        can_packet_acdc::GenericSetting s(payload);
        telemetry.ac_phase_a_active_power = s.value / 1000.;
    } break;

    case 0x210A: {
        can_packet_acdc::GenericSetting s(payload);
        telemetry.ac_phase_b_active_power = s.value / 1000.;
    } break;

    case 0x210B: {
        can_packet_acdc::GenericSetting s(payload);
        telemetry.ac_phase_c_active_power = s.value / 1000.;
    } break;

    case 0x2108: {
        can_packet_acdc::GenericSetting s(payload);
        telemetry.ac_total_active_ower = s.value / 1000.;
    } break;

    case 0x210D: {
        can_packet_acdc::GenericSetting s(payload);
        telemetry.ac_phase_a_reactive_power = s.value / 1000.;
    } break;

    case 0x210E: {
        can_packet_acdc::GenericSetting s(payload);
        telemetry.ac_phase_b_reactive_power = s.value / 1000.;
    } break;

    case 0x210F: {
        can_packet_acdc::GenericSetting s(payload);
        telemetry.ac_phase_c_reactive_power = s.value / 1000.;
    } break;

    case 0x210C: {
        can_packet_acdc::GenericSetting s(payload);
        telemetry.ac_total_reactive_ower = s.value / 1000.;
    } break;

    case 0x2110: {
        can_packet_acdc::GenericSetting s(payload);
        telemetry.ac_phase_a_apparent_power = s.value / 1000.;
    } break;

    case 0x2111: {
        can_packet_acdc::GenericSetting s(payload);
        telemetry.ac_phase_b_apparent_power = s.value / 1000.;
    } break;

    case 0x2112: {
        can_packet_acdc::GenericSetting s(payload);
        telemetry.ac_phase_c_apparent_power = s.value / 1000.;
    } break;

    case 0x2113: {
        can_packet_acdc::GenericSetting s(payload);
        telemetry.ac_total_apparent_ower = s.value / 1000.;
    } break;

    case 0x4101: {
        telemetry.bus_voltage = {payload};
    } break;

    case 0x4102: {
        telemetry.bus_current = {payload};
        if (inverter_mode.load()) {
            signalVoltageCurrent(telemetry.bus_voltage.volt, telemetry.bus_current.ampere);
        }
    } break;

    default: {
        can_packet_acdc::GenericSetting s(payload);
    }
    }
}

void InfyCanDevice::txThread() {
    while (!exitTxThread) {
        const int delay_us = 50000;

        // request which modules are in this group. Answer will be processed by RX thread.
        request_rx(can_packet_acdc::DEV_GROUP, can_packet_acdc::PowerGroupNumber());
        usleep(delay_us);

        // request state. Answer will be processed by RX thread.
        request_rx(can_packet_acdc::DEV_MODULE, can_packet_acdc::PowerModuleStatus());
        usleep(delay_us);

        // request inverter state. Answer will be processed by RX thread.
        request_rx(can_packet_acdc::DEV_MODULE, can_packet_acdc::InverterStatus());
        usleep(delay_us);

        tx(can_packet_acdc::DEV_GROUP, can_packet_acdc::OnOff(on));
        usleep(delay_us);

        // request current battery-side DC voltage. Answer will be processed by RX thread.
        request_rx(can_packet_acdc::DEV_GROUP, can_packet_acdc::BatteryDCVoltage());
        usleep(delay_us);

        // request current battery-side DC current. Answer will be processed by RX thread.
        request_rx(can_packet_acdc::DEV_GROUP, can_packet_acdc::BatteryDCCurrent());
        usleep(delay_us);

        // request current bus-side DC voltage. Answer will be processed by RX thread.
        request_rx(can_packet_acdc::DEV_MODULE, can_packet_acdc::BusDCVoltage());
        usleep(delay_us);

        // request current bus-side DC current. Answer will be processed by RX thread.
        request_rx(can_packet_acdc::DEV_MODULE, can_packet_acdc::BusDCCurrent());
        usleep(delay_us);

        if (inverter_mode.load()) {
            if (setpoint_import_voltage > 150.0) {
                // Configure the bus side limits
                tx(can_packet_acdc::DEV_GROUP, can_packet_acdc::BusDCVoltage(setpoint_import_voltage));
                usleep(delay_us);
                tx(can_packet_acdc::DEV_GROUP, can_packet_acdc::BusDCCurrent(setpoint_import_current));
                usleep(delay_us);

                // Configure the battery side limits
                tx(can_packet_acdc::DEV_GROUP, can_packet_acdc::BatteryDCCurrent(setpoint_import_current * 2));
                usleep(delay_us);
            }
        } else {
            if (setpoint_export_voltage > 150.0) {
                // Configure the battery side limits
                tx(can_packet_acdc::DEV_GROUP, can_packet_acdc::BatteryDCVoltage(setpoint_export_voltage));
                usleep(delay_us);
                tx(can_packet_acdc::DEV_GROUP, can_packet_acdc::BatteryDCCurrent(setpoint_export_current));
                usleep(delay_us);

                // Configure the bus side limits
                tx(can_packet_acdc::DEV_GROUP, can_packet_acdc::BusDCCurrent(setpoint_export_current * 2));
                usleep(delay_us);
            }
        }

        tx(can_packet_acdc::DEV_GROUP, can_packet_acdc::WalkInEnable(walkin_enable));
        usleep(delay_us);

        tx(can_packet_acdc::DEV_GROUP, can_packet_acdc::WorkingMode(inverter_mode));
        usleep(delay_us);
    }
}

bool InfyCanDevice::switch_on_off(bool _on) {
    on = _on;
    return true;
}

bool InfyCanDevice::set_walkin_enabled(bool on) {
    walkin_enable = on;
    return true;
}

bool InfyCanDevice::set_inverter_mode(bool i) {
    inverter_mode = i;
    return true;
}

bool InfyCanDevice::adjust_power_factor(float pf) {
    return tx(can_packet_acdc::DEV_GROUP, can_packet_acdc::PowerFactorAdjust(pf));
}

bool InfyCanDevice::set_voltage_current(float voltage, float current, bool mode_export) {
    if (mode_export) {
        setpoint_export_current = current;
        setpoint_export_voltage = voltage;
    } else {
        setpoint_import_current = current;
        setpoint_import_voltage = voltage;
    }
    return true;
}

bool InfyCanDevice::set_generic_setting(uint8_t byte0, uint8_t byte1, uint32_t value) {
    return tx(can_packet_acdc::DEV_GROUP, can_packet_acdc::GenericSetting(byte0, byte1, value));
}

bool InfyCanDevice::set_output_mode(OutputMode mode) {
    return set_generic_setting(0x11, 0x26, static_cast<std::underlying_type<OutputMode>::type>(mode));
}

bool InfyCanDevice::tx(const uint8_t dev, const std::vector<uint8_t>& payload) {
    std::vector<uint8_t> destinations{};
    if (dev == can_packet_acdc::DEV_MODULE) {
        // TODO: Pass module_addresses to avoid re-locking
        std::lock_guard<std::mutex> lock(module_addresses_mutex);
        destinations = module_addresses;
    } else {
        destinations.push_back(group_address);
    }

    bool success = true;
    for (const auto dst : destinations) {
        auto can_id = can_packet_acdc::encode_can_id(controller_address, dst, can_packet_acdc::CMD_WRITE, dev, 0);
        can_id |= 0x80000000U; // Extended frame format
        success &= _tx(can_id, payload);
    }
    return success;
}

bool InfyCanDevice::request_rx(const uint8_t dev, const std::vector<uint8_t>& payload) {
    std::vector<uint8_t> destinations{};
    if (dev == can_packet_acdc::DEV_MODULE) {
        // TODO: Pass module_addresses to avoid re-locking
        std::lock_guard<std::mutex> lock(module_addresses_mutex);
        destinations = module_addresses;
    } else {
        destinations.push_back(group_address);
    }

    bool success = true;
    for (const auto dst : destinations) {
        uint32_t can_id = can_packet_acdc::encode_can_id(controller_address, dst, can_packet_acdc::CMD_READ, dev, 0);
        can_id |= 0x80000000U; // Extended frame format
        success &= _tx(can_id, payload);
    }
    return success;
}

std::ostream& operator<<(std::ostream& out, const InfyCanDevice::Telemetry& self) {
    out << "\n------------------------------------------------\nTelemetry\n---------\n";

    out << "AC line: AB:" << std::to_string(self.ac_ab_line_voltage)
        << " BC: " << std::to_string(self.ac_bc_line_voltage) << " CA: " << std::to_string(self.ac_ca_line_voltage)
        << std::endl;

    out << "AC Phase Voltages: V_A:" << std::to_string(self.ac_phase_a_voltage)
        << " V_B: " << std::to_string(self.ac_phase_b_voltage) << " V_C: " << std::to_string(self.ac_phase_c_voltage)
        << std::endl;

    out << "AC Phase Currents: I_A:" << std::to_string(self.ac_phase_a_current)
        << " I_B: " << std::to_string(self.ac_phase_b_current) << " I_C: " << std::to_string(self.ac_phase_c_current)
        << std::endl;

    out << "AC Active power: Total: " << std::to_string(self.ac_total_active_ower)
        << "P_A:" << std::to_string(self.ac_phase_a_active_power)
        << " P_B: " << std::to_string(self.ac_phase_b_active_power)
        << " P_C: " << std::to_string(self.ac_phase_c_active_power) << std::endl;

    out << "AC Re-Active power: Total: " << std::to_string(self.ac_total_reactive_ower)
        << "P_A:" << std::to_string(self.ac_phase_a_reactive_power)
        << " P_B: " << std::to_string(self.ac_phase_b_reactive_power)
        << " P_C: " << std::to_string(self.ac_phase_c_reactive_power) << std::endl;

    out << "AC Apparent power: Total: " << std::to_string(self.ac_total_apparent_ower)
        << "P_A:" << std::to_string(self.ac_phase_a_apparent_power)
        << " P_B: " << std::to_string(self.ac_phase_b_apparent_power)
        << " P_C: " << std::to_string(self.ac_phase_c_apparent_power) << std::endl;

    out << "AC frequency: " << self.ac_frequency << std::endl;
    out << "Ambient temperature: " << self.ambient_temperature << std::endl;

    out << "DC High Voltage side: Voltage: " << std::to_string(self.bus_voltage.volt)
        << " Current: " << std::to_string(self.bus_current.ampere) << std::endl;

    out << "Capabilities: dc_min: " << self.dc_min_output_voltage << "V dc_max: " << self.dc_max_output_voltage
        << "V dc_max_current: " << self.dc_max_output_current << "A max_watt: " << self.dc_rated_output_power << "W "
        << std::endl;

    out << self.status << std::endl;
    out << self.battery_voltage << std::endl << self.battery_current << std::endl;

    out << "------------------------------------------------\n";

    return out;
}
