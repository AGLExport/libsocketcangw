#include "libsocketcangw.c"
#include <setjmp.h>
#include <cmocka.h>
#include <getopt.h>

int print_gw_rules(socketcan_gw_rules_t *gw_rules)
{
	char src_ifname[IF_NAMESIZE];
	char dst_ifname[IF_NAMESIZE];

	for(size_t i=0; i < gw_rules->rule_num; i++) {
		socketcan_gw_rule_t *rule = gw_rules->rules[i];

		fprintf(stdout, "cangw: -s %s -d %s -f %03X:%X echo=%d\n",
			if_indextoname(rule->src_ifindex, src_ifname),
			if_indextoname(rule->dst_ifindex, dst_ifname),
			rule->filter.can_id,
			rule->filter.can_mask,
			rule->echo
		);
	}

	return 0;
}
//------------------------------------------------------------------------------------------
static int rule_setup(void **state) {
	int ret = -1;
	unsigned long ifindex_tmp = 0;

	ifindex_tmp = if_nametoindex("vcan0");
	if (ifindex_tmp == 0) {
		fprintf(stderr,"Did not create vcan0 interface. That interface needs in test.\n");
		return -1;
	}

	ifindex_tmp = if_nametoindex("vcan1");
	if (ifindex_tmp == 0) {
		fprintf(stderr,"Did not create vcan1 interface. That interface needs in test.\n");
		return -1;
	}

	ifindex_tmp = if_nametoindex("vxcan0");
	if (ifindex_tmp == 0) {
		fprintf(stderr,"Did not create vxcan0 interface. That interface needs in test.\n");
		return -1;
	}

	ret = cangw_clean_rule();
	if (ret < 0) {
		fprintf(stderr,"Could not clean rules.\n");
		return -1;
	}

	return 0;
}

static int rule_teardown(void **state) {
	int ret = -1;

	ret = cangw_clean_rule();
	if (ret < 0) {
		fprintf(stderr,"Could not clean rules.\n");
		return -1;
	}

	return 0;
}
//------------------------------------------------------------------------------------------
static void add_rule_test_10(void **state) {
	int ret = -1;
	socketcan_gw_rule_t gw_rule;
	socketcan_gw_rules_t *gw_rules = NULL;

	memset(&gw_rule, 0, sizeof(gw_rule));

	gw_rule.src_ifindex = if_nametoindex("vcan0");
	gw_rule.dst_ifindex = if_nametoindex("vcan1");

	gw_rule.options |= SOCKETCAN_GW_RULE_ECHO;
	gw_rule.echo = 1;

	gw_rule.options |= SOCKETCAN_GW_RULE_FILTER;
	gw_rule.invert_can_id = 0;
	gw_rule.filter.can_id = 0x3C0;
	gw_rule.filter.can_mask = 0xff0;

	ret = cangw_add_rule(&gw_rule);
	assert_true(ret == 0);

	ret = cangw_get_rules(&gw_rules);
	assert_true(ret == 0);
	assert_non_null(gw_rules);
	assert_true(gw_rules->rule_num == 1);
	assert_true(gw_rules->rules[0]->src_ifindex == if_nametoindex("vcan0"));
	assert_true(gw_rules->rules[0]->dst_ifindex == if_nametoindex("vcan1"));
	assert_true(gw_rules->rules[0]->options == (SOCKETCAN_GW_RULE_ECHO | SOCKETCAN_GW_RULE_FILTER));
	assert_true(gw_rules->rules[0]->echo == 1);
	assert_true(gw_rules->rules[0]->invert_can_id == 0);
	assert_true(gw_rules->rules[0]->filter.can_id == 0x3C0);
	assert_true(gw_rules->rules[0]->filter.can_mask == 0xff0);

	cangw_release_rules(gw_rules);
}
//------------------------------------------------------------------------------------------
static void add_rule_test_11(void **state) {
	int ret = -1;
	socketcan_gw_rule_t gw_rule;
	socketcan_gw_rules_t *gw_rules = NULL;

	memset(&gw_rule, 0, sizeof(gw_rule));

	gw_rule.src_ifindex = if_nametoindex("vcan0");
	gw_rule.dst_ifindex = if_nametoindex("vxcan0");

	gw_rule.options |= SOCKETCAN_GW_RULE_ECHO;
	gw_rule.echo = 0;

	gw_rule.options |= SOCKETCAN_GW_RULE_FILTER;
	gw_rule.invert_can_id = 0;
	gw_rule.filter.can_id = 0x3C0;
	gw_rule.filter.can_mask = 0xff0;

	ret = cangw_add_rule(&gw_rule);
	assert_true(ret == 0);

	ret = cangw_get_rules(&gw_rules);
	assert_true(ret == 0);
	assert_non_null(gw_rules);
	assert_true(gw_rules->rule_num == 1);
	assert_true(gw_rules->rules[0]->src_ifindex == if_nametoindex("vcan0"));
	assert_true(gw_rules->rules[0]->dst_ifindex == if_nametoindex("vxcan0"));
	assert_true(gw_rules->rules[0]->options == (SOCKETCAN_GW_RULE_ECHO | SOCKETCAN_GW_RULE_FILTER));
	assert_true(gw_rules->rules[0]->echo == 0);
	assert_true(gw_rules->rules[0]->invert_can_id == 0);
	assert_true(gw_rules->rules[0]->filter.can_id == 0x3C0);
	assert_true(gw_rules->rules[0]->filter.can_mask == 0xff0);

	cangw_release_rules(gw_rules);
}
//------------------------------------------------------------------------------------------
static void add_rule_test_12(void **state) {
	int ret = -1;
	socketcan_gw_rule_t gw_rule;
	socketcan_gw_rules_t *gw_rules = NULL;

	memset(&gw_rule, 0, sizeof(gw_rule));

	gw_rule.src_ifindex = if_nametoindex("vxcan0");
	gw_rule.dst_ifindex = if_nametoindex("vcan0");

	gw_rule.options |= SOCKETCAN_GW_RULE_ECHO;
	gw_rule.echo = 0;

	gw_rule.options |= SOCKETCAN_GW_RULE_FILTER;
	gw_rule.invert_can_id = 0;
	gw_rule.filter.can_id = 0x188;
	gw_rule.filter.can_mask = 0xfff;

	ret = cangw_add_rule(&gw_rule);
	assert_true(ret == 0);

	ret = cangw_get_rules(&gw_rules);
	assert_true(ret == 0);
	assert_non_null(gw_rules);
	assert_true(gw_rules->rule_num == 1);
	assert_true(gw_rules->rules[0]->src_ifindex == if_nametoindex("vxcan0"));
	assert_true(gw_rules->rules[0]->dst_ifindex == if_nametoindex("vcan0"));
	assert_true(gw_rules->rules[0]->options == (SOCKETCAN_GW_RULE_ECHO | SOCKETCAN_GW_RULE_FILTER));
	assert_true(gw_rules->rules[0]->echo == 0);
	assert_true(gw_rules->rules[0]->invert_can_id == 0);
	assert_true(gw_rules->rules[0]->filter.can_id == 0x188);
	assert_true(gw_rules->rules[0]->filter.can_mask == 0xfff);

	cangw_release_rules(gw_rules);
}
//------------------------------------------------------------------------------------------
static void add_rule_test_13(void **state) {
	int ret = -1;
	socketcan_gw_rule_t gw_rule[2];
	socketcan_gw_rules_t *gw_rules = NULL;

	memset(&gw_rule[0], 0, sizeof(gw_rule[0]));
	memset(&gw_rule[1], 0, sizeof(gw_rule[1]));

	gw_rule[0].src_ifindex = if_nametoindex("vcan0");
	gw_rule[0].dst_ifindex = if_nametoindex("vcan1");
	gw_rule[0].options |= SOCKETCAN_GW_RULE_ECHO;
	gw_rule[0].echo = 1;
	gw_rule[0].options |= SOCKETCAN_GW_RULE_FILTER;
	gw_rule[0].invert_can_id = 0;
	gw_rule[0].filter.can_id = 0x001;
	gw_rule[0].filter.can_mask = 0x0ff;

	gw_rule[1].src_ifindex = if_nametoindex("vxcan0");
	gw_rule[1].dst_ifindex = if_nametoindex("vcan0");
	gw_rule[1].options |= SOCKETCAN_GW_RULE_FILTER;
	gw_rule[1].invert_can_id = 0;
	gw_rule[1].filter.can_id = 0x00f;
	gw_rule[1].filter.can_mask = 0xff0;

	ret = cangw_add_rule(&gw_rule[1]);
	assert_true(ret == 0);

	ret = cangw_add_rule(&gw_rule[0]);
	assert_true(ret == 0);

	ret = cangw_get_rules(&gw_rules);
	assert_true(ret == 0);
	assert_non_null(gw_rules);
	assert_true(gw_rules->rule_num == 2);
	assert_true(gw_rules->rules[0]->src_ifindex == if_nametoindex("vcan0"));
	assert_true(gw_rules->rules[0]->dst_ifindex == if_nametoindex("vcan1"));
	assert_true(gw_rules->rules[0]->options == (SOCKETCAN_GW_RULE_ECHO | SOCKETCAN_GW_RULE_FILTER));
	assert_true(gw_rules->rules[0]->echo == 1);
	assert_true(gw_rules->rules[0]->invert_can_id == 0);
	assert_true(gw_rules->rules[0]->filter.can_id == 0x001);
	assert_true(gw_rules->rules[0]->filter.can_mask == 0x0ff);
	assert_true(gw_rules->rules[1]->src_ifindex == if_nametoindex("vxcan0"));
	assert_true(gw_rules->rules[1]->dst_ifindex == if_nametoindex("vcan0"));
	assert_true(gw_rules->rules[1]->options == (SOCKETCAN_GW_RULE_ECHO | SOCKETCAN_GW_RULE_FILTER));
	assert_true(gw_rules->rules[1]->echo == 0);
	assert_true(gw_rules->rules[1]->invert_can_id == 0);
	assert_true(gw_rules->rules[1]->filter.can_id == 0x00f);
	assert_true(gw_rules->rules[1]->filter.can_mask == 0xff0);

	cangw_release_rules(gw_rules);
}
//------------------------------------------------------------------------------------------
static void add_rule_test_14(void **state) {
	int ret = -1;
	socketcan_gw_rule_t gw_rule;
	socketcan_gw_rules_t *gw_rules = NULL;

	memset(&gw_rule, 0, sizeof(gw_rule));
	gw_rule.invert_can_id = 0;

	gw_rule.src_ifindex = if_nametoindex("vcan0");
	gw_rule.dst_ifindex = if_nametoindex("vcan1");

	gw_rule.options |= SOCKETCAN_GW_RULE_ECHO;
	gw_rule.echo = 1;

	gw_rule.options |= SOCKETCAN_GW_RULE_FILTER;
	gw_rule.invert_can_id = 0;

	canid_t id = 1;
	for(int i=0;i < 256;i++) {
		gw_rule.filter.can_id = id;
		gw_rule.filter.can_mask = 0xfff;
		ret = cangw_add_rule(&gw_rule);
		assert_true(ret == 0);		
		id++;
	}

	ret = cangw_get_rules(&gw_rules);
	assert_true(ret == 0);
	assert_non_null(gw_rules);
	assert_true(gw_rules->rule_num == 256);
	for(int i=0;i < 256;i++) {
		id--;
		assert_true(gw_rules->rules[i]->src_ifindex == if_nametoindex("vcan0"));
		assert_true(gw_rules->rules[i]->dst_ifindex == if_nametoindex("vcan1"));
		assert_true(gw_rules->rules[i]->options == (SOCKETCAN_GW_RULE_ECHO | SOCKETCAN_GW_RULE_FILTER));
		assert_true(gw_rules->rules[i]->echo == 1);
		assert_true(gw_rules->rules[i]->invert_can_id == 0);
		assert_true(gw_rules->rules[i]->filter.can_id == id);
		assert_true(gw_rules->rules[i]->filter.can_mask == 0xfff);
	}

	cangw_release_rules(gw_rules);
}
//------------------------------------------------------------------------------------------
static void add_rule_test_15(void **state) {
	int ret = -1;
	socketcan_gw_rule_t gw_rule;
	socketcan_gw_rules_t *gw_rules = NULL;

	memset(&gw_rule, 0, sizeof(gw_rule));

	gw_rule.src_ifindex = if_nametoindex("vxcan0");
	gw_rule.dst_ifindex = if_nametoindex("vcan0");

	gw_rule.options |= SOCKETCAN_GW_RULE_ECHO;
	gw_rule.echo = 0;

	gw_rule.options |= SOCKETCAN_GW_RULE_FILTER;
	gw_rule.invert_can_id = 1;
	gw_rule.filter.can_id = 0x188;
	gw_rule.filter.can_mask = 0xfff;

	ret = cangw_add_rule(&gw_rule);
	assert_true(ret == 0);

	ret = cangw_get_rules(&gw_rules);
	assert_true(ret == 0);
	assert_non_null(gw_rules);
	assert_true(gw_rules->rule_num == 1);
	assert_true(gw_rules->rules[0]->src_ifindex == if_nametoindex("vxcan0"));
	assert_true(gw_rules->rules[0]->dst_ifindex == if_nametoindex("vcan0"));
	assert_true(gw_rules->rules[0]->options == (SOCKETCAN_GW_RULE_ECHO | SOCKETCAN_GW_RULE_FILTER));
	assert_true(gw_rules->rules[0]->echo == 0);
	assert_true(gw_rules->rules[0]->invert_can_id == 1);
	assert_true(gw_rules->rules[0]->filter.can_id == 0x188);
	assert_true(gw_rules->rules[0]->filter.can_mask == 0xfff);

	cangw_release_rules(gw_rules);
}
//------------------------------------------------------------------------------------------
static void add_rule_test_16(void **state) {
	int ret = -1;
	socketcan_gw_rule_t gw_rule[2];
	socketcan_gw_rules_t *gw_rules = NULL;

	memset(&gw_rule[0], 0, sizeof(gw_rule[0]));
	memset(&gw_rule[1], 0, sizeof(gw_rule[1]));

	gw_rule[0].src_ifindex = if_nametoindex("vcan0");
	gw_rule[0].dst_ifindex = if_nametoindex("vcan1");
	gw_rule[0].options |= SOCKETCAN_GW_RULE_ECHO;
	gw_rule[0].echo = 1;
	gw_rule[0].options |= SOCKETCAN_GW_RULE_FILTER;
	gw_rule[0].invert_can_id = 0;
	gw_rule[0].filter.can_id = 0x001;
	gw_rule[0].filter.can_mask = 0x0ff;

	gw_rule[1].src_ifindex = if_nametoindex("vxcan0");
	gw_rule[1].dst_ifindex = if_nametoindex("vcan0");
	gw_rule[1].options |= SOCKETCAN_GW_RULE_FILTER;
	gw_rule[1].invert_can_id = 1;
	gw_rule[1].filter.can_id = 0x00f;
	gw_rule[1].filter.can_mask = 0xff0;

	ret = cangw_add_rule(&gw_rule[1]);
	assert_true(ret == 0);

	ret = cangw_add_rule(&gw_rule[0]);
	assert_true(ret == 0);

	ret = cangw_get_rules(&gw_rules);
	assert_true(ret == 0);
	assert_non_null(gw_rules);
	assert_true(gw_rules->rule_num == 2);
	assert_true(gw_rules->rules[0]->src_ifindex == if_nametoindex("vcan0"));
	assert_true(gw_rules->rules[0]->dst_ifindex == if_nametoindex("vcan1"));
	assert_true(gw_rules->rules[0]->options == (SOCKETCAN_GW_RULE_ECHO | SOCKETCAN_GW_RULE_FILTER));
	assert_true(gw_rules->rules[0]->echo == 1);
	assert_true(gw_rules->rules[0]->invert_can_id == 0);
	assert_true(gw_rules->rules[0]->filter.can_id == 0x001);
	assert_true(gw_rules->rules[0]->filter.can_mask == 0x0ff);
	assert_true(gw_rules->rules[1]->src_ifindex == if_nametoindex("vxcan0"));
	assert_true(gw_rules->rules[1]->dst_ifindex == if_nametoindex("vcan0"));
	assert_true(gw_rules->rules[1]->options == (SOCKETCAN_GW_RULE_ECHO | SOCKETCAN_GW_RULE_FILTER));
	assert_true(gw_rules->rules[1]->echo == 0);
	assert_true(gw_rules->rules[1]->invert_can_id == 1);
	assert_true(gw_rules->rules[1]->filter.can_id == 0x00f);
	assert_true(gw_rules->rules[1]->filter.can_mask == 0xff0);

	cangw_release_rules(gw_rules);
}
//------------------------------------------------------------------------------------------
static void add_rule_test_17(void **state) {
	int ret = -1;
	socketcan_gw_rule_t gw_rule[2];
	socketcan_gw_rules_t *gw_rules = NULL;

	memset(&gw_rule[0], 0, sizeof(gw_rule[0]));
	memset(&gw_rule[1], 0, sizeof(gw_rule[1]));

	gw_rule[0].src_ifindex = if_nametoindex("vcan0");
	gw_rule[0].dst_ifindex = if_nametoindex("vcan1");
	gw_rule[0].options |= SOCKETCAN_GW_RULE_ECHO;
	gw_rule[0].echo = 1;
	gw_rule[0].direction = SOCKETCAN_GW_DIRECTION_IN;
	gw_rule[0].options |= SOCKETCAN_GW_RULE_FILTER;
	gw_rule[0].invert_can_id = 0;
	gw_rule[0].filter.can_id = 0x001;
	gw_rule[0].filter.can_mask = 0x0ff;

	gw_rule[1].src_ifindex = if_nametoindex("vxcan0");
	gw_rule[1].dst_ifindex = if_nametoindex("vcan0");
	gw_rule[1].direction = SOCKETCAN_GW_DIRECTION_OUT;
	gw_rule[1].options |= SOCKETCAN_GW_RULE_FILTER;
	gw_rule[1].invert_can_id = 0;
	gw_rule[1].filter.can_id = 0x00f;
	gw_rule[1].filter.can_mask = 0xff0;

	ret = cangw_add_rule(&gw_rule[1]);
	assert_true(ret == 0);

	ret = cangw_add_rule(&gw_rule[0]);
	assert_true(ret == 0);

	ret = cangw_get_rules(&gw_rules);
	assert_true(ret == 0);
	assert_non_null(gw_rules);
	assert_true(gw_rules->rule_num == 2);
	assert_true(gw_rules->rules[0]->src_ifindex == if_nametoindex("vcan0"));
	assert_true(gw_rules->rules[0]->dst_ifindex == if_nametoindex("vcan1"));
	assert_true(gw_rules->rules[0]->options == (SOCKETCAN_GW_RULE_ECHO | SOCKETCAN_GW_RULE_FILTER));
	assert_true(gw_rules->rules[0]->echo == 1);
	assert_true(gw_rules->rules[0]->invert_can_id == 0);
	assert_true(gw_rules->rules[0]->filter.can_id == 0x001);
	assert_true(gw_rules->rules[0]->filter.can_mask == 0x0ff);
	assert_true(gw_rules->rules[1]->src_ifindex == if_nametoindex("vcan0"));
	assert_true(gw_rules->rules[1]->dst_ifindex == if_nametoindex("vxcan0"));
	assert_true(gw_rules->rules[1]->options == (SOCKETCAN_GW_RULE_ECHO | SOCKETCAN_GW_RULE_FILTER));
	assert_true(gw_rules->rules[1]->echo == 0);
	assert_true(gw_rules->rules[1]->invert_can_id == 0);
	assert_true(gw_rules->rules[1]->filter.can_id == 0x00f);
	assert_true(gw_rules->rules[1]->filter.can_mask == 0xff0);

	cangw_release_rules(gw_rules);
}
//------------------------------------------------------------------------------------------
static void delete_rule_test_20(void **state) {
	int ret = -1;
	socketcan_gw_rule_t gw_rule;
	socketcan_gw_rules_t *gw_rules = NULL;

	memset(&gw_rule, 0, sizeof(gw_rule));
	gw_rule.invert_can_id = 0;

	gw_rule.src_ifindex = if_nametoindex("vcan0");
	gw_rule.dst_ifindex = if_nametoindex("vcan1");

	gw_rule.options |= SOCKETCAN_GW_RULE_ECHO;
	gw_rule.echo = 1;

	gw_rule.options |= SOCKETCAN_GW_RULE_FILTER;
	gw_rule.filter.can_id = 0x3C0;
	gw_rule.filter.can_mask = 0xff0;

	ret = cangw_add_rule(&gw_rule);
	assert_true(ret == 0);

	ret = cangw_delete_rule(&gw_rule);
	assert_true(ret == 0);

	ret = cangw_get_rules(&gw_rules);
	assert_true(ret == 0);
	assert_non_null(gw_rules);
	assert_true(gw_rules->rule_num == 0);

	cangw_release_rules(gw_rules);
}
//------------------------------------------------------------------------------------------
static void delete_rule_test_21(void **state) {
	int ret = -1;
	socketcan_gw_rule_t gw_rule;
	socketcan_gw_rules_t *gw_rules = NULL;

	memset(&gw_rule, 0, sizeof(gw_rule));
	gw_rule.invert_can_id = 0;

	gw_rule.src_ifindex = if_nametoindex("vcan0");
	gw_rule.dst_ifindex = if_nametoindex("vxcan0");

	gw_rule.options |= SOCKETCAN_GW_RULE_ECHO;
	gw_rule.echo = 0;

	gw_rule.options |= SOCKETCAN_GW_RULE_FILTER;
	gw_rule.filter.can_id = 0x123;
	gw_rule.filter.can_mask = 0x000;

	ret = cangw_add_rule(&gw_rule);
	assert_true(ret == 0);

	ret = cangw_delete_rule(&gw_rule);
	assert_true(ret == 0);

	ret = cangw_get_rules(&gw_rules);
	assert_true(ret == 0);
	assert_non_null(gw_rules);
	assert_true(gw_rules->rule_num == 0);

	cangw_release_rules(gw_rules);
}
//------------------------------------------------------------------------------------------
static void delete_rule_test_22(void **state) {
	int ret = -1;
	socketcan_gw_rule_t gw_rule;
	socketcan_gw_rules_t *gw_rules = NULL;

	memset(&gw_rule, 0, sizeof(gw_rule));
	gw_rule.invert_can_id = 0;

	gw_rule.src_ifindex = if_nametoindex("vxcan0");
	gw_rule.dst_ifindex = if_nametoindex("vcan0");

	gw_rule.options |= SOCKETCAN_GW_RULE_ECHO;
	gw_rule.echo = 0;

	gw_rule.options |= SOCKETCAN_GW_RULE_FILTER;
	gw_rule.filter.can_id = 0x188;
	gw_rule.filter.can_mask = 0xfff;

	ret = cangw_add_rule(&gw_rule);
	assert_true(ret == 0);

	ret = cangw_delete_rule(&gw_rule);
	assert_true(ret == 0);

	ret = cangw_get_rules(&gw_rules);
	assert_true(ret == 0);
	assert_non_null(gw_rules);
	assert_true(gw_rules->rule_num == 0);

	cangw_release_rules(gw_rules);
}
//------------------------------------------------------------------------------------------
static void argument_error_test_40(void **state) {
	int ret = -1;

	ret = cangw_add_rule(NULL);
	assert_true(ret == -3);

	ret = cangw_delete_rule(NULL);
	assert_true(ret == -3);

	ret = cangw_get_rules(NULL);
	assert_true(ret == -3);

	ret = cangw_release_rules(NULL);
	assert_true(ret == -3);
}
//------------------------------------------------------------------------------------------
static void init_device_rules(socketcan_gw_rule_t values[3],
	socketcan_gw_rule_t *entries[3], socketcan_gw_rules_t *rules)
{
	const unsigned int src[] = {
		if_nametoindex("vcan0"), if_nametoindex("vcan0"), if_nametoindex("vxcan0")
	};
	const unsigned int dst[] = {
		if_nametoindex("vcan1"), if_nametoindex("vxcan0"), if_nametoindex("vcan1")
	};

	memset(values, 0, 3 * sizeof(*values));
	memset(rules, 0, sizeof(*rules));
	for (size_t i = 0; i < 3; i++) {
		assert_true(src[i] != 0);
		assert_true(dst[i] != 0);
		values[i].src_ifindex = src[i];
		values[i].dst_ifindex = dst[i];
		values[i].options = SOCKETCAN_GW_RULE_ECHO | SOCKETCAN_GW_RULE_FILTER;
		values[i].echo = i != 1;
		values[i].invert_can_id = i == 1;
		values[i].direction = SOCKETCAN_GW_DIRECTION_IN;
		values[i].filter.can_id = 0x101 * (i + 1);
		values[i].filter.can_mask = 0x7ff;
		entries[i] = &values[i];
	}
	rules->rules = entries;
	rules->rule_num = 3;
}

/* Compare by CAN ID so the test does not depend on kernel listing order. */
static void assert_device_rules(socketcan_gw_rule_t *const *expected, size_t count)
{
	socketcan_gw_rules_t *actual = NULL;

	assert_int_equal(cangw_get_rules(&actual), 0);
	assert_non_null(actual);
	assert_int_equal(actual->rule_num, count);
	for (size_t i = 0; i < count; i++) {
		const socketcan_gw_rule_t *rule = expected[i];
		const socketcan_gw_rule_t *found = NULL;
		unsigned int src = rule->src_ifindex;
		unsigned int dst = rule->dst_ifindex;

		if (rule->direction == SOCKETCAN_GW_DIRECTION_OUT) {
			src = rule->dst_ifindex;
			dst = rule->src_ifindex;
		}
		for (size_t j = 0; j < actual->rule_num; j++) {
			if (actual->rules[j]->filter.can_id == rule->filter.can_id) {
				assert_null(found);
				found = actual->rules[j];
			}
		}
		assert_non_null(found);
		assert_int_equal(found->src_ifindex, src);
		assert_int_equal(found->dst_ifindex, dst);
		assert_int_equal(found->options, rule->options);
		assert_int_equal(found->echo, rule->echo);
		assert_int_equal(found->invert_can_id, rule->invert_can_id);
		assert_int_equal(found->filter.can_mask, rule->filter.can_mask);
	}
	assert_int_equal(cangw_release_rules(actual), 0);
}

static void add_rules_test_18(void **state)
{
	socketcan_gw_rule_t values[3];
	socketcan_gw_rule_t *entries[3];
	socketcan_gw_rules_t rules;
	unsigned int src, dst;
	(void) state;

	init_device_rules(values, entries, &rules);
	values[1].direction = SOCKETCAN_GW_DIRECTION_OUT;
	src = values[1].src_ifindex;
	dst = values[1].dst_ifindex;
	assert_int_equal(cangw_add_rules(&rules), 0);
	assert_device_rules(entries, 3);
	assert_int_equal(values[1].src_ifindex, src);
	assert_int_equal(values[1].dst_ifindex, dst);
	assert_int_equal(values[1].direction, SOCKETCAN_GW_DIRECTION_OUT);
}

static void add_rules_test_19(void **state)
{
	socketcan_gw_rule_t values[3];
	socketcan_gw_rule_t *entries[3];
	socketcan_gw_rules_t rules;
	(void) state;

	init_device_rules(values, entries, &rules);
	/* A nonzero invalid ifindex makes the second request fail in the kernel. */
	values[1].src_ifindex = (unsigned int) -1;
	assert_int_equal(cangw_add_rules(&rules), -1);
	/* The first rule remains installed and the third rule was not added. */
	assert_device_rules(entries, 1);
}

static void delete_rules_test_23(void **state)
{
	socketcan_gw_rule_t values[3];
	socketcan_gw_rule_t *entries[3];
	socketcan_gw_rules_t rules;
	socketcan_gw_rules_t selected;
	(void) state;

	init_device_rules(values, entries, &rules);
	assert_int_equal(cangw_add_rules(&rules), 0);
	assert_device_rules(entries, 3);
	selected = rules;
	selected.rule_num = 2;
	assert_int_equal(cangw_delete_rules(&selected), 0);
	assert_device_rules(&entries[2], 1);
	selected.rules = &entries[2];
	selected.rule_num = 1;
	assert_int_equal(cangw_delete_rules(&selected), 0);
	assert_device_rules(NULL, 0);
}

static void delete_rules_test_24(void **state)
{
	socketcan_gw_rule_t values[3];
	socketcan_gw_rule_t *entries[3];
	socketcan_gw_rules_t rules;
	socketcan_gw_rule_t missing;
	socketcan_gw_rule_t *delete_entries[3];
	socketcan_gw_rules_t selected;
	(void) state;

	init_device_rules(values, entries, &rules);
	assert_int_equal(cangw_add_rules(&rules), 0);
	assert_device_rules(entries, 3);
	missing = values[1];
	missing.filter.can_id = 0x777;
	delete_entries[0] = &values[0];
	delete_entries[1] = &missing;
	delete_entries[2] = &values[1];
	selected = rules;
	selected.rules = delete_entries;
	assert_int_equal(cangw_delete_rules(&selected), -1);
	/* The first deletion persists; failure prevents the third deletion. */
	assert_device_rules(&entries[1], 2);
}
//------------------------------------------------------------------------------------------
static struct option long_options[] = {
	{"add-rule", no_argument, 0, 10},
	{"delete-rule", no_argument, 0, 20},
	{"clean-rule", no_argument, 0, 30},
	{"arg-error", no_argument, 0, 40},
	{0, 0, 0, 0},
};

int main(int argc, char *argv[])
{
	int ret = -1;

	ret = getopt_long(argc, argv, "", long_options, NULL);
	switch (ret) {
		case 10:
		{
			{
				const struct CMUnitTest tests[] = {
					cmocka_unit_test(add_rule_test_10),
				};
				(void) cmocka_run_group_tests(tests, rule_setup, rule_teardown);	
			}
			{
				const struct CMUnitTest tests[] = {
					cmocka_unit_test(add_rule_test_11),
				};
				(void) cmocka_run_group_tests(tests, rule_setup, rule_teardown);	
			}
			{
				const struct CMUnitTest tests[] = {
					cmocka_unit_test(add_rule_test_12),
				};
				(void) cmocka_run_group_tests(tests, rule_setup, rule_teardown);	
			}
			{
				const struct CMUnitTest tests[] = {
					cmocka_unit_test(add_rule_test_13),
				};
				(void) cmocka_run_group_tests(tests, rule_setup, rule_teardown);	
			}
			{
				const struct CMUnitTest tests[] = {
					cmocka_unit_test(add_rule_test_14),
				};
				(void) cmocka_run_group_tests(tests, rule_setup, rule_teardown);	
			}
			{
				const struct CMUnitTest tests[] = {
					cmocka_unit_test(add_rule_test_15),
				};
				(void) cmocka_run_group_tests(tests, rule_setup, rule_teardown);	
			}
			{
				const struct CMUnitTest tests[] = {
					cmocka_unit_test(add_rule_test_16),
				};
				(void) cmocka_run_group_tests(tests, rule_setup, rule_teardown);	
			}
			{
				const struct CMUnitTest tests[] = {
					cmocka_unit_test(add_rule_test_17),
				};
				(void) cmocka_run_group_tests(tests, rule_setup, rule_teardown);
			}
			{
				const struct CMUnitTest tests[] = {
					cmocka_unit_test(add_rules_test_18),
				};
				(void) cmocka_run_group_tests(tests, rule_setup, rule_teardown);
			}
			{
				const struct CMUnitTest tests[] = {
					cmocka_unit_test(add_rules_test_19),
				};
				(void) cmocka_run_group_tests(tests, rule_setup, rule_teardown);
			}
			break;
		}
		case 20:
		{
			{
				const struct CMUnitTest tests[] = {
					cmocka_unit_test(delete_rule_test_20),
				};
				(void) cmocka_run_group_tests(tests, rule_setup, rule_teardown);	
			}
			{
				const struct CMUnitTest tests[] = {
					cmocka_unit_test(delete_rule_test_21),
				};
				(void) cmocka_run_group_tests(tests, rule_setup, rule_teardown);	
			}
			{
				const struct CMUnitTest tests[] = {
					cmocka_unit_test(delete_rule_test_22),
				};
				(void) cmocka_run_group_tests(tests, rule_setup, rule_teardown);	
			}
			{
				const struct CMUnitTest tests[] = {
					cmocka_unit_test(delete_rules_test_23),
				};
				(void) cmocka_run_group_tests(tests, rule_setup, rule_teardown);
			}
			{
				const struct CMUnitTest tests[] = {
					cmocka_unit_test(delete_rules_test_24),
				};
				(void) cmocka_run_group_tests(tests, rule_setup, rule_teardown);
			}
			break;
		}
		case 30:
		{
			/* nop */
			break;
		}
		case 40:
		{
			{
				const struct CMUnitTest tests[] = {
					cmocka_unit_test(argument_error_test_40),
				};
				(void) cmocka_run_group_tests(tests, rule_setup, rule_teardown);
			}
			break;
		}
		default:
		{
			(void) fprintf(stderr, "This option is not support.\n");
			break;
		}
	}

	return 0;
}