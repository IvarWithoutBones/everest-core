// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2025 Pionix GmbH and Contributors to EVerest

#include "InfyPower_BEG1K075G.hpp"

namespace module {

void InfyPower_BEG1K075G::init() {
    // Validate the configuration
    if (config.controller_address < 0xF0 || config.controller_address > 0xF8) {
        EVLOG_AND_THROW(EVEXCEPTION(Everest::EverestConfigError, "Invalid controller address '", std::hex,
                                    config.controller_address, "'. Must be an integer between 0xF0 and 0xF8."));
    }

    if (config.group_address < 0x00 || config.group_address > 0x3E) {
        EVLOG_AND_THROW(EVEXCEPTION(Everest::EverestConfigError, "Invalid group address '", std::hex,
                                    config.group_address, "'. Must be an integer between 0x00 and 0x3E."));
    }

    // Configure the InfyCanDevice
    acdc.set_config(config.group_address, static_cast<uint8_t>(config.controller_address));

    // open DCDC CAN device
    if (!acdc.open_device(config.can_device.c_str())) {
        EVLOG_AND_THROW(EVEXCEPTION(Everest::EverestConfigError, "Could not open CAN interface ", config.can_device));
    }

    invoke_init(*p_main);
}

void InfyPower_BEG1K075G::ready() {
    invoke_ready(*p_main);
}

} // namespace module
