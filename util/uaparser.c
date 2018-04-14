#include <stdio.h>
#include "uap/uap.h"
#include "regexes.yaml.h"

int main(int argc, char **argv) {

	if (argc < 2) {
		printf("usage: %s <user agent string>\n", argv[0]);
		return -1;
	}

	struct uap_parser *ua_parser = uap_parser_create();
	struct uap_useragent_info *ua_info = uap_useragent_info_create();

	uap_parser_read_buffer(ua_parser, ___uap_core_regexes_yaml, ___uap_core_regexes_yaml_len);

	if (uap_parser_parse_string(ua_parser, ua_info, argv[1])) {

		printf("user_agent.family\t%s\n",  ua_info->user_agent.family);
		printf("user_agent.major\t%s\n",   ua_info->user_agent.major);
		printf("user_agent.minor\t%s\n",   ua_info->user_agent.minor);
		printf("user_agent.patch\t%s\n",   ua_info->user_agent.patch);

		printf("os.family\t%s\n",          ua_info->os.family);
		printf("os.major\t%s\n",           ua_info->os.major);
		printf("os.minor\t%s\n",           ua_info->os.minor);
		printf("os.patch\t%s\n",           ua_info->os.patch);
		printf("os.patchMinor\t%s\n",      ua_info->os.patch);

		printf("device.family\t%s\n",      ua_info->device.family);
		printf("device.brand\t%s\n",       ua_info->device.brand);
		printf("device.model\t%s\n",       ua_info->device.model);

	}

	uap_parser_destroy(ua_parser);
	uap_useragent_info_destroy(ua_info);

	return 0;
}
