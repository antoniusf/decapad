#ifndef DYNAMIC_ARRAY_H
#define DYNAMIC_ARRAY_H
#include "main.h"

typedef struct DynamicArray_ulong
{
    unsigned long *array;
    unsigned int length;
    unsigned int allocated_length;
} DynamicArray_ulong;

int
initDynamicArray_ulong ( DynamicArray_ulong *array );

int
addToDynamicArray_ulong ( DynamicArray_ulong *array, unsigned long item );

int
concatDynamicArrays_ulong ( DynamicArray_ulong *array1, DynamicArray_ulong *array2 ); //result will be in array1

typedef struct DynamicArray_uint32
{
    Uint32 *array;
    unsigned int length;
    unsigned int allocated_length;
} DynamicArray_uint32;

int
initDynamicArray_uint32 ( DynamicArray_uint32 *array );

int
addToDynamicArray_uint32 ( DynamicArray_uint32 *array, Uint32 item );

typedef struct DynamicArray_char
{
    char *array;
    unsigned int length;
    unsigned int allocated_length;
} DynamicArray_char;

int
initDynamicArray_char ( DynamicArray_char *array );

int
addToDynamicArray_char ( DynamicArray_char *array, char item );

int
addStringToDynamicArray_char ( DynamicArray_char *array, char *string );

/*int //this segfaults
concatDynamicArrays_char ( DynamicArray_char *array1, DynamicArray_char *array2 ); //result will be in array1 */

#ifdef MAIN_H

typedef struct TextInsertSet
{
    TextInsert *array;
    unsigned int length;
    unsigned int allocated_length;
} TextInsertSet;

int
initTextInsertSet ( TextInsertSet *array );

int
addToTextInsertSet ( TextInsertSet *array, TextInsert item );

int
concatTextInsertSets ( TextInsertSet *array1, TextInsertSet *array2 ); //result will be in array1

#endif

typedef struct DynamicArray_pointer
{
    void **array;
    unsigned int length;
    unsigned int allocated_length;
} DynamicArray_pointer;

int
initDynamicArray_pointer ( DynamicArray_pointer *array );

int
addToDynamicArray_pointer ( DynamicArray_pointer *array, void *item );

#endif
