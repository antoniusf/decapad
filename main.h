#ifndef MAIN_H
#define MAIN_H
#include <stdint.h>

typedef uint8_t Uint8;
typedef uint16_t Uint16;
typedef uint32_t Uint32;
typedef uint64_t Uint64;

//typedef int8_t s8;
//typedef int16_t s16;
//typedef int32_t s32;
//typedef int64_t s64;

typedef Uint32 insertID;

struct TextInsert
{
    Uint32 selfID;
    Uint32 parentID;
    Uint32 author;
    Uint8 charPos;
    char lock;
    Uint8 length;
    char *content;
};
typedef struct TextInsert TextInsert;

#endif
