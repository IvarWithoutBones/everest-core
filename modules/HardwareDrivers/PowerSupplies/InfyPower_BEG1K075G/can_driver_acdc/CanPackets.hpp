// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2025 Pionix GmbH and Contributors to EVerest

#ifndef CAN_PACKETS_HPP
#define CAN_PACKETS_HPP

#include <linux/can.h>
#include <ostream>
#include <stdint.h>
#include <vector>

namespace can_packet_acdc {

uint32_t encode_can_id(uint8_t source_address, uint8_t destination_address, uint8_t command_number,
                       uint8_t device_number, uint8_t error_code);

uint8_t destination_address_from_can_id(uint32_t id);
uint8_t source_address_from_can_id(uint32_t id);
uint8_t command_number_from_can_id(uint32_t id);
uint8_t device_number_from_can_id(uint32_t id);
uint8_t error_code_from_can_id(uint32_t id);

/*
 CAN IDs are not just simple IDs with this inverter. More information is encoded into the 29 bit CAN IDs:

 bits 0..7: Source Address
 bits 18..15: Destination Address
    0x3F: broadcast message
    0x00..0x3E: if device number 0x0A: actual module address range, automatic allocation after power on.
                if device number 0x0B: group address, set with dial on the module.
    0xF0..0xF8: controller address range, default is 0xF0.

 bits 16..21: Command number
    0x23: read power module data, actual content encoded in first to bytes
    0x24: write power module data, actual content encoded in first to bytes
        Byte 0:
            0x10: System basic information
            0x11: Single power module basic information
            0x21: Single power module AC side information
            0x41: Single power module bidirectional DC/DC side information




 bits 22..25: Device number
    0x0A: Controller to single module
    0x0B: Controller to module group

 bits 26..28: Error code:
    0x00: Normal
    0x02: Command invalid
    0x03: Data invalid
    0x07: In start processing

*/

// Commands
const uint8_t CMD_READ = 0x23;
const uint8_t CMD_WRITE = 0x24;

const uint8_t DEV_MODULE = 0x0A;
const uint8_t DEV_GROUP = 0x0B;

// RX and TX packet definitions

struct GenericSetting {
    GenericSetting(uint8_t _byte0, uint8_t _byte1, uint32_t _value);
    GenericSetting(const std::vector<uint8_t> raw);
    friend std::ostream& operator<<(std::ostream& out, const GenericSetting& self);
    operator std::vector<uint8_t>();
    uint32_t value{0};
    uint8_t byte0{0};
    uint8_t byte1{0};
};

struct BusDCVoltage {
    BusDCVoltage();
    BusDCVoltage(float _volt);
    BusDCVoltage(const std::vector<uint8_t> raw);
    friend std::ostream& operator<<(std::ostream& out, const BusDCVoltage& self);
    operator std::vector<uint8_t>();

    float volt{0};
};

struct BusDCCurrent {
    BusDCCurrent();
    BusDCCurrent(float _ampere);
    BusDCCurrent(const std::vector<uint8_t> raw);
    friend std::ostream& operator<<(std::ostream& out, const BusDCCurrent& self);
    operator std::vector<uint8_t>();

    float ampere{0};
};

struct BatteryDCVoltage {
    BatteryDCVoltage();
    BatteryDCVoltage(float _volt);
    BatteryDCVoltage(const std::vector<uint8_t> raw);
    friend std::ostream& operator<<(std::ostream& out, const BatteryDCVoltage& self);
    operator std::vector<uint8_t>();

    float volt{0};
};

struct BatteryDCCurrent {
    BatteryDCCurrent();
    BatteryDCCurrent(float _ampere);
    BatteryDCCurrent(const std::vector<uint8_t> raw);
    friend std::ostream& operator<<(std::ostream& out, const BatteryDCCurrent& self);
    operator std::vector<uint8_t>();

    float ampere{0};
};

// RX packet definitions

struct PowerModuleNumber {
    PowerModuleNumber();
    PowerModuleNumber(const std::vector<uint8_t> raw);
    friend std::ostream& operator<<(std::ostream& out, const PowerModuleNumber& self);
    operator std::vector<uint8_t>();

    uint16_t number{0};
};

struct PowerGroupNumber {
    PowerGroupNumber();
    PowerGroupNumber(const std::vector<uint8_t> raw);
    friend std::ostream& operator<<(std::ostream& out, const PowerGroupNumber& self);
    operator std::vector<uint8_t>();

    uint16_t number{0};
};

struct DcMinOutputVoltage {
    DcMinOutputVoltage();
    DcMinOutputVoltage(const std::vector<uint8_t> raw);
    friend std::ostream& operator<<(std::ostream& out, const DcMinOutputVoltage& self);
    operator std::vector<uint8_t>();

    float volt{0};
};

struct DcMaxOutputVoltage {
    DcMaxOutputVoltage();
    DcMaxOutputVoltage(const std::vector<uint8_t> raw);
    friend std::ostream& operator<<(std::ostream& out, const DcMaxOutputVoltage& self);
    operator std::vector<uint8_t>();

    float volt{0};
};

struct DcMaxOutputCurrent {
    DcMaxOutputCurrent();
    DcMaxOutputCurrent(const std::vector<uint8_t> raw);
    friend std::ostream& operator<<(std::ostream& out, const DcMaxOutputCurrent& self);
    operator std::vector<uint8_t>();

    float ampere{0};
};

struct DcRatedOutputPower {
    DcRatedOutputPower();
    DcRatedOutputPower(const std::vector<uint8_t> raw);
    friend std::ostream& operator<<(std::ostream& out, const DcRatedOutputPower& self);
    operator std::vector<uint8_t>();

    float watt{0};
};

struct PowerModuleStatus {
    PowerModuleStatus();
    PowerModuleStatus(const std::vector<uint8_t> raw);
    friend std::ostream& operator<<(std::ostream& out, const PowerModuleStatus& self);
    operator std::vector<uint8_t>();

    bool output_short_current{false};
    bool sleeping{false};
    bool discharge_abnormal{false};
    bool dc_side_off{false};
    bool fault_alarm{false};
    bool protection_alarm{false};
    bool fan_fault_alarm{false};
    bool over_temperature_alarm{false};
    bool output_over_voltage_alarm{false};
    bool walk_in_enable{false};
    bool communication_interrupt_alarm{false};
    bool power_limit_status{false};
    bool id_repeat_alarm{false};
    bool load_sharing_alarm{false};
    bool input_phase_lost_alarm{false};
    bool input_unbalanced_alarm{false};
    bool input_low_voltage_alarm{false};
    bool input_over_voltage_protection{false};
    bool pfc_side_off{false};
};

struct InverterStatus {
    InverterStatus();
    InverterStatus(const std::vector<uint8_t> raw);
    friend std::ostream& operator<<(std::ostream& out, const InverterStatus& self);
    operator std::vector<uint8_t>();

    bool invert_mode{false};
};

// TX packet definitions

struct OnOff {
    OnOff(bool o);
    operator std::vector<uint8_t>();
    bool on{false};
};

struct WalkInEnable {
    WalkInEnable(bool o);
    operator std::vector<uint8_t>();
    bool enabled{false};
};

struct WorkingMode {
    WorkingMode(bool o);
    operator std::vector<uint8_t>();
    bool inverter{false};
};

struct PowerFactorAdjust {
    PowerFactorAdjust(float pf);
    operator std::vector<uint8_t>();
    float pf{1.0};
};

struct PortVoltageRegulation {
    enum class Mode : uint8_t {
        /// No port voltage regulation.
        Disabled = 0xA0,
        /// Attempt to stabilize the battery-side voltage.
        Battery = 0xA1,
        /// Attempt to stabilize the bus-side voltage.
        Bus = 0xA3,
    };

    PortVoltageRegulation(Mode mode);
    operator std::vector<uint8_t>();
    Mode mode{Mode::Disabled};
};

} // namespace can_packet_acdc

#endif // CAN_PACKETS_HPP
