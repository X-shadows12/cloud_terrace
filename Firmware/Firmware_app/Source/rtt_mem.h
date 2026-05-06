#ifndef RTT_MEM_H
#define RTT_MEM_H

/*
 * Reserve a small non-cacheable window in XRAM3 for RTT traffic.
 * The MPU is configured to make this region non-cacheable before DCache is enabled.
 */
#define RTT_RAM_BASE              0x30004000U
#define RTT_RAM_SIZE              0x00004000U

#define RTT_SEGGER_CB_SECTION     ".ARM.__at_0x30004000"
#define RTT_SEGGER_UP0_SECTION    ".ARM.__at_0x30004100"
#define RTT_SEGGER_DOWN0_SECTION  ".ARM.__at_0x30004500"
#define RTT_SCOPE_BUFFER_SECTION  ".ARM.__at_0x30006000"

#endif
