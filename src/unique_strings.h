#pragma once

struct unique_strings_t;

struct buffer_t;
struct unique_string_handle_t {
	size_t addr;
	struct buffer_t* parent;
};


struct unique_strings_t *unique_strings_create();

void unique_strings_destroy(struct unique_strings_t *);

struct unique_string_handle_t unique_strings_add(struct unique_strings_t *, const char *str);
void unique_strings_freeze(struct unique_strings_t *);

const char* unique_strings_get(struct unique_string_handle_t);

void unique_strings_dump(struct unique_strings_t *, const char *filename);

// Check if the given string is owned by the unique strings instance
// If it is owned, then it's managed and you shouldn't attempt to free it.
bool unique_strings_owns(struct unique_strings_t *, const char *str);
