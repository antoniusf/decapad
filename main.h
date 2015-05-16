#ifndef MAIN_H
#define MAIN_H

typedef unsigned long insertID;

struct TextBuffer
{
    unsigned short length;
    unsigned short cursor;
    char *buffer;
    insertID activeInsertID;

};
typedef struct TextBuffer TextBuffer;

struct TextInsert
{
    unsigned long selfID;
    unsigned long parentID;
    unsigned short charPos;
    char lock;
    unsigned short length;
    char *content;
};
typedef struct TextInsert TextInsert;

#endif
