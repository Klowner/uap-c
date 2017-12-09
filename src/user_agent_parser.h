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
        const char *family;
        const char *brand;
        const char *model;
    } device;
};


struct user_agent_parser;


// Allocate and initialize a new user_agent_parser.
struct user_agent_parser * user_agent_parser_create();


// Ingest a "regexes.yaml" from the uap-parser/uap-core project.
int user_agent_parser_read_file(struct user_agent_parser *uap, FILE *fd);


// Destroy and free a user_agent_parser instance.
void user_agent_parser_destroy(struct user_agent_parser *uap);


// Parse a user agent string into the provided user_agent_info structure.
// The user_agent_info instance can be reused for different user agent strings.
void user_agent_parser_parse_string(
        struct user_agent_parser *uap,
        struct user_agent_info *info,
        const char *user_agent_string);


// Create a new structure for holding parsed user-agent results.
struct user_agent_info * user_agent_info_create();


// Free the results structure.
void user_agent_info_destroy(struct user_agent_info *);


