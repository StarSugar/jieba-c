#ifndef JIEBA_H_
#define JIEBA_H_

#include <stddef.h>

struct jieba__data_base;

struct jieba_data_base {
  char *whole_memory;
  size_t whole_memory_size;
  struct jieba__data_base *root;
};

enum jieba_init_result {
  JIEBA_INIT_SUCCESS,
  JIEBA_INIT_FAIL_NOMEM
};

enum jieba_init_result jieba_init_data_base(
    struct jieba_data_base *restrict data_base, void *restrict whole_memory,
    size_t whole_memory_size, size_t estimated_word_count, size_t *required
);

size_t jieba_estimate_memory_size(size_t estimated_word_count);

enum jieba_add_word_result {
  JIEBA_ADD_WORD_SUCCESS,
  JIEBA_ADD_WORD_FAIL_TOO_LONG,
  JIEBA_ADD_WORD_FAIL_NOMEM,
  JIEBA_ADD_WORD_FAIL_ALREADY_EXISTS,
  JIEBA_ADD_WORD_NO_ENOUGH_CHARACTER,
  JIEBA_ADD_WORD_BAD_UTF8
};

enum jieba_add_word_result
jieba_add_word(
    unsigned char *restrict word, size_t word_size,
    struct jieba_data_base *restrict data_base
);

enum jieba_separate_result {
  JIEBA_SEPARATE_SUCCESS,
  JIEBA_SEPARATE_NO_ENOUGH_CHARACTER,
  JIEBA_SEPARATE_BAD_UTF8
};

enum jieba_separate_result
jieba_separate(
    const unsigned char *str, size_t strsize, size_t *word_size,
    struct jieba_data_base *data_base
);

#endif /* JIEBA_H_ */
