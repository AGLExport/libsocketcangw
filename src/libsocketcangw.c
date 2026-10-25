/* libsocketcangw.c
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright (C) 2025 Automotive Grade Linux
 *
 * This code is based on can-utils/cangw.c.
 * It's licensed by BSD-3-Clause
 * It's copyright as a
 * Copyright (c) 2010 Volkswagen Group Electronic Research All rights reserved.
 */

/**
 * @file
 * @brief Socket CAN gateway library
 */
#include <errno.h>
#include <fcntl.h>
#include <linux/can/gw.h>
#include <linux/if_link.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <net/if.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include <libsocketcangw.h>

struct s_request_data {
	struct nlmsghdr nh;
	struct rtcanmsg rtcan;
	char buf[1500];
};

#define RTCAN_RTA(r) ((struct rtattr *) (((char *) (r)) + NLMSG_ALIGN(sizeof(struct rtcanmsg))))
#define RTCAN_PAYLOAD(n) NLMSG_PAYLOAD(n, sizeof(struct rtcanmsg))
#define NLMSG_TAIL(nmsg) ((struct rtattr *) (((void *) (nmsg)) + NLMSG_ALIGN((nmsg)->nlmsg_len)))

static int addattr_l(struct nlmsghdr *n, size_t maxlen, int type, const void *data, int alen)
{
	int len = RTA_LENGTH(alen);
	struct rtattr *rta;

	if (NLMSG_ALIGN(n->nlmsg_len) + RTA_ALIGN(len) > maxlen) {
		return -1;
	}

	rta = NLMSG_TAIL(n);
	rta->rta_type = type;
	rta->rta_len = len;
	memcpy(RTA_DATA(rta), data, alen);
	n->nlmsg_len = NLMSG_ALIGN(n->nlmsg_len) + RTA_ALIGN(len);

	return 0;
}

/**
 * @ingroup intern
 * send_cangw_set_request - send request to add gw rule into kernel
 * @param req pointer to request data.
 *
 * @return 0 if success
 * @return -1 if operation is failed
 * @return -2 if linux does not support can gateway
 * @return -3 if returned fail response
 */
static int send_cangw_set_request(struct s_request_data *req)
{
	int result = 0;
	int sock_fd = -1;
	ssize_t ret = -1;
	struct nlmsghdr *nlh = NULL;
	struct nlmsgerr *rte = NULL;
	struct sockaddr_nl nladdr;
	unsigned char rxbuf[8192];

	// Open netlink socket interface
	sock_fd = socket(PF_NETLINK, SOCK_RAW, NETLINK_ROUTE);
	if (sock_fd < 0) {
		result = -1;
		goto do_return;
	}

	memset(&nladdr, 0, sizeof(nladdr));
	nladdr.nl_family = AF_NETLINK;
	nladdr.nl_pid = 0;
	nladdr.nl_groups = 0;

	ret = sendto(sock_fd, req, req->nh.nlmsg_len, 0, (struct sockaddr *) &nladdr, sizeof(nladdr));
	if (ret < 0) {
		result = -1;
		goto do_return;
	}

	memset(rxbuf, 0, sizeof(rxbuf));
	ret = recv(sock_fd, &rxbuf, sizeof(rxbuf), 0);
	if (ret < 0) {
		result = -1;
		goto do_return;
	}

	nlh = (struct nlmsghdr *) rxbuf;
	if (nlh->nlmsg_type != NLMSG_ERROR) {
		result = -2;
		goto do_return;
	}

	rte = (struct nlmsgerr *) NLMSG_DATA(nlh);
	if (rte->error < 0) {
		result = -3;
	}

do_return:
	if (sock_fd >= 0) {
		close(sock_fd);
	}

	return result;
}

/**
 * @ingroup intern
 * push_gw_rule - push gw rule into gw_rules
 * @param gw_rules pointer to rules structure of the can gateway.
 * @param rule pointer to new rule to push into gw_rules.
 *
 * @return 0 if success
 * @return -1 if operation is failed
 */
static int push_gw_rule(socketcan_gw_rules_t *gw_rules, socketcan_gw_rule_t *rule)
{
	int result = 0;

	if (gw_rules->rules == NULL) {
		// Create rules array
		gw_rules->rule_num = 0;	 // Initial size
		gw_rules->array_num = 2; // Initial size
		gw_rules->rules = (socketcan_gw_rule_t **) malloc(
			sizeof(socketcan_gw_rule_t *) * gw_rules->array_num);
		if (gw_rules->rules == NULL) {
			result = -1;
			goto do_return;
		}
	}

	if (!(gw_rules->rule_num < gw_rules->array_num)) {
		// Extend array
		socketcan_gw_rule_t **pnew_rules = NULL;
		gw_rules->array_num = gw_rules->array_num * 2;
		pnew_rules = (socketcan_gw_rule_t **) realloc(
			gw_rules->rules, (sizeof(socketcan_gw_rule_t *) * gw_rules->array_num));
		if (pnew_rules != NULL) {
			gw_rules->rules = pnew_rules;
		} else {
			gw_rules->array_num = gw_rules->array_num / 2;	// failed to extend, restore array_num
			result = -1;
			goto do_return;
		}
	}

	gw_rules->rules[gw_rules->rule_num] = rule;
	gw_rules->rule_num = gw_rules->rule_num + 1;

do_return:
	return result;
}

/**
 * @ingroup intern
 * free_gw_rules - free memory of gw_rules
 * @param gw_rules pointer to rules structure of the can gateway.
 *
 * @return 0 if success
 */
static int free_gw_rules(socketcan_gw_rules_t *gw_rules)
{
	for (size_t i = 0; i < gw_rules->rule_num; i++) {
		free(gw_rules->rules[i]);
		gw_rules->rules[i] = NULL;
	}

	free(gw_rules->rules);
	gw_rules->rules = NULL;
	gw_rules->rule_num = 0;
	gw_rules->array_num = 0;

	return 0;
}

/**
 * @ingroup intern
 * parse_listing_data - parse routing data that get from kernel
 * @param gw_rules rules pointer to rules structure of the can gateway to add rule element.
 * @param rxbuf buffer of received data from kernel.
 * @param len buffer length of received data from kernel.
 *
 * @return 1 if completed to get routing rule from kernel
 * @return 0 if end of received data
 * @return -1 if operation is failed
 */
static int parse_listing_data(socketcan_gw_rules_t *gw_rules, unsigned char *rxbuf, int len)
{
	socketcan_gw_rule_t *rule = NULL;
	struct rtcanmsg *rtc = NULL;
	struct rtattr *rta = NULL;
	struct nlmsghdr *nlh = NULL;
	int rtlen = 0;
	int ret = -1;
	int result = 0;

	nlh = (struct nlmsghdr *) rxbuf;

	while (1) {
		// check the message is valid
		if (!NLMSG_OK(nlh, len)) {
			result = 0;
			break;
		}

		// if it is error message, return error
		if (nlh->nlmsg_type == NLMSG_ERROR) {
			result = -1;
			break;
		}

		// if it is done message, return completed
		if (nlh->nlmsg_type == NLMSG_DONE) {
			result = 1;
			break;
		}

		// Check the AF_CAN message
		rtc = (struct rtcanmsg *) NLMSG_DATA(nlh);
		if (rtc->can_family != AF_CAN) {
			result = -1;
			break;
		}

		// Check the functionality of the can gateway
		if (rtc->gwtype != CGW_TYPE_CAN_CAN) {
			result = -1;
			break;
		}

		rule = (socketcan_gw_rule_t *) malloc(sizeof(socketcan_gw_rule_t));
		if (rule == NULL) {
			result = -1;
			goto error_return;
		}
		memset(rule, 0, sizeof(socketcan_gw_rule_t));

		// Pick up the src_ifindex and dst_ifindex from the netlink message
		rta = (struct rtattr *) RTCAN_RTA(rtc);
		rtlen = RTCAN_PAYLOAD(nlh);
		while (RTA_OK(rta, rtlen)) {
			switch (rta->rta_type) {
			case CGW_SRC_IF:
				rule->src_ifindex = (*(unsigned int *) RTA_DATA(rta));
				break;
			case CGW_DST_IF:
				rule->dst_ifindex = (*(unsigned int *) RTA_DATA(rta));
				break;
			default:
				break;
			}
			rta = RTA_NEXT(rta, rtlen);
		}

		rule->options = (SOCKETCAN_GW_RULE_ECHO | SOCKETCAN_GW_RULE_FILTER);
		// Pick up the echo status from the netlink message
		if ((rtc->flags & CGW_FLAGS_CAN_ECHO) == CGW_FLAGS_CAN_ECHO) {
			rule->echo = 1;
		} else {
			rule->echo = 0;
		}

		// Pick up the filter from the netlink message
		rta = (struct rtattr *) RTCAN_RTA(rtc);
		rtlen = RTCAN_PAYLOAD(nlh);
		while (RTA_OK(rta, rtlen)) {
			switch (rta->rta_type) {
			case CGW_FILTER: {
				struct can_filter *filter = (struct can_filter *) RTA_DATA(rta);
				if ((filter->can_id & CAN_INV_FILTER) == CAN_INV_FILTER) {
					rule->filter.can_id = (filter->can_id & ~CAN_INV_FILTER);
					rule->invert_can_id = 1;
				} else {
					rule->filter.can_id = filter->can_id;
					rule->invert_can_id = 0;
				}
				rule->filter.can_mask = filter->can_mask;
				break;
			}

			// To be extended for other attributes in the future

			default:
				break;
			}
			rta = RTA_NEXT(rta, rtlen);
		}

		// push the rule into gw_rules
		ret = push_gw_rule(gw_rules, rule);
		if (ret < 0) {
			result = -1;
			goto error_return;
		}

		// Move to next message
		nlh = NLMSG_NEXT(nlh, len);
	}

	return result;

error_return:
	(void) free(rule);
	return result;
}

/**
 * @ingroup intern
 * @brief init_req_data - initialize for req data
 *
 * @param req pointer to s_request_data structure that is initialized by this function.
 * @param flags value of the nlmsg_flags
 * @param type value of the nlmsg_type
 *
 * Set a netlink request data from rule to req.
 */
static void init_req_data(struct s_request_data *req, unsigned short flags, unsigned short type)
{
	// Setup common message
	memset(req, 0, sizeof(struct s_request_data));

	req->nh.nlmsg_flags = flags;
	req->nh.nlmsg_type = type;
	req->nh.nlmsg_len = NLMSG_LENGTH(sizeof(struct rtcanmsg));
	req->nh.nlmsg_seq = 0;

	req->rtcan.can_family = AF_CAN;
	req->rtcan.gwtype = CGW_TYPE_CAN_CAN;
	req->rtcan.flags = 0;
}

/**
 * @ingroup intern
 * @brief operate_rule_options - operate to options of gw rule
 *
 * @param req pointer to s_request_data structure that is wrote the options.
 * @param rule pointer to source data of the gw configuration rule
 *
 * Set a netlink request data from rule to req.
 */
static void operate_rule_options(struct s_request_data *req, socketcan_gw_rule_t *rule)
{
	if ((rule->options & SOCKETCAN_GW_RULE_ECHO) == SOCKETCAN_GW_RULE_ECHO) {
		if (rule->echo == 1) {
			req->rtcan.flags |= CGW_FLAGS_CAN_ECHO;
		}
	}

	if ((rule->options & SOCKETCAN_GW_RULE_FILTER) == SOCKETCAN_GW_RULE_FILTER) {
		struct can_filter filter;
		memset(&filter, 0, sizeof(struct can_filter));

		filter.can_id = rule->filter.can_id;
		filter.can_mask = rule->filter.can_mask;
		if (rule->invert_can_id == 1) {
			filter.can_id = filter.can_id | CAN_INV_FILTER;
		}

		addattr_l(
			&req->nh,
			sizeof(struct s_request_data),
			CGW_FILTER,
			&filter,
			sizeof(struct can_filter));
	}
}

/**
 * @ingroup extern
 * cangw_add_rule - add routing rule to can gateway
 * @param rule rule data of the can gateway.
 *
 * @return 0 if success
 * @return -1 if operation is failed
 * @return -2 if linux does not support can gateway
 * @return -3 if argument is invalid
 */
int cangw_add_rule(socketcan_gw_rule_t *rule)
{
	int result = 0;
	int ret = -1;
	unsigned int src_ifindex = 0;
	unsigned int dst_ifindex = 0;
	struct s_request_data req;

	if (rule == NULL) {
		result = -3;
		goto do_return;
	}

	// Setup common message
	init_req_data(&req, (NLM_F_REQUEST | NLM_F_ACK), RTM_NEWROUTE);

	if ((rule->src_ifindex == 0) || (rule->dst_ifindex == 0)) {
		// invalid ifindex
		result = -3;
		goto do_return;
	}

	if (rule->direction == SOCKETCAN_GW_DIRECTION_OUT) {
		src_ifindex = rule->dst_ifindex;
		dst_ifindex = rule->src_ifindex;
	} else {
		src_ifindex = rule->src_ifindex;
		dst_ifindex = rule->dst_ifindex;
	}
	addattr_l(&req.nh, sizeof(req), CGW_SRC_IF, &src_ifindex, sizeof(src_ifindex));
	addattr_l(&req.nh, sizeof(req), CGW_DST_IF, &dst_ifindex, sizeof(dst_ifindex));

	// operate options
	operate_rule_options(&req, rule);

	ret = send_cangw_set_request(&req);
	if (ret < 0) {
		result = -1;
		goto do_return;
	}

do_return:
	return result;
}

/**
 * @ingroup extern
 * cangw_delete_rule - delete routing rule to can gateway
 * @param rule rule data of the can gateway.
 *
 * @return 0 if success
 * @return -1 if operation is failed
 * @return -2 if linux does not support can gateway
 * @return -3 if argument is invalid
 */
int cangw_delete_rule(socketcan_gw_rule_t *rule)
{
	int result = 0;
	int ret = -1;
	struct s_request_data req;

	if (rule == NULL) {
		result = -3;
		goto do_return;
	}

	// Setup common message
	init_req_data(&req, (NLM_F_REQUEST | NLM_F_ACK), RTM_DELROUTE);

	if ((rule->src_ifindex == 0) || (rule->dst_ifindex == 0)) {
		// invalid ifindex
		result = -3;
		goto do_return;
	}
	addattr_l(&req.nh, sizeof(req), CGW_SRC_IF, &rule->src_ifindex, sizeof(rule->src_ifindex));
	addattr_l(&req.nh, sizeof(req), CGW_DST_IF, &rule->dst_ifindex, sizeof(rule->dst_ifindex));

	// operate options
	operate_rule_options(&req, rule);

	ret = send_cangw_set_request(&req);
	if (ret < 0) {
		result = -1;
		goto do_return;
	}

do_return:
	return result;
}

/**
 * @ingroup extern
 * cangw_add_rules - add routing rules to can gateway in array order
 * @param gw_rules Container of rules to add. Its rule array must not be NULL.
 *
 * Stop at the first failure and return -1 if cangw_add_rule() fails.
 * Previously added rules remain installed; no rollback is performed.
 *
 * @return 0 if all rules were added successfully
 * @return -1 if an operation failed
 * @return -3 if the container or its rule array is NULL
 */
int cangw_add_rules(socketcan_gw_rules_t *gw_rules)
{
	int result = -1, ret = -1;

	if ((gw_rules == NULL) || (gw_rules->rules == NULL)) {
		result = -3;
		goto do_return;
	}

	for (size_t i = 0; i < gw_rules->rule_num; i++) {
		ret = cangw_add_rule(gw_rules->rules[i]);
		if (ret < 0) {
			result = -1;
			goto do_return;
		}
	}

	result = 0;

do_return:
	return result;
}

/**
 * @ingroup extern
 * cangw_delete_rules - delete routing rules from can gateway in array order
 * @param gw_rules Container of rules to delete. Its rule array must not be NULL.
 *
 * Stop at the first failure and return -1 if cangw_delete_rule() fails.
 * Previously deleted rules are not restored.
 *
 * @return 0 if all rules were deleted successfully
 * @return -1 if an operation failed
 * @return -3 if the container or its rule array is NULL
 */
int cangw_delete_rules(socketcan_gw_rules_t *gw_rules)
{
	int result = -1, ret = -1;

	if ((gw_rules == NULL) || (gw_rules->rules == NULL)) {
		result = -3;
		goto do_return;
	}

	for (size_t i = 0; i < gw_rules->rule_num; i++) {
		ret = cangw_delete_rule(gw_rules->rules[i]);
		if (ret < 0) {
			result = -1;
			goto do_return;
		}
	}

	result = 0;

do_return:
	return result;
}

/**
 * @ingroup extern
 * cangw_push_gw_rule - push gw rule into gw_rules
 * @param gw_rules pointer to rules structure of the can gateway.
 * @param rule pointer to new rule to push into gw_rules. This rule copies into the new memory. This rule can be free after function call.
 *
 * @return 0 if push operation was successed
 * @return -1 if operation is failed
 */
int cangw_push_gw_rule(socketcan_gw_rules_t *gw_rules, const socketcan_gw_rule_t *rule)
{
	socketcan_gw_rule_t *prule = NULL;
	int ret = -1, result = -1;

	prule = (socketcan_gw_rule_t *)malloc(sizeof(socketcan_gw_rule_t));
	if (prule == NULL) {
		result = -1;
		goto do_return;
	}

	(void) memcpy(prule, rule, sizeof(socketcan_gw_rule_t));
	ret = push_gw_rule(gw_rules, prule);
	if (ret < 0) {
		result = -1;
		goto do_return;
	}
	prule = NULL;
	result = 0;

do_return:
	(void) free(prule);

	return result;
}

/**
 * @ingroup extern
 * cangw_pop_gw_rule - remove the first matching rule from gw_rules
 * @param gw_rules Container owning the registered rules.
 * @param rule Rule to match by all fields; it may point to a registered rule.
 *
 * Free the matching registered rule, compact the array and clear its old tail.
 * Keep the array capacity and the order of the remaining rules unchanged.
 * The caller's comparison rule is not freed unless it is the removed rule.
 * This operation changes only the container, not the kernel gateway rules.
 *
 * @return 0 if a matching rule was removed
 * @return -1 if no matching rule was found
 * @return -3 if an argument or a nonempty container's array is NULL
 */
int cangw_pop_gw_rule(socketcan_gw_rules_t *gw_rules, const socketcan_gw_rule_t *rule)
{
	int result = -1;

	if ((gw_rules == NULL) || (rule == NULL) ||
		(gw_rules->rules == NULL)) {
		result = -3;
		goto do_return;
	}

	for (size_t i = 0; i < gw_rules->rule_num; i++) {
		socketcan_gw_rule_t *entry = gw_rules->rules[i];

		if (entry == NULL) {
			continue;
		}

		if ((entry->src_ifindex != rule->src_ifindex) ||
			(entry->dst_ifindex != rule->dst_ifindex) ||
			(entry->options != rule->options) ||
			(entry->echo != rule->echo) ||
			(entry->invert_can_id != rule->invert_can_id) ||
			(entry->direction != rule->direction) ||
			(entry->filter.can_id != rule->filter.can_id) ||
			(entry->filter.can_mask != rule->filter.can_mask)) {
			continue;
		}

		(void) memmove(&gw_rules->rules[i], &gw_rules->rules[i + 1],
			(gw_rules->rule_num - i - 1) * sizeof(*gw_rules->rules));
		gw_rules->rule_num--;
		gw_rules->rules[gw_rules->rule_num] = NULL;
		(void) free(entry);
		result = 0;
		goto do_return;
	}

do_return:
	return result;
}

/**
 * @ingroup extern
 * cangw_clean_rule - delete all routing rule to can gateway
 *
 * @return 0 if success
 * @return -1 if operation is failed
 * @return -2 if linux does not support can gateway
 */
int cangw_clean_rule(void)
{
	int result = 0;
	int ret = -1;
	unsigned int ifindex = 0;
	struct s_request_data req;

	// Setup common message
	init_req_data(&req, (NLM_F_REQUEST | NLM_F_ACK), RTM_DELROUTE);

	// If src and dst ifindex set to 0, the all rule are deleted.
	addattr_l(&req.nh, sizeof(req), CGW_SRC_IF, &ifindex, sizeof(ifindex));
	addattr_l(&req.nh, sizeof(req), CGW_DST_IF, &ifindex, sizeof(ifindex));

	ret = send_cangw_set_request(&req);
	if (ret < 0) {
		result = -1;
		goto do_return;
	}

do_return:
	return result;
}

/**
 * @ingroup extern
 * cangw_get_rules - get can gateway routing rule
 * @param gw_rules double pointer to rules structure of the can gateway to get existing rules.
 *
 * @return 0 if success
 * @return -1 if operation is failed
 * @return -2 if linux does not support can gateway
 * @return -3 if argument is invalid
 */
int cangw_get_rules(socketcan_gw_rules_t **gw_rules)
{
	int result = 0;
	int sock_fd = -1;
	ssize_t ret = -1;
	struct s_request_data req;
	struct sockaddr_nl nladdr;
	socketcan_gw_rules_t *pgw_rules = NULL;

	if (gw_rules == NULL) {
		result = -3;
		goto do_return;
	}

	// Setup common message
	init_req_data(&req, (NLM_F_REQUEST | NLM_F_DUMP), RTM_GETROUTE);

	// Open netlink socket interface
	sock_fd = socket(PF_NETLINK, SOCK_RAW, NETLINK_ROUTE);
	if (sock_fd < 0) {
		result = -2;
		goto do_return;
	}

	memset(&nladdr, 0, sizeof(nladdr));
	nladdr.nl_family = AF_NETLINK;
	nladdr.nl_pid = 0;
	nladdr.nl_groups = 0;

	ret = sendto(sock_fd, &req, req.nh.nlmsg_len, 0, (struct sockaddr *) &nladdr, sizeof(nladdr));
	if (ret < 0) {
		result = -1;
		goto do_return;
	}

	pgw_rules = malloc(sizeof(socketcan_gw_rules_t));
	if (pgw_rules == NULL) {
		result = -1;
		goto do_return;
	}

	pgw_rules->rule_num = 0;
	pgw_rules->array_num = 0;
	pgw_rules->rules = NULL;

	while (1) {
		int localret = -1;
		unsigned char rxbuf[8192];
		memset(rxbuf, 0, sizeof(rxbuf));

		ret = recv(sock_fd, &rxbuf, sizeof(rxbuf), 0);
		if (ret < 0) {
			result = -1;
			goto do_return;
		}

		localret = parse_listing_data(pgw_rules, rxbuf, ret);
		if (localret > 0) {
			break; /* completed to get routing rule from kernel */
		} else if (localret < 0) {
			result = -1;
			goto do_return;
		} else {
			;/* continue to get routing rule from kernel */
		}
	}

	(*gw_rules) = pgw_rules;
	pgw_rules = NULL;

do_return:
	if (pgw_rules != NULL) {
		cangw_release_rules(pgw_rules);
	}

	if (sock_fd >= 0) {
		close(sock_fd);
	}

	return result;
}
/**
 * @ingroup extern
 * cangw_release_rules - delete routing rule to can gateway
 * @param gw_rules rules pointer to rules structure of the can gateway to free allocated memory.
 *
 * @return 0 if success
 * @return -3 if argument is invalid
 */
int cangw_release_rules(socketcan_gw_rules_t *gw_rules)
{
	int result = 0;

	if (gw_rules == NULL) {
		result = -3;
		goto do_return;
	}

	(void) free_gw_rules(gw_rules);
	(void) free(gw_rules);

do_return:
	return result;
}