#!/bin/sh

cc -dynamiclib jieba.c -O3 -o jieba.dylib
cc -dynamiclib jieba.c jieba-dict.c -O3 -o jieba-dict.dylib
