/* SPDX-License-Identifier: BSD-3-Clause */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define LIBSOCKETCANGW_CONFIG_CHECKER

/* Reuse the file reader, version-1 parser and duplicate-key checker. */
#include "libsocketcangw_config.c"

static void usage(FILE *stream, const char *program)
{
	fprintf(stream, "Usage: %s FILE\n", program);
}

static void report_json_error(const char *file, const char *text, const char *end)
{
	unsigned long line = 1, column = 1;

	if (end == NULL) {
		cangw_config_log("cangw: Failed to parse JSON in '%s'.\n", file);
		return;
	}
	for (const char *cursor = text; cursor < end && *cursor != '\0'; cursor++) {
		if (*cursor == '\n') {
			line++;
			column = 1;
		} else {
			column++;
		}
	}
	cangw_config_log("cangw: Invalid JSON in '%s' at line %lu, column %lu.\n", file, line, column);
}

static int check_file(const char *file)
{
	char *text = NULL;
	const char *end = NULL;
	cJSON *json = NULL;
	const cJSON *version, *filters;
	socketcan_gw_rule_t **rules = NULL;
	size_t count = 0;
	int result = EXIT_FAILURE;

	text = cangw_config_read_jsonstring(file);
	if (text == NULL)
		goto done;

	json = cJSON_ParseWithOpts(text, &end, 1);
	if (json == NULL) {
		report_json_error(file, text, end);
		goto done;
	}
	if (json_check_duplicate_keys(json, stderr) != 0)
		goto done;
	if (!cJSON_IsObject(json)) {
		cangw_config_log("cangw: The configuration root must be an object.\n");
		goto done;
	}

	/* Keep the same version dispatch as the runtime loader. */
	version = cJSON_GetObjectItemCaseSensitive(json, "version");
	if (!cJSON_IsNumber(version) || version->valueint != 1) {
		cangw_config_log("cangw: Invalid or unsupported format version.\n");
		goto done;
	}
	filters = cJSON_GetObjectItemCaseSensitive(json, "filters");
	if (filters == NULL) {
		cangw_config_log("cangw: The 'filters' array is missing in format version 1.\n");
		goto done;
	}
	if (cangw_parse_filters_v1(&rules, &count, filters) != 0)
		goto done;

	printf("cangw: Configuration '%s' is valid (%zu filters).\n", file, count);
	result = EXIT_SUCCESS;

done:
	for (size_t i = 0; i < count; i++)
		free(rules[i]);
	free(rules);
	cJSON_Delete(json);
	cangw_config_release_jsonstring(text);
	if (result != EXIT_SUCCESS)
		cangw_config_log("cangw: Configuration '%s' could not be validated.\n", file);
	return result;
}

int main(int argc, char **argv)
{
	if (argc == 2 && (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0)) {
		usage(stdout, argv[0]);
		return EXIT_SUCCESS;
	}
	if (argc != 2) {
		usage(stderr, argv[0]);
		return 2;
	}

	cangw_config_log_enable();
	return check_file(argv[1]);
}
