/******************************************************************************
 * The MIT License
 *
 * Copyright (c) 2010 Perry Hung.
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use, copy,
 * modify, merge, publish, distribute, sublicense, and/or sell copies
 * of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *****************************************************************************/

/**
 * @brief USB virtual serial terminal
 */

#include "usb_serial.h"
#include "stm32f10x_iwdg.h"

#include "string.h"
#include "stdint.h"

#include <usb_cdcacm.h>
#include <usb.h>

static void rxHook(unsigned, void*);
static void ifaceSetupHook(unsigned, void*);

/*
 * USBSerial interface
 */

USBSerial::USBSerial(void) {

}

void USBSerial::begin(void) {
    usb_cdcacm_enable(GPIOB, GPIO_Pin_10);
    usb_cdcacm_set_hooks(USB_CDCACM_HOOK_RX, rxHook);
    usb_cdcacm_set_hooks(USB_CDCACM_HOOK_IFACE_SETUP, ifaceSetupHook);
}

void USBSerial::end(void) {
    usb_cdcacm_disable(GPIOB, GPIO_Pin_10);
    usb_cdcacm_remove_hooks(USB_CDCACM_HOOK_RX | USB_CDCACM_HOOK_IFACE_SETUP);
}

uint32_t USBSerial::write(uint8_t ch) {
uint32_t n = 0;
    this->write(&ch, 1);
		return n;
}

uint32_t USBSerial::write(const uint8_t *buf, uint32_t len)
{
uint32_t n = 0;
   // Roger Clark. 
   // Remove checking of isConnected for ZUMspot
   if (!buf || !usb_is_connected(USBLIB) || !usb_is_configured(USBLIB)) {
        return 0;
    }

    uint32_t txed = 0;
    while (txed < len) {
        txed += usb_cdcacm_tx((const uint8_t*)buf + txed, len - txed);
    }

	return n;
}

int USBSerial::available(void) {
    return usb_cdcacm_data_available();
}

int USBSerial::availableForWrite(void) {
    return usb_cdcacm_available_for_write();
}

int USBSerial::peek(void)
{
    uint8_t b;
	if (usb_cdcacm_peek(&b, 1)==1)
	{
		return b;
	}
	else
	{
		return -1;
	}
}

void USBSerial::flush(void)
{
/*Roger Clark. Rather slow method. Need to improve this */
    uint8_t b;
	while(usb_cdcacm_data_available())
	{
		this->read(&b, 1);
	}
    return;
}

uint32_t USBSerial::read(uint8_t * buf, uint32_t len) {
    uint32_t rxed = 0;
    while (rxed < len) {
        rxed += usb_cdcacm_rx(buf + rxed, len - rxed);
    }

    return rxed;
}

int USBSerial::read(void) {
    uint8_t b;
	
	if (usb_cdcacm_rx(&b, 1)==0)
	{
		return -1;
	}
	else
	{
		return b;
	}
}

uint8_t USBSerial::pending(void) {
    return usb_cdcacm_get_pending();
}

uint8_t USBSerial::isConnected(void) {
    return usb_is_connected(USBLIB) && usb_is_configured(USBLIB) && usb_cdcacm_get_dtr();
}

uint8_t USBSerial::getDTR(void) {
    return usb_cdcacm_get_dtr();
}

uint8_t USBSerial::getRTS(void) {
    return usb_cdcacm_get_rts();
}

#if defined(ENABLE_USB)
USBSerial usbserial;
#endif

/*
 * Bootloader hook implementations
 */

enum reset_state_t {
    DTR_UNSET,
    DTR_HIGH,
    DTR_NEGEDGE,
    DTR_LOW
};

static reset_state_t reset_state = DTR_UNSET;

static void ifaceSetupHook(unsigned hook, void *requestvp) {
    uint8_t request = *(uint8_t*)requestvp;

    // Ignore requests we're not interested in.
    if (request != USB_CDCACM_SET_CONTROL_LINE_STATE) {
        return;
    }

    // We need to see a negative edge on DTR before we start looking
    // for the in-band magic reset byte sequence.
    uint8_t dtr = usb_cdcacm_get_dtr();
    switch (reset_state) {
    case DTR_UNSET:
        reset_state = dtr ? DTR_HIGH : DTR_LOW;
        break;
    case DTR_HIGH:
        reset_state = dtr ? DTR_HIGH : DTR_NEGEDGE;
        break;
    case DTR_NEGEDGE:
        reset_state = dtr ? DTR_HIGH : DTR_LOW;
        break;
    case DTR_LOW:
        reset_state = dtr ? DTR_HIGH : DTR_LOW;
        break;
    }

	if ((usb_cdcacm_get_baud() == 1200) && (reset_state == DTR_NEGEDGE)) {
		
		IWDG_WriteAccessCmd(IWDG_WriteAccess_Enable);
		IWDG_SetPrescaler(IWDG_Prescaler_4);
		IWDG_SetReload(10);
		IWDG_Enable();
		IWDG_ReloadCounter();
		
		while (1);
	}
}

#define RESET_DELAY 100000

static void wait_reset(void) {
	volatile unsigned int delay;
	for(delay = 0;delay<RESET_DELAY;delay++);
	NVIC_SystemReset();
}

#define STACK_TOP 0x20000800
#define EXC_RETURN 0xFFFFFFF9
#define DEFAULT_CPSR 0x61000000

static void rxHook(unsigned hook, void *ignored) {
    /* FIXME this is mad buggy; we need a new reset sequence. E.g. NAK
     * after each RX means you can't reset if any bytes are waiting. */
    if (reset_state == DTR_NEGEDGE) {
        reset_state = DTR_LOW;

        if (usb_cdcacm_data_available() >= 4) {
            // The magic reset sequence is "1EAF".

            static const uint8_t magic[4] = {'1', 'E', 'A', 'F'};	

            uint8_t chkBuf[4];

            // Peek at the waiting bytes, looking for reset sequence,
            // bailing on mismatch.
            usb_cdcacm_peek_ex(chkBuf, usb_cdcacm_data_available() - 4, 4);
            for (unsigned i = 0; i < sizeof(magic); i++) {
                if (chkBuf[i] != magic[i]) {
                    return;
                }
            }

            // Got the magic sequence -> reset, presumably into the bootloader.
            // Return address is wait_reset, but we must set the thumb bit.
            uintptr_t target = (uintptr_t)wait_reset | 0x1;
            asm volatile("mov r0, %[stack_top]      \n\t" // Reset stack
                         "mov sp, r0                \n\t"
                         "mov r0, #1                \n\t"
                         "mov r1, %[target_addr]    \n\t"
                         "mov r2, %[cpsr]           \n\t"
                         "push {r2}                 \n\t" // Fake xPSR
                         "push {r1}                 \n\t" // PC target addr
                         "push {r0}                 \n\t" // Fake LR
                         "push {r0}                 \n\t" // Fake R12
                         "push {r0}                 \n\t" // Fake R3
                         "push {r0}                 \n\t" // Fake R2
                         "push {r0}                 \n\t" // Fake R1
                         "push {r0}                 \n\t" // Fake R0
                         "mov lr, %[exc_return]     \n\t"
                         "bx lr"
                         :
                         : [stack_top] "r" (STACK_TOP),
                           [target_addr] "r" (target),
                           [exc_return] "r" (EXC_RETURN),
                           [cpsr] "r" (DEFAULT_CPSR)
                         : "r0", "r1", "r2");

        }
    }
}
