#include <stdio.h>
#include <stdlib.h>
#include <yaml.h>
#include <pcre.h>
#include <assert.h>

#include "user_agent.h"
#include "unique_strings.h"


struct ua_replacement {
	enum ua_replacement_type {
		UNKNOWN,
		BRAND,
		DEVICE,
		FAMILY,
		MODEL,
		OS,
		OS_V1,
		OS_V2,
		OS_V3,
		OS_V4,
		V1,
		V2,
		V3,
	} type;

	struct unique_string_handle_t value;
	struct ua_replacement *next;
};


struct ua_expression_pair {
	pcre *regex;
	pcre_extra *pcre_extra;
	struct ua_replacement *replacements;
	struct ua_expression_pair *next;
};



struct ua_parse_state {
	const char *string;

	struct ua_parse_state_user_agent {
		const char *family;
		const char *major;
		const char *minor;
		const char *patch;
	} user_agent;

	struct ua_parse_state_os {
		const char *family;
		const char *major;
		const char *minor;
		const char *patch;
		const char *patchMinor;
	} os;

	struct ua_parse_state_device {
		const char *family;
		const char *brand;
		const char *model;
	} device;
};


struct ua_parser_group {
	struct ua_expression_pair* expression_pairs;
	void (*apply_replacements_cb)(struct ua_parse_state*, struct ua_expression_pair*, const char *matches[8], const size_t matches_size);
};


struct user_agent_parser {
	struct ua_parser_group user_agent_parser_group;
	struct ua_parser_group os_parser_group;
	struct ua_parser_group device_parser_group;
};



struct ua_expression_pair ** ua_expression_pair_last(struct ua_expression_pair *pair) {
	while (pair && pair->next) {
		pair = pair->next;
	}
	return &pair->next;
}


static void ua_replacement_destroy(struct ua_replacement *replacement) {
	struct ua_replacement *next;

	while (replacement) {
		next = replacement->next;
		free(replacement);
		replacement = next;
	}
}

static void ua_expression_pair_destroy(struct ua_expression_pair *pair) {
	struct ua_expression_pair *next;

	while (pair) {
		next = pair->next;

		ua_replacement_destroy(pair->replacements);
		pcre_free(pair->regex);
		pcre_free(pair->pcre_extra);
		free(pair);

		pair = next;
	}
}

static void ua_parser_group_exec(
		const struct ua_parser_group *group,
		struct ua_parse_state *state,
		const char *ua_string)
{
	struct ua_expression_pair *pair = group->expression_pairs;
	const size_t ua_string_length = strlen(ua_string);
	const char *substring_matches[8] = { NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL};
	/*const char *match = NULL;*/
	int substrings[64];

	while (pair) {
		int pcre_result = pcre_exec(
				pair->regex,
				pair->pcre_extra,
				ua_string,
				ua_string_length,
				0,
				0,
				substrings,
				64);

		if (pcre_result > 0) {
			// a smallish buffer to hold the extracted strings
			char buffer[1024];
			char *buffer_iter = &buffer[0];
			int buffer_avail = 1024;

			for (int i=0; i < pcre_result-1; i++) {
				int copy_size = pcre_copy_substring(ua_string, substrings, pcre_result, i + 1, buffer_iter, buffer_avail);
				if (copy_size) {
					substring_matches[i] = buffer_iter;
					buffer_avail -= copy_size;
					buffer_iter += copy_size;
				}

				printf("!!!!matched: %d %s\n", i, substring_matches[i]);
			}

			group->apply_replacements_cb(state, pair, &(substring_matches[0]), sizeof(buffer) - buffer_avail);
		}

		pair = pair->next;
	}
}

static void format_repl(const char **dest, const char *replacement, const char *matches[8]) {

	/*printf("we've got %s\n", replacement);*/
	/**dest = replacement;*/
}

static void apply_replacements_user_agent(
		struct ua_parse_state *state,
		struct ua_expression_pair *pair,
		const char *matches[8],
		const size_t matches_size)
{
	struct ua_replacement *repl = pair->replacements;
	struct ua_parse_state_user_agent *ua_state = &state->user_agent;

	/*
	while (repl) {
		const char* replacement_string = unique_strings_get(repl->value);
		const char** dest = NULL;

		switch (repl->type) {
			case FAMILY:  dest = &(ua_state->family); break;
			case V1:      dest = &(ua_state->major); break;
			case V2:      dest = &(ua_state->minor); break;
			case V3:      dest = &(ua_state->patch); break;
			default: break;
		}

		format_repl(dest, replacement_string, matches);

		repl = repl->next;
	}
*/
	/*printf("matches!: %s\n", matches[0]);*/
	/*if (ua_state->family == NULL && matches[0]) {*/
		/*ua_state->family = strdup(matches[0]);*/
	/*}*/
}

static void apply_replacements_os(
		struct ua_parse_state *state,
		struct ua_expression_pair *pair,
		const char *matches[8],
		const size_t matches_size)
{
	struct ua_replacement *repl = pair->replacements;
	struct ua_parse_state_os *os_state = &state->os;


	while (repl) {
		const char* value = unique_strings_get(repl->value);
		printf("REPL VAL: %s\n", value);
		switch (repl->type) {
			case OS:        os_state->family     = value; break;
			case OS_V1:     os_state->major      = value; break;
			case OS_V2:     os_state->minor      = value; break;
			case OS_V3:     os_state->patch      = value; break;
			case OS_V4:     os_state->patchMinor = value; break;
			default: break;
		}

		repl = repl->next;
	}

}

static void apply_replacements_device(struct ua_parse_state *state, struct ua_expression_pair *pair, const char *matches[8]) {
	struct ua_replacement *repl = pair->replacements;
	struct ua_parse_state_user_agent *ua_state = &state->user_agent;

	while (repl) {
		const char* value = unique_strings_get(repl->value);
		switch (repl->type) {
			case FAMILY: ua_state->family = value; break;
			case V1:     ua_state->major  = value; break;
			case V2:     ua_state->minor  = value; break;
			case V3:     ua_state->patch  = value; break;
			default: break;
		}
		repl = repl->next;
	}
}

struct user_agent_parser *user_agent_parser_create() {
	struct user_agent_parser *ua_parser = malloc(sizeof(struct user_agent_parser));
	ua_parser->user_agent_parser_group.expression_pairs = NULL;
	ua_parser->os_parser_group.expression_pairs = NULL;
	ua_parser->device_parser_group.expression_pairs = NULL;

	ua_parser->user_agent_parser_group.apply_replacements_cb = &apply_replacements_user_agent;
	ua_parser->os_parser_group.apply_replacements_cb         = &apply_replacements_os;
	ua_parser->device_parser_group.apply_replacements_cb     = &apply_replacements_device;

	yaml_parser_t parser;
	FILE* fh = NULL;

	// Initialize YAML parser
	if (!yaml_parser_initialize(&parser)) {
		fputs("Failed to initialize YAML parser!\n", stderr);
		goto fail;
	}

	// Open signatures yaml file
	fh = fopen("regexes.yaml", "r");
	if (fh == NULL) {
		fputs("Failed to open regexes.yaml!\n", stderr);
		goto fail;
	}

	// Create unique_strings_t for string deduping/packing of replacement strings
	struct unique_strings_t *strings = unique_strings_create();

	{
		// Structure to retain the active parsing state
		struct {
			enum {
				UNKNOWN,
				REGEX, // regex
				REPLACEMENT,
				PARSER, // user_agent_parsers, os_parsers, device_parsers
				REGEX_FLAG,
			} key_type;

			enum {
				KEY,
				VALUE,
			} scalar_type;

			enum ua_replacement_type current_replacement_type;
			struct ua_parser_group *current_parser_group;
			struct ua_expression_pair **current_expression_pair_insert;
			struct ua_expression_pair *new_expression_pair;

			char *regex_temp;
			size_t regex_temp_size;
			char regex_flag;

		} state = {
			.key_type = UNKNOWN,
			.scalar_type = KEY,
			.current_replacement_type = UNKNOWN,
			.current_parser_group = NULL,
			.current_expression_pair_insert = NULL,
			.new_expression_pair = NULL,
			.regex_temp = NULL,
			.regex_temp_size = 0,
			.regex_flag = '\0',
		};

		// Parse. That. Yaml.
		yaml_token_t token;
		yaml_parser_set_input_file(&parser, fh);

		do {
			yaml_token_delete(&token);
			yaml_parser_scan(&parser, &token);

			switch (token.type) {
				case YAML_KEY_TOKEN: {
					/*printf("key: ");*/
					state.scalar_type = KEY;
				} break;

				case YAML_VALUE_TOKEN: {
					/*printf("val: ");*/
					state.scalar_type = VALUE;
				} break;

				case YAML_BLOCK_END_TOKEN: {
					struct ua_expression_pair *new_pair = state.new_expression_pair;
					state.new_expression_pair = NULL;

					//##################################
					// Commit the active item if present
					//##################################
					if (new_pair != NULL) {
						const char *error;
						int erroffset;

						// Compile the expression (handled here
						pcre *re = pcre_compile(
								state.regex_temp,
								0,           // options - @toto handle regex_flag
								&error,      // error message
								&erroffset,  // error offset
								NULL);       // use default character tables

						// Clear temporary state properties
						state.regex_temp[0] = '\0';
						state.regex_flag    = '\0';

						// If the expression compiled successfully, attach it to
						// the new expression_pair, otherwise free the new pair and continue
						if (re) {
							new_pair->regex = re;
							new_pair->pcre_extra = pcre_study(re, 0, &error);
						} else {
							printf("pcre error: %d %s\n", erroffset, error);
							ua_expression_pair_destroy(new_pair);
							break;
						}

						// Show some info about the current item.
						if (1) {
							printf("pcre %p\n", new_pair->regex);
							struct ua_replacement *repl = new_pair->replacements;
							while (repl) {
								printf("\trepl type(%d): %s\n", repl->type, unique_strings_get(repl->value));
								repl = repl->next;
							}
						}

						if (state.current_expression_pair_insert) {
							*state.current_expression_pair_insert  = new_pair;
							state.current_expression_pair_insert   = &(new_pair->next);
						}
					}
				} break;

				case YAML_SCALAR_TOKEN: {
					const char* value = (const char*)token.data.scalar.value;

					/*printf("scalar_token: %s scalar_type:%d key_type:%d\n ", value, state.scalar_type, state.key_type);*/
					switch (state.scalar_type) {
						case KEY:
							if (strcmp(value, "regex") == 0) {
								state.key_type = REGEX;

							} else if (strstr(value, "_replacement") != NULL) {
								state.key_type = REPLACEMENT;

#define MAKE_FOURCC(a,b,c,d) ((a)|((b)<<8)|((c)<<16)|((d)<<24))
								switch (MAKE_FOURCC(value[1], value[2], value[3], value[4])) {
									case MAKE_FOURCC('r','a','n','d'): state.current_replacement_type = BRAND; break;
									case MAKE_FOURCC('e','v','i','c'): state.current_replacement_type = DEVICE; break;
									case MAKE_FOURCC('a','m','i','y'): state.current_replacement_type = FAMILY; break;
									case MAKE_FOURCC('o','d','e','l'): state.current_replacement_type = MODEL; break;
									case MAKE_FOURCC('s','_','r','e'): state.current_replacement_type = OS; break;
									case MAKE_FOURCC('s','_','v','1'): state.current_replacement_type = OS_V1; break;
									case MAKE_FOURCC('s','_','v','2'): state.current_replacement_type = OS_V2; break;
									case MAKE_FOURCC('s','_','v','3'): state.current_replacement_type = OS_V3; break;
									case MAKE_FOURCC('s','_','v','4'): state.current_replacement_type = OS_V4; break;
									case MAKE_FOURCC('1','_','r','e'): state.current_replacement_type = V1; break;
									case MAKE_FOURCC('2','_','r','e'): state.current_replacement_type = V2; break;
									case MAKE_FOURCC('3','_','r','e'): state.current_replacement_type = V3; break;
								}
#undef MAKE_FOURCC

							} else if (strstr(value, "_parsers") != NULL) {
								state.key_type = PARSER;

								// Switch to appropriate parsers group
								if (strcmp(value, "user_agent_parsers") == 0) {
									state.current_parser_group = &ua_parser->user_agent_parser_group;
								} else if (strcmp(value, "os_parsers") == 0) {
									state.current_parser_group = &ua_parser->os_parser_group;
								} else if (strcmp(value, "device_parsers") == 0) {
									state.current_parser_group = &ua_parser->device_parser_group;
								}
								assert(state.current_parser_group);

								// Switch to the new parser group's expression list
								state.current_expression_pair_insert = &(state.current_parser_group->expression_pairs);

							} else if (strcmp(value, "regex_flag") == 0) {
								state.key_type = REGEX_FLAG;

							} else {
								state.key_type = UNKNOWN;
							}

							break;

						case VALUE:
							// Ensure we have somewhere to put the parsed data
							if (state.new_expression_pair == NULL) {
								state.new_expression_pair = malloc(sizeof(struct ua_expression_pair));
								memset(state.new_expression_pair, 0, sizeof(struct ua_expression_pair));
							}

							switch (state.key_type) {

								case REGEX: {
									// Ensure our scratch space is large enough the old the regular expression.
									// Unfortunately the regular expression needs to be held onto until the block
									// is finished due to the possible existence of a "regex_flags" value.
									const size_t regex_length = strlen(value) + 1;
									if (state.regex_temp_size < regex_length) {
										state.regex_temp = realloc(state.regex_temp, regex_length);
										state.regex_temp_size = regex_length;
									}
									strcpy(state.regex_temp, value);

								} break;

								case REGEX_FLAG: {
									state.regex_flag = value[0];
								} break;

								case REPLACEMENT: {
									if (state.current_replacement_type != UNKNOWN) {
										/*printf("repl type: %d  val %s\n", state.current_replacement_type, value);*/

										// Create a new ua_replacement
										struct ua_replacement *repl = malloc(sizeof(struct ua_replacement));
										repl->value = unique_strings_add(strings, value);
										repl->type = state.current_replacement_type;

										// Append the new ua_replacement to the current new_expression_pair
										repl->next = state.new_expression_pair->replacements;
										state.new_expression_pair->replacements = repl;

									}
								} break;


								default: break;
							}
							break;
					}
					break;
				}
				default: break;
			}

		} while (token.type != YAML_STREAM_END_TOKEN);
		free(state.regex_temp);
		yaml_token_delete(&token);
	}

	yaml_parser_delete(&parser);
	fclose(fh);

	unique_strings_dump(strings, "output.hex");
	unique_strings_destroy(strings);

	return ua_parser;

fail:
	if (fh != NULL) {
		fclose(fh);
	}

	if (ua_parser != NULL) {
		free(ua_parser);
	}

	return NULL;
}

void user_agent_parser_destroy(struct user_agent_parser *ua_parser) {
	ua_expression_pair_destroy(ua_parser->user_agent_parser_group.expression_pairs);
	ua_expression_pair_destroy(ua_parser->os_parser_group.expression_pairs);
	ua_expression_pair_destroy(ua_parser->device_parser_group.expression_pairs);
	free(ua_parser);
}


void user_agent_parse_string(struct user_agent_parser *ua_parser, struct user_agent_info *info, const char* user_agent_string) {
	/*char buff[1024];*/
	/*const char *buff_end = &buff[1024];*/
	/*char *iter = &buff;*/


	struct ua_parse_state state;
	memset(&state, 0, sizeof(struct ua_parse_state));

	ua_parser_group_exec(&ua_parser->user_agent_parser_group, &state, user_agent_string);
	ua_parser_group_exec(&ua_parser->os_parser_group, &state, user_agent_string);

	printf("ua:%s\n\n", user_agent_string);
	printf("ua:\n\tfamily: %s\n\tmajor: %s\n\t minor: %s\n\t patch %s\n",
			state.user_agent.family,
			state.user_agent.major,
			state.user_agent.minor,
			state.user_agent.patch);

	printf("os:\n\tfamily: %s\n\tmajor: %s\n\t minor: %s\n\t patch %s\n",
			state.os.family,
			state.os.major,
			state.os.minor,
			state.os.patch);

}
