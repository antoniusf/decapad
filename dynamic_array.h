#ifndef DYNAMIC_ARRAY_H
#define DYNAMIC_ARRAY_H

typedef struct DynamicArray_ulong
{
    unsigned long *array;
    unsigned int used_length;
    unsigned int allocated_length;
} DynamicArray_ulong;

int
initDynamicArray_ulong ( DynamicArray_ulong *array );

int
addToDynamicArray_ulong ( DynamicArray_ulong *array, unsigned long item );

int
concatDynamicArrays_ulong ( DynamicArray_ulong *array1, DynamicArray_ulong *array2 ); //result will be in array1

typedef struct DynamicArray_char
{
    char *array;
    unsigned int used_length;
    unsigned int allocated_length;
} DynamicArray_char;

int
initDynamicArray_char ( DynamicArray_char *array );

int
addToDynamicArray_char ( DynamicArray_char *array, char item );

int
concatDynamicArrays_char ( DynamicArray_char *array1, DynamicArray_char *array2 ); //result will be in array1
#endif
