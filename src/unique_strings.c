#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#include "unique_strings.h"

#define UNIQUE_STRING_BUCKETS 32
#define MURMUR_SEED 0xf9a025a4 // random


struct buffer_t {
	size_t used;
	size_t capacity;
	char *data;
} buffer;


struct string_hash_pair_t {
	uint32_t hash;
	const char *str;
};


struct unique_string_node {
	struct unique_string_node *next;
	uint32_t hash;
	struct unique_string_handle_t buffer_ptr;
};

struct unique_strings_t {
	struct buffer_t buffer;
	struct unique_string_node **buckets; // pointer to an array of UNIQUE_STRING_BUCKETS pointers
};


static uint32_t murmur2_hash(const char *data, int len, uint32_t seed) {
	const uint32_t m = 0x5bd1e995;
	const int r = 24;

	uint32_t h = seed ^ len;

	while (len >= 4) {
		uint32_t k = *(uint32_t*)data;

		k *= m;
		k ^= k >> r;
		k *= m;

		h *= m;
		h ^= k;

		data += 4;
		len -= 4;
	}

	switch (len) {
		case 3: h ^= data[2] << 16;
		case 2: h ^= data[1] << 8;
		case 1: h ^= data[0];
			h *= m;
	}

	h ^= h >> 13;
	h *= m;
	h ^= h >> 15;

	return h;
}

///###############################
//# Simple managed growable buffer
///###############################

// Allocate a unique_string_handle_t associated with the requested size.
// Use buffer_addr() to get an actual pointer
static struct unique_string_handle_t buffer_alloc(struct buffer_t* buffer, size_t size) {
	if (buffer->used + size >= buffer->capacity) {
		buffer->capacity += 1024 * ((size / 1024) + 1); // grow by 1kB
		buffer->data = realloc(buffer->data, buffer->capacity);
	}

	struct unique_string_handle_t ptr = { .addr = buffer->used, .parent = buffer };
	buffer->used += size;
	return ptr;
}


// Returns actual memory address for a unique_string_handle_t
static char* buffer_addr(struct buffer_t *buffer, struct unique_string_handle_t ptr) {
	return buffer->data + ptr.addr;
}

/*const char* unique_strings_get(struct unique_strings_t *strings, struct unique_string_handle_t ptr) {*/
	/*return strings->buffer.data + ptr.addr;*/
/*}*/

const char* unique_strings_get(struct unique_string_handle_t handle) {
	return handle.parent->data + handle.addr;
}

// Frees buffer's backing storage and resets usage data
static void buffer_clear(struct buffer_t *buffer) {
	free(buffer->data);
	buffer->capacity = 0;
	buffer->used = 0;
	buffer->data = NULL;
}



// Copies the string pointer to the string_hash_pair_t and generates
// a hash from it. Also returns the hash.
static uint32_t _string_hash_pair_prepare(struct string_hash_pair_t* shp, const char *str) {
	shp->str = str;
	shp->hash = murmur2_hash(str, strlen(str), MURMUR_SEED);
	return shp->hash;
}


struct unique_strings_t *unique_strings_create() {
	struct unique_strings_t *us = malloc(sizeof(struct unique_strings_t));
	memset((void*)us, 0, sizeof(struct unique_strings_t));
	us->buckets = malloc(sizeof(struct unique_string_node *) * UNIQUE_STRING_BUCKETS);
	memset(us->buckets, 0, sizeof(struct unique_string_node *) * UNIQUE_STRING_BUCKETS);
	return us;
}


static uint32_t _get_bucket_index(const struct string_hash_pair_t *pair) {
	return pair->hash % UNIQUE_STRING_BUCKETS;
}


// Returns true on successful find
// `node` will point to node on success, otherwise
// it will be set to point to the parent (used for insertion)
static bool _unique_strings_find_node(
		struct unique_string_node** node,
		struct unique_strings_t *us,
		struct string_hash_pair_t *pair)
{
	const uint32_t bucket_id = _get_bucket_index(pair);
	const uint32_t pair_hash = pair->hash;
	struct unique_string_node *iter = us->buckets[bucket_id];
	*node = NULL;

	while (iter) {
		*node = iter;

		if (iter->hash == pair_hash &&
			strcmp(buffer_addr(&us->buffer, iter->buffer_ptr), pair->str) == 0)
		{
			// found matching hash and string
			return true;
		}

		// Sorted search/insert
		if (iter->hash < pair_hash) {
			break;
		}

		iter = iter->next;
	}

	return false;
}


// Copies data from `pair` into `node`.
// Hash is copied over, while the string data is allocated via
// via the managed buffer.
static struct unique_string_node *unique_string_node_create(
		struct string_hash_pair_t *pair,
		struct buffer_t *buffer)
{
	struct unique_string_node *node = malloc(sizeof(struct unique_string_node));
	node->next = NULL;
	node->hash = pair->hash;
	node->buffer_ptr = buffer_alloc(buffer, strlen(pair->str) + 1);
	char* ptr = buffer_addr(buffer, node->buffer_ptr);
	const size_t length = strlen(pair->str);
	memcpy(ptr, pair->str, length + 1);
	return node;
}


struct unique_string_handle_t unique_strings_add(struct unique_strings_t *us, const char *str) {
	struct string_hash_pair_t pair;
	_string_hash_pair_prepare(&pair, str);
	struct unique_string_node *found = NULL;

	if (! _unique_strings_find_node(&found, us, &pair)) {
		// Node was not found...
		struct unique_string_node *new_node = unique_string_node_create(&pair, &us->buffer);

		if (found) {
			// If parent is set, then it is the appropriate parent node
			// for the new node. If it's null, then we just throw the new
			// node into the appropriate bucket.
			new_node->next = found->next;
			found->next = new_node;
		} else {
			// Insert a new root node into the bucket
			us->buckets[_get_bucket_index(&pair)] = new_node;
		}
		return new_node->buffer_ptr;
	}

	return found->buffer_ptr;
}


static void _unique_string_free_nodes(struct unique_string_node *node) {
	if (node->next) {
		_unique_string_free_nodes(node->next);
	}

	free(node);
}


// Returns the pointer to the data buffer, this is to be called
// after you're done using the unique_strings system and you
// have valid pointers to all the strings. This unlinks the
// monolithic string data buffer and resets all unique_strings_t
// structures.
/*const char* unique_string_extract_buffer(struct unique_strings_t *us) {*/
/*}*/

void unique_strings_destroy(struct unique_strings_t *us) {
	if (us->buckets) {
		for (unsigned int i=0; i < UNIQUE_STRING_BUCKETS; i++) {
			if (us->buckets[i]) {
				_unique_string_free_nodes(us->buckets[i]);
			}
		}
		free(us->buckets);
	}
	buffer_clear(&us->buffer);
	free(us);
}


void unique_strings_dump(struct unique_strings_t *us, const char *filename) {
	FILE *fd = fopen(filename, "wb");
	fwrite((void*)us->buffer.data, 1, us->buffer.used, fd);
	fclose(fd);
}
