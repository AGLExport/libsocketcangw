/* SPDX-License-Identifier: BSD-3-Clause */
/* White-box cmocka tests; no CAN devices or root privileges required. */
#include <cjson/cJSON.h>
#include <errno.h>
#include <fcntl.h>
#include <libsocketcangw.h>
#include <libsocketcangw_config.h>
#include <limits.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

/* cmocka requires stdarg.h, stddef.h and setjmp.h before its header. */
#include <cmocka.h>

static int config_test_open(const char *path, int flags, ...);
static int config_test_fstat(int fd, struct stat *status);
static ssize_t config_test_read(int fd, void *buffer, size_t size);
static int config_test_close(int fd);
static void *config_test_malloc(size_t size);
static void config_test_free(void *pointer);
static int config_test_vfprintf(FILE *stream, const char *format, va_list args);
static int config_test_fprintf(FILE *stream, const char *format, ...);

/* Include once to exercise static functions without changing the library.
 * Preinclude headers so substitutions affect only the implementation. */
#define LIBSOCKETCANGW_CONFIG_CHECKER 1
#define open config_test_open
#define fstat config_test_fstat
#define read config_test_read
#define close config_test_close
#define malloc config_test_malloc
#define free config_test_free
#define vfprintf config_test_vfprintf
#define fprintf config_test_fprintf
#include "../src/libsocketcangw_config.c"
#undef open
#undef fstat
#undef read
#undef close
#undef malloc
#undef free
#undef vfprintf
#undef fprintf

#define VALID_FILTER "{\"can_id\":\"0x123\",\"can_mask\":\"0x7ff\"}"
#define VALID_JSON "{\"version\":1,\"filters\":[" VALID_FILTER "]}"
#define TWO_FILTERS "{\"version\":1,\"filters\":[" VALID_FILTER "," VALID_FILTER "]}"
#define TEST_FD 42

static struct {
	const char *data;
	size_t length, offset, chunk;
	off_t stat_size;
	mode_t mode;
	int open_error, stat_error, opened;
	unsigned int interrupts, read_error_at;
	unsigned int opens, stats, reads, closes;
} input;

static size_t allocation_calls, allocation_fail_at;
static size_t json_allocation_calls, json_allocation_fail_at;
static size_t live_blocks;
static char log_text[32768];
static size_t log_size, log_calls;

static void set_input(const char *data)
{
	memset(&input, 0, sizeof(input));
	input.data = data;
	input.length = strlen(data);
	input.stat_size = (off_t) input.length;
	input.mode = S_IFREG | 0600;
	input.chunk = (size_t) -1;
}

static int config_test_open(const char *path, int flags, ...)
{
	assert_string_equal(path, "config.json");
	assert_int_equal(flags, O_RDONLY);
	assert_false(input.opened);
	input.opens++;
	if (input.open_error) {
		errno = input.open_error;
		return -1;
	}
	input.opened = 1;
	return TEST_FD;
}

static int config_test_fstat(int fd, struct stat *status)
{
	assert_int_equal(fd, TEST_FD);
	assert_true(input.opened);
	input.stats++;
	if (input.stat_error) {
		errno = input.stat_error;
		return -1;
	}
	memset(status, 0, sizeof(*status));
	status->st_mode = input.mode;
	status->st_size = input.stat_size;
	return 0;
}

static ssize_t config_test_read(int fd, void *buffer, size_t size)
{
	size_t count;
	assert_int_equal(fd, TEST_FD);
	assert_true(input.opened);
	assert_non_null(buffer);
	input.reads++;
	if (input.interrupts) {
		input.interrupts--;
		errno = EINTR;
		return -1;
	}
	if (input.reads == input.read_error_at) {
		errno = EIO;
		return -1;
	}
	count = input.length - input.offset;
	if (count > size)
		count = size;
	if (count > input.chunk)
		count = input.chunk;
	memcpy(buffer, input.data + input.offset, count);
	input.offset += count;
	return (ssize_t) count;
}

static int config_test_close(int fd)
{
	assert_int_equal(fd, TEST_FD);
	assert_true(input.opened);
	input.opened = 0;
	input.closes++;
	return 0;
}

static void *tracked_malloc(size_t size)
{
	void *pointer = test_malloc(size);
	assert_non_null(pointer);
	live_blocks++;
	return pointer;
}

static void config_test_free(void *pointer)
{
	if (pointer != NULL) {
		assert_true(live_blocks > 0);
		test_free(pointer);
		live_blocks--;
	}
}

static void *config_test_malloc(size_t size)
{
	allocation_calls++;
	if (allocation_calls == allocation_fail_at)
		return NULL;
	return tracked_malloc(size);
}

static void *json_test_malloc(size_t size)
{
	json_allocation_calls++;
	if (json_allocation_calls == json_allocation_fail_at)
		return NULL;
	return tracked_malloc(size);
}

static int config_test_vfprintf(FILE *stream, const char *format, va_list args)
{
	int count;
	assert_ptr_equal(stream, stderr);
	count = vsnprintf(log_text + log_size, sizeof(log_text) - log_size, format, args);
	assert_true(count >= 0);
	assert_true((size_t) count < sizeof(log_text) - log_size);
	log_size += (size_t) count;
	log_calls++;
	return count;
}

static int config_test_fprintf(FILE *stream, const char *format, ...)
{
	int count;
	va_list args;
	va_start(args, format);
	count = config_test_vfprintf(stream, format, args);
	va_end(args);
	return count;
}

static int setup(void **state)
{
	cJSON_Hooks hooks = { json_test_malloc, config_test_free };
	(void) state;
	assert_int_equal(live_blocks, 0);
	set_input(VALID_JSON);
	allocation_calls = allocation_fail_at = 0;
	json_allocation_calls = json_allocation_fail_at = 0;
	log_size = log_calls = 0;
	log_text[0] = '\0';
	g_log_output_enable = 0;
	cJSON_InitHooks(&hooks);
	return 0;
}

static int teardown(void **state)
{
	(void) state;
	assert_false(input.opened);
	assert_int_equal(live_blocks, 0);
	cJSON_InitHooks(NULL);
	g_log_output_enable = 0;
	return 0;
}

static cJSON *parse(const char *text)
{
	cJSON *node = cJSON_Parse(text);
	assert_non_null(node);
	return node;
}

static void assert_rule(const socketcan_gw_rule_t *rule, uint32_t id, uint32_t mask, unsigned int echo)
{
	assert_non_null(rule);
	assert_int_equal(rule->src_ifindex, 0);
	assert_int_equal(rule->dst_ifindex, 0);
	assert_int_equal(rule->filter.can_id, id);
	assert_int_equal(rule->filter.can_mask, mask);
	assert_int_equal(rule->echo, echo);
	assert_int_equal(rule->options, SOCKETCAN_GW_RULE_FILTER | (echo ? SOCKETCAN_GW_RULE_ECHO : 0));
}

static void expect_load_error(int code, const char *message)
{
	socketcan_gw_rules_t marker;
	socketcan_gw_rules_t *rules = &marker;
	size_t before = live_blocks;
	log_size = log_calls = 0;
	log_text[0] = '\0';
	assert_int_equal(cangw_config_load_from_file(&rules, "config.json"), code);
	assert_ptr_equal(rules, &marker); /* Do not publish partial results. */
	assert_int_equal(live_blocks, before);
	assert_false(input.opened);
	assert_non_null(strstr(log_text, "Failed to load gateway configuration"));
	if (message != NULL)
		assert_non_null(strstr(log_text, message));
}

static void test_logging(void **state)
{
	(void) state;
	cangw_config_log("hidden %d\n", 7);
	assert_int_equal(log_calls, 0);
	assert_int_equal(cangw_config_log_enable(), 0);
	assert_int_equal(cangw_config_log_enable(), 1);
	cangw_config_log("cangw: %s %d %08x\n", "message", -12, 0xabcU);
	assert_int_equal(log_calls, 1);
	assert_string_equal(log_text, "cangw: message -12 00000abc\n");
}

static void test_load_arguments(void **state)
{
	socketcan_gw_rules_t *rules = NULL;
	(void) state;
	cangw_config_log_enable();
	assert_int_equal(cangw_config_load_from_file(NULL, "config.json"), -1);
	assert_int_equal(cangw_config_load_from_file(&rules, NULL), -1);
	assert_int_equal(cangw_config_load_from_file(NULL, NULL), -1);
	assert_null(rules);
	assert_int_equal(input.opens, 0);
	assert_int_equal(log_calls, 3);
}

static void test_file_open_error(void **state)
{
	(void) state;
	cangw_config_log_enable();
	input.open_error = ENOENT;
	expect_load_error(-2, "Failed to open file");
	assert_int_equal(input.stats, 0);
	assert_int_equal(input.closes, 0);
}

static void test_file_stat_error(void **state)
{
	(void) state;
	cangw_config_log_enable();
	input.stat_error = EIO;
	expect_load_error(-2, "Failed to get file status");
	assert_int_equal(input.reads, 0);
	assert_int_equal(input.closes, 1);
}

static void test_file_type_and_size(void **state)
{
	(void) state;
	cangw_config_log_enable();
	input.mode = S_IFDIR | 0700;
	expect_load_error(-2, "not a regular file");
	assert_int_equal(input.reads, 0);
	assert_int_equal(input.closes, 1);
	set_input(VALID_JSON);
	input.stat_size = (off_t) g_cangw_config_max_bytes + 1;
	expect_load_error(-2, "File size exceeds limit");
	assert_int_equal(input.reads, 0);
	assert_int_equal(input.closes, 1);
}

static void test_file_read_error(void **state)
{
	(void) state;
	cangw_config_log_enable();
	input.read_error_at = 1;
	expect_load_error(-2, NULL); /* Common log suffices for read failures. */
	assert_int_equal(log_calls, 1);
	assert_int_equal(input.closes, 1);
	set_input(VALID_JSON);
	input.chunk = 4;
	input.read_error_at = 2;
	expect_load_error(-2, NULL);
	assert_int_equal(input.offset, 4);
	assert_int_equal(input.closes, 1);
}

static void test_file_short_reads_and_eintr(void **state)
{
	char *text;
	(void) state;
	input.chunk = 3;
	input.interrupts = 2;
	text = cangw_config_read_jsonstring("config.json");
	assert_non_null(text);
	assert_string_equal(text, VALID_JSON);
	assert_int_equal(input.reads, (strlen(VALID_JSON) + 2) / 3 + 3);
	assert_int_equal(input.closes, 1);
	cangw_config_release_jsonstring(text);
	cangw_config_release_jsonstring(NULL);
}

static void test_file_size_boundary(void **state)
{
	socketcan_gw_rules_t *rules = NULL;
	char *data = test_malloc(g_cangw_config_max_bytes + 1);
	(void) state;
	memset(data, ' ', g_cangw_config_max_bytes);
	memcpy(data, VALID_JSON, strlen(VALID_JSON));
	data[g_cangw_config_max_bytes] = '\0';
	set_input(data);
	assert_int_equal(cangw_config_load_from_file(&rules, "config.json"), 0);
	assert_int_equal(rules->rule_num, 1);
	assert_int_equal(input.offset, g_cangw_config_max_bytes);
	cangw_config_release_config(rules);
	test_free(data);
}

static void test_json_and_version_errors(void **state)
{
	static const struct {
		const char *json;
		int code;
		const char *message;
	} cases[] = { { "", -2, "Failed to parse JSON" },
		      { "{", -2, "Failed to parse JSON" },
		      { VALID_JSON "junk", -2, "Failed to parse JSON" },
		      { "null", -4, "Unsupported format version" },
		      { "[]", -4, "Unsupported format version" },
		      { "{}", -4, "Unsupported format version" },
		      { "{\"version\":null}", -4, "Unsupported format version" },
		      { "{\"version\":\"1\"}", -4, "Unsupported format version" },
		      { "{\"version\":0}", -4, "Unsupported format version" },
		      { "{\"version\":2}", -4, "Unsupported format version" },
		      { "{\"version\":1}", -2, "'filters' array is missing" },
		      { "{\"version\":1,\"filters\":null}", -2, "does not array" },
		      { "{\"version\":1,\"filters\":{}}", -2, "does not array" },
		      { "{\"version\":1,\"filters\":[]}", -2, "Invalid array size" },
		      { "{\"version\":1,\"filters\":[null]}", -2, "Invalid filter format" },
		      { "{\"version\":1,\"filters\":[" VALID_FILTER ",false]}", -2, "Invalid filter format" },
		      { "{\"version\":1,\"filters\":[" VALID_FILTER ",{}]}", -2, "No can_id" } };
	(void) state;
	cangw_config_log_enable();
	for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
		set_input(cases[i].json);
		expect_load_error(cases[i].code, cases[i].message);
		assert_int_equal(input.closes, 1);
	}
}

static void test_load_success(void **state)
{
	socketcan_gw_rules_t *rules = NULL;
	(void) state;
	set_input("{\"version\":1,\"filters\":[" VALID_FILTER
		  ","
		  "{\"can_id\":\"0X1fffffff\",\"can_mask\":\"0x1fffffff\",\"invert\":true,\"echo\":true}]}");
	assert_int_equal(cangw_config_load_from_file(&rules, "config.json"), 0);
	assert_non_null(rules);
	assert_int_equal(rules->rule_num, 2);
	assert_int_equal(rules->array_num, 2);
	assert_rule(rules->rules[0], 0x123, 0x7ff, 0);
	assert_rule(rules->rules[1], 0x1fffffff | CAN_INV_FILTER, 0x1fffffff, 1);
	assert_int_equal(input.closes, 1);
	assert_int_equal(log_calls, 0);
	cangw_config_release_config(rules);
}

static void test_preexisting_version_and_duplicate_policy(void **state)
{
	socketcan_gw_rules_t *rules = NULL;
	(void) state;
	/* Dispatch uses valueint; duplicate members use the first occurrence. */
	set_input(
		"{\"version\":1.9,\"version\":2,\"filters\":["
		"{\"can_id\":\"0x123\",\"can_id\":\"invalid\",\"can_mask\":\"0x7ff\"}]}");
	assert_int_equal(cangw_config_load_from_file(&rules, "config.json"), 0);
	assert_rule(rules->rules[0], 0x123, 0x7ff, 0);
	cangw_config_release_config(rules);
}

static void test_hex_values(void **state)
{
	static const struct {
		const char *text;
		int result;
		uint32_t value;
	} cases[] = { { "", -1, 0 },
		      { "0x", -1, 0 },
		      { "123", -1, 0 },
		      { "1x0", -1, 0 },
		      { "0z0", -1, 0 },
		      { "0xG", -1, 0 },
		      { "0x12G", -1, 0 },
		      { "0x1 ", -1, 0 },
		      { "0xffffffffffffffffffffffffffffffff", -1, 0 },
		      { "0x100000000", -1, 0 },
		      { "0x0", 0, 0 },
		      { "0XABC", 0, 0xabc },
		      { "0xffffffff", 0, UINT32_MAX } };
	uint32_t value = 0x55;
	(void) state;
	assert_int_equal(get_hex_value_u32(NULL, "0x0"), -1);
	assert_int_equal(get_hex_value_u32(&value, NULL), -1);
	assert_int_equal(value, 0x55);
	for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
		value = 0x55;
		assert_int_equal(get_hex_value_u32(&value, cases[i].text), cases[i].result);
		assert_int_equal(value, cases[i].result == 0 ? cases[i].value : 0x55);
	}
}

static void test_filter_arguments(void **state)
{
	cJSON *filter = parse(VALID_FILTER);
	socketcan_gw_rule_t *rule = NULL;
	(void) state;
	cangw_config_log_enable();
	assert_int_equal(cangw_parse_filter(NULL, filter), -1);
	assert_int_equal(cangw_parse_filter(&rule, NULL), -1);
	assert_null(rule);
	assert_int_equal(log_calls, 0); /* Static argument errors have no log. */
	cJSON_Delete(filter);
}

static void test_filter_errors(void **state)
{
	static const struct {
		const char *json;
		int code;
		const char *message;
	} cases[] = { { "{}", -1, "No can_id" },
		      { "{\"can_id\":123}", -1, "No can_id" },
		      { "{\"can_id\":null}", -1, "No can_id" },
		      { "{\"can_id\":\"bad\"}", -2, "Invalid can_id" },
		      { "{\"can_id\":\"0x20000000\"}", -2, "Invalid can_id" },
		      { "{\"can_id\":\"0x40000000\"}", -2, "Invalid can_id" },
		      { "{\"can_id\":\"0x80000000\"}", -2, "Invalid can_id" },
		      { "{\"can_id\":\"0x0\"}", -1, "No can_mask" },
		      { "{\"can_id\":\"0x0\",\"can_mask\":false}", -1, "No can_mask" },
		      { "{\"can_id\":\"0x0\",\"can_mask\":null}", -1, "No can_mask" },
		      { "{\"can_id\":\"0x0\",\"can_mask\":\"bad\"}", -2, "Invalid can_mask" },
		      { "{\"can_id\":\"0x0\",\"can_mask\":\"0x20000000\"}", -2, "Invalid can_mask" },
		      { "{\"can_id\":\"0x0\",\"can_mask\":\"0x40000000\"}", -2, "Invalid can_mask" },
		      { "{\"can_id\":\"0x0\",\"can_mask\":\"0x80000000\"}", -2, "Invalid can_mask" } };
	(void) state;
	cangw_config_log_enable();
	for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
		cJSON *filter = parse(cases[i].json);
		socketcan_gw_rule_t *rule = NULL;
		log_size = 0;
		log_text[0] = '\0';
		assert_int_equal(cangw_parse_filter(&rule, filter), cases[i].code);
		assert_null(rule);
		assert_non_null(strstr(log_text, cases[i].message));
		cJSON_Delete(filter);
	}
	/* Exercise string nodes with NULL valuestring in a constructed tree. */
	for (int mask = 0; mask < 2; mask++) {
		cJSON *filter = parse(VALID_FILTER);
		cJSON *item = cJSON_GetObjectItemCaseSensitive(filter, mask ? "can_mask" : "can_id");
		socketcan_gw_rule_t *rule = NULL;
		config_test_free(item->valuestring);
		item->valuestring = NULL;
		assert_int_equal(cangw_parse_filter(&rule, filter), -1);
		assert_null(rule);
		cJSON_Delete(filter);
	}
}

static void test_filter_optional_values(void **state)
{
	static const char *const values[] = { NULL, "null", "false", "true", "0", "\"true\"", "{}", "[]" };
	(void) state;
	for (size_t i = 0; i < sizeof(values) / sizeof(values[0]); i++) {
		char text[256];
		cJSON *filter;
		socketcan_gw_rule_t *rule = NULL;
		if (values[i] == NULL)
			snprintf(text, sizeof(text), "%s", VALID_FILTER);
		else
			snprintf(
				text,
				sizeof(text),
				"{\"can_id\":\"0x0\",\"can_mask\":\"0x0\",\"invert\":%s,\"echo\":%s}",
				values[i],
				values[i]);
		filter = parse(text);
		assert_int_equal(cangw_parse_filter(&rule, filter), 0);
		if (i == 0)
			assert_rule(rule, 0x123, 0x7ff, 0);
		else
			assert_rule(rule, i == 3 ? CAN_INV_FILTER : 0, 0, i == 3);
		config_test_free(rule);
		cJSON_Delete(filter);
	}
	/* Verify echo and inversion independently, not just equal values. */
	for (int invert = 0; invert < 2; invert++) {
		char text[256];
		cJSON *filter;
		socketcan_gw_rule_t *rule = NULL;
		snprintf(
			text,
			sizeof(text),
			"{\"can_id\":\"0x123\",\"can_mask\":\"0x7ff\",\"invert\":%s,\"echo\":%s}",
			invert ? "true" : "false",
			invert ? "false" : "true");
		filter = parse(text);
		assert_int_equal(cangw_parse_filter(&rule, filter), 0);
		assert_rule(rule, 0x123 | (invert ? CAN_INV_FILTER : 0), 0x7ff, !invert);
		config_test_free(rule);
		cJSON_Delete(filter);
	}
}

static void test_filters_arguments(void **state)
{
	cJSON *filters = parse("[" VALID_FILTER "]");
	socketcan_gw_rule_t **rules = NULL;
	size_t count = 99;
	(void) state;
	cangw_config_log_enable();
	assert_int_equal(cangw_parse_filters_v1(NULL, &count, filters), -1);
	assert_int_equal(cangw_parse_filters_v1(&rules, NULL, filters), -1);
	assert_int_equal(cangw_parse_filters_v1(&rules, &count, NULL), -1);
	assert_null(rules);
	assert_int_equal(count, 99);
	assert_int_equal(log_calls, 0);
	cJSON_Delete(filters);
}

static void test_filter_count_boundaries(void **state)
{
	const size_t capacity = (size_t) (g_cangw_filter_max + 1) * (sizeof(VALID_FILTER) + 1) + 64;
	char *text = test_malloc(capacity);
	(void) state;
	cangw_config_log_enable();
	for (int count = g_cangw_filter_max; count <= g_cangw_filter_max + 1; count++) {
		size_t used = (size_t) snprintf(text, capacity, "{\"version\":1,\"filters\":[");
		socketcan_gw_rules_t *rules = NULL;
		for (int i = 0; i < count; i++)
			used += (size_t) snprintf(
				text + used, capacity - used, "%s%s", i ? "," : "", VALID_FILTER);
		snprintf(text + used, capacity - used, "]}");
		set_input(text);
		if (count == g_cangw_filter_max) {
			assert_int_equal(cangw_config_load_from_file(&rules, "config.json"), 0);
			assert_int_equal(rules->rule_num, count);
			for (int i = 0; i < count; i++)
				assert_rule(rules->rules[i], 0x123, 0x7ff, 0);
			cangw_config_release_config(rules);
		} else {
			expect_load_error(-2, "Array size");
		}
	}
	test_free(text);
}

static void test_parser_allocation_failures(void **state)
{
	/* Buffer, container, array, first rule, second rule. The last failure
	 * must also release the successfully allocated first rule. */
	(void) state;
	cangw_config_log_enable();
	for (size_t fail_at = 1; fail_at <= 5; fail_at++) {
		set_input(TWO_FILTERS);
		allocation_calls = 0;
		allocation_fail_at = fail_at;
		expect_load_error(fail_at == 1 ? -2 : -3, "Failed to allocate memory");
		assert_int_equal(allocation_calls, fail_at);
		assert_int_equal(input.closes, 1);
	}
	allocation_fail_at = 0;
}

static void test_json_allocation_failures(void **state)
{
	socketcan_gw_rules_t *rules = NULL;
	size_t allocations;
	(void) state;
	cangw_config_log_enable();
	set_input(TWO_FILTERS);
	assert_int_equal(cangw_config_load_from_file(&rules, "config.json"), 0);
	allocations = json_allocation_calls;
	assert_true(allocations > 0);
	cangw_config_release_config(rules);
	for (size_t fail_at = 1; fail_at <= allocations; fail_at++) {
		set_input(TWO_FILTERS);
		json_allocation_calls = 0;
		json_allocation_fail_at = fail_at;
		expect_load_error(-2, "Failed to parse JSON");
		assert_true(json_allocation_calls >= fail_at);
	}
	json_allocation_fail_at = 0;
}

static void test_release_partial_container(void **state)
{
	socketcan_gw_rules_t *rules = config_test_malloc(sizeof(*rules));
	(void) state;
	memset(rules, 0, sizeof(*rules));
	rules->rule_num = rules->array_num = 2;
	rules->rules = config_test_malloc(2 * sizeof(*rules->rules));
	rules->rules[0] = NULL;
	rules->rules[1] = config_test_malloc(sizeof(*rules->rules[1]));
	cangw_config_release_config(rules);
	cangw_config_release_config(NULL);
	rules = config_test_malloc(sizeof(*rules));
	memset(rules, 0, sizeof(*rules));
	cangw_config_release_config(rules);
}

static void test_duplicate_checker(void **state)
{
	cJSON *json = parse(
		"{\"a\":1,\"a\":2,\"a\":3,\"A\":4,\"nested\":["
		"{\"b\":1,\"b\":2},{\"b\":3}],\"other\":{\"a\":5}}");
	(void) state;
	assert_int_equal(json_check_duplicate_keys(NULL, stderr), 0);
	assert_int_equal(json_check_duplicate_keys(json, NULL), 3);
	assert_int_equal(log_calls, 0);
	assert_int_equal(json_check_duplicate_keys(json, stderr), 3);
	assert_int_equal(log_calls, 3);
	assert_string_equal(
		log_text, "duplicate JSON key: \"a\"\nduplicate JSON key: \"a\"\nduplicate JSON key: \"b\"\n");
	cJSON_Delete(json);
	json = parse("[{\"x\":1},{\"x\":2},[],{}]");
	assert_int_equal(json_check_duplicate_keys(json, NULL), 0);
	cJSON_Delete(json);
}

#define CASE(function) cmocka_unit_test_setup_teardown(function, setup, teardown)

int main(void)
{
	const struct CMUnitTest tests[] = {
		CASE(test_logging),
		CASE(test_load_arguments),
		CASE(test_file_open_error),
		CASE(test_file_stat_error),
		CASE(test_file_type_and_size),
		CASE(test_file_read_error),
		CASE(test_file_short_reads_and_eintr),
		CASE(test_file_size_boundary),
		CASE(test_json_and_version_errors),
		CASE(test_load_success),
		CASE(test_preexisting_version_and_duplicate_policy),
		CASE(test_hex_values),
		CASE(test_filter_arguments),
		CASE(test_filter_errors),
		CASE(test_filter_optional_values),
		CASE(test_filters_arguments),
		CASE(test_filter_count_boundaries),
		CASE(test_parser_allocation_failures),
		CASE(test_json_allocation_failures),
		CASE(test_release_partial_container),
		CASE(test_duplicate_checker),
	};
	return cmocka_run_group_tests(tests, NULL, NULL);
}
