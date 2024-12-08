#include "types.h"
#include <mach/mach.h>

static inline u64 read_cpu_timer() {
    u64 timestamp;
    asm volatile("mrs %0, CNTVCT_EL0" : "=r" (timestamp));
    return timestamp;
}

static inline u64 get_cpu_frequency() {
    u64 freq;
    asm volatile("mrs %0, CNTFRQ_EL0" : "=r" (freq));
    return freq;
}

static inline u64 get_os_page_faults() {
    task_events_info info;
    mach_msg_type_number_t count = TASK_EVENTS_INFO_COUNT;

    kern_return_t kr = task_info(mach_task_self(), TASK_EVENTS_INFO, (task_info_t)&info, &count);
    if (kr != KERN_SUCCESS) {
        return 0;
    }

    return info.faults;
}
