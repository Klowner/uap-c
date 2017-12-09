#include <stdio.h>
#include <stdlib.h>

#include "user_agent.h"



int main(int argc, char** argv) {
	(void)argc;
	(void)argv;


	struct user_agent_parser *parser = user_agent_parser_create();
	struct user_agent_info *ua_info = user_agent_info_create();

	FILE *fd = fopen("regexes.yaml", "rb");
	if (fd != NULL) {
		user_agent_parser_read_file(parser, fd);
		fclose(fd);
	} else {
		user_agent_parser_destroy(parser);
		user_agent_info_destroy(ua_info);
		return -1;
	}

	user_agent_parser_parse_string(parser, ua_info, "Mozilla/5.0 (Macintosh; Intel Mac OS X 10_9_4) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/41.0.2272.104 Safari/537.36");
	user_agent_parser_destroy(parser);

	printf("user agent:\n  family: %s\n  major: %s\n  minor: %s\n  patch: %s\n",
			ua_info->user_agent.family,
			ua_info->user_agent.major,
			ua_info->user_agent.minor,
			ua_info->user_agent.patch
			);

	printf("os:\n  family: %s\n  major: %s\n  minor: %s\n  patch: %s\n  patchMinor: %s\n",
			ua_info->os.family,
			ua_info->os.major,
			ua_info->os.minor,
			ua_info->os.patch,
			ua_info->os.patchMinor
			);

	printf("device:\n  family: %s\n  brand: %s\n  model: %s\n",
			ua_info->device.family,
			ua_info->device.brand,
			ua_info->device.model
			);

	user_agent_info_destroy(ua_info);
	return 0;
}
