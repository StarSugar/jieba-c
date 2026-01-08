#include "jieba-dict.h"
#include <stdio.h>
#include <stdlib.h>

char data[] = "新华社北京1月6日电 1月6日，中国共产党中央委员会致电祝贺老挝人民革命党第十二次全国代表大会召开。贺电说：\n"
"老挝人民革命党是老挝人民和老挝社会主义事业的坚强领导核心。老挝党十一大以来，以通伦总书记为首的老挝党中央致力于加强党的自身建设、巩固党的领导地位，团结带领老挝各族人民，积极探索符合自身国情的社会主义发展道路，推动党和国家各项事业取得一系列重要发展成就。我们对此感到由衷高兴并予以积极评价。\n"
"老挝党十二大是在老挝社会主义事业发展进程中具有里程碑意义的重要会议。大会将审议通过党的第三部政治纲领等重要政治文件，对未来一段时期老挝党和国家各项事业发展作出战略规划和具体部署。相信老挝人民将在老挝党坚强领导下，胜利实现大会确立的各项目标任务，将老挝社会主义事业推向新的发展阶段。\n"
"中老两国都是共产党领导的社会主义国家。中国党和政府始终从战略高度和长远角度看待和把握中老两党两国关系。新形势下，中方愿同老方一道，以两党两国最高领导人重要共识为根本遵循，加强战略沟通，深化交往合作，扎实推进中老命运共同体建设，推动中老全面战略合作伙伴关系持续健康稳定发展，造福两国和两国人民，为促进世界和平、发展与进步事业作出新的积极贡献。";

void print_word(char *str, size_t n) {
  for (size_t i = 0; i < n; i++) {
    putchar(str[i]);
  }
  puts("");
}

int main() {
  init_jieba_dict();

  char *str = data;
  size_t size = sizeof(data);
  while (*str != '\0') {
    enum jieba_separate_result res;
    size_t word_size;
    res = jieba_dict_separate(str, size, &word_size);

    switch (res) {
    case JIEBA_SEPARATE_SUCCESS:
      break;
    case JIEBA_SEPARATE_NO_ENOUGH_CHARACTER:
      printf("no enough character\n");
      exit(1);
    case JIEBA_SEPARATE_BAD_UTF8:
      printf("bad utf 8\n");
      exit(1);
    }

    printf("%zu ", word_size);
    print_word(str, word_size);
    str += word_size;
    size -= word_size;
  }
}
