/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2023-2024 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef _RMNET_MEM_PRIV_H_
#define _RMNET_MEM_PRIV_H_

#include <linux/kernel.h>
#include <linux/netdevice.h>
#include <linux/module.h>
#include <linux/mm.h>

#define IPA_ID 1
#define RMNET_CORE_ID 2

#define POOL_LEN 4

#define MAX_STATIC_POOL 700
#define MAX_POOL_O3 675
#define MAX_POOL_O2 224

void rmnet_mem_adjust(unsigned perm_size, u8 order);

#define rm_err(fmt, ...)  \
	do { if (0) pr_err(fmt, __VA_ARGS__); } while (0)

extern int max_pool_size[POOL_LEN];
extern int static_pool_size[POOL_LEN];
extern int pool_unbound_feature[POOL_LEN];
extern int rmnet_mem_order_requests[POOL_LEN];
extern int rmnet_mem_id_req[POOL_LEN];
extern int rmnet_mem_id_recycled[POOL_LEN];
extern int target_static_pool_size[POOL_LEN];

#endif
