#pragma once

struct unique_strings_t;

struct buffer_t;

struct unique_string_handle_t {
	size_t addr;
	struct buffer_t* parent;
};


// Allocate and initialize a new buffer_strings_t instance.
struct unique_strings_t *unique_strings_create();


// Destroy and free a buffer_strings_t instance.
void unique_strings_destroy(struct unique_strings_t *);


// Add a new string to the collection to be de-duped. Repeated additions of
// identical strings will yield the same unique_string_handle_t.  The string
// associated with the handle can be obtained by passing the handle to
// unique_strings_get(). NOTE: Does not take ownership of `str`!
struct unique_string_handle_t unique_strings_add(struct unique_strings_t *, const char *str);


// Free internal structures and buffers. Memory footprint will be reduced to
// little more than the space required to hold all the added strings.  After
// freezing, no more strings may be added.
void unique_strings_freeze(struct unique_strings_t *);


// Get a pointer to the string identified by the handle returned previously by
// a call to unique_strings_add().
const char* unique_strings_get(const struct unique_string_handle_t *);


// Check if the given string is owned by the unique strings instance.  If it is
// owned, then it's managed and you shouldn't attempt to free it.
bool unique_strings_owns(struct unique_strings_t *, const char *str);
