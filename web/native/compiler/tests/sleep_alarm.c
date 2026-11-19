/* A clock and one runnable thread for the original sleep/queue/dispatch path. */
#include "khdays_references.h"

#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdint.h>
#include <stdlib.h>

extern void OS_Sleep(unsigned milliseconds);
extern void OSi_ArrangeTimer(void);
extern void OSi_SleepAlarmCallback(khdays_ref arg);
extern unsigned khdays_cpsr;

static unsigned thread[0xb4 / 4];
static khdays_ref current_thread;
static khdays_ref thread_info[3], alarm_queue[3];
static unsigned irq_check, sleep_count, resume_count;
static unsigned short timer_control;
static unsigned long long tick = 100;
static khdays_ref slot;

void *khdays_window_address(khdays_ref ref) {
    if (ref >= 0x0204430c && ref < 0x0204430c + sizeof(thread_info))
        return (char *)thread_info + (ref - 0x0204430c);
    if (ref == 0x02044674) return alarm_queue;
    if (ref == 0x027e3ff8) return &irq_check;
    if (ref == 0x04000106) return &timer_control;
    return (void *)(uintptr_t)ref;
}
khdays_ref khdays_reference(const void *host) { return (khdays_ref)(uintptr_t)host; }
khdays_function_pointer khdays_function(khdays_ref address) {
    assert(address == 0x02002178);
    return (khdays_function_pointer)OSi_SleepAlarmCallback;
}

void khdays_irq_poll(void) {}
unsigned long long OS_GetTick(void) { return tick; }
unsigned OS_DisableIrqMask(unsigned mask) { assert(mask == 16); return 0; }
void OS_Terminate(void) { abort(); }
void OSi_SetTimer(khdays_ref alarm) {
    assert(alarm == thread[0xb0 / 4]);
    assert(((const unsigned *)KHDAYS_W(alarm))[0] == 0x02002178);
    slot = ((const unsigned *)KHDAYS_W(alarm))[1];
    assert(*(const khdays_ref *)KHDAYS_W(slot) == current_thread);
}
void OS_SleepThread(khdays_ref queue) {
    ++sleep_count;
    assert(!queue && sleep_count == 1);
    assert(thread[0xb0 / 4] && (khdays_cpsr & 0x80));
    tick += (100ULL * 0x82ea) >> 6;
    OSi_ArrangeTimer();
}
void OSi_RescheduleThread(void) {
    assert(*(const khdays_ref *)KHDAYS_W(slot) == 0);
    assert(thread[0xb0 / 4] == 0 && thread[0x64 / 4] == 1);
    ++resume_count;
}

int main(void) {
    current_thread = KHDAYS_REF(thread);
    thread_info[2] = KHDAYS_REF(&current_thread);
    OS_Sleep(100);
    OSi_ArrangeTimer(); /* An elapsed one-shot cannot wake its old stack slot again. */
    assert(sleep_count == 1 && resume_count == 1 && irq_check == 16);
    assert(!alarm_queue[1] && !alarm_queue[2] && !(khdays_cpsr & 0x80));
    return 0;
}
