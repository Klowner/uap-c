#include <stdio.h>
#include <stdlib.h>

#include "user_agent.h"



int main(int argc, char** argv) {
	(void)argc;
	(void)argv;


	/*struct unique_strings_t *strings = unique_strings_create();*/
	/*unique_strings_add(strings, "fruitcake");*/
	/*unique_strings_add(strings, "baby");*/
	/*unique_strings_add(strings, "Firefox");*/
	/*unique_strings_add(strings, "cupacabra");*/
	/*unique_strings_add(strings, "Firefox");*/
	/*unique_strings_add(strings, "Firefox");*/
	/*unique_strings_add(strings, "baby");*/
	/*unique_strings_add(strings, "baby");*/
	/*unique_strings_add(strings, "fruitcake");*/
	/*unique_strings_add(strings, "AlexanderPotatoman");*/
	/*unique_strings_dump(strings, "output.hex");*/
	/*unique_strings_destroy(strings);*/

	struct user_agent_parser *parser = user_agent_parser_create();
	const char* ua_string = "Mozilla/5.0 (X11; Linux x86_64; rv:57.0) Gecko/20100101 Firefox/57.0";
	struct user_agent_info ua_info;

	printf("\n\n\n");
	user_agent_parse_string(parser, &ua_info, ua_string);

	user_agent_parser_destroy(parser);

	return 0;
}
