#include "jieba.h"
#include <stdio.h>

static const char * const jieba_dict[] = {
#include "dict.h"
};

int main() {
  printf("%zu", jieba_estimate_memory_size(sizeof(jieba_dict)/sizeof(jieba_dict[0])));
}
