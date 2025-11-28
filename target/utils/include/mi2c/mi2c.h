#pragma once

#include <cstdint>

#include "hardware/i2c.h"

namespace MPico
{

void m_i2c_init(i2c_inst_t* port, uint32_t clock, uint32_t data, uint32_t baud = 100000);

} // namespace MPico