/*
 * gpr_zephyr_open_amp.c
 *
 * This file has implementation platform wrapper for the GPR datalink layer
 *
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <stdlib.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <pthread.h>

// AudioReach includes
#include "gpr_comdef.h"
#include "ipc_dl_api.h"
#include "gpr_ids_domains.h"
#include "ar_osal_error.h"
#include "ar_osal_log.h"
#include "gpr_packet.h"
#include "ar_osal_timer.h"
#include "ar_msg.h"

// Zephyr includes
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/ipm.h>
#include <zephyr/logging/log.h>

// OpenAMP includes
#include <openamp/open_amp.h>
#include <metal/sys.h>
#include <metal/io.h>
#include <resource_table.h>
#include <addr_translation.h>

#define SHM_DEVICE_NAME    "shm"

#if !DT_HAS_CHOSEN(zephyr_ipc_shm)
#error "Sample requires definition of shared memory for rpmsg"
#endif

#if CONFIG_IPM_MAX_DATA_SIZE > 0
#define    IPM_SEND(dev, w, id, d, s) ipm_send(dev, w, id, d, s)
#else
#define IPM_SEND(dev, w, id, d, s) ipm_send(dev, w, id, NULL, 0)
#endif

/* Constants derived from device tree */
#define SHM_NODE        DT_CHOSEN(zephyr_ipc_shm)
#define SHM_START_ADDR    DT_REG_ADDR(SHM_NODE)
#define SHM_SIZE        DT_REG_SIZE(SHM_NODE)

#define APP_TASK_STACK_SIZE (1024)

/* Add 1024 extra bytes for the TTY task stack for the "tx_buff" buffer. */
#define APP_TTY_TASK_STACK_SIZE (1536)

K_THREAD_STACK_DEFINE(thread_mng_stack, APP_TASK_STACK_SIZE);
K_THREAD_STACK_DEFINE(thread_tty_stack, APP_TTY_TASK_STACK_SIZE);

static struct k_thread thread_mng_data;
static struct k_thread thread_tty_data;

static const struct device *const ipm_handle =
    DEVICE_DT_GET(DT_CHOSEN(zephyr_ipc));

static metal_phys_addr_t shm_physmap = SHM_START_ADDR;
static metal_phys_addr_t rsc_tab_physmap;

static struct metal_io_region shm_io_data; /* shared memory */
static struct metal_io_region rsc_io_data; /* rsc_table memory */

struct rpmsg_rcv_msg {
    void *data;
    size_t len;
};

static struct metal_io_region *shm_io = &shm_io_data;

static struct metal_io_region *rsc_io = &rsc_io_data;
static struct rpmsg_virtio_device rvdev;

static void *rsc_table;
static struct rpmsg_device *rpdev;

static struct rpmsg_endpoint tty_ept;
static struct rpmsg_rcv_msg tty_msg;

static K_SEM_DEFINE(data_sem, 0, 32);
static K_SEM_DEFINE(data_tty_sem, 0, 1);

#define MAX_QUEUED_MESSAGES 32
K_MSGQ_DEFINE(rpmsg_msgq, sizeof(struct rpmsg_rcv_msg), MAX_QUEUED_MESSAGES, 4);

#define RPMSG_ENDPOINT_NAME "audioreach-rpmsg"
#define RPMSG_TX_BUFFER_SIZE 512

/** Data receive notification callback type*/
typedef uint32_t (*gpr_dl_zephyr_receive_cb)(void *ptr, uint32_t length);

/** Data send done notification callback type*/
typedef uint32_t (*gpr_dl_zephyr_send_done_cb)(void *ptr, uint32_t length);

typedef struct gpr_dl_zephyr_port{
    uint32_t domain_id;
    gpr_dl_zephyr_receive_cb rx_cb;
    gpr_dl_zephyr_send_done_cb send_done;
} gpr_dl_zephyr_port_t;

/*Array of structure pointers each member pointer corresponds to one domain*/
gpr_dl_zephyr_port_t *gpr_dl_zephyr_ports[GPR_PL_NUM_TOTAL_DOMAINS_V]={NULL};

static uint32_t gpr_dl_zephyr_send(uint32_t domain_id, void *buf, uint32_t size);

static uint32_t gpr_dl_zephyr_receive_done(uint32_t domain_id, void *buf);

/*ipc datalink function table*/
static ipc_to_gpr_vtbl_t gpr_dl_zephyr_vtbl = {
    gpr_dl_zephyr_send,
    gpr_dl_zephyr_receive_done,
};


static gpr_dl_zephyr_port_t * gpr_dl_zephyr_local_init(uint32_t src_domain_id, uint32_t dst_domain_id)
{
    gpr_dl_zephyr_port_t *dl_zephyr_port;

    if ((dst_domain_id < 0) || (dst_domain_id >= GPR_PL_NUM_TOTAL_DOMAINS_V)
        || (src_domain_id < 0) || (src_domain_id >= GPR_PL_NUM_TOTAL_DOMAINS_V)) {
        AR_MSG(DBG_ERROR_PRIO, "invalid domain(src domain id %d, dst domain id %d)",
                src_domain_id, dst_domain_id);
        return NULL;
    }

    AR_MSG(DBG_MED_PRIO," port setup for src domain id %d and dst domain id %d",
            src_domain_id, dst_domain_id);

    if (gpr_dl_zephyr_ports[dst_domain_id] != NULL){
        AR_MSG(DBG_ERROR_PRIO,"port already setup for domain id:%d",
               dst_domain_id);
        return gpr_dl_zephyr_ports[dst_domain_id];
    }
    dl_zephyr_port = (gpr_dl_zephyr_port_t *)calloc(1, sizeof(gpr_dl_zephyr_port_t));
    if (dl_zephyr_port == NULL){
        AR_MSG(DBG_ERROR_PRIO,"calloc failed");
        return NULL;
    }
    dl_zephyr_port->domain_id = dst_domain_id;

    return dl_zephyr_port;
}

static uint32_t gpr_dl_zephyr_local_deinit(uint32_t src_domain_id, uint32_t dst_domain_id)
{
    uint32_t status = AR_EOK;
    gpr_dl_zephyr_port_t *dl_zephyr_port;

    if (gpr_dl_zephyr_ports[dst_domain_id] == NULL) {
        AR_MSG(DBG_ERROR_PRIO,"deinit already done");
        return AR_EOK;
    }
    dl_zephyr_port = gpr_dl_zephyr_ports[dst_domain_id];
    gpr_dl_zephyr_ports[dst_domain_id] = NULL;

    free(dl_zephyr_port);
    return status;
}

// Zephyr specific functions for platform initialization.
static int rpmsg_recv_tty_callback(struct rpmsg_endpoint *ept, void *data,
                   size_t len, uint32_t src, void *priv)
{
    struct rpmsg_rcv_msg msg;
    int ret = 0;

    // Hold the buffer
    rpmsg_hold_rx_buffer(ept, data);

    // Queue the message instead of just setting a pointer
    msg.data = data;
    msg.len = len;

    ret = k_msgq_put(&rpmsg_msgq, &msg, K_NO_WAIT);
    if (ret != 0) {
        // Queue is full, release the buffer and log error
        AR_MSG(DBG_ERROR_PRIO, "Message queue full, dropping message");
        rpmsg_release_rx_buffer(ept, data);
        return RPMSG_ERR_NO_MEM;
    }

    // Signal processing thread
    k_sem_give(&data_tty_sem);

    return RPMSG_SUCCESS;
}

void app_rpmsg_tty(gpr_dl_zephyr_port_t *dl_zephyr_port, void *arg2, void *arg3)
{
    ARG_UNUSED(arg2);
    ARG_UNUSED(arg3);

    int ret = 0;
    uint32_t status = 0;

    k_sem_take(&data_tty_sem,  K_FOREVER);

    AR_MSG(DBG_LOW_PRIO,"OpenAMP responder started");

    tty_ept.priv = &tty_msg;
    ret = rpmsg_create_ept(&tty_ept, rpdev, RPMSG_ENDPOINT_NAME,
                   RPMSG_ADDR_ANY, RPMSG_ADDR_ANY,
                   rpmsg_recv_tty_callback, NULL);
    if (ret) {
        AR_MSG(DBG_ERROR_PRIO, "Could not create endpoint: %d", ret);
        goto task_end;
    }

    AR_MSG(DBG_LOW_PRIO,"OpenAMP rpmsg_create_ept success with rc: %d", ret);

    struct rpmsg_rcv_msg msg;
    while (tty_ept.addr !=  RPMSG_ADDR_ANY) {

        k_sem_take(&data_tty_sem,  K_FOREVER);

        while (k_msgq_get(&rpmsg_msgq, &msg, K_NO_WAIT) == 0) {
            if (msg.len) {
                status = dl_zephyr_port->rx_cb(msg.data, msg.len);
                if (status != AR_EOK) {
                    AR_MSG(DBG_ERROR_PRIO,"receive callback failed");
                    //continue;
                }

                rpmsg_release_rx_buffer(&tty_ept, msg.data);
            }
            msg.len = 0;
            msg.data = NULL;
        }
    }

    AR_MSG(DBG_LOW_PRIO, "After while loop processing. connection disconnected");

    rpmsg_destroy_ept(&tty_ept);

task_end:
    AR_MSG(DBG_LOW_PRIO,"OpenAMP responder ended");
}

int mailbox_notify(void *priv, uint32_t id)
{
    ARG_UNUSED(priv);

    AR_MSG(DBG_LOW_PRIO, "msg receive");
    IPM_SEND(ipm_handle, 0, id, &id, 4);

    return 0;
}

static void platform_ipm_callback(const struct device *dev, void *context,
                  uint32_t id, volatile void *data)
{
    AR_MSG(DBG_LOW_PRIO, "msg received from mb %d", id);
    k_sem_give(&data_sem);
}

static void receive_message(unsigned char **msg, unsigned int *len)
{
    int status = k_sem_take(&data_sem, K_FOREVER);

    if (status == 0) {
        rproc_virtio_notified(rvdev.vdev, VRING1_ID);
    }
}

static void new_service_cb(struct rpmsg_device *rdev, const char *name,
               uint32_t src)
{
    AR_MSG(DBG_ERROR_PRIO,"unexpected ns service receive for name %s",
        name);
}

int platform_init(void)
{
    int rsc_size;
    struct metal_init_params metal_params = METAL_INIT_DEFAULTS;
    int status;

    AR_MSG(DBG_MED_PRIO, "platform_init enter");

    status = metal_init(&metal_params);
    if (status) {
        AR_MSG(DBG_ERROR_PRIO, "metal_init: failed: %d", status);
        return -1;
    }

    /* declare shared memory region */
    metal_io_init(shm_io, (void *)SHM_START_ADDR, &shm_physmap,
              SHM_SIZE, -1, 0, addr_translation_get_ops(shm_physmap));

    /* declare resource table region */
    rsc_table_get(&rsc_table, &rsc_size);
    rsc_tab_physmap = (uintptr_t)rsc_table;

    metal_io_init(rsc_io, rsc_table,
              &rsc_tab_physmap, rsc_size, -1, 0, NULL);

    /* setup IPM */
    if (!device_is_ready(ipm_handle)) {
        AR_MSG(DBG_ERROR_PRIO, "IPM device is not ready");
        return -1;
    }
    ipm_register_callback(ipm_handle, platform_ipm_callback, NULL);

    status = ipm_set_enabled(ipm_handle, 1);
    if (status) {
        AR_MSG(DBG_ERROR_PRIO, "ipm_set_enabled failed with status %d", status);
        return -1;
    }
    AR_MSG(DBG_LOW_PRIO, "platform_init exit");

    return 0;
}

struct  rpmsg_device *
platform_create_rpmsg_vdev(unsigned int vdev_index,
               unsigned int role,
               void (*rst_cb)(struct virtio_device *vdev),
               rpmsg_ns_bind_cb ns_cb)
{
    struct fw_rsc_vdev_vring *vring_rsc;
    struct virtio_device *vdev;
    int ret;

    AR_MSG(DBG_LOW_PRIO, "platform_create_rpmsg_vdev called.");

    vdev = rproc_virtio_create_vdev(VIRTIO_DEV_DEVICE, VDEV_ID,
                    rsc_table_to_vdev(rsc_table),
                    rsc_io, NULL, mailbox_notify, NULL);

    if (!vdev) {
        AR_MSG(DBG_ERROR_PRIO, "failed to create vdev");
        return NULL;
    }

    /* wait master rpmsg init completion */
    rproc_virtio_wait_remote_ready(vdev);

    vring_rsc = rsc_table_get_vring0(rsc_table);

    ret = rproc_virtio_init_vring(vdev, 0, vring_rsc->notifyid,
                      (void *)vring_rsc->da, rsc_io,
                      vring_rsc->num, vring_rsc->align);
    if (ret) {
        AR_MSG(DBG_ERROR_PRIO, "failed to init vring 0");
        goto failed;
    }

    AR_MSG(DBG_MED_PRIO, "vring: vring_rsc->num: %d", vring_rsc->num);
    vring_rsc = rsc_table_get_vring1(rsc_table);
    ret = rproc_virtio_init_vring(vdev, 1, vring_rsc->notifyid,
                      (void *)vring_rsc->da, rsc_io,
                      vring_rsc->num, vring_rsc->align);
    if (ret) {
        AR_MSG(DBG_ERROR_PRIO, "failed to init vring 1");
        goto failed;
    }

    ret = rpmsg_init_vdev(&rvdev, vdev, ns_cb, shm_io, NULL);
    if (ret) {
        AR_MSG(DBG_ERROR_PRIO, "failed rpmsg_init_vdev");
        goto failed;
    }

    AR_MSG(DBG_LOW_PRIO, "After rpmsg vitio device init");

    return rpmsg_virtio_get_rpmsg_device(&rvdev);

failed:
    rproc_virtio_remove_vdev(vdev);

    return NULL;
}

static void cleanup_system(void)
{
    AR_MSG(DBG_HIGH_PRIO, "RPmsg clean up called");
    ipm_set_enabled(ipm_handle, 0);
    rpmsg_deinit_vdev(&rvdev);
    metal_finish();
}

void rpmsg_mng_task(void *arg1, void *arg2, void *arg3)
{
    ARG_UNUSED(arg1);
    ARG_UNUSED(arg2);
    ARG_UNUSED(arg3);

    unsigned char *msg;
    unsigned int len;
    int ret = 0;

    AR_MSG(DBG_LOW_PRIO, "OpenAMP responder started");

    /* Initialize platform */
    ret = platform_init();
    if (ret) {
        AR_MSG(DBG_ERROR_PRIO, "Failed to initialize platform");
        ret = -1;
        goto task_end;
    }

    rpdev = platform_create_rpmsg_vdev(0, VIRTIO_DEV_DEVICE, NULL,
                       new_service_cb);
    if (!rpdev) {
        AR_MSG(DBG_ERROR_PRIO, "Failed to create rpmsg virtio device");
        ret = -1;
        goto task_end;
    }

#ifdef CONFIG_SHELL_BACKEND_RPMSG
    (void)shell_backend_rpmsg_init_transport(rpdev);
#endif

    /* start the rpmsg clients */
    k_sem_give(&data_tty_sem);

    while (1) {
        receive_message(&msg, &len);
    }

task_end:
    cleanup_system();

    AR_MSG(DBG_LOW_PRIO, "OpenAMP ended");
}

uint32_t ipc_dl_zephyr_init(uint32_t src_domain_id,
                        uint32_t dest_domain_id,
                        const gpr_to_ipc_vtbl_t *p_gpr_to_ipc_vtbl,
                        ipc_to_gpr_vtbl_t ** pp_ipc_to_gpr_vtbl)
{
    gpr_dl_zephyr_port_t *dl_zephyr_port;

    if ((dest_domain_id < 0) || (dest_domain_id >= GPR_PL_NUM_TOTAL_DOMAINS_V)
        || (src_domain_id < 0) || (src_domain_id >= GPR_PL_NUM_TOTAL_DOMAINS_V)) {
        AR_MSG(DBG_ERROR_PRIO, "invalid domain(src domain id %d, dst domain id %d)",
                src_domain_id, dest_domain_id);
        return AR_EBADPARAM;
    }

    dl_zephyr_port = gpr_dl_zephyr_local_init(src_domain_id, dest_domain_id);
    if (dl_zephyr_port == NULL) {
        AR_MSG(DBG_ERROR_PRIO, "local_init failed");
        return AR_EFAILED;
    }
    *pp_ipc_to_gpr_vtbl = &gpr_dl_zephyr_vtbl;
    if (p_gpr_to_ipc_vtbl->receive && p_gpr_to_ipc_vtbl->send_done) {
        dl_zephyr_port->rx_cb = p_gpr_to_ipc_vtbl->receive;
        dl_zephyr_port->send_done = p_gpr_to_ipc_vtbl->send_done;
    } else {
        AR_MSG(DBG_ERROR_PRIO, "no gpr cbs error out");
        return AR_EBADPARAM;
    }
    gpr_dl_zephyr_ports[dest_domain_id] = dl_zephyr_port;

    k_thread_create(&thread_mng_data, thread_mng_stack, APP_TASK_STACK_SIZE,
        rpmsg_mng_task,
        NULL, NULL, NULL, K_PRIO_COOP(8), 0, K_NO_WAIT);

    AR_MSG(DBG_LOW_PRIO, "After rpmsg_mng_task launch thread");

    k_thread_create(&thread_tty_data, thread_tty_stack, APP_TTY_TASK_STACK_SIZE,
        app_rpmsg_tty,
        dl_zephyr_port, NULL, NULL, K_PRIO_COOP(7), 0, K_NO_WAIT);

    AR_MSG(DBG_LOW_PRIO, "After app_rpmsg_tty thread launch");

#ifdef CONFIG_SHELL_BACKEND_RPMSG
    (void)shell_backend_rpmsg_init_transport(rpdev);
#endif

    return AR_EOK;
}

uint32_t ipc_dl_zephyr_deinit(uint32_t src_domain_id, uint32_t dest_domain_id)
{
    uint32_t status = AR_EOK;

    status = gpr_dl_zephyr_local_deinit(src_domain_id, dest_domain_id);

    cleanup_system();
    AR_MSG(DBG_LOW_PRIO, "OpenAMP responder ended");
    return status;
}

// Send the data received from GPR to other processor via IPC
static uint32_t gpr_dl_zephyr_send(uint32_t domain_id, void *buf, uint32_t size)
{
    int32_t status = 0;
    uint64_t time = 0;
    unsigned char tx_buff[RPMSG_TX_BUFFER_SIZE];
    gpr_dl_zephyr_port_t *dl_zephyr_port;

    if ((dl_zephyr_port = gpr_dl_zephyr_ports[domain_id]) == NULL) {
        AR_MSG(DBG_ERROR_PRIO,"port domain %d not initialized",
              domain_id);
        return AR_ENOTEXIST;
    }

    if (size > sizeof(tx_buff)) {
        AR_MSG(DBG_ERROR_PRIO, "Message size %d exceeds buffer size %zu", 
               size, sizeof(tx_buff));
        return AR_EBADPARAM;
    }

    AR_MSG(DBG_LOW_PRIO, "Sending buffer of size %d to driver", size);

    memcpy(&tx_buff, buf, size);
    status = rpmsg_send(&tty_ept, tx_buff, size);
    if (status < 0) {
        AR_MSG(DBG_ERROR_PRIO,"write to driver failed %d", errno);
        if (errno == ENETRESET)
            return AR_ESUBSYSRESET;
        else
            return AR_EFAILED;
    }
    dl_zephyr_port->send_done(buf, size);
    AR_MSG(DBG_LOW_PRIO, "Send done");
    return AR_EOK;
}

static uint32_t gpr_dl_zephyr_receive_done(uint32_t domain_id, void *buf)
{
    uint32_t status = AR_EOK;
    gpr_dl_zephyr_port_t *dl_zephyr_port;

    if ((dl_zephyr_port = gpr_dl_zephyr_ports[domain_id]) == NULL) {
        AR_MSG(DBG_ERROR_PRIO,"port domain %d not initialized",
              domain_id);
        return AR_ENOTEXIST;
    }
    return status;
}
