#ifndef JIEBA_DICT_H_
#define JIEBA_DICT_H_

#include "jieba.h"

void init_jieba_dict(void);

enum jieba_separate_result
jieba_dict_separate(
    const unsigned char *str, size_t strsize, size_t *word_size
);

#endif
