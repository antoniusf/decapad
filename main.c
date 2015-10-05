#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <ft2build.h>
#include FT_FREETYPE_H
#include <SDL2/SDL.h>
#include "main.h"
#include "dynamic_array.h"

#define SETPIXEL(x, y, value) ( *(pixels+(x)+(y)*pitch) = (value) )

Uint64 ID_start;
Uint64 ID_end;
Uint64 author_ID;

int window_width = 640;
int window_height = 400;
int pitch;

Uint8 crc_0x97_table[256];

struct TextBuffer
{
    int cursor;
    int x;
    int line_y;
    int y_padding;
    int line;
    insertID activeInsertID;
    DynamicArray_char text;
    DynamicArray_ulong ID_table;
    DynamicArray_ulong author_table;
    DynamicArray_ulong charPos_table;
};
typedef struct TextBuffer TextBuffer;

typedef struct network_data
{
    int read_fifo;
    int write_fifo;
    int init_acknowledged;
    DynamicArray_ulong send_queue;
    DynamicArray_pointer send_queue_free_slots;
} network_data;

int
get_string_length (char *buffer) //*not* counting the 0 at the end
{
    int length = 0;
    while (1)
    {
        if ( *(buffer+length) == 0 )
        {
            break;
        }
        else
        {
            length++;
        }
    }
    return length;
}


void
string_concat (char **buffer1, char **buffer2) //result will be in buffer1, both buffers must be 0-terminated
{
    int length1, length2, i;
    length1 = get_string_length(*buffer1);
    length2 = get_string_length(*buffer2);
    *buffer1 = realloc(*buffer1, length1+length2+1);
    for (i=0; i<=length2; i++) //the <= is so we copy over the 0 byte from buffer2, too
    {
        *(*buffer1+length1+i) = *(*buffer2+i);
    }
    return;
}

int
string_compare (char *buffer1, char *buffer2, int length)
{
    int i;
    for (i=0; i<length; i++)
    {
        if (buffer1[i] != buffer2[i])
        {
            return 0;
        }
    }
    return 1;
}

long //so we can have all the unsigned ints *and* return -1 on not finding an insert
getInsertByID (TextInsertSet *set, Uint32 selfID)
{
    unsigned int i;
    for (i=0; i<set->used_length; i++)
    {
        if ((set->array)[i].selfID == selfID)
        {
            return i;
        }
    }
    return -1;
}

void
crc8_0x97_fill_table ( void )
{
    Uint8 poly = 0x97; //actually, we will read in 0x97 as the reversed reciprocal polynomial of what it actually is, but this will have no effect on error correction strength (https://en.wikipedia.org/wiki/Mathematics_of_cyclic_redundancy_checks#Reciprocal_polynomials)
    
    int i, j;
    for (i=0; i<256; i++)
    {
        Uint8 indexbyte = i;
        for (j=0; j<8; j++)
        {
            if ( (indexbyte >> 7) == 1 )
            {
                indexbyte <<= 1;
                indexbyte ^= poly;
            }
            else
            {
                indexbyte <<= 1;
            }
        }
        crc_0x97_table[i] = indexbyte;
    }
}

Uint8
crc8_0x97 ( void *void_data, size_t length )
//Implemented following http://www.ross.net/crc/download/crc_v3.txt
//NOTE: switch to 0xA6 for bit lengths > 119 (polynomial data from http://ieeexplore.ieee.org/xpls/icp.jsp?arnumber=1311885#table3 )
{


    Uint8 *data = (Uint8 *) void_data;
    Uint8 result = 0xFF;
    size_t k;
    for (k=0; k<length; k++)
    {
        result = crc_0x97_table[data[k] ^ result];
    }

    /* even more unreadable alternative
    while ( length-- > 0)
    {
        result = crc_0x97_table[(*data++)^result];
    }*/

    return result;
}

char
check_crc8_0x97 ( void *data, size_t length ) //checksum must be appended to data
{
    Uint8 crc_value = crc8_0x97(data, length);
    if ( crc_value == 0 )
    {
        return 0;
    }
    else
    {
        return -1;
    }
}

void
base32_enc_crc ( Uint8 crc_value, char *output )
{
    output[0] = (crc_value>>4) + 65;
    output[1] = (crc_value&0x0F) + 65;
}

Uint8
base32_dec_crc ( char *data )
{
    Uint8 crc_value = ( (data[0]-65)<<4 ) + (data[1]-65);
    return crc_value;
}

void
base85_enc_uint32 ( Uint32 input, char *output )
{
    output[0] =  42 + ( input % 85 );
    input /= 85;

    output[1] =  42 + ( input % 85 );
    input /= 85;

    output[2] =  42 + ( input % 85 );
    input /= 85;

    output[3] =  42 + ( input % 85 );
    input /= 85;

    output[4] =  42 + ( input % 85 );

}

void
append_base85_enc_uint32 ( Uint32 input, DynamicArray_char *output )
{
    addToDynamicArray_char( output, 42 + ( input % 85 ) );
    input /= 85;

    addToDynamicArray_char( output, 42 + ( input % 85 ) );
    input /= 85;

    addToDynamicArray_char( output, 42 + ( input % 85 ) );
    input /= 85;

    addToDynamicArray_char( output, 42 + ( input % 85 ) );
    input /= 85;

    addToDynamicArray_char( output, 42 + ( input % 85 ) );

}

Uint32
base85_dec_uint32 ( char *input )
{
    Uint32 decoded = 0;

    decoded += input[4] - 42;
    decoded *= 85;
    
    decoded += input[3] - 42;
    decoded *= 85;

    decoded += input[2] - 42;
    decoded *= 85;

    decoded += input[1] - 42;
    decoded *= 85;

    decoded += input[0] - 42;

    return decoded;
}

void
serialize_insert ( TextInsert *insert, DynamicArray_char *output )
{
    int start_length = output->used_length;

    addToDynamicArray_char( output, 73 );// I for insert

    append_base85_enc_uint32( insert->selfID, output );
    append_base85_enc_uint32( insert->parentID, output );
    append_base85_enc_uint32( insert->author, output );
    append_base85_enc_uint32( (insert->charPos << 16) + insert->length + (insert->lock << 31), output );

    //copy over the text
    int i;
    for (i=0; i < insert->length; i++)
    {
        addToDynamicArray_char( output, (insert->content)[i] );
    }

    Uint8 crc_value = crc8_0x97( output->array+start_length, output->used_length - start_length );
    addToDynamicArray_char(output, (crc_value>>4) + 65);
    addToDynamicArray_char(output, (crc_value&0x0F) + 65);
    //printf("serialize crc: %i\n", crc_value);
}

Sint64
unserialize_insert ( TextInsertSet *set, char *string, size_t maxlength, size_t *return_insert_length)
{
    Uint32 mix = base85_dec_uint32( string+1+5+5+5 );
    int insert_content_length = mix&0xFFFF;
    size_t total_length = 1+5+5+5+5+insert_content_length+2;

    if (total_length > maxlength)
    {
        printf("Insert unserialization failed: invalid length.\n");
        return -1;
    }

    Uint8 checksum = ( (string[total_length-2] - 65) << 4) + (string[total_length-1] - 65);
    //printf("unserialize crc: %i\n", checksum);
    string[total_length-2] = checksum;
    if ( check_crc8_0x97( string, total_length-1 ) == -1 )
    {
        printf("Insert unserialization failed: invalid checksum.\n");
        return -1;
    }

    TextInsert insert;

    insert.selfID = base85_dec_uint32( string+1 );

    insert.parentID = base85_dec_uint32( string+1+5 );
    insert.author = base85_dec_uint32(string+1+5+5);

    insert.charPos = (mix>>16)&0xFFFF;
    insert.length = insert_content_length;
    insert.lock = (mix>>31)&1;

    insert.content = malloc(insert.length);
    int i;
    for (i=0; i<insert.length; i++)
    {
        insert.content[i] = string[i+1+5+5+5+5];
    }

    long old_insert_pos = getInsertByID(set, insert.selfID);
    if (old_insert_pos != -1)
    {
        TextInsert *old_insert = (set->array)+old_insert_pos;
        if ( insert.length >= old_insert->length ) //assume this is an update for now, actually we would need authenticity checking
        {
            int i;
            for (i=0; i<(old_insert->length); i++)
            {
                if (old_insert->content[i] == 127)
                {
                    insert.content[i] = 127;
                }
            }
            *old_insert = insert;
        }
        else
        {
            int i;
            for (i=0; i<insert.length; i++)
            {
                if (insert.content[i] == 127)
                {
                    old_insert->content[i] = 127;
                }
            }
        }
    }

    else
    {
        addToTextInsertSet(set, insert);
    }
    
    *return_insert_length = total_length;
    return insert.selfID;
}

int
send_data ( char *data, size_t length, network_data *network ) //data must have five unused leading characters. Those will be used for length encoding.
{
    if (length <= 5)
    {
        printf("You have nothing to send!\n");
        return -1;
    }

    base85_enc_uint32(length-5, data);

    if ( write(network->write_fifo, data, length) < 0 )
    {
        printf("Sending data failed.\n");
        return -1;
    }

    return 0;
}

void
send_init (network_data *network)
{
    int i;
    for (i=0; i<10; i++)
    {
        char init_message[] = "[len]Init/urid/enid..";
        base85_enc_uint32(ID_end+1, &init_message[9]);
        base85_enc_uint32(20048, &init_message[14]);
        Uint8 crc_value = crc8_0x97(&init_message[5], 4+5+5);
        base32_enc_crc(crc_value, &init_message[19]);
        if (send_data(init_message, 21, network) == 0)
        {
            return;
        }
    }
    printf("Sending init failed for unknown reasons\n");
}

int
send_insert ( TextInsert *insert, network_data *network )
{
    DynamicArray_char message, messagelength;
    initDynamicArray_char(&message);
    initDynamicArray_char(&messagelength);

    addStringToDynamicArray_char(&message, "*****data");
    serialize_insert(insert, &message);
    send_data(message.array, message.used_length, network);

    //enqueue insert pointer if not alread in queue
    int i;
    int enqueued = 0;
    for (i=0; i < network->send_queue.used_length; i++)
    {
        if ( network->send_queue.array[i] == insert->selfID )
        {
            printf("Insert already in queue, do not enqueue again.\n");
            enqueued = 1;
            break;
        }
    }

    if (!enqueued)
    {
        printf("Enqueueing insert %lu at %lu.\n", insert->selfID, insert);
        if (network->send_queue_free_slots.used_length == 0)
        {
            addToDynamicArray_ulong(&network->send_queue, insert->selfID);
        }
        else
        {
            //pop a free slot and fill it with the insert ID
            insertID *free_slot = network->send_queue_free_slots.array[network->send_queue_free_slots.used_length - 1];
            network->send_queue_free_slots.used_length--;
            *free_slot = insert->selfID;
        }
    }

    free(message.array);
    return 0;
}

void
serialize_document ( TextInsertSet *set, DynamicArray_char *output )
{
    int i;
    for (i=0; i < set->used_length; i++)
    {
        serialize_insert( (set->array) + i, output );
    }
}

int
unserialize_document ( TextInsertSet *set, char *string, size_t length )
{
    size_t insert_length;
    while ( length > 0 )
    {
        if ( unserialize_insert( set, string, length, &insert_length ) == -1 )
        {
            return -1;
        }
        string += insert_length;
        length -= insert_length;
    }
    return 0;
}

void
draw_cursor (int x, int y, Uint32 *pixels, FT_Face fontface)
{
    int i;
    int height = (int) fontface->size->metrics.height >> 6;
    for ( i=0; i<height; i++ )
    {
        if ( (x >= 0) && (y-i+height/8 >= 0) && (x < window_width) && (y < window_height) )
        {
            SETPIXEL(x, y-i+height/8, 0xFFFFFFFF);
        }
    }
}

int
number_of_linewraps (char *text, int left_padding, FT_Face fontface)
{
    int i = 0;
    char character = text[0]; //for the first check
    int linewraps = 0;
    int x = left_padding;
    int word_length = 0;

    for (i = 0; (character != 10 && character != 0); i++)
    {
        character = text[i];
        if (character == 32)
        {
            x += word_length;
            word_length = 0;
            FT_Load_Char(fontface, character, FT_LOAD_RENDER);
            x += fontface->glyph->advance.x / 64;

            if (x > window_width)
            {
                x = left_padding;
                linewraps++;
            }
        }
        else
        {
            FT_Load_Char(fontface, character, FT_LOAD_DEFAULT);
            word_length += fontface->glyph->advance.x / 64;

            if ( word_length+x > window_width )
            {
                x = left_padding;
                linewraps++;
            }
        }
    }

    return linewraps;
}

char *
seek_to_line (char *text, int line)
{
    if (line == 0)
    {
        return text;
    }

    int newlines = 0;
    while (*text++)
    {
        if (*(text-1) == 10)
        {
            newlines++;

            if (newlines == line)
            {
                break;
            }
        }
    }
    
    if (*(text-1) == 0) //we ran out text, line number too high
    {
        return NULL;
    }
    return text;
}


void
draw_text (TextBuffer *buffer, char *text, Uint32 *pixels, char show_cursor, FT_Face fontface, int set_cursor_x, int set_cursor_y)
{
    char character;
    int x = buffer->x;
    int y = buffer->line_y + buffer->y_padding;
    int zero_x = x;
    int error;
    int height = (int) fontface->size->metrics.height / 64;
    y += height;
    int current_line = 0;

    int i = 0;
    while ((character=text[i]))
    {
        if (current_line >= buffer->line)
        {
            int linewrap = 0;

            if (character == 10) {
                linewrap = 1;

                if ( show_cursor == 1 && i == buffer->cursor )
                {
                    draw_cursor(x, y, pixels, fontface);
                }
            }

            else
            {
                if (character == 32)
                {
                    char *lookahead = text+i;
                    int lookahead_x = x;
                    error = FT_Load_Char(fontface, *lookahead, FT_LOAD_DEFAULT);
                    lookahead_x += fontface->glyph->advance.x / 64;
                    lookahead++;

                    while ( (*lookahead != 0) && (*lookahead != 32) && (*lookahead != 10))
                    {
                        error = FT_Load_Char(fontface, *lookahead, FT_LOAD_RENDER);
                        lookahead_x += fontface->glyph->advance.x / 64;
                        lookahead++;

                        if (lookahead_x > window_width)
                        {
                            linewrap = 1;
                            break;
                        }
                    }
                    if (lookahead_x > window_width)
                    {
                        linewrap = 1;
                    }
                }

                error = FT_Load_Char( fontface, character, FT_LOAD_RENDER );

                FT_Bitmap bitmap = fontface->glyph->bitmap;

                if ( bitmap.pixel_mode != FT_PIXEL_MODE_GRAY )
                {
                    printf("Not the right Freetype glyph bitmap pixel mode! Sorry, it ran on my computer...\n");
                    return;
                }

                //copy glyph into pixel array

                int target_x, target_y;
                target_x = x + fontface->glyph->bitmap_left;
                target_y = y - fontface->glyph->bitmap_top;

                if ( bitmap.pitch < 0 )
                {
                    printf("Freetype glyph bitmap pitch is negative. Surely wasn't expecting that...\n");
                    return;
                }

                int row;
                int col;
                unsigned char *glyhp_buffer = (unsigned char *) (bitmap.buffer);
                for ( row = 0; row < bitmap.rows; row++ )
                {
                    for ( col = 0; col < bitmap.width; col++ )
                    {
                        if ( (target_y+row < window_height) && (target_y+row > 0) && (target_x+col < window_width) && (target_x+col > 0) )
                        {
                            Uint32 color = *( glyhp_buffer + row * (bitmap.pitch) + col );
                            SETPIXEL(target_x+col, target_y+row, (color<<24)+(color<<16)+(color<<8)+255);
                        }
                    }
                }

                int advance = fontface->glyph->advance.x >> 6;

                //draw underline
                Uint32 underline_color = (0xFF<<16) + 0xFF;
                if (buffer->author_table.array[i] != author_ID)
                {
                    underline_color += 0xFF<<24;
                }

                for (col = 0; col < advance; col++)
                {
                    if ( (y+2 < window_height) && (y+1 >= 0) && (target_x+col < window_width) && (target_x+col >= 0) )
                    {
                        SETPIXEL(target_x+col, y+1, underline_color);
                        SETPIXEL(target_x+col, y+2, underline_color);
                    }
                }

                if ( show_cursor == 1 && i == buffer->cursor )
                {
                    draw_cursor(x, y, pixels, fontface);
                }

                x += advance;

                if ( (set_cursor_x >= 0) && (set_cursor_x <= x-(advance>>1)) && (set_cursor_y <= y) )
                {
                    buffer->cursor = i;
                    set_cursor_x = set_cursor_y = -1; //click handled
                }

            }

            if (linewrap)
            {
                if ( (set_cursor_x >= 0) && (set_cursor_y <= y) ) //end of line click cursor positioning is not handled otherwise
                {
                    buffer->cursor = i;
                    set_cursor_x = set_cursor_y = -1;
                }

                x = zero_x;
                y += height;

                if (y-height > window_height)
                {
                    break;
                }
            }
        }

        else
        {
            if (character == 10)
            {
                current_line += 1;
            }
        }

        i++;
    }

    if (set_cursor_x >= 0)
    {
        buffer->cursor = i;
    }

    if ( show_cursor == 1 && i == buffer->cursor )
    {
        draw_cursor(x, y, pixels, fontface);
    }
}

void
quicksort (unsigned long *array, unsigned long min, unsigned long max)
{
    if (min < max)
    {
        unsigned long pivot = min + (max-min)/2;
        unsigned long pivot_value = *(array+pivot);
        unsigned long lower_index = min;
        unsigned long temp_value = *(array+max);
        unsigned long i;
        *(array+max) = pivot_value;
        *(array+pivot) = temp_value;
        for (i = min; i < max; i++)
        {
            if ( *(array+i) < pivot_value )
            {
                if (i == lower_index)
                {
                    lower_index++;
                }
                else
                {
                    temp_value = *(array+lower_index);
                    *(array+lower_index) = *(array+i);
                    *(array+i) = temp_value;
                    lower_index++;
                }
            }
        }
        *(array+max) = *(array+lower_index);
        *(array+lower_index) = pivot_value;

        if (lower_index > 0)
        {
            quicksort(array, min, lower_index-1);
        }
        quicksort(array, lower_index+1, max);
    }
}


void
render_text (TextInsertSet *set, Uint32 parentID, Uint8 charPos, TextBuffer *buffer)//buffer.text needs to be initialized; buffer.ID_table (needs to be initialized too) stores the ID of the insertion mark which contains each character, buffer.charPos_table stores the index of the character within its insertion mark. TODO: this is terribly inefficient with memory. fix sometime.
{
    int i;
    DynamicArray_ulong IDs;
    initDynamicArray_ulong(&IDs);

    for (i=0; i<set->used_length; i++)
    {
        TextInsert insert = *(set->array+i);
        if (insert.parentID == parentID && insert.charPos == charPos)
        {
            addToDynamicArray_ulong(&IDs, insert.selfID);
        }
    }

    //sort all the inserts
    if (IDs.used_length > 0)
    {
        quicksort(IDs.array, 0, IDs.used_length-1);
    }


    //render them in order

    for (i=0; i<IDs.used_length; i++)
    {
        //get insert
        TextInsert *current_insert = set->array+getInsertByID(set, IDs.array[i]);

        //draw it
        int pos;
        for (pos=0; pos<current_insert->length; pos++)
        {
            //render the inserts before this character position
            render_text(set, current_insert->selfID, pos, buffer);

            //stick the appropriate letter on the back
            if (current_insert->content[pos] != 127)
            {
                addToDynamicArray_char(&buffer->text, current_insert->content[pos]);
                addToDynamicArray_ulong(&buffer->ID_table, current_insert->selfID);
                addToDynamicArray_ulong(&buffer->author_table, current_insert->author);
                addToDynamicArray_ulong(&buffer->charPos_table, pos);
            }
        }

        render_text(set, current_insert->selfID, current_insert->length, buffer);
    }

    free(IDs.array);
}

void
update_buffer (TextInsertSet *set, TextBuffer *buffer)
{
    //update buffer
    buffer->text.used_length = 0;
    buffer->ID_table.used_length = 0;
    buffer->author_table.used_length = 0;
    buffer->charPos_table.used_length = 0;
    render_text(set, 0, 0, buffer);
    addToDynamicArray_char(&buffer->text, 0);
}

int
is_a_ancestor_of_b (TextInsertSet *set, TextInsert *a, TextInsert *b)
{
    if (b->parentID == a->selfID)
    {
        return 1;
    }
    else
    {
        if (b->parentID != 0)
        {
            long b_parent_pos = getInsertByID(set, b->parentID);
            if (b_parent_pos > 0)
            {
                TextInsert *b_parent = (set->array)+b_parent_pos;
                return is_a_ancestor_of_b(set, a, b_parent);
            }
        }
    }
    return 0;
}

int
insert_letter (TextInsertSet *set, TextBuffer *buffer, char letter, network_data *network)
{

    if (buffer->activeInsertID)
    {
        TextInsert *insert = set->array + getInsertByID(set, buffer->activeInsertID);
        insert->content = realloc(insert->content, insert->length+1);
        insert->content[insert->length] = letter;
        insert->length++;

        send_insert(insert, network);
    }

    else
    {

        int pos = buffer->cursor;
        Uint32 insert_ID;
        Uint8 charPos;
        if ( pos == 0 )
        {
            if (set->used_length == 0)
            {
                insert_ID = 0;
                charPos = 0;
            }
            else
            {
                insert_ID = buffer->ID_table.array[pos];
                charPos = buffer->charPos_table.array[pos];
            }
        }

        else
        {
            if (pos == buffer->ID_table.used_length )
            {
                insert_ID = buffer->ID_table.array[pos-1];
                charPos = buffer->charPos_table.array[pos-1] + 1;
            }

            else
            {

                insertID insert_1_ID = buffer->ID_table.array[pos-1];
                insertID insert_2_ID = buffer->ID_table.array[pos];
                TextInsert *insert_1 = set->array + getInsertByID(set, insert_1_ID);
                TextInsert *insert_2 = set->array + getInsertByID(set, insert_2_ID);

                if ( is_a_ancestor_of_b (set, insert_1, insert_2) )
                {
                    insert_ID = insert_2->selfID;
                    charPos = buffer->charPos_table.array[pos];
                }
                else
                {
                    insert_ID = insert_1->selfID;
                    charPos = buffer->charPos_table.array[pos-1] + 1;
                }
            }
        }


        TextInsert new_insert;
        new_insert.selfID = ID_start++;
        if (ID_end < ID_start)
        {
            printf("Ran out of IDs!!!\n");
            exit(1);
        }
        new_insert.parentID = insert_ID;
        new_insert.author = author_ID;
        new_insert.charPos = charPos;
        new_insert.lock = 0;
        new_insert.length = 1;
        new_insert.content = malloc(1);
        new_insert.content[0] = letter;

        addToTextInsertSet(set, new_insert);
        TextInsert *new_insert_pointer = set->array + set->used_length-1; //NOTE: not thread safe!
        buffer->activeInsertID = new_insert.selfID;
        
        send_insert(new_insert_pointer, network);

    }

    buffer->cursor++;
    return 0;
}

int
delete_letter ( TextInsertSet *set, TextBuffer *buffer, network_data *network )
{
    if (buffer->cursor < buffer->ID_table.used_length)
    {
        insertID insert_ID = buffer->ID_table.array[buffer->cursor];
        TextInsert *insert = set->array + getInsertByID(set, insert_ID);
        Uint8 inner_pos = buffer->charPos_table.array[buffer->cursor];

        insert->content[inner_pos] = 127;

        send_insert(insert, network);
    }
    return 0;
}

int main (void)
{

    int error;

    //CRC table computation
    crc8_0x97_fill_table();

    //fifo setup

    //this ID range is only for the first client...
    author_ID = ID_start = 1;
    ID_end = 1024;

    network_data network;
    network.init_acknowledged = 1;
    initDynamicArray_ulong(&network.send_queue);
    initDynamicArray_pointer(&network.send_queue_free_slots);

    int read_channel;
    network.write_fifo = -1;

    error = mkfifo( "/tmp/deca_channel_1", 0777 );
    if (error == -1)
    {
        if ( errno == EEXIST )
        {
            //read channel is channel 2
            error = mkfifo( "/tmp/deca_channel_2", 0777 );
            read_channel = 2;

            if ( error == -1 )
            {
                printf("FIFO creation failed.\n");
                return 1;
            }

            network.write_fifo = open("/tmp/deca_channel_1", O_WRONLY);
            char init_request[] = "[len]inrq";
            send_data(init_request, 9, &network);
            network.read_fifo = open("/tmp/deca_channel_2", O_RDONLY | O_NONBLOCK);
        }

        else
        {
            printf("FIFO creation failed.\n");
            return 1;
        }
    }

    else
    {
        read_channel = 1;
        network.read_fifo = open("/tmp/deca_channel_1", O_RDONLY | O_NONBLOCK);
    }
    
    //Freetype setup
    
    FT_Library ft_library;
    error = FT_Init_FreeType( &ft_library);
    if (error)
    {
        printf("FreeType initialization failed.\n");
        return 1;
    }

    FT_Face fontface;
    error = FT_New_Face( ft_library, "ClearSans-Regular.ttf", 0, &fontface);
    if ( error )
    {
        printf("Font could not be loaded.\n");
        return 1;
    }

    error = FT_Set_Pixel_Sizes(fontface, 0, 24);

    int line_height = (int) fontface->size->metrics.height / 64;

    //SDL setup

    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        printf("SDL initialization failed: %s", SDL_GetError());
        return 1;
    }

    SDL_Window *window = SDL_CreateWindow("Decapad", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, window_width, window_height, SDL_WINDOW_RESIZABLE);
    if (window == NULL) {
        SDL_Quit();
        printf("Window creation failed: %s", SDL_GetError());
        return 1;
    }

    SDL_Renderer *renderer = SDL_CreateRenderer(window, -1, 0);
    if (renderer == NULL) {
        SDL_DestroyWindow(window);
        SDL_Quit();
        printf("Renderer creation failed %s", SDL_GetError());
        return 1;
    }

    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderClear(renderer);
    SDL_RenderPresent(renderer);

    SDL_Texture *texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_STREAMING, window_width, window_height);

    Uint32 *pixels;


    //Logic
    TextInsertSet set;
    initTextInsertSet(&set);

    TextBuffer buffer;
    buffer.cursor = 0;
    buffer.activeInsertID = 0;
    buffer.x = 10;
    buffer.line_y = 0;
    buffer.y_padding = 10;
    buffer.line = 0;

    initDynamicArray_char(&buffer.text);
    initDynamicArray_ulong(&buffer.ID_table);
    initDynamicArray_ulong(&buffer.author_table);
    initDynamicArray_ulong(&buffer.charPos_table);

    render_text(&set, 0, 0, &buffer);
    addToDynamicArray_char(&buffer.text, 0);


    //main loop
    int quit=0;
    //int i;
    //int x, y;
    int click_x, click_y;
    click_x = click_y = -1;


    char unsigned blink_timer = 0;
    int resend_timer = 0;

    while (!quit)
    {
        SDL_Event e;
        while (SDL_PollEvent(&e))
        {
            switch (e.type)
            {
                case SDL_QUIT:
                    quit = 1;
                    break;

                case SDL_TEXTINPUT:
                {
                    insert_letter(&set, &buffer, e.text.text[0], &network);
                    blink_timer = 0;

                    update_buffer(&set, &buffer);
                } break;

                case SDL_KEYDOWN:
                {
                    switch (e.key.keysym.sym)
                    {
                        case SDLK_RETURN:
                        {
                            insert_letter(&set, &buffer, 10, &network);
                        } break;

                        case SDLK_BACKSPACE:
                        {
                            if (buffer.cursor > 0)
                            {
                                buffer.cursor--;
                                delete_letter (&set, &buffer, &network);
                            }
                        } break;

                        case SDLK_RIGHT:
                        {
                            if (buffer.cursor < buffer.text.used_length-1)
                            {
                                buffer.cursor++;
                                buffer.activeInsertID = 0;
                            }
                        } break;

                        case SDLK_LEFT:
                        {
                            if (buffer.cursor > 0)
                            {
                                buffer.cursor--;
                                buffer.activeInsertID = 0;
                            }
                        } break;

                        case SDLK_ESCAPE:
                        {
                            quit = 1;
                        } break;
                    }

                    blink_timer = 0;

                    update_buffer(&set, &buffer);
                    //printf("Rendered text: %s\n # Inserts: %i\n", output_buffer.array, set.used_length);

                } break;

                case SDL_MOUSEBUTTONDOWN:
                {
                    click_x = e.button.x;
                    click_y = e.button.y;
                    blink_timer = 0;
                    buffer.activeInsertID = 0;
                } break;

                case SDL_MOUSEWHEEL:
                {
                    if (e.wheel.y < 0)
                    {
                        buffer.line_y -= line_height;
                    }
                    else
                    {
                        buffer.line_y += line_height;
                    }

                    if (buffer.line_y > 0)
                    {
                        if (buffer.line == 0)
                        {
                            buffer.line_y = 0;
                        }

                        else
                        {
                            char *previous_line = seek_to_line(buffer.text.array, buffer.line-1);
                            int offset = (number_of_linewraps(previous_line, buffer.x, fontface)+1) * line_height;
                            buffer.line--;
                            buffer.line_y -= offset;
                        }
                    }

                    else
                    {
                        char *current_line = seek_to_line(buffer.text.array, buffer.line);
                        int current_line_height = (number_of_linewraps(current_line, buffer.x, fontface)+1) * line_height;
                        if (-buffer.line_y > current_line_height)
                        {
                            char *next_line = seek_to_line(buffer.text.array, buffer.line+1);
                            if (next_line)
                            {
                                buffer.line++;
                                buffer.line_y += current_line_height;
                            }
                            else
                            {
                                buffer.line_y = -current_line_height;
                            }
                        }
                    }

                } break;

                case SDL_WINDOWEVENT:
                {
                    switch (e.window.event)
                    {
                        case SDL_WINDOWEVENT_RESIZED:
                        {
                            window_width = e.window.data1;
                            window_height = e.window.data2;

                            SDL_DestroyTexture(texture);
                            texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_STREAMING, window_width, window_height);
                        } break;
                        
                        default:
                            break;
                    }
                } break;

                default:
                    break;
            }
        }

        int byte_pitch;
        SDL_LockTexture(texture, NULL, &pixels, &byte_pitch);
        pitch = byte_pitch/4;

        //clear pixel buffer
        {
            Uint32 *pointer = pixels;
            while(pointer < pixels+window_width*window_height)
            {
                *pointer = 0;
                pointer++;
            }
        }

        if (blink_timer < 128)
        {
            draw_text(&buffer, buffer.text.array, pixels, 1, fontface, click_x, click_y);
        }
        else
        {
            draw_text(&buffer, buffer.text.array, pixels, 0, fontface, click_x, click_y);
        }
        click_x = click_y = -1;

        //SDL_UpdateTexture(texture, NULL, pixels, window_width*sizeof(Uint32));
        SDL_UnlockTexture(texture);
        SDL_RenderClear(renderer);
        SDL_RenderCopy(renderer, texture, NULL, NULL);
        SDL_RenderPresent(renderer);
        
        blink_timer+=9;
        resend_timer += 30;
        SDL_Delay(30);

        //check FIFO
        {
            char *base85_length = malloc(5);
            if (read(network.read_fifo, base85_length, 5) == 5)
            {
                Uint32 length = base85_dec_uint32(base85_length);
                printf("received message of length %i\n", length);

                char *input = malloc(length);
                read(network.read_fifo, input, length);

                if (string_compare(input, "Init", 4))
                {
                    Uint8 crc_value = base32_dec_crc(input+14);
                    input[14] = crc_value;
                    if (check_crc8_0x97(input, 4+5+5+1) == 0)
                    {
                        author_ID = ID_start = base85_dec_uint32(input+4);
                        ID_end = base85_dec_uint32(input+9);
                        //ack init
                        char ack_message[] = "[len]acki";
                        send_data(ack_message, 9, &network);
                    }
                }

                else if (string_compare(input, "inrq", 4))
                {
                    network.write_fifo = open("/tmp/deca_channel_2", O_WRONLY);
                    network.init_acknowledged = 0;
                    send_init(&network);
                }

                else if (string_compare(input, "data", 4))
                {
                    size_t insert_length;
                    Sint64 insert_ID = unserialize_insert(&set, input+4, length-4, &insert_length);
                    update_buffer(&set, &buffer);

                    if(insert_ID >= 0)
                    {
                        //ACK
                        char ack[14] = "*****ack *****";
                        base85_enc_uint32(insert_ID, ack+9);
                        send_data(ack, 14, &network);
                    }
                }

                else if (string_compare(input, "ack ", 4))
                {
                    if (length == 4+5)
                    {
                        int i;
                        Uint32 ack_id = base85_dec_uint32(input+4);
                        for (i=0; i < network.send_queue.used_length; i++)
                        {
                            if ( network.send_queue.array[i] == ack_id )
                            {
                                //remove insert from the queue and mark its spot as free
                                addToDynamicArray_pointer(&network.send_queue_free_slots, network.send_queue.array + i);
                                network.send_queue.array[i] = 0;
                                break;
                            }
                        }
                    }
                }
                else if (string_compare(input, "acki", 4))
                {
                    network.init_acknowledged = 1;
                }


                free(input);
            }
            free(base85_length);

            //check whether we have to take back the cursor after a deletion
            if (buffer.cursor > buffer.text.used_length-1)//NOTE: if removing zero termination of buffer text, this -1 MUST be removed! (else, SEGFAULT....)
            {
                buffer.cursor = buffer.text.used_length-1;
            }
        }

        //resend un-ACKed inserts
        if (resend_timer >= 1000) //every second
        {
            resend_timer = 0;

            TextInsert *resend_insert;
            int i;
            for(i=0; i<network.send_queue.used_length; i++)
            {
                if (network.send_queue.array[i] != 0)
                {
                    resend_insert = set.array + getInsertByID(&set, network.send_queue.array[i]);
                    printf("Resending insert %lu at %lu.\n", resend_insert->selfID, resend_insert);
                    send_insert(resend_insert, &network);
                }
            }

            if (network.init_acknowledged == 0)
            {
                send_init(&network);
            }
        }
    }

    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();

    free(buffer.text.array);
    free(buffer.ID_table.array);
    free(buffer.charPos_table.array);

    int i;
    for (i=0; i<set.used_length; i++)
    {
        free( set.array[i].content );
    }

    free(set.array);

    FT_Done_FreeType(ft_library);

    if ( read_channel == 1)
    {
        remove( "/tmp/deca_channel_1" );
    }

    else
    {
        remove( "/tmp/deca_channel_2" );
    }

    return 0;
}
