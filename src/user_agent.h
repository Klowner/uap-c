#pragma once


struct user_agent_info {
    struct {
        const char *family;
        const char *major;
        const char *minor;
        const char *patch;
    } user_agent;

    struct {
        const char *family;
        const char *major;
        const char *minor;
        const char *patch;
        const char *patchMinor;
    } os;

	struct {
        const char *brand;
        const char *family;
        const char *model;
    } device;
};

struct user_agent_parser;

struct user_agent_parser *user_agent_parser_create();
void user_agent_parser_destroy(struct user_agent_parser *ua_parser);
void user_agent_parse_string(struct user_agent_parser *ua_parser, struct user_agent_info *info, const char *user_agent_string);

struct user_agent_info * user_agent_info_create();
void user_agent_info_destroy(struct user_agent_info *);


