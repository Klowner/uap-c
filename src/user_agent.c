#define NDEBUG
#include <assert.h>
#include <pcre.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <yaml.h>

#include "user_agent.h"
#include "unique_strings.h"

#define MAX_PATTERN_MATCHES 10


struct ua_replacement {
	union {
		enum ua_user_agent_replacement_type {
			UA_REPL_FAMILY = 0, // family_replacment
			UA_REPL_V1,         // v1_replacement
			UA_REPL_V2,         // v2_replacement
			UA_REPL_V3,         // v3_replacement
		} user_agent_type;

		enum ua_os_replacement_type {
			OS_REPL = 0,        // os_replacement
			OS_REPL_V1,         // v1_replacement
			OS_REPL_V2,         // v2_replacement
			OS_REPL_V3,         // v3_replacement
			OS_REPL_V4,         // v4_replacement
		} os_type;

		enum ua_device_replacement_type {
			DEV_REPL_DEVICE = 0,
			DEV_REPL_BRAND,
			DEV_REPL_MODEL,
		} dev_type;

		enum ua_replacement_type {
			GENERIC_REPL,
		} type;
	};

	bool has_placeholders; // pattern contains $1, etc.
	struct unique_string_handle_t value;
	struct ua_replacement *next;
};


enum ua_parser_type {
	PARSER_TYPE_UNKNOWN = 0,
	PARSER_TYPE_USER_AGENT,
	PARSER_TYPE_OS,
	PARSER_TYPE_DEVICE,
};


// This should maintain the same layout as the user_agent_info struct
struct ua_parse_state {

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


struct ua_expression_pair {
	pcre *regex;
	pcre_extra *pcre_extra;
	struct ua_replacement *replacements;
	struct ua_expression_pair *next;
};


struct ua_parser_group {
	struct ua_expression_pair* expression_pairs;
	void (*apply_replacements_cb)(struct ua_parse_state*, struct ua_expression_pair*,
			const char *matches[MAX_PATTERN_MATCHES], pcre *replacement_re);
};


struct user_agent_parser {
	struct ua_parser_group user_agent_parser_group;
	struct ua_parser_group os_parser_group;
	struct ua_parser_group device_parser_group;
	struct unique_strings_t *strings;
	pcre *replacement_re;
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
		const char *ua_string,
		pcre *replacement_re)
{
	struct ua_expression_pair *pair = group->expression_pairs;
	const size_t ua_string_length = strlen(ua_string);
	const char *substring_matches[MAX_PATTERN_MATCHES];
	memset(substring_matches, 0, sizeof(const char*) * MAX_PATTERN_MATCHES);

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

					// Insert a null terminator
					*buffer_iter = '\0';
					buffer_iter++;
					buffer_avail -= 1;
				}
			}

			group->apply_replacements_cb(state, pair, &(substring_matches[0]), replacement_re);

			// All done
			return;
		}

		pair = pair->next;
	}
}


// Free any dynamically allocated strings but don't bother with data which
// is managed by the unique_strings system.
static void ua_parse_state_destroy(struct ua_parse_state *state, struct unique_strings_t *strings) {
	// ua_parse_state is just a bunch of const character pointers, so, this is fine.
	char **field = (char**)state;

	// the overall size of the structure, divided by the size of a const character pointer (size of each field)
	const char **end = (const char**)field + (sizeof(struct ua_parse_state) / sizeof(const char*));

	while ((const char**)field < end) {
		if (*field != NULL && !unique_strings_owns(strings, *field)) {
			free(*field);
			*field = NULL;
		}
		field++;
	}
}


static void ua_parse_state_create_user_agent_info(
		struct user_agent_info *info,
		struct ua_parse_state *state)
{
	// Calculate total buffer requirements for all info strings
	size_t size = 0;
	const char **src_field = (const char**)state;
	const char **end = src_field + (sizeof(struct ua_parse_state) / sizeof(const char*));

	// Total up all the string lengths plus null terminators
	while (src_field < end) {
		size += *src_field ? strlen(*src_field) + 1 : 0;
		src_field++;
	}

	// Allocate a new buffer to hold the strings, this will be
	// attached to the user_agent_info structure which has a
	// lifetime beyond this system, so it will need to be freed.
	// (automatically handled by user_agent_info_free());
	char *buffer = malloc(size);

	// Go back to the first field to begin copying to the buffer
	{
		char *write_ptr = buffer;
		src_field = (const char**)state;
		const char **dst_field = (const char**)info;

		while (src_field < end) {
			*dst_field = write_ptr;

			if (*src_field != NULL) {
				const size_t len = strlen(*src_field) + 1;
				memcpy(write_ptr, *src_field, len);
				write_ptr += len;
			} else {
				// This will point to a null terminator, since it's
				// at the end of the buffer.
				*dst_field = &buffer[size-1];
			}

			src_field++;
			dst_field++;
		}
	}
}


static void _apply_replacements(
		const char **state_fields,
		struct ua_expression_pair *pair,
		const char *matches[MAX_PATTERN_MATCHES],
		pcre *replacement_re)
{
	struct ua_replacement *repl = pair->replacements;

	while (repl) {
		// state_fields points to the first field, so the repl->type enum
		// can be used to adjust the pointer to the appropriate field.
		const char **dest = (state_fields + repl->type);

		// Check if we need to replace instances of $1...$9 with
		// the matched values or not. If we do, then new memory
		// is allocated for the field value rather than just
		// assigned to the unique_strings buffer. This is fine.
		if (repl->has_placeholders) {
			const char* src = unique_strings_get(&repl->value);
			size_t out_size = strlen(src);
			int substring_vec[MAX_PATTERN_MATCHES];

			int pcre_res = pcre_exec(
					replacement_re,
					NULL, // simple expression; doesn't utilize study data
					src,
					strlen(src),
					0,
					0,
					substring_vec,
					32
					);

			if (pcre_res > 0) {
				int replacement_index[MAX_PATTERN_MATCHES]; // lookup for matches[]

				for (int i = 0; i < (pcre_res - 1); i++) {
					const char *match;
					pcre_get_substring(src, substring_vec, pcre_res, i + 1, &(match));

					// Convert the ASCII character to the matching integer
					// ASCII '1' is 48, then subtract 1 so '$1' becomes 0, '$2' => 1, etc.
					replacement_index[i] = (match[0] - 48 - 1);
					pcre_free_substring(match);

					// For each replacement match, we SUBTRACT 2 from the out_size (strlen("$1"))
					// and then we ADD the size of the matches string that will be inserted
					out_size += (-2) + strlen(matches[replacement_index[i]]);
				}

				// Allocate a new buffer for the output
				char *out = malloc(out_size + 1);
				memset(out, 0, out_size + 1);

				int write_index = 0;
				const char *read_ptr = src;
				for (int i=0; i<(pcre_res-1); i++) {
					// Copy everything from the replacement string up to the point of
					// the match replacement identifier.
					while (write_index < substring_vec[i]) {
						out[write_index++] = *read_ptr;
						read_ptr++;
					}

					// Advance the read_ptr past the match replacement identifier eg: "$1"
					read_ptr += 2;

					// Copy the matched pattern data
					const char *match_read_ptr = matches[replacement_index[i]];
					while (*match_read_ptr != '\0') {
						out[write_index++] = *match_read_ptr;
						match_read_ptr++;
					}
				}

				// Copy any remaining replacement string to the output
				while (write_index < out_size) {
					out[write_index++] = *read_ptr;
					read_ptr++;
				}

				// Cap it!
				out[write_index] = '\0';

				// All done
				*dest = out;
			}
		} else {
			*dest = unique_strings_get(&repl->value);
		}

		repl = repl->next;
	}

	// Now fill in all remaining NULL fields with extracted match data
	for (int i=0; matches[i] != NULL; i++) {
		const char **dest = (state_fields + i); //repl->type);
		if (*dest == NULL) {
			const size_t len = strlen(matches[i]) + 1;
			char *out = malloc(len);
			memcpy(out, matches[i], len);
			*dest = out;
		}
	}
}


inline static void apply_replacements_os(
		struct ua_parse_state *state,
		struct ua_expression_pair *pair,
		const char *matches[MAX_PATTERN_MATCHES],
		pcre *replacement_re)
{
	_apply_replacements((const char**)&state->os, pair, matches, replacement_re);
}


inline static void apply_replacements_device(
		struct ua_parse_state *state,
		struct ua_expression_pair *pair,
		const char *matches[MAX_PATTERN_MATCHES],
		pcre *replacement_re)
{
	_apply_replacements((const char**)&state->device, pair, matches, replacement_re);
}


inline static void apply_replacements_user_agent(
		struct ua_parse_state *state,
		struct ua_expression_pair *pair,
		const char *matches[MAX_PATTERN_MATCHES],
		pcre *replacement_re)
{
	_apply_replacements((const char**)&state->user_agent, pair, matches, replacement_re);
}


struct user_agent_parser *user_agent_parser_create() {
	struct user_agent_parser *ua_parser = malloc(sizeof(struct user_agent_parser));

	ua_parser->user_agent_parser_group.expression_pairs = NULL;
	ua_parser->os_parser_group.expression_pairs         = NULL;
	ua_parser->device_parser_group.expression_pairs     = NULL;
	ua_parser->strings                                  = NULL;

	ua_parser->user_agent_parser_group.apply_replacements_cb = &apply_replacements_user_agent;
	ua_parser->os_parser_group.apply_replacements_cb         = &apply_replacements_os;
	ua_parser->device_parser_group.apply_replacements_cb     = &apply_replacements_device;

	// Compile expressions for finding $1...$9 in replacement strings
	{
		const char *error;
		int error_offset;
		ua_parser->replacement_re = pcre_compile("\\$(\\d)", PCRE_UTF8, &error, &error_offset, NULL);
		assert(ua_parser->replacement_re);
	}

	return ua_parser;
}


static void _user_agent_parser_parse_yaml(struct user_agent_parser *ua_parser, yaml_parser_t *yaml_parser) {
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
		enum ua_parser_type current_parser_type;
		struct ua_expression_pair **current_expression_pair_insert;
		struct ua_expression_pair *new_expression_pair;

		char *regex_temp;
		size_t regex_temp_size;
		char regex_flag;

	} state = {
		.key_type                       = UNKNOWN,
		.scalar_type                    = KEY,
		.current_replacement_type       = UNKNOWN,
		.current_parser_group           = NULL,
		.current_parser_type            = PARSER_TYPE_UNKNOWN,
		.current_expression_pair_insert = NULL,
		.new_expression_pair            = NULL,
		.regex_temp                     = NULL,
		.regex_temp_size                = 0,
		.regex_flag                     = '\0',
	};

	yaml_token_t token;

	// Parse. That. Yaml.
	do {
		yaml_token_delete(&token);
		yaml_parser_scan(yaml_parser, &token);

		switch (token.type) {
			case YAML_KEY_TOKEN: {
				state.scalar_type = KEY;
			} break;

			case YAML_VALUE_TOKEN: {
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

					int options = 0
						| PCRE_UTF8
						| PCRE_EXTRA
						| (state.regex_flag == 'i' ? PCRE_CASELESS : 0)
						;

					// Compile the expression (handled here
					pcre *re = pcre_compile(
							state.regex_temp,
							options,     // options - @toto handle regex_flag
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

					if (state.current_expression_pair_insert) {
						*state.current_expression_pair_insert  = new_pair;
						state.current_expression_pair_insert   = &(new_pair->next);
					}
				}
			} break;

			case YAML_SCALAR_TOKEN: {
				const char* value = (const char*)token.data.scalar.value;

				switch (state.scalar_type) {
					case KEY:
						if (strcmp(value, "regex") == 0) {
							state.key_type = REGEX;

						} else if (strstr(value, "_replacement") != NULL) {
							state.key_type = REPLACEMENT;

#define MAKE_FOURCC(a,b,c,d) ((a)|((b)<<8)|((c)<<16)|((d)<<24))

							switch (state.current_parser_type) {
								case PARSER_TYPE_USER_AGENT: {
									switch (MAKE_FOURCC(value[1], value[2], value[3], value[4])) {
										case MAKE_FOURCC('a','m','i','l'): state.current_replacement_type = (enum ua_replacement_type)UA_REPL_FAMILY; break;
										case MAKE_FOURCC('1','_','r','e'): state.current_replacement_type = (enum ua_replacement_type)UA_REPL_V1; break;
										case MAKE_FOURCC('2','_','r','e'): state.current_replacement_type = (enum ua_replacement_type)UA_REPL_V2; break;
										case MAKE_FOURCC('3','_','r','e'): state.current_replacement_type = (enum ua_replacement_type)UA_REPL_V3; break;
										default:
											break;
									}
								} break;

								case PARSER_TYPE_OS: {
									switch (MAKE_FOURCC(value[1], value[2], value[3], value[4])) {
										case MAKE_FOURCC('s','_','r','e'): state.current_replacement_type = (enum ua_replacement_type)OS_REPL; break;
										case MAKE_FOURCC('s','_','v','1'): state.current_replacement_type = (enum ua_replacement_type)OS_REPL_V1; break;
										case MAKE_FOURCC('s','_','v','2'): state.current_replacement_type = (enum ua_replacement_type)OS_REPL_V2; break;
										case MAKE_FOURCC('s','_','v','3'): state.current_replacement_type = (enum ua_replacement_type)OS_REPL_V3; break;
										case MAKE_FOURCC('s','_','v','4'): state.current_replacement_type = (enum ua_replacement_type)OS_REPL_V4; break;
										default:
											break;
									}
								} break;

								case PARSER_TYPE_DEVICE: {
									switch (MAKE_FOURCC(value[1], value[2], value[3], value[4])) {
										case MAKE_FOURCC('e','v','i','c'): state.current_replacement_type = (enum ua_replacement_type)DEV_REPL_DEVICE; break;
										case MAKE_FOURCC('r','a','n','d'): state.current_replacement_type = (enum ua_replacement_type)DEV_REPL_BRAND; break;
										case MAKE_FOURCC('o','d','e','l'): state.current_replacement_type = (enum ua_replacement_type)DEV_REPL_MODEL; break;
										default:
											printf("\t\t\tunknown repl type %s\n", value);
											break;
									}
								} break;

								case PARSER_TYPE_UNKNOWN:
								default:
									printf("unknown replacement type %s\n", value);
									break;
							}

						} else if (strstr(value, "_parsers") != NULL) {
							state.key_type = PARSER;

							// Determine the parser type, set the group pointer and type in the state
							switch (MAKE_FOURCC(value[0], value[1], value[2], value[3])) {
								case MAKE_FOURCC('u','s','e','r'): // user_agent_parsers
									state.current_parser_group = &ua_parser->user_agent_parser_group;
									state.current_parser_type  = PARSER_TYPE_USER_AGENT;
									break;

								case MAKE_FOURCC('o','s','_','p'): // os_parsers
									state.current_parser_group = &ua_parser->os_parser_group;
									state.current_parser_type  = PARSER_TYPE_OS;
									break;

								case MAKE_FOURCC('d','e','v','i'): // device_parsers
									state.current_parser_group = &ua_parser->device_parser_group;
									state.current_parser_type  = PARSER_TYPE_DEVICE;
									break;

								default:
									state.current_parser_group = NULL;
									state.current_parser_type  = PARSER_TYPE_UNKNOWN;
									break;
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
								// Create a new ua_replacement
								struct ua_replacement *repl = malloc(sizeof(struct ua_replacement));
								repl->value = unique_strings_add(ua_parser->strings, value);
								repl->has_placeholders = strstr(unique_strings_get(&repl->value), "$") != NULL;
								repl->type = state.current_replacement_type;

								// Append the new ua_replacement to the current new_expression_pair
								repl->next = state.new_expression_pair->replacements;
								state.new_expression_pair->replacements = repl;
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

	if (state.new_expression_pair) {
		ua_expression_pair_destroy(state.new_expression_pair);
		state.new_expression_pair = NULL;
	}

	free(state.regex_temp);
	yaml_token_delete(&token);
}


int user_agent_parser_read_file(struct user_agent_parser *ua_parser, FILE *fd) {
	yaml_parser_t parser;

	// Initialize YAML parser
	if (!yaml_parser_initialize(&parser)) {
		return 0;
	}

	yaml_parser_set_input_file(&parser, fd);

	// Create unique_strings_t for string deduping/packing of replacement strings
	ua_parser->strings = unique_strings_create();

	_user_agent_parser_parse_yaml(ua_parser, &parser);

	// Free the YAML parser
	yaml_parser_delete(&parser);

	// Free look-up structures and shrink allocated space if necessary
	unique_strings_freeze(ua_parser->strings);

	return 1;
}


void user_agent_parser_destroy(struct user_agent_parser *ua_parser) {
	ua_expression_pair_destroy(ua_parser->user_agent_parser_group.expression_pairs);
	ua_expression_pair_destroy(ua_parser->os_parser_group.expression_pairs);
	ua_expression_pair_destroy(ua_parser->device_parser_group.expression_pairs);
	unique_strings_destroy(ua_parser->strings);
	pcre_free(ua_parser->replacement_re);
	free(ua_parser);
}


void user_agent_parser_parse_string(struct user_agent_parser *ua_parser, struct user_agent_info *info, const char* user_agent_string) {
	struct ua_parse_state state;
	memset(&state, 0, sizeof(struct ua_parse_state));

	ua_parser_group_exec(&ua_parser->user_agent_parser_group, &state, user_agent_string, ua_parser->replacement_re);
	ua_parser_group_exec(&ua_parser->os_parser_group, &state, user_agent_string, ua_parser->replacement_re);
	ua_parser_group_exec(&ua_parser->device_parser_group, &state, user_agent_string, ua_parser->replacement_re);

	ua_parse_state_create_user_agent_info(info, &state);
	ua_parse_state_destroy(&state, ua_parser->strings);
}


struct user_agent_info * user_agent_info_create() {
	struct user_agent_info *info = malloc(sizeof(struct user_agent_info));
	memset(info, 0, sizeof(struct user_agent_info));
	return info;
}


void user_agent_info_destroy(struct user_agent_info *info) {
	free((void*)info->user_agent.family);
	free(info);
}
