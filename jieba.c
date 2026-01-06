#include <stdint.h>
#include <stddef.h>
#include <assert.h>
#include <string.h>
#include "jieba.h"

#define JIEBA_MAX_WORD_LENGTH 32
#define JIEBA_ASSUME_AVERAGE_WORD_LENGTH 4
#define JIEBA_ESTIMATED_WORD_COUNT_OFFSET 1024
#define JIEBA_ESTIMATED_HASH_CELL_COUNT_COEFFICIENT 1.414
#define JIEBA_HASH_TABLE_NODE_BUCKET_NUMBER 2047
#define JIEBA_HASH_TABLE_INITIAL_MAX_CELL_PER_BUCKET 8

struct jieba__utf32be {
  uint8_t data[4];
};

struct jieba__string {
  size_t count;
  size_t first_character_pos;
};

struct jieba__hash_table_cell {
  size_t next_cell_pos;
  uint64_t hash;
  struct jieba__string string;
};

struct jieba__hash_table_bucket {
  size_t count;
  size_t first_cell_pos;
};

struct jieba__hash_table_node {
  size_t next_node_pos;
  size_t unused_for_alignment;
  struct jieba__hash_table_bucket buckets[JIEBA_HASH_TABLE_NODE_BUCKET_NUMBER];
};

struct jieba__hash_table {
  size_t count;
  size_t size;
  size_t max_cell_per_bucket;
  size_t first_node_pos;
};

struct jieba__data_base_node {
  size_t next_node_pos;
  int n_chinese_letter;
  struct jieba__hash_table table;
};

struct jieba__data_base {
  size_t character_space_size;
  size_t character_space_used;
  struct jieba__utf32be *characterp; /* for bump */

  size_t hash_table_cell_space_size;
  size_t hash_table_cell_first_free;
  struct jieba__hash_table_cell *hash_table_cells;

  size_t hash_table_node_space_size;
  size_t hash_table_node_first_free;
  struct jieba__hash_table_node *hash_table_nodes;

  size_t data_base_node_space_size;
  size_t data_base_node_first_free;
  struct jieba__data_base_node *data_base_nodes;

  size_t first_data_base_node_pos;
};

static size_t jieba__character_space_size(size_t estimated_word_count) {
  size_t count, size;
  size = sizeof(struct jieba__utf32be);
  count = estimated_word_count + JIEBA_ESTIMATED_WORD_COUNT_OFFSET;
  return size * count * JIEBA_ASSUME_AVERAGE_WORD_LENGTH;
}

static size_t jieba__init_character_space(
  size_t estimated_word_count, void *restrict whole_memory,
  size_t whole_memory_used, struct jieba__data_base *root
) {
  size_t size = jieba__character_space_size(estimated_word_count);
  root->character_space_size = size;
  root->character_space_used = 0;
  root->characterp = whole_memory + whole_memory_used;
  return size;
}

static size_t jieba__hash_table_cell_space_size(size_t estimated_word_count) {
  size_t count, size;
  size = sizeof(struct jieba__hash_table_cell);
  count = estimated_word_count + JIEBA_ESTIMATED_WORD_COUNT_OFFSET;
  count *= JIEBA_ESTIMATED_HASH_CELL_COUNT_COEFFICIENT;
  return count * size;
}

static size_t jieba__init_hash_table_cell_space(
  size_t estimated_word_count, void *restrict whole_memory,
  size_t whole_memory_used, struct jieba__data_base *root
) {
  size_t size = jieba__hash_table_cell_space_size(estimated_word_count);
  root->hash_table_cell_space_size = size;
  root->hash_table_cell_first_free = 0;
  root->hash_table_cells = whole_memory + whole_memory_used;
  return size;
}

static void jieba__init_hash_table_cell_free_list(
    size_t estimated_word_count, struct jieba__data_base *root
) {
  size_t size = jieba__hash_table_cell_space_size(estimated_word_count);
  size_t count = size / sizeof(struct jieba__hash_table_cell);
  for (size_t i = 0; i < count; i++)
    root->hash_table_cells[i].next_cell_pos = i + 1;
  root->hash_table_cells[count - 1].next_cell_pos = (size_t)-1;
}

static size_t jieba__hash_table_node_space_size(size_t estimated_word_count) {
  size_t count, size;
  size = sizeof(struct jieba__hash_table_node);
  count = estimated_word_count + JIEBA_ESTIMATED_WORD_COUNT_OFFSET;
  count *= JIEBA_ESTIMATED_HASH_CELL_COUNT_COEFFICIENT;
  count /= JIEBA_HASH_TABLE_INITIAL_MAX_CELL_PER_BUCKET;
  count = (count + (JIEBA_HASH_TABLE_NODE_BUCKET_NUMBER - 1))
    / JIEBA_HASH_TABLE_NODE_BUCKET_NUMBER;
  count *= 2; /* for hash table extending */
  return size * count;
}

static size_t jieba__init_hash_table_node_space(
  size_t estimated_word_count, void *restrict whole_memory,
  size_t whole_memory_used, struct jieba__data_base *root
) {
  size_t size = jieba__hash_table_node_space_size(estimated_word_count);
  root->hash_table_node_space_size = size;
  root->hash_table_node_first_free = 0;
  root->hash_table_nodes = whole_memory + whole_memory_used;
  return size;
}

static void jieba__init_hash_table_node_free_list(
    size_t estimated_word_count, struct jieba__data_base *root
) {
  size_t size = jieba__hash_table_node_space_size(estimated_word_count);
  size_t count = size / sizeof(struct jieba__hash_table_node);
  for (size_t i = 0; i < count; i++)
    root->hash_table_nodes[i].next_node_pos = i + 1;
  root->hash_table_nodes[count - 1].next_node_pos = (size_t)-1;
}

static size_t jieba__data_base_node_space_size() {
  size_t count, size;
  count = JIEBA_MAX_WORD_LENGTH;
  size = sizeof(struct jieba__data_base_node);
  return count * size;
}

static size_t jieba__init_data_base_node_space(
  void *restrict whole_memory, size_t whole_memory_used,
  struct jieba__data_base *root
) {
  size_t size = jieba__data_base_node_space_size();
  root->data_base_node_space_size = size;
  root->data_base_node_first_free = 0;
  root->data_base_nodes = whole_memory + whole_memory_used;
  return size;
}

static void jieba__init_data_base_node_free_list(
    struct jieba__data_base *root
) {
  size_t size = jieba__data_base_node_space_size();
  size_t count = size / sizeof(struct jieba__data_base_node);
  for (size_t i = 0; i < count; i++)
    root->data_base_nodes[i].next_node_pos = i + 1;
  root->data_base_nodes[count - 1].next_node_pos = (size_t)-1;
}

enum jieba_init_result
jieba_init_data_base(
    struct jieba_data_base *restrict data_base, void *restrict whole_memory,
    size_t whole_memory_size, size_t estimated_word_count, size_t *required
) {
  size_t whole_memory_used;
  struct jieba__data_base *root;

  /* calculate size and initialize each fields */

  whole_memory_used = 0;

  data_base->whole_memory = whole_memory;
  data_base->whole_memory_size = whole_memory_size;

  root = data_base->root = whole_memory;
  whole_memory_used += sizeof(struct jieba__data_base);

  whole_memory_used += jieba__init_character_space(
      estimated_word_count, whole_memory, whole_memory_used, root
  );

  whole_memory_used += jieba__init_hash_table_cell_space(
      estimated_word_count, whole_memory, whole_memory_used, root
  );

  whole_memory_used += jieba__init_hash_table_node_space(
      estimated_word_count, whole_memory, whole_memory_used, root
  );

  whole_memory_used += jieba__init_data_base_node_space(
      whole_memory, whole_memory_used, root
  );

  *required = whole_memory_used;
  if (whole_memory_used > whole_memory_size) return JIEBA_INIT_FAIL_NOMEM;

  /* initialize free lists */

  jieba__init_hash_table_cell_free_list(estimated_word_count, root);
  jieba__init_hash_table_node_free_list(estimated_word_count, root);
  jieba__init_data_base_node_free_list(root);

  return JIEBA_INIT_SUCCESS;
}

static enum jieba_add_word_result
jieba__mbtoc32be(
    const unsigned char *in, size_t in_len, struct jieba__utf32be *outchar,
    size_t *cvt_len
) {
  uint8_t *out = outchar->data;

  if (in_len == 0) return JIEBA_ADD_WORD_NO_ENOUGH_CHARACTER;

  uint32_t cp;
  uint8_t c = in[0];

  if (c < 0x80) {
    cp = c;
    assert(in_len >= 1);
    out[0]=0; out[1]=0; out[2]=0; out[3]=cp;
    *cvt_len = 1;
    return JIEBA_ADD_WORD_SUCCESS;
  }

  if ((c & 0xE0) == 0xC0) {
    if (in_len < 2) return JIEBA_ADD_WORD_NO_ENOUGH_CHARACTER;
    if ((in[1] & 0xc0) != 0x80) return JIEBA_ADD_WORD_BAD_UTF8;
    if (c < 0xC2) return JIEBA_ADD_WORD_BAD_UTF8;
    cp = ((c & 0x1F) << 6)
       | (in[1] & 0x3F);
    out[0]=0; out[1]=0; out[2]=cp>>8; out[3]=cp;
    *cvt_len = 2;
    return JIEBA_ADD_WORD_SUCCESS;
  }

  if ((c & 0xF0) == 0xE0) {
    if (in_len < 3) return JIEBA_ADD_WORD_NO_ENOUGH_CHARACTER;
    if ((in[1] & 0xc0) != 0x80) return JIEBA_ADD_WORD_BAD_UTF8;
    if ((in[2] & 0xc0) != 0x80) return JIEBA_ADD_WORD_BAD_UTF8;
    if (c == 0xE0 && in[1] < 0xA0) return JIEBA_ADD_WORD_BAD_UTF8;
    if (c == 0xED && in[1] >= 0xA0) return JIEBA_ADD_WORD_BAD_UTF8;
    cp = ((c & 0x0F) << 12)
       | ((in[1] & 0x3F) << 6)
       | (in[2] & 0x3F);
    out[0]=0; out[1]=cp>>16; out[2]=cp>>8; out[3]=cp;
    *cvt_len = 3;
    return JIEBA_ADD_WORD_SUCCESS;
  }

  if ((c & 0xF8) == 0xF0) {
    if (in_len < 4) return JIEBA_ADD_WORD_NO_ENOUGH_CHARACTER;
    if ((in[1] & 0xc0) != 0x80) return JIEBA_ADD_WORD_BAD_UTF8;
    if ((in[2] & 0xc0) != 0x80) return JIEBA_ADD_WORD_BAD_UTF8;
    if ((in[3] & 0xc0) != 0x80) return JIEBA_ADD_WORD_BAD_UTF8;
    if (c == 0xF0 && in[1] < 0x90) return JIEBA_ADD_WORD_BAD_UTF8;
    if (c == 0xF4 && in[1] > 0x8F) return JIEBA_ADD_WORD_BAD_UTF8;
    if (c > 0xF4) return JIEBA_ADD_WORD_BAD_UTF8;
    cp = ((c & 0x07) << 18)
       | ((in[1] & 0x3F) << 12)
       | ((in[2] & 0x3F) << 6)
       | (in[3] & 0x3F);
    out[0]=cp>>24; out[1]=cp>>16; out[2]=cp>>8; out[3]=cp;
    *cvt_len = 4;
    return JIEBA_ADD_WORD_SUCCESS;
  }

  return JIEBA_ADD_WORD_BAD_UTF8;
}

static enum jieba_add_word_result
jieba__mbtoc32bestr(
    const unsigned char *in, size_t in_len, struct jieba__utf32be *outstr,
    size_t *out_len
) {
  size_t i = 0, cvt_len;
  enum jieba_add_word_result res;

  while (in_len != 0) {
    res = jieba__mbtoc32be(in, in_len, &outstr[i], &cvt_len);
    if (res != JIEBA_ADD_WORD_SUCCESS) return res;
    in = &in[cvt_len]; in_len -= cvt_len; i += 1;
  }
  *out_len = i;
  return JIEBA_ADD_WORD_SUCCESS;
}

static void jieba__init_hash_table(struct jieba__hash_table *table) {
  table->count = table->size = 0;
  table->max_cell_per_bucket = JIEBA_HASH_TABLE_INITIAL_MAX_CELL_PER_BUCKET;
  table->first_node_pos = -1;
}

static size_t jieba__allocate_data_base_node(
    size_t n_chinese_letter, struct jieba__data_base *data_base
) {
  size_t *data_base_node_first_free = &data_base->data_base_node_first_free;
  struct jieba__data_base_node *nodes = data_base->data_base_nodes;

  if (*data_base_node_first_free == (size_t)-1) return (size_t)-1;

  size_t new_pos = *data_base_node_first_free;
  *data_base_node_first_free = nodes[new_pos].next_node_pos;

  nodes[new_pos].next_node_pos = -1;
  nodes[new_pos].n_chinese_letter = n_chinese_letter;
  jieba__init_hash_table(&nodes[new_pos]);
  
  return new_pos;
}

static enum jieba_add_word_result
jieba__find_data_base_node(
  size_t word_size, size_t *data_base_node_first_free,
  size_t *first_data_base_node_pos, struct jieba__data_base_node *nodes,
  struct jieba__data_base *data_base, size_t *data_base_node_pos
) {
  if (*first_data_base_node_pos == (size_t)-1 ||
      word_size > nodes[*first_data_base_node_pos].n_chinese_letter)
  {
    size_t new_pos = jieba__allocate_data_base_node(word_size, data_base);
    if (new_pos == -1) return JIEBA_ADD_WORD_FAIL_NOMEM;

    nodes[new_pos].next_node_pos = *first_data_base_node_pos;
    *first_data_base_node_pos = new_pos;

    *data_base_node_pos = new_pos;
    return JIEBA_ADD_WORD_SUCCESS;
  } else {
    size_t last_pos = *first_data_base_node_pos;
    size_t pos = nodes[*first_data_base_node_pos].next_node_pos;

    while (pos != -1 && word_size > nodes[pos].n_chinese_letter) {
      if (word_size == nodes[pos].n_chinese_letter) {
        *data_base_node_pos = pos;
        return JIEBA_ADD_WORD_SUCCESS;
      }
      last_pos = pos;
      pos = nodes[pos].next_node_pos;
    }

    size_t new_pos = jieba__allocate_data_base_node(word_size, data_base);
    if (new_pos == -1) return JIEBA_ADD_WORD_FAIL_NOMEM;

    nodes[new_pos].next_node_pos = pos;
    nodes[last_pos].next_node_pos = new_pos;

    *data_base_node_pos = new_pos;
    return JIEBA_ADD_WORD_SUCCESS;
  }
}

enum jieba_add_word_result
jieba_add_word(
    unsigned char *restrict word, size_t word_size,
    struct jieba_data_base *restrict data_base
) {
  struct jieba__utf32be c32str_cache[128];
  size_t c32str_cache_size = 0;
  enum jieba_add_word_result res;

  res = jieba__mbtoc32bestr(
      word, word_size, c32str_cache, &c32str_cache_size
  );
  if (res != JIEBA_ADD_WORD_SUCCESS) return res;

  size_t data_base_node_pos;
  res = jieba__find_data_base_node(
      word_size, &data_base->data_base_node_first_free,
      &data_base->first_data_base_node_pos, data_base->data_base_nodes,
      data_base, &data_base_node_pos
  );
  if (res != JIEBA_ADD_WORD_SUCCESS) return res;
}
