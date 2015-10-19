#ifndef DYNAMIC_ARRAY_H
#define DYNAMIC_ARRAY_H
#include "main.h"

typedef struct DynamicArray_ulong
{
    unsigned long *array;
    long length;
    long allocated_length;
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
    long length;
    long allocated_length;
} DynamicArray_uint32;

int
initDynamicArray_uint32 ( DynamicArray_uint32 *array );

int
addToDynamicArray_uint32 ( DynamicArray_uint32 *array, Uint32 item );

int
insertIntoDynamicArray_uint32 ( DynamicArray_uint32 *array, Uint32 item, long int position);

void deleteFromDynamicArray_uint32 ( DynamicArray_uint32 *array, long int position );

int
concatDynamicArrays_uint32 ( DynamicArray_uint32 *array1, DynamicArray_uint32 *array2 );

typedef struct DynamicArray_char
{
    char *array;
    long length;
    long allocated_length;
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
    long length;
    long allocated_length;
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
    long length;
    long allocated_length;
} DynamicArray_pointer;

int
initDynamicArray_pointer ( DynamicArray_pointer *array );

int
addToDynamicArray_pointer ( DynamicArray_pointer *array, void *item );

#endif
