#define NDEBUG
#include <assert.h>
#include <pcre.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <yaml.h>

#include "user_agent_parser.h"
#include "unique_strings.h"

#define MAX_PATTERN_MATCHES (32)
#define SUBSTRING_VEC_COUNT (MAX_PATTERN_MATCHES*2)

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
			DEV_REPL_DEVICE = 0, // device_replacement (family)
			DEV_REPL_BRAND,      // brand_replacement
			DEV_REPL_MODEL,      // model_replacement
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

	const char *buffer;
};


struct ua_expression_pair {
	pcre *regex;
	pcre_extra *pcre_extra;
	struct ua_replacement *replacements;
	struct ua_expression_pair *next;
};


struct ua_parser_group {
	struct ua_expression_pair* expression_pairs;
	void (*apply_replacements_cb)(
			struct ua_parse_state*,
			const char *ua_string,
			struct ua_expression_pair*,
			const int *matches_vector, // SUBSTRING_VEC_COUNT
			const int num_matches,
			pcre *replacement_re);
};


struct user_agent_parser {
	struct ua_parser_group user_agent_parser_group;
	struct ua_parser_group os_parser_group;
	struct ua_parser_group device_parser_group;
	struct unique_strings_t *strings;
	struct unique_string_handle_t string_handle_other; // handle -> "Other"
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

static int ua_parser_group_exec(
		const struct ua_parser_group *group,
		struct ua_parse_state *state,
		const char *ua_string,
		pcre *replacement_re)
{
	struct ua_expression_pair *pair = group->expression_pairs;
	const size_t ua_string_length = strlen(ua_string);

	// @TODO urldecode ua_string
	int matches_vector[SUBSTRING_VEC_COUNT];

	while (pair) {
		int pcre_result = pcre_exec(
				pair->regex,
				pair->pcre_extra,
				ua_string,
				ua_string_length,
				0,
				0,
				matches_vector,
				SUBSTRING_VEC_COUNT);

		if (pcre_result > 0) {
			group->apply_replacements_cb(state, ua_string, pair, &matches_vector[0], pcre_result, replacement_re);

			// Found a matching expression, all done.
			return 1;

		} else {
			switch (pcre_result) {
				case PCRE_ERROR_NOMATCH: break;
				default:
					printf("PCRE Error %d\n", pcre_result);
			}
		}

		pair = pair->next;
	}

	// Failed to match any expressions!
	return 0;
}


// Free any dynamically allocated strings but don't bother with data which
// is managed by the unique_strings system.
static void ua_parse_state_destroy(struct ua_parse_state *state, struct unique_strings_t *strings) {
	// ua_parse_state is just a bunch of const character pointers, so, this is fine.
	char **field = (char**)state;

	// the overall size of the structure, divided by the size of a const character pointer (size of each field)
	const char **end = (const char**)field + (sizeof(struct ua_parse_state) / sizeof(const char*));

	while ((const char**)field < end) {
		if (!unique_strings_owns(strings, *field)) {
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
	/*// Wipe the info*/
	/*memset(info, '\0', sizeof(struct user_agent_info));*/

	// Calculate total buffer requirements for all info strings
	size_t size = 0;
	const char **src_field = (const char**)state;
	const char **end = src_field + (sizeof(struct ua_parse_state) / sizeof(const char*));

	// Total up all the string lengths plus null terminators
	while (src_field != end) {
		size += *src_field ? strlen(*src_field) + 1 : 0;
		src_field++;
	}

	// Allocate a new buffer to hold the strings, this will be
	// attached to the user_agent_info structure which has a
	// lifetime beyond this system, so it will need to be freed.
	// (automatically handled by user_agent_info_free());
	char *buffer = realloc((void*)info->strings, size);

	// Wipe the info entirely (overwriting info->strings as well,
	// but we'll re-attach that near the end)
	memset(info, '\0', sizeof(struct user_agent_info));

	// Go back to the first field to begin copying to the buffer
	{
		char *write_ptr = buffer;
		src_field = (const char**)state;
		const char **dst_field = (const char**)info;

		while (src_field != end) {
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

	// Store a pointer to the beginning of the buffer
	info->strings = buffer;
}


static void _apply_replacements(
		const char **state_fields,
		const char *ua_string,
		struct ua_expression_pair *pair,
		const int *matches_vector, // SUBSTRING_VEC_COUNT
		pcre *replacement_re)
{
	struct ua_replacement *repl = pair->replacements;

	{
		int i = 0;
		while (repl) {
			repl = repl->next;
			i++;
		}
	}
	repl = pair->replacements;

	while (repl) {
		// state_fields points to the first field, so the repl->type enum
		// can be used to adjust the pointer to the appropriate field.
		const char **dest = (state_fields + repl->type);

		// Check if we need to replace instances of $1...$9 with
		// the matched values or not. If we do, then new memory
		// is allocated for the field value rather than just
		// assigned to the unique_strings buffer. This is fine.
		if (repl->has_placeholders) {
			const char* replacement_str = unique_strings_get(&repl->value);
			int out_size = strlen(replacement_str) + 1;
			int replacements_vector[SUBSTRING_VEC_COUNT];


			// At this phase we're scanning a replacement expression for the
			// "$N" patterns and collecting their positions within the replacement string.
			// These positions will determine where we inject the matched patterns from the
			// original user agent string (identified by `substring_vec`)
			//

			int replacements_count = 0;
			{
				int repl_vec[6]; // pattern only has one expression, at most 2 matches
				int start_offset = 0;
				const int replacement_strlen = strlen(replacement_str);

				while (start_offset < replacement_strlen && replacements_count < MAX_PATTERN_MATCHES) {
					const int pcre_res = pcre_exec(
						replacement_re,
						NULL, // Simple expression; doesn't utilize study data.
						replacement_str,
						replacement_strlen,
						start_offset,
						0, // No options.
						repl_vec,
						6);

					// Found a match
					if (pcre_res > 0) {
						replacements_vector[replacements_count * 2]     = repl_vec[0];
						replacements_vector[replacements_count * 2 + 1] = repl_vec[1];
						replacements_count++;
						start_offset = repl_vec[1];
					} else {
						// No matches, exit the loop
						start_offset = replacement_strlen;
					}
				}
			}

			// repl->has_placeholders is true, so this should always succeed.
			if (replacements_count > 0) {
				// lookup for ua_string/substring_vec[] positions
				int replacement_index[MAX_PATTERN_MATCHES];

				// Iterate over replacement identifier matches
				for (int i = 0; i < replacements_count; i++) {
					// Convert the ASCII character to the matching integer
					// "$1" becomes integer 1. We don't start with the 0-position pattern match
					// because that is the overall PCRE match result.
					replacement_index[i] = replacement_str[replacements_vector[i*2] + 1] - 48;
					assert(replacement_index[i] > 0 && replacement_index[i] < 10);

					if (replacement_index[i] < 1 || replacement_index[i] > 9) {
						// Invalid replacement index, this is likely an input error of regexes.yaml
						// and so we'll treat it as a fatal error, since it could cause a buffer overflow.
						return;
					}

					const int idx = replacement_index[i];
					const int replacement_strlen = matches_vector[idx * 2 + 1] - matches_vector[idx * 2];

					// For each replacement match, we SUBTRACT 2 from the out_size (strlen("$1"))
					// and then we ADD the size of the matches string that will be inserted
					if (replacement_strlen > 0) {
						out_size += (-2) + replacement_strlen;
					}
				}

				// Allocate a new buffer for the output
				char *out = calloc(1, out_size);

				// Now combine matched user agent patterns with replacement patterns.
				int write_index = 0; // to output.
				int read_index = 0; // from replacement_str, eg: "Foo $5 Bar $2 Baz"

				for (int i = 0; i < replacements_count; i++) {

					// Copy everything from the replacement string leading up
					// to the point of the first match.
					//
					// "Foo $5 Bar $2 Baz"
					//  ---^
					while (read_index < replacements_vector[i * 2]) {
						out[write_index++] = replacement_str[read_index++];
					}

					// Advance the read_index by 2 bytes to skip the "$N" which
					// we should now be at.
					//
					// "Foo $5 Bar $2 Baz"
					//  ------^
					assert(replacement_str[read_index] == '$');
					read_index += 2;

					// Copy the matched pattern from the original user agent
					// string, identified by `matches_vector`.
					int match_start     = matches_vector[replacement_index[i] * 2];
					const int match_end = matches_vector[replacement_index[i] * 2 + 1];

					while (match_start < match_end) {
						out[write_index++] = ua_string[match_start++];
					}
				}

				// Copy any remaining replacement string data
				while (replacement_str[read_index]) {
					out[write_index++] = replacement_str[read_index++];
				}

				// Trim leading whitespace
				{
					int i = 0;
					while (i < out_size && out[i] == ' ') i++;

					for (int j = i; j < out_size; j++) {
						out[j - i] = out[j];
					}
				}

				// Trim trailing whitespace
				while (write_index > 0 && out[--write_index] == ' ') {
					out[write_index] = '\0';
				}

				// All done
				*dest = out;
			}
		} else {
			*dest = unique_strings_get(&repl->value);
		}

		repl = repl->next;
	}
}


static void _apply_defaults(
		const char **field,
		const char *ua_string,
		const int num_fields,
		const int *matches_vector,
		const int num_matches)
{
#define MIN(_a, _b) (_a < _b ? _a : _b)
	const int max_iterations = MIN(num_fields, (num_matches-1));
#undef MIN

	for (int i = 0; i < max_iterations; i++) {
		if (!*field) {
			const int mv_idx = (i + 1) * 2;
			const size_t len = matches_vector[mv_idx + 1] - matches_vector[mv_idx];
			char *out = calloc(1, len + 1);
			memcpy(out, &ua_string[matches_vector[mv_idx]], len);
			*field = out;
		}

		++field;
	}
}


static void _apply_defaults_for_device(
		struct ua_parse_state_device *device,
		const char *ua_string,
		const int *matches_vector,
		const int num_matches)
{
	if (num_matches > 1) {
		const char **fields[] = { &device->family, &device->model };
		for (int i = 0; i < 2; i++) {
			// If the field is undefined, use the first matched pattern if available
			if (!*fields[i]) {
				const size_t len = matches_vector[2 + 1] - matches_vector[2];
				*fields[i] = calloc(1, len + 1);
				memcpy((void*)*fields[i], &ua_string[matches_vector[2]], len);
			}
		}
	}
}


inline static void apply_replacements_user_agent(
		struct ua_parse_state *state,
		const char *ua_string,
		struct ua_expression_pair *pair,
		const int *matches_vector, // SUBSTRING_VEC_COUNT
		const int num_matches,
		pcre *replacement_re)
{
	_apply_replacements((const char**)&state->user_agent, ua_string, pair, matches_vector, replacement_re);
	_apply_defaults((const char**)&state->user_agent, ua_string, 4, matches_vector, num_matches);
}


inline static void apply_replacements_os(
		struct ua_parse_state *state,
		const char *ua_string,
		struct ua_expression_pair *pair,
		const int *matches_vector, // SUBSTRING_VEC_COUNT
		const int num_matches,
		pcre *replacement_re)
{
	_apply_replacements((const char**)&state->os, ua_string, pair, matches_vector, replacement_re);
	_apply_defaults((const char**)&state->os, ua_string, 5, matches_vector, num_matches);
}


inline static void apply_replacements_device(
		struct ua_parse_state *state,
		const char *ua_string,
		struct ua_expression_pair *pair,
		const int *matches_vector, // SUBSTRING_VEC_COUNT
		const int num_matches,
		pcre *replacement_re)
{
	_apply_replacements((const char**)&state->device, ua_string, pair, matches_vector, replacement_re);
	_apply_defaults_for_device(&state->device, ua_string, matches_vector, num_matches);
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
		ua_parser->replacement_re = pcre_compile("\\$\\d", PCRE_UTF8, &error, &error_offset, NULL);
		assert(ua_parser->replacement_re);
	}

	return ua_parser;
}


void user_agent_parser_destroy(struct user_agent_parser *ua_parser) {
	ua_expression_pair_destroy(ua_parser->user_agent_parser_group.expression_pairs);
	ua_expression_pair_destroy(ua_parser->os_parser_group.expression_pairs);
	ua_expression_pair_destroy(ua_parser->device_parser_group.expression_pairs);
	unique_strings_destroy(ua_parser->strings);
	pcre_free(ua_parser->replacement_re);
	free(ua_parser);
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
	memset(&token, 0, sizeof(yaml_token_t));

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
				if (state.new_expression_pair && state.regex_temp) {
					struct ua_expression_pair *new_pair = state.new_expression_pair;
					state.new_expression_pair = NULL;

					//##################################
					// Commit the active item if present
					//##################################
					if (new_pair != NULL && state.regex_temp) {
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

						// If the expression compiled successfully, attach it to
						// the new expression_pair, otherwise free the new pair and continue
						if (re) {
							new_pair->regex = re;
							new_pair->pcre_extra = pcre_study(re, 0, &error);
							state.regex_flag = '\0';
						} else {
							printf("pcre error: %d %s\n", erroffset, error);
							ua_expression_pair_destroy(new_pair);
							break;
						}

						int i = 0;
						struct ua_replacement *repl = new_pair->replacements;
						while (repl) {
							i++;
							repl = repl->next;
						}

						if (state.current_expression_pair_insert) {
							*state.current_expression_pair_insert  = new_pair;
							state.current_expression_pair_insert   = &(new_pair->next);
						} else {
							ua_expression_pair_destroy(new_pair);
						}

					}
					new_pair = NULL;
				}
			} break;

			case YAML_SCALAR_TOKEN: {
				const char *value = (const char*)token.data.scalar.value;

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
							state.new_expression_pair = calloc(1, sizeof(struct ua_expression_pair));
						}

						switch (state.key_type) {

							case REGEX: {
								// Ensure our scratch space is large enough the old the regular expression.
								// Unfortunately the regular expression needs to be held onto until the block
								// is finished due to the possible existence of a "regex_flags" value.
								const size_t regex_length = strlen(value) + 1;
								if (state.regex_temp_size < regex_length) {
									state.regex_temp = realloc(state.regex_temp, regex_length);
									assert(state.regex_temp);
									state.regex_temp_size = regex_length;
								}
								memcpy(state.regex_temp, value, regex_length);
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


							default:
								printf("WARNING INVALID KEY TYPE *****\n");
								break;
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


static void _user_agent_parser_init(struct user_agent_parser *ua_parser, yaml_parser_t *parser) {
	// Create unique_strings_t for string deduping/packing of replacement strings
	ua_parser->strings = unique_strings_create();

	// device.family should default to "Other" if nothing is parsed, so we'll
	// add "Other" as a unique string and grab a handle for possible later user.
	ua_parser->string_handle_other = unique_strings_add(ua_parser->strings, "Other");

	_user_agent_parser_parse_yaml(ua_parser, parser);

	// Free the YAML parser
	yaml_parser_delete(parser);

	// Free look-up structures and shrink allocated space if necessary
	unique_strings_freeze(ua_parser->strings);
}


int user_agent_parser_read_file(struct user_agent_parser *ua_parser, FILE *fd) {
	yaml_parser_t parser;

	// Initialize YAML parser
	if (!yaml_parser_initialize(&parser)) {
		return 0;
	}

	yaml_parser_set_input_file(&parser, fd);
	_user_agent_parser_init(ua_parser, &parser);

	return 1;
}


int user_agent_parser_read_buffer(struct user_agent_parser *ua_parser, const unsigned char *buffer, const size_t bufsize) {
	yaml_parser_t parser;

	if (!yaml_parser_initialize(&parser)) {
		return 0;
	}

	yaml_parser_set_input_string(&parser, buffer, bufsize);
	_user_agent_parser_init(ua_parser, &parser);

	return 1;
}


int user_agent_parser_parse_string(struct user_agent_parser *ua_parser, struct user_agent_info *info, const char* user_agent_string) {
	struct ua_parse_state state;
	memset(&state, 0, sizeof(struct ua_parse_state));

	const int matched_groups = 0
		+ ua_parser_group_exec(&ua_parser->user_agent_parser_group, &state, user_agent_string, ua_parser->replacement_re)
		+ ua_parser_group_exec(&ua_parser->os_parser_group, &state, user_agent_string, ua_parser->replacement_re)
		+ ua_parser_group_exec(&ua_parser->device_parser_group, &state, user_agent_string, ua_parser->replacement_re);

	// Special case for family, if (null) then set to "Other"
	const char **family[] = { &state.device.family, &state.os.family, &state.user_agent.family };
	for (int i = 0; i < 3; i++) {
		if (*family[i] == NULL) {
			*family[i] = unique_strings_get(&ua_parser->string_handle_other);
		}
	}

	if (matched_groups > 0) {
		ua_parse_state_create_user_agent_info(info, &state);
	}

	ua_parse_state_destroy(&state, ua_parser->strings);

	return matched_groups;
}


struct user_agent_info * user_agent_info_create() {
	struct user_agent_info *info = calloc(1, sizeof(struct user_agent_info));
	return info;
}


void user_agent_info_destroy(struct user_agent_info *info) {
	if (info != NULL) {
		if (info->strings) {
			free((void*)info->strings);
		}
		free(info);
	}
}
