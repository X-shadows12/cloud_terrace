/*
    Copyright 2021 codenocold codenocold@qq.com
    Address : https://github.com/codenocold/ctm
    This file is part of the ctm firmware.
    The ctm firmware is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.
    The ctm firmware is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.
    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "rtt_scope.h"
#include "rtt_mem.h"
#include <stdarg.h>

#if CTM_ENABLE_RTT_SCOPE
#include "SEGGER_RTT.h"

#define RTT_SCOPE_CHANNEL     1U
#define RTT_SCOPE_BUFFER_SIZE 4096U

static uint8_t mRttScopeBuffer[RTT_SCOPE_BUFFER_SIZE] __attribute__((section(RTT_SCOPE_BUFFER_SECTION), aligned(32)));

void RTT_init(void)
{
    SEGGER_RTT_Init();
    SEGGER_RTT_ConfigUpBuffer(RTT_SCOPE_CHANNEL,
                              "LKS_Scope",
                              mRttScopeBuffer,
                              sizeof(mRttScopeBuffer),
                              SEGGER_RTT_MODE_NO_BLOCK_SKIP);
}

void RTT_log(const char *format, ...)
{
    va_list args;

    va_start(args, format);
    (void) SEGGER_RTT_vprintf(0U, format, &args);
    va_end(args);
}

void RTT_scope_write_raw(const void *data, uint32_t size)
{
    (void) SEGGER_RTT_Write(RTT_SCOPE_CHANNEL, data, size);
}

void RTT_scope_write2(float f1, float f2)
{
    float value[2];

    value[0] = f1;
    value[1] = f2;
    RTT_scope_write_raw(value, sizeof(value));
}

void RTT_scope_write6(float f1, float f2, float f3, float f4, float f5, float f6)
{
    float value[6];

    value[0] = f1;
    value[1] = f2;
    value[2] = f3;
    value[3] = f4;
    value[4] = f5;
    value[5] = f6;
    RTT_scope_write_raw(value, sizeof(value));
}
#endif
