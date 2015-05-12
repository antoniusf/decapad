#include <stdlib.h>
#include <stdio.h>
#include "dynamic_array.h"

int
initDynamicArray_ulong ( DynamicArray_ulong *array )
{
    array->used_length = 0;
    array->array = malloc(4*sizeof(unsigned long));
    if (!array->array)
    {
        printf("Error in initDynamicArray_ulong: malloc didn't work.\n");
        return -1;
    }
    array->allocated_length = 4;
    return 0;
}

int
addToDynamicArray_ulong ( DynamicArray_ulong *array, unsigned long item )
{
    if (array->used_length == array->allocated_length)
    {
        array->array = realloc(array->array, (array->allocated_length)*2*sizeof(unsigned long));
        if ( array->array == NULL )
        {
            printf("Error in addToDynamicArray_ulong: realloc didn't work.\n");
            return -1;
        }
        array->allocated_length *= 2;
    }
    array->array[array->used_length] = item;
    array->used_length++;
    return 0;
}

int
concatDynamicArrays_ulong ( DynamicArray_ulong *array1, DynamicArray_ulong *array2 ) //result will be in array1
{
    unsigned int new_length = array1->used_length + array2->used_length;
    if (new_length > array1->allocated_length)
    {
        array1->array = realloc(array1->array, new_length);
        if (!array1->array)
        {
            printf("Error in concatDynamicArrays_ulong: realloc didn't work.\n");
            return -1;
        }
    }
    
    unsigned int i;
    for (i=0; i < array2->used_length; i++)
    {
        array1[array1->used_length + i] = array2[i];
    }
    array1->used_length = new_length;
    return 0;
}

int
initDynamicArray_char ( DynamicArray_char *array )
{
    array->used_length = 0;
    array->array = malloc(4*sizeof(char));
    if (!array->array)
    {
        printf("Error in initDynamicArray_char: malloc didn't work.\n");
        return -1;
    }
    array->allocated_length = 4;
    return 0;
}

int
addToDynamicArray_char ( DynamicArray_char *array, char item )
{
    if (array->used_length == array->allocated_length)
    {
        array->array = realloc(array->array, (array->allocated_length)*2*sizeof(char));
        if ( array->array == NULL )
        {
            printf("Error in addToDynamicArray_char: realloc didn't work.\n");
            return -1;
        }
        array->allocated_length *= 2;
    }
    array->array[array->used_length] = item;
    array->used_length++;
    return 0;
}

int
concatDynamicArrays_char ( DynamicArray_char *array1, DynamicArray_char *array2 ) //result will be in array1
{
    unsigned int new_length = array1->used_length + array2->used_length;
    if (new_length > array1->allocated_length)
    {
        array1->array = realloc(array1->array, new_length);
        if (!array1->array)
        {
            printf("Error in concatDynamicArrays_char: realloc didn't work.\n");
            return -1;
        }
    }
    
    unsigned int i;
    for (i=0; i < array2->used_length; i++)
    {
        array1[array1->used_length + i] = array2[i];
    }
    array1->used_length = new_length;
    return 0;
}
