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
		V1,
		V2,
	} type;

	struct unique_string_handle_t value;
	struct ua_replacement *next;
};


struct ua_expression_pair {
	pcre *regex;
	struct ua_replacement *replacements;
	struct ua_expression_pair *next;
};


struct ua_parser_group {
	struct ua_expression_pair* expression_pairs;
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
		free(pair);

		pair = next;
	}
}


struct user_agent_parser *user_agent_parser_create() {
	struct user_agent_parser *ua_parser = malloc(sizeof(struct user_agent_parser));
	ua_parser->user_agent_parser_group.expression_pairs = NULL;
	ua_parser->os_parser_group.expression_pairs = NULL;
	ua_parser->device_parser_group.expression_pairs = NULL;

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

				case YAML_BLOCK_SEQUENCE_START_TOKEN: {
					printf("block (SEQUENCE)\n");
				} break;
				/*case YAML_BLOCK_ENTRY_TOKEN: puts("block (ENTRY)\n"); break;*/

				case YAML_BLOCK_END_TOKEN: {
					struct ua_expression_pair *new_pair = state.new_expression_pair;
					state.new_expression_pair = NULL;

					//##################################
					// Commit the active item if present
					//##################################
					if (new_pair != NULL) {
						const char *error;
						int erroffset;

						puts("block END FINISHING PAIR\n");

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
								printf("\trepl type(%d): %s\n", repl->type, unique_strings_get(strings, repl->value));
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
									case MAKE_FOURCC('1','_','r','e'): state.current_replacement_type = V1; break;
									case MAKE_FOURCC('2','_','r','e'): state.current_replacement_type = V2; break;
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
								/*state.current_expression_pair_insert = ua_expression_pair_last(state.current_parser_group->expression_pairs);*/

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

							/*printf("value type %d %s\n", state.key_type, value);*/
							switch (state.key_type) {

								case REGEX: {
									// Ensure our scratch space is large enough the old the regular expression.
									// Unfortunately the regular expression needs to be held onto until the block
									// is finished due to the possible existence of a "regex_flags" value.
									printf("regx: %s\n",  value);
									const size_t regex_length = strlen(value) + 1;
									if (state.regex_temp_size < regex_length) {
										state.regex_temp = realloc(state.regex_temp, regex_length);
										state.regex_temp_size = regex_length;
									}
									strcpy(state.regex_temp, value);

								} break;

								case REGEX_FLAG: {
									printf("reg flag: %s\n",  value);
									state.regex_flag = value[0];
								} break;

								case REPLACEMENT: {
									if (state.current_replacement_type != UNKNOWN) {
										printf("repl type: %d  val %s\n", state.current_replacement_type, value);

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
					/*printf("scalar: %s\n", token.data.scalar.value);*/
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


void parse_user_agent(struct user_agent_info *info, const char* user_agent_string) {

	printf("user info %s", user_agent_string);
}
