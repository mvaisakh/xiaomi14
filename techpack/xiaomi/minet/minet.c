// SPDX-License-Identifier: GPL-2.0-only

#include <linux/module.h>
#include <linux/netlink.h>
#include <net/genetlink.h>

#define MINET_FAMILY_NAME "minet_genl"
#define MIWILL_FAMILY_NAME "miwill_genl"

/* * Dummy operation handler. 
 * Any command minetd sends will be silently acknowledged as a success.
 */
static int minet_dummy_doit(struct sk_buff *skb, struct genl_info *info)
{
	pr_debug_ratelimited("minet_stub: stub cmd passed %d\n", info->genlhdr->cmd);
	return 0;
}

/* * Map the first 8 command IDs (MINET_CMD_SET, MINET_CMD_TC_ENABLE, etc.) 
 * to our dummy handler.
 */
static const struct genl_ops minet_ops[] = {
	{ .cmd = 1, .validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP, .doit = minet_dummy_doit },
	{ .cmd = 2, .validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP, .doit = minet_dummy_doit },
	{ .cmd = 3, .validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP, .doit = minet_dummy_doit },
	{ .cmd = 4, .validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP, .doit = minet_dummy_doit },
	{ .cmd = 5, .validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP, .doit = minet_dummy_doit },
	{ .cmd = 6, .validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP, .doit = minet_dummy_doit },
	{ .cmd = 7, .validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP, .doit = minet_dummy_doit },
	{ .cmd = 8, .validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP, .doit = minet_dummy_doit },
};

static const struct genl_multicast_group minet_mcgrps[] = {
	{ .name = "minet_event", },
};

static struct genl_family minet_family = {
	.name = MINET_FAMILY_NAME,
	.version = 1,
	.maxattr = 0,
	.netnsok = true,
	.module = THIS_MODULE,
	.ops = minet_ops,
	.n_ops = ARRAY_SIZE(minet_ops),
    .mcgrps = minet_mcgrps,
	.n_mcgrps = ARRAY_SIZE(minet_mcgrps),
    .resv_start_op = 9,
};

static const struct genl_multicast_group miwill_mcgrps[] = {
	{ .name = "miwill_event", },
};

static struct genl_family miwill_family = {
	.name = MIWILL_FAMILY_NAME,
	.version = 1,
	.maxattr = 0,
	.netnsok = true,
	.module = THIS_MODULE,
	.ops = minet_ops,
	.n_ops = ARRAY_SIZE(minet_ops),
	.mcgrps = miwill_mcgrps,
	.n_mcgrps = ARRAY_SIZE(miwill_mcgrps),
	.resv_start_op = 9,
};

static int __init minet_init(void)
{
	int rc;

    rc = genl_register_family(&minet_family);
	if (rc)
		pr_err("minet_stub: Failed to register '%s' (rc: %d)\n", MINET_FAMILY_NAME, rc);
	else
		pr_info("minet_stub: Successfully registered '%s'\n", MINET_FAMILY_NAME);

	rc = genl_register_family(&miwill_family);
	if (rc)
		pr_err("minet_stub: Failed to register '%s' (rc: %d)\n", MIWILL_FAMILY_NAME, rc);
	else
		pr_info("minet_stub: Successfully registered '%s'\n", MIWILL_FAMILY_NAME);

	return 0;
}

module_init(minet_init);

MODULE_DESCRIPTION("Xiaomi minet Netlink Stub");
MODULE_LICENSE("GPL v2");