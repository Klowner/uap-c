#include <stdio.h>
#include <stdlib.h>
#include <yaml.h>
#include <pcre.h>

#include "user_agent.h"




struct ua_expression_pair {
	pcre *regex;
	const char *family_replacement;
};

struct ua_parser_group {
	struct ua_expression_pair* expression_pairs;
};

struct user_agent_parser {
	struct ua_parser_group user_agent_parser_group;
	struct ua_parser_group os_parser_group;
	struct ua_parser_group device_parser_group;
};

struct user_agent_parser *user_agent_parser_create() {
	struct user_agent_parser *ua_parser = malloc(sizeof(struct user_agent_parser));
	yaml_parser_t parser;
	FILE* fh = NULL;

	// Initialize YAML parser
	if (!yaml_parser_initialize(&parser)) {
		fputs("Failed to initialize YAML parser!\n", stderr);
		goto fail;
	}

	fh = fopen("regexes.yaml", "r");
	if (fh == NULL) {
		fputs("Failed to open regexes.yaml!\n", stderr);
		goto fail;
	}

	{
		yaml_token_t token;
		yaml_parser_set_input_file(&parser, fh);

		do {
			yaml_token_delete(&token);
			yaml_parser_scan(&parser, &token);

			switch (token.type) {
				/* stream markers */
				case YAML_STREAM_START_TOKEN: puts("Stream start"); break;
				case YAML_STREAM_END_TOKEN: puts("Stream end"); break;

				/* token types */
				/* block delimiters */
				/* data */
				case YAML_BLOCK_MAPPING_START_TOKEN: puts("start block"); break;
				case YAML_SCALAR_TOKEN: printf("scalar: %s\n", token.data.scalar.value); break;


				default: break;
			}

		} while (token.type != YAML_STREAM_END_TOKEN);
	}

	yaml_parser_delete(&parser);
	fclose(fh);

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
	free(ua_parser);
}

void parse_user_agent(struct user_agent_info *info, const char* user_agent_string) {

	printf("user info %s", user_agent_string);
}
