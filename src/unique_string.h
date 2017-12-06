#pragma once

struct unique_strings_t;
struct unique_strings_t *unique_strings_create();
void unique_strings_destroy(struct unique_strings_t *);

void unique_strings_add(struct unique_strings_t *, const char *str);
void unique_strings_freeze(struct unique_strings_t *);
const char* unique_strings_find(struct unique_strings_t *, const char *str);

void unique_strings_dump(struct unique_strings_t *, const char *filename);
