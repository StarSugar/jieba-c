#! /bin/sh

cc -shared jieba.c -O3 -o jieba.so
cc -shared jieba.c jieba-dict.c -O3 -o jieba-dict.so
