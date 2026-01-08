#include <stdint.h>
#include <stddef.h>
#include <assert.h>
#include <string.h>
#include <stdio.h>
#include "jieba.h"
#include "wyhash.h"

#ifdef JIEBA__DEBUG
# define jieba__log(fmt, ...)\
  printf("file: %s; func: %s; line: %d; " fmt,  __FILE__, __func__, __LINE__,\
         ## __VA_ARGS__)
# define jieba__assert(...) assert(__VA_ARGS__)
#else
# define jibea__log(...)
# define jieba__assert(...)
#endif

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
  size_t size; /* buckets number */
  size_t max_cell_per_bucket;
  size_t first_node_pos;
};

struct jieba__data_base_node {
  size_t next_node_pos;
  int n_chinese_letter;
  struct jieba__hash_table table;
};

struct jieba__data_base {
  size_t estimated_word_count;

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

static size_t jieba__character_space_count(size_t estimated_word_count) {
  size_t count;
  count = estimated_word_count + JIEBA_ESTIMATED_WORD_COUNT_OFFSET;
  count *= JIEBA_ASSUME_AVERAGE_WORD_LENGTH;
  return count;
}

static size_t jieba__character_space_size(size_t estimated_word_count) {
  size_t count, size;
  size = sizeof(struct jieba__utf32be);
  count = jieba__character_space_count(estimated_word_count);
  return size * count;
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

static size_t jieba__hash_table_cell_space_count(size_t estimated_word_count) {
  size_t count;
  count = estimated_word_count + JIEBA_ESTIMATED_WORD_COUNT_OFFSET;
  count *= JIEBA_ESTIMATED_HASH_CELL_COUNT_COEFFICIENT;
  return count;
}

static size_t jieba__hash_table_cell_space_size(size_t estimated_word_count) {
  size_t count, size;
  size = sizeof(struct jieba__hash_table_cell);
  count = jieba__hash_table_cell_space_count(estimated_word_count);
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
  size_t count = jieba__hash_table_cell_space_count(estimated_word_count);
  for (size_t i = 0; i < count; i++)
    root->hash_table_cells[i].next_cell_pos = i + 1;
  root->hash_table_cells[count - 1].next_cell_pos = (size_t)-1;
}

static size_t jieba__hash_table_node_space_count(size_t estimated_word_count) {
  size_t count;
  count = estimated_word_count + JIEBA_ESTIMATED_WORD_COUNT_OFFSET;
  count *= JIEBA_ESTIMATED_HASH_CELL_COUNT_COEFFICIENT;
  count /= JIEBA_HASH_TABLE_INITIAL_MAX_CELL_PER_BUCKET;
  count = (count + (JIEBA_HASH_TABLE_NODE_BUCKET_NUMBER - 1))
    / JIEBA_HASH_TABLE_NODE_BUCKET_NUMBER;
  count *= 2; /* for hash table extending */
  return count;
}

static size_t jieba__hash_table_node_space_size(size_t estimated_word_count) {
  size_t count, size;
  size = sizeof(struct jieba__hash_table_node);
  count = jieba__hash_table_node_space_count(estimated_word_count);
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
  size_t count = jieba__hash_table_node_space_count(estimated_word_count);
  for (size_t i = 0; i < count; i++)
    root->hash_table_nodes[i].next_node_pos = i + 1;
  root->hash_table_nodes[count - 1].next_node_pos = (size_t)-1;
}

static size_t jieba__data_base_node_space_count() {
  return JIEBA_MAX_WORD_LENGTH;
}

static size_t jieba__data_base_node_space_size() {
  size_t count, size;
  count = jieba__data_base_node_space_count();
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
  size_t count = jieba__data_base_node_space_count();
  for (size_t i = 0; i < count; i++)
    root->data_base_nodes[i].next_node_pos = i + 1;
  root->data_base_nodes[count - 1].next_node_pos = (size_t)-1;
}

size_t jieba_estimate_memory_size(size_t estimated_word_count) {
  return sizeof(struct jieba__data_base)
    + jieba__character_space_size(estimated_word_count)
    + jieba__hash_table_cell_space_size(estimated_word_count)
    + jieba__hash_table_node_space_size(estimated_word_count)
    + jieba__data_base_node_space_size();
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

  root->estimated_word_count = estimated_word_count;

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

enum jieba__mbtoc32be_result {
  JIEBA__MBTOC32BE_SUCCESS,
  JIEBA__MBTOC32BE_NO_ENOUGH_CHARACTER,
  JIEBA__MBTOC32BE_BAD_UTF8,
};

static enum jieba__mbtoc32be_result
jieba__mbtoc32be(
    const unsigned char *in, size_t in_len, struct jieba__utf32be *outchar,
    size_t *cvt_len
) {
  uint8_t *out = outchar->data;

  if (in_len == 0) return JIEBA__MBTOC32BE_NO_ENOUGH_CHARACTER;

  uint32_t cp;
  uint8_t c = in[0];

  if (c < 0x80) {
    cp = c;
    assert(in_len >= 1);
    out[0]=0; out[1]=0; out[2]=0; out[3]=cp;
    *cvt_len = 1;
    return JIEBA__MBTOC32BE_SUCCESS;
  }

  if ((c & 0xE0) == 0xC0) {
    if (in_len < 2) return JIEBA__MBTOC32BE_NO_ENOUGH_CHARACTER;
    if ((in[1] & 0xc0) != 0x80) return JIEBA__MBTOC32BE_BAD_UTF8;
    if (c < 0xC2) return JIEBA__MBTOC32BE_BAD_UTF8;
    cp = ((c & 0x1F) << 6)
       | (in[1] & 0x3F);
    out[0]=0; out[1]=0; out[2]=cp>>8; out[3]=cp;
    *cvt_len = 2;
    return JIEBA__MBTOC32BE_SUCCESS;
  }

  if ((c & 0xF0) == 0xE0) {
    if (in_len < 3) return JIEBA__MBTOC32BE_NO_ENOUGH_CHARACTER;
    if ((in[1] & 0xc0) != 0x80) return JIEBA__MBTOC32BE_BAD_UTF8;
    if ((in[2] & 0xc0) != 0x80) return JIEBA__MBTOC32BE_BAD_UTF8;
    if (c == 0xE0 && in[1] < 0xA0) return JIEBA__MBTOC32BE_BAD_UTF8;
    if (c == 0xED && in[1] >= 0xA0) return JIEBA__MBTOC32BE_BAD_UTF8;
    cp = ((c & 0x0F) << 12)
       | ((in[1] & 0x3F) << 6)
       | (in[2] & 0x3F);
    out[0]=0; out[1]=cp>>16; out[2]=cp>>8; out[3]=cp;
    *cvt_len = 3;
    return JIEBA__MBTOC32BE_SUCCESS;
  }

  if ((c & 0xF8) == 0xF0) {
    if (in_len < 4) return JIEBA__MBTOC32BE_NO_ENOUGH_CHARACTER;
    if ((in[1] & 0xc0) != 0x80) return JIEBA__MBTOC32BE_BAD_UTF8;
    if ((in[2] & 0xc0) != 0x80) return JIEBA__MBTOC32BE_BAD_UTF8;
    if ((in[3] & 0xc0) != 0x80) return JIEBA__MBTOC32BE_BAD_UTF8;
    if (c == 0xF0 && in[1] < 0x90) return JIEBA__MBTOC32BE_BAD_UTF8;
    if (c == 0xF4 && in[1] > 0x8F) return JIEBA__MBTOC32BE_BAD_UTF8;
    if (c > 0xF4) return JIEBA__MBTOC32BE_BAD_UTF8;
    cp = ((c & 0x07) << 18)
       | ((in[1] & 0x3F) << 12)
       | ((in[2] & 0x3F) << 6)
       | (in[3] & 0x3F);
    out[0]=cp>>24; out[1]=cp>>16; out[2]=cp>>8; out[3]=cp;
    *cvt_len = 4;
    return JIEBA__MBTOC32BE_SUCCESS;
  }

  return JIEBA__MBTOC32BE_BAD_UTF8;
}

static enum jieba__mbtoc32be_result
jieba__mbtoc32bestr(
    const unsigned char *in, size_t in_len, struct jieba__utf32be *outstr,
    size_t *out_len
) {
  size_t i = 0, cvt_len;
  enum jieba__mbtoc32be_result res;

  while (in_len != 0) {
    res = jieba__mbtoc32be(in, in_len, &outstr[i], &cvt_len);
    if (res != JIEBA__MBTOC32BE_SUCCESS) return res;
    in = &in[cvt_len]; in_len -= cvt_len; i += 1;
  }
  *out_len = i;
  return JIEBA__MBTOC32BE_SUCCESS;
}

static void jieba__init_hash_table(struct jieba__hash_table *table) {
  size_t estimated_word_count;
  table->count = table->size = 0;
  table->max_cell_per_bucket = JIEBA_HASH_TABLE_INITIAL_MAX_CELL_PER_BUCKET;
  table->first_node_pos = -1;
}

static size_t jieba__allocate_data_base_node2(
    size_t n_chinese_letter, struct jieba__data_base *data_base,
    size_t *data_base_node_first_free,struct jieba__data_base_node *nodes 
) {
  if (*data_base_node_first_free == (size_t)-1) return (size_t)-1;

  size_t new_pos = *data_base_node_first_free;
  *data_base_node_first_free = nodes[new_pos].next_node_pos;

  nodes[new_pos].next_node_pos = -1;
  nodes[new_pos].n_chinese_letter = n_chinese_letter;
  jieba__init_hash_table(&nodes[new_pos].table);

  return new_pos;
}

static size_t jieba__allocate_data_base_node(
    size_t n_chinese_letter, struct jieba__data_base *data_base
) {
  jieba__assert(n_chinese_letter <= JIEBA_MAX_WORD_LENGTH);
  jieba__assert(
      data_base->data_base_node_first_free == (size_t)-1 || (
        0 <= data_base->data_base_node_first_free &&
        data_base->data_base_node_first_free <=
          jieba__data_base_node_space_count()
      )
  );

  size_t res = jieba__allocate_data_base_node2(
      n_chinese_letter, data_base, &data_base->data_base_node_first_free,
      data_base->data_base_nodes
  );

  jieba__assert(
      data_base->data_base_node_first_free == (size_t)-1 || (
        0 <= data_base->data_base_node_first_free &&
        data_base->data_base_node_first_free <=
          jieba__data_base_node_space_count()
      )
  );
  return res;
}

static enum jieba_add_word_result
jieba__find_data_base_node3(
    size_t word_size, size_t *data_base_node_first_free,
    size_t *first_data_base_node_pos, struct jieba__data_base_node *nodes,
    struct jieba__data_base *data_base, size_t *data_base_node_pos
) {
  if (*first_data_base_node_pos == (size_t)-1 ||
      word_size > nodes[*first_data_base_node_pos].n_chinese_letter)
  {
    jieba__log(
        "allocate data base node due to no exists data base node or, no exists "
        "data base node whoes chinese character number is greater than the "
        "given one %zu",
        word_size
    );

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
      jieba__assert(
          nodes[pos].n_chinese_letter > nodes[last_pos].n_chinese_letter
      );

      if (word_size == nodes[pos].n_chinese_letter) {
        *data_base_node_pos = pos;
        return JIEBA_ADD_WORD_SUCCESS;
      }
      last_pos = pos;
      pos = nodes[pos].next_node_pos;
    }

    jieba__log(
        "allocate data base node due to no corresponding data base "
        "node whoes chinese letter number equal to the given one %zu",
        word_size
    );

    size_t new_pos = jieba__allocate_data_base_node(word_size, data_base);
    if (new_pos == -1) return JIEBA_ADD_WORD_FAIL_NOMEM;

    nodes[new_pos].next_node_pos = pos;
    nodes[last_pos].next_node_pos = new_pos;

    *data_base_node_pos = new_pos;
    return JIEBA_ADD_WORD_SUCCESS;
  }
}

static enum jieba_add_word_result
jieba__find_data_base_node2(
    size_t word_size, size_t *data_base_node_first_free,
    size_t *first_data_base_node_pos, struct jieba__data_base_node *nodes,
    struct jieba__data_base *data_base, size_t *data_base_node_pos
) {
  jieba__assert(
      *data_base_node_first_free == -1 || (
        0 <= *data_base_node_first_free &&
        *data_base_node_first_free <= jieba__data_base_node_space_count()
      )
  );
  jieba__assert(
      *first_data_base_node_pos == -1 || (
        0 <= *first_data_base_node_pos &&
        *first_data_base_node_pos <= jieba__data_base_node_space_count()
      )
  );

  enum jieba_add_word_result res;
  res = jieba__find_data_base_node3(
      word_size, &data_base->data_base_node_first_free,
      &data_base->first_data_base_node_pos, data_base->data_base_nodes,
      data_base, data_base_node_pos
  );
 
  jieba__assert(
      *data_base_node_first_free == -1 || (
        0 <= *data_base_node_first_free &&
        *data_base_node_first_free <= jieba__data_base_node_space_count()
      )
  );
  jieba__assert(
      *first_data_base_node_pos == -1 || (
        0 <= *first_data_base_node_pos &&
        *first_data_base_node_pos <= jieba__data_base_node_space_count()
      )
  );
  jieba__assert(
      *data_base_node_pos == -1 || (
        0 <= *first_data_base_node_pos &&
        *first_data_base_node_pos <= jieba__data_base_node_space_count()
      )
  );
  return res;
}

static enum jieba_add_word_result
jieba__find_data_base_node(
    size_t word_size, struct jieba__data_base *data_base,
    size_t *data_base_node_pos
) {
  return jieba__find_data_base_node2(
      word_size, &data_base->data_base_node_first_free,
      &data_base->first_data_base_node_pos, data_base->data_base_nodes,
      data_base, data_base_node_pos
  );
}

static uint64_t jieba__hash(void *str, size_t size) {
  return wyhash(str, size, 0, _wyp);
}

static uint64_t jieba__hash_u32bearr(struct jieba__utf32be *arr, size_t n) {
  return jieba__hash(arr, sizeof(struct jieba__utf32be) * n);
}

static size_t
jieba__allocate_hash_table_nodes2(
    size_t N, struct jieba__data_base *data_base,
    size_t *hash_table_node_first_free, struct jieba__hash_table_node *nodes
) {
  if (N == 0) return (size_t)-1;

  size_t new_pos = *hash_table_node_first_free;
  size_t last_pos = new_pos;
  size_t new_free_pos = nodes[new_pos].next_node_pos;

  jieba__assert(
      new_free_pos == (size_t)-1 || (
        0 <= new_free_pos &&
        new_free_pos <=
          jieba__hash_table_node_space_count(data_base->estimated_word_count)
      )
  );

  for (size_t i = 1; i < N; i++) {
    if (new_free_pos == (size_t)-1) return -1;
    last_pos = new_free_pos;
    new_free_pos = nodes[new_free_pos].next_node_pos;
    jieba__assert(
        new_free_pos == (size_t)-1 || (
          0 <= new_free_pos &&
          new_free_pos <=
            jieba__hash_table_node_space_count(data_base->estimated_word_count)
        )
    );
  }

  *hash_table_node_first_free = new_free_pos;
  nodes[last_pos].next_node_pos = (size_t)-1;

  size_t pos = new_pos;
  for (size_t i = 0; i < N; i++) {
    for (size_t j = 0; j < JIEBA_HASH_TABLE_NODE_BUCKET_NUMBER; j++) {
      nodes[pos].buckets[j].count = 0;
      nodes[pos].buckets[j].first_cell_pos = (size_t)-1;
    }
    pos = nodes[pos].next_node_pos;
  }
  return new_pos;
}

static size_t
jieba__allocate_hash_table_nodes(size_t N, struct jieba__data_base *data_base) {
  jieba__assert(
      data_base->hash_table_node_first_free == (size_t) -1 || (
        0 <= data_base->hash_table_node_first_free &&
        data_base->hash_table_node_first_free <=
          jieba__hash_table_node_space_count(data_base->estimated_word_count)
      )
  );

  size_t res;
  res = jieba__allocate_hash_table_nodes2(
      N, data_base, &data_base->hash_table_node_first_free,
      data_base->hash_table_nodes
  );

  jieba__assert(
      data_base->hash_table_node_first_free == (size_t) -1 || (
        0 <= data_base->hash_table_node_first_free &&
        data_base->hash_table_node_first_free <=
          jieba__hash_table_node_space_count(data_base->estimated_word_count)
      )
  );
  return res;
}

static void jieba__free_hash_table_node(
    size_t pos, struct jieba__data_base *data_base
) {
  struct jieba__hash_table_node *nodes = data_base->hash_table_nodes;
  nodes[pos].next_node_pos = data_base->hash_table_node_first_free;
  data_base->hash_table_node_first_free = pos;
}

static size_t
jieba__allocate_hash_table_cell2(
    struct jieba__data_base *data_base, struct jieba__hash_table_cell *cells,
    size_t *hash_table_cell_first_free
) {
  if (*hash_table_cell_first_free == (size_t)-1) return -1;

  size_t new_pos = *hash_table_cell_first_free;
  data_base->data_base_node_first_free = cells[new_pos].next_cell_pos;

  cells[new_pos].next_cell_pos = -1;
  cells[new_pos].hash = 0;
  cells[new_pos].string.count = 0;

  return new_pos;
}

static size_t
jieba__allocate_hash_table_cell(struct jieba__data_base *data_base) {
  jieba__assert(
      data_base->hash_table_cell_first_free == -1 || (
        0 <= data_base->hash_table_cell_first_free &&
        data_base->hash_table_cell_first_free <=
          jieba__hash_table_cell_space_count(data_base->estimated_word_count)
      )
  );

  size_t res =  jieba__allocate_hash_table_cell2(
      data_base, data_base->hash_table_cells,
      &data_base->hash_table_cell_first_free
  );

  jieba__assert(
      data_base->hash_table_cell_first_free == -1 || (
        0 <= data_base->hash_table_cell_first_free &&
        data_base->hash_table_cell_first_free <=
          jieba__hash_table_cell_space_count(data_base->estimated_word_count)
      )
  );

  return res;
}

static void jieba__free_hash_table_cell(
    size_t pos, struct jieba__data_base *data_base
) {
  struct jieba__hash_table_cell *cells = data_base->hash_table_cells;
  cells[pos].next_cell_pos = data_base->hash_table_cell_first_free;
  data_base->hash_table_cell_first_free = pos;
}

static int jieba__init_and_allocate_string(
    struct jieba__utf32be *contents, size_t contents_size,
    struct jieba__data_base *data_base, struct jieba__string *string
) {
  size_t new_size = contents_size + data_base->character_space_used;
  if (new_size < data_base->character_space_size)
    return -1;
  size_t strpos = data_base->character_space_used;
  data_base->character_space_used += contents_size;
  for (size_t i = 0; i < contents_size; i++)
    data_base->characterp[strpos + i] = contents[i];
  string->count = contents_size;
  string->first_character_pos = strpos;
  return 0;
}

/* primes */
/* How could we give users the JIEBA_HASH_TABLE_NODE_BUCKET_NUMBER macro if we use such a primes table? */
const size_t jieba__hash_table_sizes[] = {
  2039, 4093, 8179, 16369, 32749, 65497, 130987, 262007, 523997, 0
};

static void
jieba__hash_table_extend_coerce_insert_cell(
    size_t cell_pos, struct jieba__hash_table *table,
    struct jieba__hash_table_cell *cells, struct jieba__hash_table_node *nodes,
    struct jieba__data_base *data_base
) {
  jieba__assert(
      0 <= cell_pos &&
      cell_pos <=
        jieba__hash_table_cell_space_count(data_base->estimated_word_count)
  );

  size_t hash = cells[cell_pos].hash;
  size_t idx = hash % table->size;
  size_t node_num = idx / JIEBA_HASH_TABLE_NODE_BUCKET_NUMBER;
  size_t bucket_num = idx % JIEBA_HASH_TABLE_NODE_BUCKET_NUMBER;

  size_t node_pos = table->first_node_pos;
  jieba__assert(node_pos != (size_t)-1);
  while (node_num--) {
    node_pos = nodes[node_pos].next_node_pos;
    jieba__assert(node_pos != (size_t)-1);
  }

  nodes[node_pos].buckets[bucket_num].count += 1;
  cells[cell_pos].next_cell_pos =
    nodes[node_pos].buckets[bucket_num].first_cell_pos;
  nodes[node_pos].buckets[bucket_num].first_cell_pos = cell_pos;

  table->count += 1;
}

static void
jieba__hash_table_extend_coerce_insert_bucket(
    struct jieba__hash_table_bucket *bucket, struct jieba__hash_table *table,
    struct jieba__hash_table_cell *cells, struct jieba__hash_table_node *nodes,
    struct jieba__data_base *data_base
) {
  while (bucket->count-- > 0) {
    size_t pos = bucket->first_cell_pos;
    bucket->first_cell_pos = cells[pos].next_cell_pos;
    jieba__hash_table_extend_coerce_insert_cell(
        pos, table, cells, nodes, data_base
    );
  }
}

static int
jieba__hash_table_extend(
    struct jieba__hash_table *table, struct jieba__data_base *data_base,
    struct jieba__hash_table_node *nodes
) {
  jieba__log("hash table extended");

  size_t original_size = table->size;
  size_t size;
  int got_size = 0;
  for (size_t i = 0; jieba__hash_table_sizes[i + 1] != 0; i++) {
    if (original_size == jieba__hash_table_sizes[i]) {
      size = jieba__hash_table_sizes[i + 1];
      got_size = 1;
      break;
    }
  }
  if (!got_size) return -1;

  size_t node_number = (size + JIEBA_HASH_TABLE_NODE_BUCKET_NUMBER - 1)
    / JIEBA_HASH_TABLE_NODE_BUCKET_NUMBER;
  size_t new_pos = jieba__allocate_hash_table_nodes(node_number, data_base);
  if (new_pos == (size_t)-1) {
    jieba__log("hash table extending fail since no enough hash table nodes");
    return -1;
  }
  
  size_t old_node = table->first_node_pos;
  size_t old_size = table->size;
  table->count = 0;
  table->size = size;
  table->first_node_pos = new_pos;

  while (old_node != -1) {
    jieba__assert(
        0 <= old_node &&
        old_node <=
          jieba__hash_table_node_space_count(data_base->estimated_word_count)
    );
    for (size_t i = 0; i < JIEBA_HASH_TABLE_NODE_BUCKET_NUMBER; i++) {
      jieba__hash_table_extend_coerce_insert_bucket(
          &nodes[old_node].buckets[i], table, data_base->hash_table_cells,
          nodes, data_base
      );
    }
    size_t next_node_pos = nodes[old_node].next_node_pos;
    jieba__free_hash_table_node(old_node, data_base);
    old_node = next_node_pos;
  }
  return 0;
}

int jieba__ensure_hash_table_has_node(
    struct jieba__hash_table *table, struct jieba__data_base *data_base
) {
  if (table->size == 0) {
    table->count = 0;
    table->size = jieba__hash_table_sizes[0];
    size_t new_pos = jieba__allocate_hash_table_nodes(1, data_base);
    if (new_pos == (size_t)-1) return -1;
    table->first_node_pos = new_pos;
  }
  return 0;
}

enum jieba__bucket_find_or_add_cell_result {
  JIEBA__BUCKET_FIND_OR_ADD_CELL_SUCCESS,
  JIEBA__BUCKET_FIND_OR_ADD_CELL_FAIL_NOMEM,
  JIEBA__BUCKET_FIND_OR_ADD_CELL_FAIL_NEED_EXTEND
};

static enum jieba__bucket_find_or_add_cell_result
jieba__bucket_find_or_add_cell(
    struct jieba__utf32be *word, size_t word_size, uint64_t hash,
    struct jieba__data_base *data_base, size_t tried_times,
    struct jieba__hash_table *table, struct jieba__hash_table_bucket *bucket,
    struct jieba__utf32be *characterp, struct jieba__hash_table_cell *cells,
    int *does_change, size_t *cell_pos
) {
  if (bucket->count == 0) {
    size_t new_pos = jieba__allocate_hash_table_cell(data_base);
    if (new_pos == (size_t)-1) return JIEBA__BUCKET_FIND_OR_ADD_CELL_FAIL_NOMEM;

    cells[new_pos].hash = hash;
    int res = jieba__init_and_allocate_string(
        word, word_size, data_base, &cells[new_pos].string
    );
    if (res != 0) {
      jieba__free_hash_table_cell(new_pos, data_base);
      return JIEBA__BUCKET_FIND_OR_ADD_CELL_FAIL_NOMEM;
    }

    bucket->count = 1;
    bucket->first_cell_pos = new_pos;
    table->count += 1;
    *does_change = 1;
    *cell_pos = new_pos;
    return JIEBA__BUCKET_FIND_OR_ADD_CELL_SUCCESS;
  } else {
    size_t a_cell_pos = bucket->first_cell_pos;
    size_t last_cell_pos = -1;
    for (size_t i = 0; i < bucket->count; i++) {
      jieba__assert(
          0 <= a_cell_pos &&
          a_cell_pos <=
            jieba__hash_table_cell_space_count(data_base->estimated_word_count)
      );

      size_t character_pos = cells[a_cell_pos].string.first_character_pos;

      if (hash == cells[a_cell_pos].hash &&
          !memcmp(
            word, &characterp[character_pos],
            sizeof(struct jieba__utf32be) * word_size
          )
      ) {
        *does_change = 0;
        *cell_pos = a_cell_pos;
        return JIEBA__BUCKET_FIND_OR_ADD_CELL_SUCCESS;
      }

      last_cell_pos = a_cell_pos;
      a_cell_pos = cells[a_cell_pos].next_cell_pos;
    }

    jieba__assert(a_cell_pos == (size_t)-1);

    if (bucket->count == table->max_cell_per_bucket) {
      if (tried_times == 0)
        return JIEBA__BUCKET_FIND_OR_ADD_CELL_FAIL_NEED_EXTEND;
      else
        table->max_cell_per_bucket += 1;
    }

    size_t new_pos = jieba__allocate_hash_table_cell(data_base);
    if (new_pos == (size_t)-1) return JIEBA__BUCKET_FIND_OR_ADD_CELL_FAIL_NOMEM;

    cells[new_pos].hash = hash;
    int res = jieba__init_and_allocate_string(
        word, word_size, data_base, &cells[new_pos].string
    );
    if (res != 0) {
      jieba__free_hash_table_cell(new_pos, data_base);
      return JIEBA__BUCKET_FIND_OR_ADD_CELL_FAIL_NOMEM;
    }

    bucket->count += 1;
    cells[last_cell_pos].next_cell_pos = new_pos;
    table->count += 1;
    *does_change = 1;
    *cell_pos = new_pos;
    return JIEBA__BUCKET_FIND_OR_ADD_CELL_SUCCESS;
  }
}

static enum jieba_add_word_result
jieba__hash_table_find_or_add_cell(
    struct jieba__utf32be *word, size_t word_size, uint64_t hash,
    struct jieba__data_base *data_base, struct jieba__hash_table *table,
    int *does_change, size_t *cell_pos
) {
  if (jieba__ensure_hash_table_has_node(table, data_base) != 0)
    return JIEBA_ADD_WORD_FAIL_NOMEM;

  size_t tried_times = 0;
  int need_extend = 0;

  while (1) {
    size_t idx = hash % table->size;
    size_t node_num = idx / JIEBA_HASH_TABLE_NODE_BUCKET_NUMBER;
    size_t bucket_num = idx % JIEBA_HASH_TABLE_NODE_BUCKET_NUMBER;

    size_t node_pos = table->first_node_pos;
    jieba__assert(
        0 <= node_pos &&
        node_pos <=
          jieba__hash_table_node_space_count(data_base->estimated_word_count)
    );

    while (node_num--) {
      node_pos = data_base->hash_table_nodes[node_pos].next_node_pos;
      jieba__assert(
          0 <= node_pos &&
          node_pos <=
            jieba__hash_table_node_space_count(data_base->estimated_word_count)
      );
    }

    enum jieba__bucket_find_or_add_cell_result res;
    res = jieba__bucket_find_or_add_cell(
        word, word_size, hash, data_base, tried_times, table,
        &data_base->hash_table_nodes[node_pos].buckets[bucket_num],
        data_base->characterp, data_base->hash_table_cells, does_change,
        cell_pos
    );
    switch (res) {
    case JIEBA__BUCKET_FIND_OR_ADD_CELL_SUCCESS:
      return JIEBA_ADD_WORD_SUCCESS;
    case JIEBA__BUCKET_FIND_OR_ADD_CELL_FAIL_NEED_EXTEND:
      need_extend = 1;
      break;
    case JIEBA__BUCKET_FIND_OR_ADD_CELL_FAIL_NOMEM:
      return JIEBA_ADD_WORD_FAIL_NOMEM;
    }

    tried_times += 1;
    jieba__assert(tried_times <= 1);

    if (need_extend)
      jieba__hash_table_extend(table, data_base, data_base->hash_table_nodes);
  }
}

static enum jieba_add_word_result
jieba__add_word(
    unsigned char *restrict word, size_t word_size,
    struct jieba__data_base *restrict data_base
) {
  struct jieba__utf32be c32str_cache[128];
  size_t c32str_cache_size = 0;
  enum jieba_add_word_result res;

  if (word_size == 0 || word_size == 1) return JIEBA_ADD_WORD_SUCCESS;

  enum jieba__mbtoc32be_result mbtoc32be_res;
  mbtoc32be_res = jieba__mbtoc32bestr(
      word, word_size, c32str_cache, &c32str_cache_size
  );
  switch (mbtoc32be_res) {
  case JIEBA__MBTOC32BE_SUCCESS:
    break;
  case JIEBA__MBTOC32BE_BAD_UTF8:
    return JIEBA_ADD_WORD_BAD_UTF8;
  case JIEBA__MBTOC32BE_NO_ENOUGH_CHARACTER:
    return JIEBA_ADD_WORD_NO_ENOUGH_CHARACTER;
  }

  size_t data_base_node_pos;
  res = jieba__find_data_base_node(word_size, data_base, &data_base_node_pos);
  if (res != JIEBA_ADD_WORD_SUCCESS) return res;

  uint64_t hash = jieba__hash_u32bearr(c32str_cache, c32str_cache_size);
  size_t cell;
  int does_change;
  res = jieba__hash_table_find_or_add_cell(
      c32str_cache, c32str_cache_size, hash, data_base,
      &data_base->data_base_nodes[data_base_node_pos].table,
      &does_change, &cell
  );
  if (res != JIEBA_ADD_WORD_SUCCESS) return res;
  if (!does_change) return JIEBA_ADD_WORD_FAIL_ALREADY_EXISTS;

  return JIEBA_ADD_WORD_SUCCESS;
}

enum jieba_add_word_result
jieba_add_word(
    unsigned char *restrict word, size_t word_size,
    struct jieba_data_base *restrict data_base
) {
  return jieba__add_word(word, word_size, data_base->root);
}

static int jieba__hash_table_bucket_word_exists(
    struct jieba__utf32be *word, size_t word_size, uint64_t hash,
    struct jieba__hash_table_bucket *bucket,
    struct jieba__hash_table_cell *cells,
    struct jieba__utf32be *characterp, struct jieba__data_base *data_base
) {
  size_t bucket_count = bucket->count;
  size_t cell_pos = bucket->first_cell_pos;
  for (size_t i = 0; i < bucket_count; i++) {
    if (cells[cell_pos].hash == hash &&
        !memcmp(
          word,
          &characterp[cells[cell_pos].string.first_character_pos],
          sizeof(struct jieba__utf32be) * word_size
        )
    )
    return 1;
  }
  jieba__assert(cell_pos == (size_t)-1);
  return 0;
}

static int jieba__hash_table_word_exists(
    struct jieba__utf32be *word, size_t word_size, uint64_t hash,
    struct jieba__data_base *data_base, struct jieba__hash_table *table,
    struct jieba__hash_table_node *nodes
) {
  size_t idx = hash % table->size;
  size_t node_idx = idx / JIEBA_HASH_TABLE_NODE_BUCKET_NUMBER;
  size_t bucket_idx = idx % JIEBA_HASH_TABLE_NODE_BUCKET_NUMBER;

  size_t node_pos = table->first_node_pos;
  jieba__assert(
      0 <= node_pos &&
      node_pos <=
        jieba__hash_table_node_space_count(data_base->estimated_word_count)
  );

  while (node_idx--) {
    node_pos = nodes[node_pos].next_node_pos;
    jieba__assert(
        0 <= node_pos &&
        node_pos <=
          jieba__hash_table_node_space_count(data_base->estimated_word_count)
    );
  }

  return jieba__hash_table_bucket_word_exists(
      word, word_size, hash, &nodes[node_pos].buckets[bucket_idx],
      data_base->hash_table_cells, data_base->characterp, data_base
  );
}

static enum jieba_separate_result
jieba__separate2(
    const unsigned char *str, size_t strsize, size_t *word_size,
    struct jieba__data_base *data_base,
    struct jieba__data_base_node *nodes
) {
  struct jieba__utf32be c32strbuf[128];

  if (strsize == 0 || strsize == 1) {
    *word_size = strsize;
    return JIEBA_SEPARATE_SUCCESS;
  }

  size_t first_node_pos = data_base->first_data_base_node_pos;
  size_t max_word_size = nodes[first_node_pos].n_chinese_letter;

  jieba__assert(
      0 <= first_node_pos &&
      first_node_pos <= jieba__data_base_node_space_count()
  );

  size_t c32strbuf_count;
  enum jieba__mbtoc32be_result mbtoc32be_res;
  mbtoc32be_res = jieba__mbtoc32bestr(
      str, strsize < max_word_size ? strsize : max_word_size,
      c32strbuf, &c32strbuf_count
  );
  switch (mbtoc32be_res) {
  case JIEBA__MBTOC32BE_SUCCESS:
    break;
  case JIEBA__MBTOC32BE_BAD_UTF8:
    return JIEBA_SEPARATE_BAD_UTF8;
  case JIEBA__MBTOC32BE_NO_ENOUGH_CHARACTER:
    return JIEBA_SEPARATE_NO_ENOUGH_CHARACTER;
  }

  size_t last_node_n_chinese_letter = (size_t)-1;
  size_t node_pos = first_node_pos;
  while (node_pos != (size_t)-1) {
    jieba__assert(
        0 <= node_pos && node_pos <= jieba__data_base_node_space_count()
    );

    size_t word_count = nodes[node_pos].n_chinese_letter;

    jieba__assert(word_count < last_node_n_chinese_letter);

    if (strsize >= word_count) {
      uint64_t hash = jieba__hash_u32bearr(c32strbuf, word_count);

      int res = jieba__hash_table_word_exists(
          c32strbuf, word_count, hash, data_base, &nodes[node_pos].table,
          data_base->hash_table_nodes
      );
      if (res) {
        *word_size = word_count;
        return JIEBA_SEPARATE_SUCCESS;
      } 
    }

    last_node_n_chinese_letter = word_count;
    node_pos = nodes[node_pos].next_node_pos;
  }

  *word_size = 1;
  return JIEBA_SEPARATE_SUCCESS;
}

static enum jieba_separate_result
jieba__separate(
    const unsigned char *str, size_t strsize, size_t *word_size,
    struct jieba__data_base *data_base
) {
  return jieba__separate2(
      str, strsize, word_size, data_base, data_base->data_base_nodes
  );
}

enum jieba_separate_result
jieba_separate(
    const unsigned char *str, size_t strsize, size_t *word_size,
    struct jieba_data_base *data_base
) {
  return jieba__separate(str, strsize, word_size, data_base->root);
}
