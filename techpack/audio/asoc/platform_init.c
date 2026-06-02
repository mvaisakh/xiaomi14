// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2017, 2020 The Linux Foundation. All rights reserved.
*/

#include <linux/kernel.h>
#include <linux/module.h>
#include "platform_init.h"

static int  audio_platform_init(void)
{

	return 0;
}

static void audio_platform_exit(void)
{
}

techpack_init(audio_platform_init);
module_exit(audio_platform_exit);

MODULE_DESCRIPTION("Audio Platform driver");
MODULE_LICENSE("GPL v2");
