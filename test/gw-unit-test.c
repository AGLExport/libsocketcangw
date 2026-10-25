/* SPDX-License-Identifier: BSD-3-Clause */
/* White-box cmocka tests; no CAN devices or root privileges required. */
#include <errno.h>
#include <fcntl.h>
#include <linux/can/gw.h>
#include <linux/if_link.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <net/if.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cmocka.h>
#include <libsocketcangw.h>

/* Compatibility with cmocka 1.x, which has no typed integer expectations. */
#ifndef check_expected_uint
#define check_expected_uint(parameter) check_expected(parameter)
#endif
#ifndef expect_uint_value
#define expect_uint_value(function, parameter, value) expect_value(function, parameter, value)
#endif

#define TEST_SOCKET_FD 42

static size_t malloc_calls, malloc_fail_at;
static const unsigned char *recv_data;
static size_t recv_length;
static int recv_error;
static size_t recv_calls, recv_error_at;
static unsigned char *send_capture;
static size_t send_capture_capacity;

static int setup(void **state)
{
	(void) state;
	malloc_calls = malloc_fail_at = 0;
	recv_data = NULL;
	recv_length = 0;
	recv_error = 0;
	recv_calls = recv_error_at = 0;
	send_capture = NULL;
	send_capture_capacity = 0;
	return 0;
}

static void *rule_test_malloc(size_t size)
{
	if (++malloc_calls == malloc_fail_at) {
		errno = ENOMEM;
		return NULL;
	}
	return test_malloc(size);
}

static int rule_test_socket(int domain, int type, int protocol)
{
	int result;
	assert_int_equal(domain, PF_NETLINK);
	assert_int_equal(type, SOCK_RAW);
	assert_int_equal(protocol, NETLINK_ROUTE);
	result = mock_type(int);
	if (result < 0)
		errno = EMFILE;
	return result;
}

static ssize_t rule_test_sendto(int fd, const void *buffer, size_t length, int flags,
	const struct sockaddr *address, socklen_t address_length)
{
	const struct sockaddr_nl *nladdr = (const struct sockaddr_nl *) address;
	int result;
	assert_int_equal(fd, TEST_SOCKET_FD);
	assert_non_null(buffer);
	check_expected_uint(length);
	assert_int_equal(flags, 0);
	assert_non_null(address);
	assert_int_equal(address_length, sizeof(*nladdr));
	assert_int_equal(nladdr->nl_family, AF_NETLINK);
	assert_int_equal(nladdr->nl_pid, 0);
	assert_int_equal(nladdr->nl_groups, 0);
	assert_int_equal(((const struct nlmsghdr *) buffer)->nlmsg_len, length);
	if (send_capture != NULL) {
		assert_true(length <= send_capture_capacity);
		memcpy(send_capture, buffer, length);
	}
	result = mock_type(int);
	if (result < 0)
		errno = ENOBUFS;
	return result;
}

static ssize_t rule_test_recv(int fd, void *buffer, size_t length, int flags)
{
	function_called();
	assert_int_equal(fd, TEST_SOCKET_FD);
	assert_int_equal(flags, 0);
	assert_non_null(buffer);
	recv_calls++;
	if (recv_error != 0 && (recv_error_at == 0 || recv_calls == recv_error_at)) {
		errno = recv_error;
		return -1;
	}
	assert_non_null(recv_data);
	assert_true(recv_length <= length);
	memcpy(buffer, recv_data, recv_length);
	return (ssize_t) recv_length;
}

static int rule_test_close(int fd)
{
	function_called();
	assert_int_equal(fd, TEST_SOCKET_FD);
	return 0;
}

static void *rule_test_realloc(void *pointer, size_t size)
{
	uintptr_t address = (uintptr_t) pointer;
	check_expected_uint(address);
	check_expected_uint(size);
	if (mock_type(int)) {
		errno = ENOMEM;
		return NULL;
	}
	return test_realloc(pointer, size);
}

/* Substitute only the implementation; all allocations use cmocka tracking. */
#define malloc rule_test_malloc
#define socket rule_test_socket
#define sendto rule_test_sendto
#define recv rule_test_recv
#define close rule_test_close
#define realloc rule_test_realloc
#define free test_free
#include "../src/libsocketcangw.c"
#undef malloc
#undef realloc
#undef free
#undef socket
#undef sendto
#undef recv
#undef close

static void test_cangw_push_gw_rule_realloc_failure(void **state)
{
	socketcan_gw_rules_t *gw_rules = test_calloc(1, sizeof(*gw_rules));
	socketcan_gw_rule_t rules[3] = { 0 };
	socketcan_gw_rule_t *registered[2];
	socketcan_gw_rule_t **original;
	(void) state;

	for (size_t i = 0; i < 3; i++) {
		rules[i].filter.can_id = (unsigned int) i + 1;
		rules[i].filter.can_mask = 0x7ff;
	}
	assert_int_equal(cangw_push_gw_rule(gw_rules, &rules[0]), 0);
	assert_int_equal(cangw_push_gw_rule(gw_rules, &rules[1]), 0);
	assert_int_equal(gw_rules->rule_num, 2);
	assert_int_equal(gw_rules->array_num, 2);
	original = gw_rules->rules;
	for (size_t i = 0; i < 2; i++)
		registered[i] = gw_rules->rules[i];

	expect_uint_value(rule_test_realloc, address, (uintptr_t) original);
	expect_uint_value(rule_test_realloc, size, 4 * sizeof(*original));
	will_return(rule_test_realloc, 1);
	assert_int_equal(cangw_push_gw_rule(gw_rules, &rules[2]), -1);
	assert_ptr_equal(gw_rules->rules, original);
	assert_int_equal(gw_rules->rule_num, 2);
	assert_int_equal(gw_rules->array_num, 2);
	for (size_t i = 0; i < 2; i++) {
		assert_ptr_equal(gw_rules->rules[i], registered[i]);
		assert_ptr_not_equal(gw_rules->rules[i], &rules[i]);
		assert_memory_equal(gw_rules->rules[i], &rules[i], sizeof(rules[i]));
	}
	/* The input remains owned by the caller on both success and failure. */
	assert_int_equal(rules[2].filter.can_id, 3);
	expect_uint_value(rule_test_realloc, address, (uintptr_t) original);
	expect_uint_value(rule_test_realloc, size, 4 * sizeof(*original));
	will_return(rule_test_realloc, 0);
	assert_int_equal(cangw_push_gw_rule(gw_rules, &rules[2]), 0);
	assert_int_equal(gw_rules->rule_num, 3);
	assert_int_equal(gw_rules->array_num, 4);
	for (size_t i = 0; i < 3; i++) {
		assert_ptr_not_equal(gw_rules->rules[i], &rules[i]);
		assert_memory_equal(gw_rules->rules[i], &rules[i], sizeof(rules[i]));
	}
	for (size_t i = 0; i < 2; i++)
		assert_ptr_equal(gw_rules->rules[i], registered[i]);
	assert_int_equal(cangw_release_rules(gw_rules), 0);
}

static void test_parse_listing_data_realloc_failure(void **state)
{
	socketcan_gw_rules_t *gw_rules = test_calloc(1, sizeof(*gw_rules));
	struct s_request_data req;
	unsigned char rxbuf[3 * NLMSG_SPACE(sizeof(struct rtcanmsg))];
	(void) state;

	init_req_data(&req, 0, RTM_NEWROUTE);
	memset(rxbuf, 0, sizeof(rxbuf));
	for (size_t i = 0; i < 3; i++)
		memcpy(rxbuf + i * NLMSG_SPACE(sizeof(struct rtcanmsg)), &req, req.nh.nlmsg_len);

	expect_any(rule_test_realloc, address);
	expect_uint_value(rule_test_realloc, size, 4 * sizeof(*gw_rules->rules));
	will_return(rule_test_realloc, 1);
	assert_int_equal(parse_listing_data(gw_rules, rxbuf, sizeof(rxbuf)), -1);
	assert_int_equal(gw_rules->rule_num, 2);
	assert_int_equal(gw_rules->array_num, 2);
	assert_non_null(gw_rules->rules[0]);
	assert_non_null(gw_rules->rules[1]);
	/* cmocka also checks that the unregistered third rule was freed. */
	assert_int_equal(cangw_release_rules(gw_rules), 0);
}

static void test_addattr_l_failure(void **state)
{
	struct nlmsghdr nlh;
	(void) state;

	memset(&nlh, 0, sizeof(nlh));
	nlh.nlmsg_len = 1024;

	assert_int_equal(addattr_l(&nlh, 0, 0, NULL, 0), -1);
}

static size_t make_listing_data(unsigned char *buffer, size_t capacity)
{
	size_t used = 0;
	for (unsigned int i = 0; i < 2; i++) {
		struct s_request_data req;
		unsigned int src = 1, dst = 2;
		struct can_filter filter = { .can_id = i + 1, .can_mask = 0x7ff };
		init_req_data(&req, 0, RTM_NEWROUTE);
		assert_int_equal(addattr_l(&req.nh, sizeof(req), CGW_SRC_IF, &src, sizeof(src)), 0);
		assert_int_equal(addattr_l(&req.nh, sizeof(req), CGW_DST_IF, &dst, sizeof(dst)), 0);
		assert_int_equal(addattr_l(&req.nh, sizeof(req), CGW_FILTER, &filter, sizeof(filter)), 0);
		assert_true(used + NLMSG_ALIGN(req.nh.nlmsg_len) <= capacity);
		memcpy(buffer + used, &req, req.nh.nlmsg_len);
		used += NLMSG_ALIGN(req.nh.nlmsg_len);
	}
	return used;
}

static void test_cangw_push_gw_rule_malloc_failure(void **state)
{
	const socketcan_gw_rule_t rule = {
		.src_ifindex = 1,
		.dst_ifindex = 2,
		.filter = { .can_id = 0x123, .can_mask = 0x7ff }
	};
	(void) state;

	/* Fail the rule copy allocation and the pointer array allocation. */
	for (size_t fail_at = 1; fail_at <= 2; fail_at++) {
		socketcan_gw_rules_t *rules = test_calloc(1, sizeof(*rules));

		malloc_calls = 0;
		malloc_fail_at = fail_at;
		assert_int_equal(cangw_push_gw_rule(rules, &rule), -1);
		assert_int_equal(malloc_calls, fail_at);
		assert_int_equal(rules->rule_num, 0);
		assert_null(rules->rules);

		/* The same input remains available for a retry. */
		malloc_calls = 0;
		malloc_fail_at = 0;
		assert_int_equal(cangw_push_gw_rule(rules, &rule), 0);
		assert_int_equal(malloc_calls, 2);
		assert_int_equal(rules->rule_num, 1);
		assert_ptr_not_equal(rules->rules[0], &rule);
		assert_memory_equal(rules->rules[0], &rule, sizeof(rule));

		assert_int_equal(cangw_release_rules(rules), 0);
	}
}

static void test_parse_listing_data_malloc_failures(void **state)
{
	unsigned char rxbuf[2 * sizeof(struct s_request_data)] = { 0 };
	size_t length = make_listing_data(rxbuf, sizeof(rxbuf));
	(void) state;
	/* Fail the first rule, its pointer array, and the second rule. */
	for (size_t fail_at = 1; fail_at <= 3; fail_at++) {
		socketcan_gw_rules_t *gw_rules = test_calloc(1, sizeof(*gw_rules));
		malloc_calls = 0;
		malloc_fail_at = fail_at;
		assert_int_equal(parse_listing_data(gw_rules, rxbuf, (int) length), -1);
		assert_int_equal(malloc_calls, fail_at);
		assert_int_equal(gw_rules->rule_num, fail_at == 3 ? 1 : 0);
		if (gw_rules->rule_num != 0) {
			assert_int_equal(gw_rules->rules[0]->filter.can_id, 1);
			assert_int_equal(gw_rules->rules[0]->filter.can_mask, 0x7ff);
		}
		assert_int_equal(cangw_release_rules(gw_rules), 0);
	}
}

static void expect_send_request(size_t length, int result)
{
	expect_uint_value(rule_test_sendto, length, length);
	will_return(rule_test_sendto, result);
}

static void test_get_rules_malloc_failures(void **state)
{
	unsigned char rxbuf[2 * sizeof(struct s_request_data)] = { 0 };
	(void) state;
	recv_data = rxbuf;
	recv_length = make_listing_data(rxbuf, sizeof(rxbuf));
	/* Container, first rule, pointer array, and second rule. */
	for (size_t fail_at = 1; fail_at <= 4; fail_at++) {
		socketcan_gw_rules_t *gw_rules = NULL;
		malloc_calls = 0;
		malloc_fail_at = fail_at;
		will_return(rule_test_socket, TEST_SOCKET_FD);
		expect_send_request(NLMSG_LENGTH(sizeof(struct rtcanmsg)),
			(int) NLMSG_LENGTH(sizeof(struct rtcanmsg)));
		if (fail_at > 1)
			expect_function_call(rule_test_recv);
		expect_function_call(rule_test_close);
		assert_int_equal(cangw_get_rules(&gw_rules), -1);
		assert_null(gw_rules);
		assert_int_equal(malloc_calls, fail_at);
	}
}

static int run_set_operation(size_t operation)
{
	socketcan_gw_rule_t rule = { .src_ifindex = 1, .dst_ifindex = 2 };
	if (operation == 0)
		return cangw_add_rule(&rule);
	if (operation == 1)
		return cangw_delete_rule(&rule);
	return cangw_clean_rule();
}

static void test_set_request_socket_failure(void **state)
{
	(void) state;
	for (size_t operation = 0; operation < 3; operation++) {
		will_return(rule_test_socket, -1);
		assert_int_equal(run_set_operation(operation), -1);
	}
	assert_int_equal(malloc_calls, 0);
}

static void test_get_rules_socket_failure(void **state)
{
	socketcan_gw_rules_t *gw_rules = NULL;
	(void) state;
	will_return(rule_test_socket, -1);
	assert_int_equal(cangw_get_rules(&gw_rules), -2);
	assert_null(gw_rules);
	assert_int_equal(malloc_calls, 0);
}

static void test_set_request_sendto_failure(void **state)
{
	const size_t length = NLMSG_LENGTH(sizeof(struct rtcanmsg)) +
		2 * RTA_ALIGN(RTA_LENGTH(sizeof(unsigned int))) +
		RTA_ALIGN(RTA_LENGTH(sizeof(struct can_filter)));
	(void) state;
	for (size_t operation = 0; operation < 3; operation++) {
		will_return(rule_test_socket, TEST_SOCKET_FD);
		expect_send_request(operation == 2 ?
			length - RTA_ALIGN(RTA_LENGTH(sizeof(struct can_filter))) : length, -1);
		expect_function_call(rule_test_close);
		assert_int_equal(run_set_operation(operation), -1);
	}
	assert_int_equal(malloc_calls, 0);
}

static void test_get_rules_sendto_failure(void **state)
{
	socketcan_gw_rules_t *gw_rules = NULL;
	(void) state;
	will_return(rule_test_socket, TEST_SOCKET_FD);
	expect_send_request(NLMSG_LENGTH(sizeof(struct rtcanmsg)), -1);
	expect_function_call(rule_test_close);
	assert_int_equal(cangw_get_rules(&gw_rules), -1);
	assert_null(gw_rules);
	assert_int_equal(malloc_calls, 0);
}

/* No socket expectations: starting communication would fail these tests. */
static void test_add_rule_zero_src_ifindex(void **state)
{
	socketcan_gw_rule_t rule = { .src_ifindex = 0, .dst_ifindex = 2 };
	(void) state;
	assert_int_equal(cangw_add_rule(&rule), -3);
	assert_int_equal(malloc_calls, 0);
}

static void test_add_rule_zero_dst_ifindex(void **state)
{
	socketcan_gw_rule_t rule = { .src_ifindex = 1, .dst_ifindex = 0 };
	(void) state;
	assert_int_equal(cangw_add_rule(&rule), -3);
	assert_int_equal(malloc_calls, 0);
}

static void test_delete_rule_zero_src_ifindex(void **state)
{
	socketcan_gw_rule_t rule = { .src_ifindex = 0, .dst_ifindex = 2 };
	(void) state;
	assert_int_equal(cangw_delete_rule(&rule), -3);
	assert_int_equal(malloc_calls, 0);
}

static void test_delete_rule_zero_dst_ifindex(void **state)
{
	socketcan_gw_rule_t rule = { .src_ifindex = 1, .dst_ifindex = 0 };
	(void) state;
	assert_int_equal(cangw_delete_rule(&rule), -3);
	assert_int_equal(malloc_calls, 0);
}

static void expect_set_request_response(struct s_request_data *req)
{
	unsigned int src = 1, dst = 2;
	init_req_data(req, NLM_F_REQUEST | NLM_F_ACK, RTM_NEWROUTE);
	assert_int_equal(addattr_l(&req->nh, sizeof(*req), CGW_SRC_IF, &src, sizeof(src)), 0);
	assert_int_equal(addattr_l(&req->nh, sizeof(*req), CGW_DST_IF, &dst, sizeof(dst)), 0);
	will_return(rule_test_socket, TEST_SOCKET_FD);
	expect_send_request(req->nh.nlmsg_len, (int) req->nh.nlmsg_len);
	expect_function_call(rule_test_recv);
	expect_function_call(rule_test_close);
}

static void test_set_request_recv_failure(void **state)
{
	struct s_request_data req;
	(void) state;
	expect_set_request_response(&req);
	recv_error = EIO;
	assert_int_equal(send_cangw_set_request(&req), -1);
	assert_int_equal(malloc_calls, 0);
}

static void test_set_request_unexpected_response_type(void **state)
{
	struct s_request_data req, reply;
	(void) state;
	expect_set_request_response(&req);
	init_req_data(&reply, 0, RTM_NEWROUTE);
	recv_data = (const unsigned char *) &reply;
	recv_length = reply.nh.nlmsg_len;
	assert_int_equal(send_cangw_set_request(&req), -2);
	assert_int_equal(malloc_calls, 0);
}

static void test_set_request_kernel_error(void **state)
{
	struct s_request_data req;
	struct {
		struct nlmsghdr header;
		int error;
		struct s_request_data request;
	} reply = { 0 };
	(void) state;
	expect_set_request_response(&req);
	/* A negative ACK contains the error followed by the original request. */
	reply.header.nlmsg_type = NLMSG_ERROR;
	reply.header.nlmsg_len = NLMSG_LENGTH(sizeof(reply.error) + req.nh.nlmsg_len);
	reply.error = -EINVAL;
	reply.request = req;
	recv_data = (const unsigned char *) &reply;
	recv_length = reply.header.nlmsg_len;
	assert_int_equal(send_cangw_set_request(&req), -3);
	assert_int_equal(malloc_calls, 0);
}

static void test_get_rules_recv_failure(void **state)
{
	socketcan_gw_rules_t *gw_rules = NULL;
	(void) state;
	will_return(rule_test_socket, TEST_SOCKET_FD);
	expect_send_request(NLMSG_LENGTH(sizeof(struct rtcanmsg)),
		(int) NLMSG_LENGTH(sizeof(struct rtcanmsg)));
	expect_function_call(rule_test_recv);
	expect_function_call(rule_test_close);
	recv_error = EIO;
	assert_int_equal(cangw_get_rules(&gw_rules), -1);
	assert_null(gw_rules);
	assert_int_equal(recv_calls, 1);
	assert_int_equal(malloc_calls, 1);
}

static void test_get_rules_recv_failure_after_rules(void **state)
{
	socketcan_gw_rules_t *gw_rules = NULL;
	unsigned char rxbuf[2 * sizeof(struct s_request_data)] = { 0 };
	(void) state;
	recv_data = rxbuf;
	recv_length = make_listing_data(rxbuf, sizeof(rxbuf));
	recv_error = EIO;
	recv_error_at = 2;
	will_return(rule_test_socket, TEST_SOCKET_FD);
	expect_send_request(NLMSG_LENGTH(sizeof(struct rtcanmsg)),
		(int) NLMSG_LENGTH(sizeof(struct rtcanmsg)));
	expect_function_calls(rule_test_recv, 2);
	expect_function_call(rule_test_close);
	assert_int_equal(cangw_get_rules(&gw_rules), -1);
	assert_null(gw_rules);
	assert_int_equal(recv_calls, 2);
	/* Container, two rules, and their pointer array must all be released. */
	assert_int_equal(malloc_calls, 4);
}

static void test_release_rules_null(void **state)
{
	(void) state;
	assert_int_equal(cangw_release_rules(NULL), -3);
	assert_int_equal(malloc_calls, 0);
}

static void check_rule_directions(int (*operation)(socketcan_gw_rule_t *),
	unsigned short message_type)
{
	static const struct {
		unsigned int direction, src, dst;
	} cases[] = {
		{ SOCKETCAN_GW_DIRECTION_IN, 11, 22 },
		{ SOCKETCAN_GW_DIRECTION_OUT, 22, 11 }
	};
	const size_t length = NLMSG_LENGTH(sizeof(struct rtcanmsg)) +
		2 * RTA_ALIGN(RTA_LENGTH(sizeof(unsigned int))) +
		RTA_ALIGN(RTA_LENGTH(sizeof(struct can_filter)));
	for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
		socketcan_gw_rule_t rule = {
			.src_ifindex = 11, .dst_ifindex = 22, .direction = cases[i].direction
		};
		struct s_request_data sent = { 0 };
		struct {
			struct nlmsghdr header;
			struct nlmsgerr ack;
		} reply = { 0 };
		unsigned int src = 0, dst = 0;
		struct rtattr *attr;
		int remaining;

		reply.header.nlmsg_type = NLMSG_ERROR;
		reply.header.nlmsg_len = NLMSG_LENGTH(sizeof(reply.ack));
		recv_data = (const unsigned char *) &reply;
		recv_length = reply.header.nlmsg_len;
		send_capture = (unsigned char *) &sent;
		send_capture_capacity = sizeof(sent);
		will_return(rule_test_socket, TEST_SOCKET_FD);
		expect_send_request(length, (int) length);
		expect_function_call(rule_test_recv);
		expect_function_call(rule_test_close);
		assert_int_equal(operation(&rule), 0);
		assert_int_equal(sent.nh.nlmsg_type, message_type);
		remaining = RTCAN_PAYLOAD(&sent.nh);
		for (attr = RTCAN_RTA(&sent.rtcan); RTA_OK(attr, remaining);
			attr = RTA_NEXT(attr, remaining)) {
			if (attr->rta_type == CGW_SRC_IF)
				memcpy(&src, RTA_DATA(attr), sizeof(src));
			if (attr->rta_type == CGW_DST_IF)
				memcpy(&dst, RTA_DATA(attr), sizeof(dst));
		}
		assert_int_equal(src, cases[i].src);
		assert_int_equal(dst, cases[i].dst);
		assert_int_equal(rule.src_ifindex, 11);
		assert_int_equal(rule.dst_ifindex, 22);
		assert_int_equal(rule.direction, cases[i].direction);
	}
	assert_int_equal(malloc_calls, 0);
}

static void test_add_rule_directions(void **state)
{
	(void) state;
	check_rule_directions(cangw_add_rule, RTM_NEWROUTE);
}

static void test_delete_rule_directions(void **state)
{
	(void) state;
	check_rule_directions(cangw_delete_rule, RTM_DELROUTE);
}

static int (*const bulk_operations[])(socketcan_gw_rules_t *) = {
	cangw_add_rules, cangw_delete_rules
};

static void expect_rule_success(size_t length)
{
	will_return(rule_test_socket, TEST_SOCKET_FD);
	expect_send_request(length, (int) length);
	expect_function_call(rule_test_recv);
	expect_function_call(rule_test_close);
}

static void test_bulk_rule_arguments(void **state)
{
	(void) state;
	for (size_t operation = 0; operation < 2; operation++) {
		socketcan_gw_rule_t rule = { .src_ifindex = 0, .dst_ifindex = 2 };
		socketcan_gw_rule_t *entries[] = { NULL };
		socketcan_gw_rules_t rules = { 0 };

		assert_int_equal(bulk_operations[operation](NULL), -3);
		assert_int_equal(bulk_operations[operation](&rules), -3);
		rules.rule_num = 1;
		assert_int_equal(bulk_operations[operation](&rules), -3);
		rules.rules = entries;
		rules.rule_num = 0;
		assert_int_equal(bulk_operations[operation](&rules), 0);
		rules.rule_num = 1;
		assert_int_equal(bulk_operations[operation](&rules), -1);
		entries[0] = &rule;
		assert_int_equal(bulk_operations[operation](&rules), -1);
		rule.src_ifindex = 1;
		rule.dst_ifindex = 0;
		assert_int_equal(bulk_operations[operation](&rules), -1);
		assert_int_equal(recv_calls, 0);
	}
	assert_int_equal(malloc_calls, 0);
}

static void test_bulk_rule_success(void **state)
{
	const size_t length = NLMSG_LENGTH(sizeof(struct rtcanmsg)) +
		2 * RTA_ALIGN(RTA_LENGTH(sizeof(unsigned int))) +
		RTA_ALIGN(RTA_LENGTH(sizeof(struct can_filter)));
	struct {
		struct nlmsghdr header;
		struct nlmsgerr ack;
	} reply = { 0 };
	(void) state;
	reply.header.nlmsg_type = NLMSG_ERROR;
	reply.header.nlmsg_len = NLMSG_LENGTH(sizeof(reply.ack));
	recv_data = (const unsigned char *) &reply;
	recv_length = reply.header.nlmsg_len;
	for (size_t operation = 0; operation < 2; operation++) {
		socketcan_gw_rule_t values[] = {
			{ .src_ifindex = 1, .dst_ifindex = 2 },
			{ .src_ifindex = 3, .dst_ifindex = 4,
				.filter = { .can_id = 0x123, .can_mask = 0x7ff } },
			{ .src_ifindex = 5, .dst_ifindex = 6, .direction = SOCKETCAN_GW_DIRECTION_OUT }
		};
		socketcan_gw_rule_t *entries[] = { &values[0], &values[1], &values[2] };
		socketcan_gw_rules_t rules = { .rule_num = 3, .rules = entries };
		struct s_request_data sent = { 0 };
		unsigned int src = 0, dst = 0;
		struct rtattr *attr;
		int remaining;

		recv_calls = 0;
		send_capture = (unsigned char *) &sent;
		send_capture_capacity = sizeof(sent);
		expect_rule_success(length);
		expect_rule_success(length);
		expect_rule_success(length);
		assert_int_equal(bulk_operations[operation](&rules), 0);
		assert_int_equal(recv_calls, 3);
		assert_int_equal(rules.rule_num, 3);
		assert_ptr_equal(rules.rules, entries);
		assert_int_equal(sent.nh.nlmsg_type, operation == 0 ? RTM_NEWROUTE : RTM_DELROUTE);
		remaining = RTCAN_PAYLOAD(&sent.nh);
		for (attr = RTCAN_RTA(&sent.rtcan); RTA_OK(attr, remaining);
			attr = RTA_NEXT(attr, remaining)) {
			if (attr->rta_type == CGW_SRC_IF)
				memcpy(&src, RTA_DATA(attr), sizeof(src));
			if (attr->rta_type == CGW_DST_IF)
				memcpy(&dst, RTA_DATA(attr), sizeof(dst));
		}
		assert_int_equal(src, 6);
		assert_int_equal(dst, 5);
	}
	assert_int_equal(malloc_calls, 0);
}

static void test_bulk_rule_operation_failure(void **state)
{
	const size_t length = NLMSG_LENGTH(sizeof(struct rtcanmsg)) +
		2 * RTA_ALIGN(RTA_LENGTH(sizeof(unsigned int))) +
		RTA_ALIGN(RTA_LENGTH(sizeof(struct can_filter)));
	struct {
		struct nlmsghdr header;
		struct nlmsgerr ack;
	} reply = { 0 };
	(void) state;
	reply.header.nlmsg_type = NLMSG_ERROR;
	reply.header.nlmsg_len = NLMSG_LENGTH(sizeof(reply.ack));
	recv_data = (const unsigned char *) &reply;
	recv_length = reply.header.nlmsg_len;
	for (size_t operation = 0; operation < 2; operation++) {
		for (size_t fail_at = 0; fail_at < 3; fail_at++) {
			socketcan_gw_rule_t rule = { .src_ifindex = 1, .dst_ifindex = 2 };
			socketcan_gw_rule_t *entries[] = { &rule, &rule, &rule };
			socketcan_gw_rules_t rules = { .rule_num = 3, .rules = entries };

			recv_calls = 0;
			for (size_t i = 0; i < fail_at; i++)
				expect_rule_success(length);
			will_return(rule_test_socket, -1);
			assert_int_equal(bulk_operations[operation](&rules), -1);
			assert_int_equal(recv_calls, fail_at);
			assert_int_equal(rules.rule_num, 3);
		}
	}
	assert_int_equal(malloc_calls, 0);
}

static void test_bulk_rule_invalid_entry_after_success(void **state)
{
	const size_t length = NLMSG_LENGTH(sizeof(struct rtcanmsg)) +
		2 * RTA_ALIGN(RTA_LENGTH(sizeof(unsigned int))) +
		RTA_ALIGN(RTA_LENGTH(sizeof(struct can_filter)));
	struct {
		struct nlmsghdr header;
		struct nlmsgerr ack;
	} reply = { 0 };
	(void) state;
	reply.header.nlmsg_type = NLMSG_ERROR;
	reply.header.nlmsg_len = NLMSG_LENGTH(sizeof(reply.ack));
	recv_data = (const unsigned char *) &reply;
	recv_length = reply.header.nlmsg_len;
	for (size_t operation = 0; operation < 2; operation++) {
		socketcan_gw_rule_t rule = { .src_ifindex = 1, .dst_ifindex = 2 };
		socketcan_gw_rule_t *entries[] = { &rule, NULL, &rule };
		socketcan_gw_rules_t rules = { .rule_num = 3, .rules = entries };

		recv_calls = 0;
		expect_rule_success(length);
		assert_int_equal(bulk_operations[operation](&rules), -1);
		assert_int_equal(recv_calls, 1);
		assert_int_equal(rules.rule_num, 3);
	}
	assert_int_equal(malloc_calls, 0);
}

static void test_pop_rule_arguments_and_missing(void **state)
{
	socketcan_gw_rule_t rule = { .src_ifindex = 1, .dst_ifindex = 2 };
	socketcan_gw_rule_t *entries[] = { NULL };
	socketcan_gw_rules_t rules = { 0 };
	(void) state;

	assert_int_equal(cangw_pop_gw_rule(NULL, &rule), -3);
	assert_int_equal(cangw_pop_gw_rule(&rules, NULL), -3);
	assert_int_equal(cangw_pop_gw_rule(&rules, &rule), -3);
	rules.rule_num = 1;
	assert_int_equal(cangw_pop_gw_rule(&rules, &rule), -3);
	rules.rules = entries;
	rules.rule_num = 0;
	assert_int_equal(cangw_pop_gw_rule(&rules, &rule), -1);
	rules.rule_num = 1;
	assert_int_equal(cangw_pop_gw_rule(&rules, &rule), -1);
	assert_int_equal(rules.rule_num, 1);
	assert_null(entries[0]);
	assert_int_equal(malloc_calls, 0);
}

static void test_pop_rule_positions(void **state)
{
	(void) state;
	for (size_t position = 0; position < 3; position++) {
		socketcan_gw_rules_t *rules = test_calloc(1, sizeof(*rules));
		socketcan_gw_rule_t *entries[3];
		socketcan_gw_rule_t match;
		socketcan_gw_rule_t **array;
		size_t remaining = 0;

		rules->rule_num = 3;
		rules->array_num = 4;
		rules->rules = test_calloc(4, sizeof(*rules->rules));
		array = rules->rules;
		for (size_t i = 0; i < 3; i++) {
			entries[i] = test_calloc(1, sizeof(*entries[i]));
			entries[i]->src_ifindex = 1;
			entries[i]->dst_ifindex = 2;
			entries[i]->filter.can_id = i + 1;
			rules->rules[i] = entries[i];
		}
		match = *entries[position];
		/* Exercise both an independent value and the owned pointer itself. */
		assert_int_equal(cangw_pop_gw_rule(rules,
			position == 1 ? entries[position] : &match), 0);
		assert_int_equal(rules->rule_num, 2);
		assert_int_equal(rules->array_num, 4);
		assert_ptr_equal(rules->rules, array);
		for (size_t i = 0; i < 3; i++) {
			if (i == position)
				continue;
			assert_ptr_equal(rules->rules[remaining], entries[i]);
			assert_int_equal(rules->rules[remaining]->filter.can_id, i + 1);
			remaining++;
		}
		assert_null(rules->rules[2]);
		assert_int_equal(malloc_calls, 0);
		assert_int_equal(cangw_release_rules(rules), 0);
	}
}

static void test_pop_rule_field_matching(void **state)
{
	socketcan_gw_rules_t *rules = test_calloc(1, sizeof(*rules));
	socketcan_gw_rule_t *entry = test_calloc(1, sizeof(*entry));
	socketcan_gw_rule_t match;
	(void) state;

	entry->src_ifindex = 1;
	entry->dst_ifindex = 2;
	entry->echo = 1;
	entry->invert_can_id = 1;
	entry->direction = SOCKETCAN_GW_DIRECTION_OUT;
	entry->filter.can_id = 0x123;
	entry->filter.can_mask = 0x7ff;
	rules->rules = test_calloc(1, sizeof(*rules->rules));
	rules->rules[0] = entry;
	rules->rule_num = rules->array_num = 1;
	for (size_t field = 0; field < 7; field++) {
		match = *entry;
		switch (field) {
		case 0: match.src_ifindex++; break;
		case 1: match.dst_ifindex++; break;
		case 2: match.echo = 0; break;
		case 3: match.invert_can_id = 0; break;
		case 4: match.direction = SOCKETCAN_GW_DIRECTION_IN; break;
		case 5: match.filter.can_id++; break;
		case 6: match.filter.can_mask++; break;
		}
		assert_int_equal(cangw_pop_gw_rule(rules, &match), -1);
		assert_int_equal(rules->rule_num, 1);
		assert_ptr_equal(rules->rules[0], entry);
	}
	match = *entry;
	assert_int_equal(cangw_pop_gw_rule(rules, &match), 0);
	assert_int_equal(rules->rule_num, 0);
	assert_int_equal(rules->array_num, 1);
	assert_null(rules->rules[0]);
	assert_int_equal(match.filter.can_id, 0x123);
	assert_int_equal(cangw_pop_gw_rule(rules, &match), -1);
	assert_int_equal(malloc_calls, 0);
	assert_int_equal(cangw_release_rules(rules), 0);
}

static void test_pop_rule_duplicate_values(void **state)
{
	socketcan_gw_rules_t *rules = test_calloc(1, sizeof(*rules));
	socketcan_gw_rule_t match = {
		.src_ifindex = 1, .dst_ifindex = 2, .filter = { .can_id = 0x123, .can_mask = 0x7ff }
	};
	socketcan_gw_rule_t *second;
	(void) state;

	rules->rules = test_calloc(3, sizeof(*rules->rules));
	rules->rule_num = rules->array_num = 3;
	/* Leave a NULL slot before two independently owned identical rules. */
	for (size_t i = 1; i < 3; i++) {
		rules->rules[i] = test_malloc(sizeof(match));
		*rules->rules[i] = match;
	}
	second = rules->rules[2];
	assert_int_equal(cangw_pop_gw_rule(rules, &match), 0);
	assert_int_equal(rules->rule_num, 2);
	assert_null(rules->rules[0]);
	assert_ptr_equal(rules->rules[1], second);
	assert_null(rules->rules[2]);
	assert_int_equal(cangw_pop_gw_rule(rules, &match), 0);
	assert_int_equal(rules->rule_num, 1);
	assert_null(rules->rules[0]);
	assert_null(rules->rules[1]);
	assert_int_equal(cangw_pop_gw_rule(rules, &match), -1);
	assert_int_equal(malloc_calls, 0);
	assert_int_equal(cangw_release_rules(rules), 0);
}

static void test_push_pop_rule_reuse(void **state)
{
	socketcan_gw_rules_t *rules = test_calloc(1, sizeof(*rules));
	socketcan_gw_rule_t entries[3] = { 0 };
	socketcan_gw_rule_t *registered[3];
	socketcan_gw_rule_t **array;
	socketcan_gw_rule_t match;
	(void) state;

	for (size_t i = 0; i < 3; i++) {
		entries[i].src_ifindex = 1;
		entries[i].dst_ifindex = 2;
		entries[i].filter.can_id = i + 1;
		entries[i].filter.can_mask = 0x7ff;
	}
	assert_int_equal(cangw_push_gw_rule(rules, &entries[0]), 0);
	assert_non_null(rules->rules);
	assert_int_equal(rules->rule_num, 1);
	assert_int_equal(rules->array_num, 2);
	assert_int_equal(malloc_calls, 2);
	array = rules->rules;
	assert_int_equal(cangw_push_gw_rule(rules, &entries[1]), 0);
	assert_ptr_equal(rules->rules, array);
	assert_int_equal(rules->rule_num, 2);
	assert_int_equal(malloc_calls, 3);
	expect_uint_value(rule_test_realloc, address, (uintptr_t) array);
	expect_uint_value(rule_test_realloc, size, 4 * sizeof(*array));
	will_return(rule_test_realloc, 0);
	assert_int_equal(cangw_push_gw_rule(rules, &entries[2]), 0);
	assert_int_equal(rules->rule_num, 3);
	assert_int_equal(rules->array_num, 4);
	assert_int_equal(malloc_calls, 4);
	for (size_t i = 0; i < 3; i++) {
		registered[i] = rules->rules[i];
		assert_ptr_not_equal(rules->rules[i], &entries[i]);
		assert_memory_equal(rules->rules[i], &entries[i], sizeof(entries[i]));
	}

	array = rules->rules;
	match = entries[1];
	assert_int_equal(cangw_pop_gw_rule(rules, &match), 0);
	assert_int_equal(rules->rule_num, 2);
	assert_ptr_equal(rules->rules[0], registered[0]);
	assert_ptr_equal(rules->rules[1], registered[2]);
	assert_null(rules->rules[2]);
	assert_int_equal(malloc_calls, 4);
	assert_int_equal(cangw_push_gw_rule(rules, &match), 0);
	assert_int_equal(rules->rule_num, 3);
	assert_ptr_not_equal(rules->rules[2], &match);
	assert_memory_equal(rules->rules[2], &match, sizeof(match));
	assert_ptr_equal(rules->rules, array);
	assert_int_equal(rules->array_num, 4);
	assert_int_equal(malloc_calls, 5);

	while (rules->rule_num != 0)
		assert_int_equal(cangw_pop_gw_rule(rules, rules->rules[0]), 0);
	for (size_t i = 0; i < 3; i++)
		assert_null(rules->rules[i]);
	assert_int_equal(cangw_pop_gw_rule(rules, &match), -1);
	assert_int_equal(malloc_calls, 5);
	assert_int_equal(cangw_push_gw_rule(rules, &match), 0);
	assert_int_equal(rules->rule_num, 1);
	assert_ptr_not_equal(rules->rules[0], &match);
	assert_memory_equal(rules->rules[0], &match, sizeof(match));
	assert_ptr_equal(rules->rules, array);
	assert_int_equal(rules->array_num, 4);
	assert_int_equal(malloc_calls, 6);
	assert_int_equal(cangw_release_rules(rules), 0);
}

static void test_cangw_push_gw_rule_copy(void **state)
{
	socketcan_gw_rules_t *rules = test_calloc(1, sizeof(*rules));
	socketcan_gw_rule_t *input = test_calloc(1, sizeof(*input));
	socketcan_gw_rule_t expected;
	(void) state;

	input->src_ifindex = 1;
	input->dst_ifindex = 2;
	input->echo = 1;
	input->invert_can_id = 1;
	input->direction = SOCKETCAN_GW_DIRECTION_OUT;
	input->filter.can_id = 0x123;
	input->filter.can_mask = 0x7ff;
	expected = *input;

	assert_int_equal(cangw_push_gw_rule(rules, input), 0);
	assert_ptr_not_equal(rules->rules[0], input);

	input->filter.can_id = 0x456;
	assert_memory_equal(rules->rules[0], &expected, sizeof(expected));

	test_free(input);
	assert_memory_equal(rules->rules[0], &expected, sizeof(expected));
	assert_int_equal(cangw_release_rules(rules), 0);
}

static void test_rule_echo_and_filter(void **state)
{
	static const struct {
		unsigned int echo, invert;
		canid_t id, mask;
	} cases[] = {
		{ 0, 0, 0, 0 },
		{ 1, 0, 0x123, 0x7ff },
		{ 1, 1, 0x456, 0x7f0 },
		{ 2, 0, 0, 0 }
	};
	int (*const operations[])(socketcan_gw_rule_t *) = {
		cangw_add_rule, cangw_delete_rule
	};
	const size_t length = NLMSG_LENGTH(sizeof(struct rtcanmsg)) +
		2 * RTA_ALIGN(RTA_LENGTH(sizeof(unsigned int))) +
		RTA_ALIGN(RTA_LENGTH(sizeof(struct can_filter)));
	struct {
		struct nlmsghdr header;
		struct nlmsgerr ack;
	} reply = { 0 };
	(void) state;

	reply.header.nlmsg_type = NLMSG_ERROR;
	reply.header.nlmsg_len = NLMSG_LENGTH(sizeof(reply.ack));
	recv_data = (const unsigned char *) &reply;
	recv_length = reply.header.nlmsg_len;
	for (size_t operation = 0; operation < 2; operation++) {
		for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
			socketcan_gw_rule_t rule = {
				.src_ifindex = 11, .dst_ifindex = 22,
				.echo = cases[i].echo, .invert_can_id = cases[i].invert,
				.filter = { .can_id = cases[i].id, .can_mask = cases[i].mask }
			};
			const socketcan_gw_rule_t original = rule;
			struct s_request_data sent = { 0 }, reset;
			struct rtattr *attr;
			unsigned int filters = 0;
			int remaining;

			/* OFF must also clear an echo flag already set in the request. */
			init_req_data(&reset, NLM_F_REQUEST | NLM_F_ACK, RTM_NEWROUTE);
			reset.rtcan.flags |= CGW_FLAGS_CAN_ECHO;
			operate_rule_options(&reset, &rule);
			assert_int_equal(reset.rtcan.flags & CGW_FLAGS_CAN_ECHO,
				rule.echo == 1 ? CGW_FLAGS_CAN_ECHO : 0);

			send_capture = (unsigned char *) &sent;
			send_capture_capacity = sizeof(sent);
			expect_rule_success(length);
			assert_int_equal(operations[operation](&rule), 0);
			assert_int_equal(sent.nh.nlmsg_type,
				operation == 0 ? RTM_NEWROUTE : RTM_DELROUTE);
			assert_int_equal(sent.rtcan.flags & CGW_FLAGS_CAN_ECHO,
				rule.echo == 1 ? CGW_FLAGS_CAN_ECHO : 0);
			remaining = RTCAN_PAYLOAD(&sent.nh);
			for (attr = RTCAN_RTA(&sent.rtcan); RTA_OK(attr, remaining);
				attr = RTA_NEXT(attr, remaining)) {
				if (attr->rta_type == CGW_FILTER) {
					struct can_filter filter;
					assert_int_equal(RTA_PAYLOAD(attr), sizeof(filter));
					memcpy(&filter, RTA_DATA(attr), sizeof(filter));
					assert_int_equal(filter.can_id,
						cases[i].id | (cases[i].invert ? CAN_INV_FILTER : 0));
					assert_int_equal(filter.can_mask, cases[i].mask);
					filters++;
				}
			}
			assert_int_equal(filters, 1);
			assert_memory_equal(&rule, &original, sizeof(rule));
		}
	}
	assert_int_equal(malloc_calls, 0);
}

#define CASE(function) cmocka_unit_test_setup(function, setup)

int main(void)
{
	const struct CMUnitTest tests[] = {
		CASE(test_cangw_push_gw_rule_realloc_failure),
		CASE(test_parse_listing_data_realloc_failure),
		CASE(test_addattr_l_failure),
		CASE(test_add_rule_zero_src_ifindex),
		CASE(test_add_rule_zero_dst_ifindex),
		CASE(test_add_rule_directions),
		CASE(test_delete_rule_directions),
		CASE(test_rule_echo_and_filter),
		CASE(test_delete_rule_zero_src_ifindex),
		CASE(test_delete_rule_zero_dst_ifindex),
		CASE(test_cangw_push_gw_rule_malloc_failure),
		CASE(test_cangw_push_gw_rule_copy),
		CASE(test_parse_listing_data_malloc_failures),
		CASE(test_get_rules_malloc_failures),
		CASE(test_set_request_socket_failure),
		CASE(test_get_rules_socket_failure),
		CASE(test_set_request_sendto_failure),
		CASE(test_set_request_recv_failure),
		CASE(test_set_request_unexpected_response_type),
		CASE(test_set_request_kernel_error),
		CASE(test_get_rules_sendto_failure),
		CASE(test_get_rules_recv_failure),
		CASE(test_get_rules_recv_failure_after_rules),
		CASE(test_release_rules_null),
		CASE(test_pop_rule_arguments_and_missing),
		CASE(test_pop_rule_positions),
		CASE(test_pop_rule_field_matching),
		CASE(test_pop_rule_duplicate_values),
		CASE(test_push_pop_rule_reuse),
		CASE(test_bulk_rule_arguments),
		CASE(test_bulk_rule_success),
		CASE(test_bulk_rule_operation_failure),
		CASE(test_bulk_rule_invalid_entry_after_success),
	};
	return cmocka_run_group_tests(tests, NULL, NULL);
}
