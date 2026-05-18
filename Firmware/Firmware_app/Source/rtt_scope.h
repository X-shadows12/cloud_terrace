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

#ifndef __RTT_SCOPE_H__
#define __RTT_SCOPE_H__

#include <stdint.h>

#ifndef CTM_ENABLE_RTT_SCOPE
#define CTM_ENABLE_RTT_SCOPE 1
#endif

#if CTM_ENABLE_RTT_SCOPE
void RTT_init(void);
void RTT_log(const char *format, ...);
void RTT_scope_write_raw(const void *data, uint32_t size);
void RTT_scope_write2(float f1, float f2);
void RTT_scope_write6(float f1, float f2, float f3, float f4, float f5, float f6);

#define DEBUG(format, ...) RTT_log((format), ##__VA_ARGS__)
#define DEBUG_PLOT(f1, f2) RTT_scope_write2((f1), (f2))
#else
#define DEBUG(format, ...) ((void) 0)

static inline void RTT_init(void)
{
}
static inline void RTT_log(const char *format, ...)
{
    (void) format;
}
static inline void RTT_scope_write_raw(const void *data, uint32_t size)
{
    (void) data;
    (void) size;
}
static inline void RTT_scope_write2(float f1, float f2)
{
    (void) f1;
    (void) f2;
}
static inline void RTT_scope_write6(float f1, float f2, float f3, float f4, float f5, float f6)
{
    (void) f1;
    (void) f2;
    (void) f3;
    (void) f4;
    (void) f5;
    (void) f6;
}

#define DEBUG_PLOT(f1, f2) ((void) 0)
#endif

#endif /* __RTT_SCOPE_H__ */
