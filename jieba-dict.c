#include "jieba.h"
#include "jieba-dict.h"
#include <stdlib.h>
#include <stdio.h>

static const char * const jieba_dict[] = {
#include "dict.h"
};

static const size_t jieba_dict_len[] = {
#include "dict-len.h"
};

#ifndef JIEBA_DICT_MEM
# define JIEBA_DICT_MEM (37302416)
#endif

static unsigned char jieba_dict_mem[JIEBA_DICT_MEM];

static struct jieba_data_base jieba_dict_data_base;

void init_jieba_dict(void) {
  size_t dict_len = sizeof(jieba_dict) / sizeof(jieba_dict[0]);

  enum jieba_init_result res;
  res = jieba_init_data_base(
      &jieba_dict_data_base, jieba_dict_mem, JIEBA_DICT_MEM,
      sizeof(jieba_dict) / sizeof(jieba_dict[0]), NULL
  );
  if (res != JIEBA_INIT_SUCCESS) {
    fprintf(stderr, "jieba-dict initialization fail");
    exit(-1);
  }

  for (size_t i = 0; i < dict_len; i++) {
    enum jieba_add_word_result res;
    res = jieba_add_word(
        (unsigned char *)jieba_dict[i], jieba_dict_len[i],
        &jieba_dict_data_base
    );
    switch (res) {
    case JIEBA_ADD_WORD_SUCCESS:
      break;
    /* following cases, except already exists, is not considered show to users */
    case JIEBA_ADD_WORD_FAIL_NOMEM:
      fprintf(stderr, "jieba-dict initialization fail, no mem\n");
      exit(-1);
    case JIEBA_ADD_WORD_FAIL_TOO_LONG:
      fprintf(stderr, "jieba-dict initialization fail, word too long\n");
      exit(-1);
    case JIEBA_ADD_WORD_BAD_UTF8:
      fprintf(stderr, "jieba-dict initialization fail, bad utf 8\n");
      exit(-1);
    case JIEBA_ADD_WORD_NO_ENOUGH_CHARACTER:
      fprintf(stderr, "jieba-dict initialization fail, no enough chars\n");
      exit(-1);
    case JIEBA_ADD_WORD_FAIL_ALREADY_EXISTS:
      fprintf(
          stderr, "WARNING: word %s presents multiple times in the dictionary\n",
          jieba_dict[i]
      );
      break;
    }
  }
}

enum jieba_separate_result
jieba_dict_separate(
    const unsigned char *str, size_t strsize, size_t *word_size
) {
  return jieba_separate(str, strsize, word_size, &jieba_dict_data_base);
}
