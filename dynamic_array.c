#include <stdlib.h>
#include <stdio.h>
#include "main.h"
#include "dynamic_array.h"

//unsigned long

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

//Uint32

int
initDynamicArray_uint32 ( DynamicArray_uint32 *array )
{
    array->used_length = 0;
    array->array = malloc(4*sizeof(Uint32));
    if (!array->array)
    {
        printf("Error in initDynamicArray_uint32: malloc didn't work.\n");
        return -1;
    }
    array->allocated_length = 4;
    return 0;
}

int
addToDynamicArray_uint32 ( DynamicArray_uint32 *array, Uint32 item )
{
    if (array->used_length == array->allocated_length)
    {
        array->array = realloc(array->array, (array->allocated_length)*2*sizeof(Uint32));
        if ( array->array == NULL )
        {
            printf("Error in addToDynamicArray_uint32: realloc didn't work.\n");
            return -1;
        }
        array->allocated_length *= 2;
    }
    array->array[array->used_length] = item;
    array->used_length++;
    return 0;
}

//char

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
addStringToDynamicArray_char ( DynamicArray_char *array, char *string )
{
    int i = 0;
    while ( string[i] )
    {
        if ( addToDynamicArray_char(array, string[i]) < 0 )
        {
            return -1;
        }
        i++;
    }
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

#ifdef MAIN_H

int
initTextInsertSet ( TextInsertSet *array )
{
    array->used_length = 0;
    array->array = malloc(4*sizeof(TextInsert));
    if (!array->array)
    {
        printf("Error in initTextInsertSet: malloc didn't work.\n");
        return -1;
    }
    array->allocated_length = 4;
    return 0;
}

int
addToTextInsertSet ( TextInsertSet *array, TextInsert item )
{
    if (array->used_length == array->allocated_length)
    {
        array->array = realloc(array->array, (array->allocated_length)*2*sizeof(TextInsert));
        if ( array->array == NULL )
        {
            printf("Error in addToTextInsertSet: realloc didn't work.\n");
            return -1;
        }
        array->allocated_length *= 2;
    }
    array->array[array->used_length] = item;
    array->used_length++;
    return 0;
}

int
concatTextInsertSets ( TextInsertSet *array1, TextInsertSet *array2 ) //result will be in array1
{
    unsigned int new_length = array1->used_length + array2->used_length;
    if (new_length > array1->allocated_length)
    {
        array1->array = realloc(array1->array, new_length);
        if (!array1->array)
        {
            printf("Error in concatTextInsertSet: realloc didn't work.\n");
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

#endif

//void *

int
initDynamicArray_pointer ( DynamicArray_pointer *array )
{
    array->used_length = 0;
    array->array = malloc(4*sizeof(void *));
    if (!array->array)
    {
        printf("Error in initDynamicArray_pointer: malloc didn't work.\n");
        return -1;
    }
    array->allocated_length = 4;
    return 0;
}

int
addToDynamicArray_pointer ( DynamicArray_pointer *array, void *item )
{
    if (array->used_length == array->allocated_length)
    {
        array->array = realloc(array->array, (array->allocated_length)*2*sizeof(void *));
        if ( array->array == NULL )
        {
            printf("Error in addToDynamicArray_pointer: realloc didn't work.\n");
            return -1;
        }
        array->allocated_length *= 2;
    }
    array->array[array->used_length] = item;
    array->used_length++;
    return 0;
}
