/*
 * Copyright 2014, NICTA
 *
 * This software may be distributed and modified according to the terms of
 * the BSD 2-Clause license. Note that NO WARRANTY is provided.
 * See "LICENSE_BSD2.txt" for details.
 *
 * @TAG(NICTA_BSD)
 */

#include <stdlib.h>
#include <string.h>

#include <sel4arm-vmm/exynos/devices.h>
#include "../../vm.h"

#define VUART_BUFLEN 300

#define ULCON       0x000 /* line control */
#define UCON        0x004 /* control */
#define UFCON       0x008 /* fifo control */
#define UMCON       0x00C /* modem control */
#define UTRSTAT     0x010 /* TX/RX status */
#define UERSTAT     0x014 /* RX error status */
#define UFSTAT      0x018 /* FIFO status */
#define UMSTAT      0x01C /* modem status */
#define UTXH        0x020 /* TX buffer */
#define URXH        0x024 /* RX buffer */
#define UBRDIV      0x028 /* baud rate divisor */
#define UFRACVAL    0x02C /* divisor fractional value */
#define UINTP       0x030 /* interrupt pending */
#define UINTSP      0x034 /* interrupt source pending */
#define UINTM       0x038 /* interrupt mask */
#define UART_SIZE   0x03C



struct vuart_priv {
    void* regs;
    char buffer[VUART_BUFLEN];
    int buf_pos;
    vm_t* vm;
};

static inline void* vuart_priv_get_regs(struct device* d)
{
    return ((struct vuart_priv*)d->priv)->regs;
}

static void vuart_reset(struct device* d)
{
    const uint32_t reset_data[] = {
        0x00000003, 0x000003c5, 0x00000111, 0x00000000,
        0x00000002, 0x00000000, 0x00010000, 0x00000011,
        0x00000000, 0x00000000, 0x00000021, 0x0000000b,
        0x00000004, 0x00000004, 0x00000000
    };
    assert(sizeof(reset_data) == UART_SIZE);
    memcpy(vuart_priv_get_regs(d), reset_data, sizeof(reset_data));
}

void
flush_vconsole_device(struct device* d)
{
    struct vuart_priv *vuart_data;
    char* buf;
    int i;
    assert(d->priv);
    vuart_data = (struct vuart_priv*)d->priv;
    buf = vuart_data->buffer;
    printf("%s", choose_colour(vuart_data->vm));
    for (i = 0; i < vuart_data->buf_pos; i++) {
        if (buf[i] != '\033') {
            putchar(buf[i]);
        } else {
            while (i < vuart_data->buf_pos && buf[i] != 'm') {
                i++;
            }
        }
    }
    printf("%s", choose_colour(NULL));
    vuart_data->buf_pos = 0;
}

static void
vuart_putchar(struct device* d, char c)
{
    struct vuart_priv *vuart_data;
    assert(d->priv);
    vuart_data = (struct vuart_priv*)d->priv;

    if (vuart_data->buf_pos == VUART_BUFLEN) {
        flush_vconsole_device(d);
    }
    assert(vuart_data->buf_pos < VUART_BUFLEN);
    vuart_data->buffer[vuart_data->buf_pos++] = c;

    if ((c & 0xff) == '\n') {
        flush_vconsole_device(d);
    }
}

static int
handle_vuart_fault(struct device* d, vm_t* vm, fault_t* fault)
{
    uint32_t *reg;
    int offset;
    uint32_t mask;

    /* Gather fault information */
    offset = fault->addr - d->pstart;
    reg = (uint32_t*)(vuart_priv_get_regs(d) + offset);
    mask = fault_get_data_mask(fault);
    /* Handle the fault */
    if (offset < 0 || UART_SIZE <= offset) {
        /* Out of range, treat as SBZ */
        fault->data = 0;
        return ignore_fault(fault);

    } else if (fault_is_read(fault)) {
        /* Blindly read out data */
        fault->data = *reg;
        return advance_fault(fault);

    } else { /* if(fault_is_write(fault))*/
        /* Blindly write to the device */
        uint32_t v;
        v = *reg & ~mask;
        v |= fault->data & mask;
        *reg = v;
        /* If it was the TX buffer, we send to the local stdout */
        if (offset == UTXH) {
            vuart_putchar(d, fault->data);
        }
        return advance_fault(fault);
    }
}

const struct device dev_uart0 = {
    .devid = DEV_UART0,
    .name = "uart0",
    .pstart = UART0_PADDR,
    .size = 0x1000,
    .handle_page_fault = handle_vuart_fault,
    .priv = NULL
};

const struct device dev_uart1 = {
    .devid = DEV_UART1,
    .name = "uart1",
    .pstart = UART1_PADDR,
    .size = 0x1000,
    .handle_page_fault = handle_vuart_fault,
    .priv = NULL
};

const struct device dev_uart2 = {
    .devid = DEV_UART2,
    .name = "uart2.console",
    .pstart = UART2_PADDR,
    .size = 0x1000,
    .handle_page_fault = handle_vuart_fault,
    .priv = NULL
};



const struct device dev_uart3 = {
    .devid = DEV_UART3,
    .name = "uart3",
    .pstart = UART3_PADDR,
    .size = 0x1000,
    .handle_page_fault = handle_vuart_fault,
    .priv = NULL
};


int vm_install_vconsole(vm_t* vm)
{
    struct vuart_priv *vuart_data;
    struct device d;
    int err;
    d = dev_vconsole;
    /* Initialise the virtual device */
    vuart_data = malloc(sizeof(struct vuart_priv));
    if (vuart_data == NULL) {
        assert(vuart_data);
        return -1;
    }
    memset(vuart_data, 0, sizeof(*vuart_data));
    vuart_data->vm = vm;

    vuart_data->regs = malloc(UART_SIZE);
    if (vuart_data->regs == NULL) {
        assert(vuart_data->regs);
        return -1;
    }
    d.priv = vuart_data;
    vuart_reset(&d);
    err = vm_add_device(vm, &d);
    assert(!err);
    if (err) {
        free(vuart_data->regs);
        free(vuart_data);
        return -1;
    }
    return 0;
}

