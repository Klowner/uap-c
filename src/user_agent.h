#pragma once


struct user_agent_info {
	struct {
        char *brand;
        char *family;
        char *model;
    } device;

    struct {
        char *family;
        char *major;
        char *minor;
        char *patch;
    } os;

    struct {
        char *family;
        char *major;
        char *minor;
        char *patch;
    } user_agent;
};

struct user_agent_parser;

struct user_agent_parser *user_agent_parser_create();
void user_agent_parser_destroy(struct user_agent_parser *ua_parser);
void user_agent_parse_string(struct user_agent_parser *ua_parser, struct user_agent_info *info, const char *user_agent_string);



