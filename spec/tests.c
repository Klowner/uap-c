#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <yaml.h>

#include "uap/uap.h"

#define MAKE_FOURCC(a,b,c,d) ((a)|((b)<<8)|((c)<<16)|((d)<<24))

#define NUM_KEY_PATTERNS 5


static int get_field_index_for_ua_test(const char *str) {
	switch (MAKE_FOURCC(str[0], str[1], str[2], str[3])) {
		case MAKE_FOURCC('f','a','m','i'): return 1;
		case MAKE_FOURCC('m','a','j','o'): return 2;
		case MAKE_FOURCC('m','i','n','o'): return 3;
		case MAKE_FOURCC('p','a','t','c'):
			return strlen(str) < 7 ? 4 : 5;
		default: return -1;
	}
}

static int get_field_index_for_os_test(const char *str) {
	switch (MAKE_FOURCC(str[0], str[1], str[2], str[3])) {
		case MAKE_FOURCC('f','a','m','i'): return 1;
		case MAKE_FOURCC('m','a','j','o'): return 2;
		case MAKE_FOURCC('m','i','n','o'): return 3;
		case MAKE_FOURCC('p','a','t','c'):
			return strlen(str) < 7 ? 4 : 5;
		default: return -1;
	}
}

static int get_field_index_for_devices_test(const char *str) {
	switch (MAKE_FOURCC(str[0], str[1], str[2], str[3])) {
		case MAKE_FOURCC('f','a','m','i'): return 1;
		case MAKE_FOURCC('b','r','a','n'): return 2;
		case MAKE_FOURCC('m','o','d','e'): return 3;
		default:
			return -1;
	}
}

static void run_test_file(
		const char *filepath,
		const int field_offset,
		struct uap_parser *ua_parser,
		int (*get_field_idx)(const char*)
		)
{
	yaml_parser_t yaml_parser;

	if (!yaml_parser_initialize(&yaml_parser)) {
		puts("failed to initialize yaml");
		return;
	}

	FILE *fd = fopen(filepath, "rb");
	yaml_parser_set_input_file(&yaml_parser, fd);

	printf("Running test cases: \"%s\" ...  ", filepath);
	const char progress[] = "-\\|/-\\|/";

	fflush(stdout);

	struct {
		enum {
			UNKNOWN_SCALAR_TYPE,
			KEY_SCALAR_TYPE,
			VALUE_SCALAR_TYPE,
		} scalar_type;

		struct {
			char *value[6];
			int value_index;
		} item;

	} state = {
		.scalar_type = UNKNOWN_SCALAR_TYPE,
		.item.value[0] = NULL,
		.item.value[1] = NULL,
		.item.value[2] = NULL,
		.item.value[3] = NULL,
		.item.value[4] = NULL,
		.item.value[5] = NULL,
		.item.value_index = -2,
	};

	yaml_token_t token;
	memset(&token, 0, sizeof(yaml_token_t));

	struct uap_useragent_info *ua_info = uap_useragent_info_create();

	int num_passed = 0;
	int num_failed = 0;
	do {
		yaml_token_delete(&token);
		yaml_parser_scan(&yaml_parser, &token);

		switch (token.type) {
			case YAML_KEY_TOKEN: state.scalar_type = KEY_SCALAR_TYPE; break;
			case YAML_VALUE_TOKEN: state.scalar_type = VALUE_SCALAR_TYPE; break;
			case YAML_SCALAR_TOKEN: {
				const char *value = (const char*)token.data.scalar.value;
				switch (state.scalar_type) {

					case KEY_SCALAR_TYPE: {
						const uint32_t fourcc = MAKE_FOURCC(value[0], value[1], value[2], value[3]);
						switch (fourcc) {
							case MAKE_FOURCC('u','s','e','r'):
								state.item.value_index = 0; // 0 is user_agent_string
								break;
							default:
								// Field index is determined by callback function
								state.item.value_index = get_field_idx(value);
								/*printf("%s => %d\t", value, state.item.value_index);*/
								break;
						}
					} break;

					case VALUE_SCALAR_TYPE: {
						const int idx = state.item.value_index;
						if (idx >= 0) {
							state.item.value[idx] = realloc(state.item.value[idx], strlen(value) + 1);
							memcpy(state.item.value[idx], value, strlen(value) + 1);
						}
					} break;

					case UNKNOWN_SCALAR_TYPE:
					default:
						break;
				} break;
			} break;

			case YAML_BLOCK_END_TOKEN: {

				// See if we have a mostly valid looking record
				if (state.item.value[0] != NULL) {
					if (uap_parser_parse_string(ua_parser, ua_info, state.item.value[0])) {

						// Little progress doo-dad
						printf("\b%c", progress[num_passed % strlen(progress)]);
						fflush(stdout);

						/*printf("\n%s\n", state.item.value[0]);*/
						const char **fields = ((const char**)ua_info + field_offset);
						for (int i = 1; i < 5; i++) {
							if (state.item.value[i] && *state.item.value[i]) {
								/*printf("%s\t=>\t%s\n", state.item.value[i], *fields);*/
								if (strcmp(state.item.value[i], *fields) == 0) {
									num_passed++;
									/*printf("*");*/
								} else {
									fprintf(stderr, "\n%s\n in: \"%s\" != out: \"%s\"\n", state.item.value[0], state.item.value[i], *fields);
									num_failed++;
									/*assert(0);*/
									/*exit(1);*/
								}
							}
							fields++;
						}
					}
				}

				// Clear the current state
				for (int i=0; i < 5; i++) {
					if (state.item.value[i]) *state.item.value[i] = '\0';
				}
			} break;

			default:
				/*printf("token type: %d\n", token.type);*/
				break;
		}
	} while (token.type && token.type != YAML_STREAM_END_TOKEN);

	printf("\b%d PASSED\n", num_passed);
	if (num_failed > 0) {
		fprintf(stderr, "%d FAILED\n", num_failed);
		exit(1);
	}

	yaml_token_delete(&token);

	for (int i = 0; i < 6; i++) {
		free(state.item.value[i]);
		state.item.value[i] = NULL;
	}

	uap_useragent_info_destroy(ua_info);
	yaml_parser_delete(&yaml_parser);
}


int main(int argc, char** argv) {
	(void)argc;
	(void)argv;


	struct uap_parser *ua_parser = uap_parser_create();
	FILE *fd = fopen("../uap-core/regexes.yaml", "rb");
	if (fd != NULL) {
		uap_parser_read_file(ua_parser, fd);
		fclose(fd);
	} else {
		uap_parser_destroy(ua_parser);
		return -1;
	}

	// Base tests
	run_test_file("../uap-core/tests/test_ua.yaml", 0, ua_parser, &get_field_index_for_ua_test);
	run_test_file("../uap-core/tests/test_os.yaml", 4, ua_parser, &get_field_index_for_os_test);
	run_test_file("../uap-core/tests/test_device.yaml", 9, ua_parser, &get_field_index_for_devices_test);

	// Additional tests
	run_test_file("../uap-core/test_resources/firefox_user_agent_strings.yaml", 0, ua_parser, &get_field_index_for_ua_test);
	run_test_file("../uap-core/test_resources/opera_mini_user_agent_strings.yaml", 0, ua_parser, &get_field_index_for_ua_test);
	run_test_file("../uap-core/test_resources/podcasting_user_agent_strings.yaml", 0, ua_parser, &get_field_index_for_ua_test);
	run_test_file("../uap-core/test_resources/additional_os_tests.yaml", 4, ua_parser, &get_field_index_for_os_test);
	run_test_file("../uap-core/test_resources/pgts_browser_list.yaml", 0, ua_parser, &get_field_index_for_ua_test);
	// ^ this thing is 2MB of user agent strings, and so it takes forever to run.

	uap_parser_destroy(ua_parser);
	return 0;
}
