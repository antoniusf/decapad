#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/un.h>
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

enum PROGRAM_STATES
{
    STATE_LOGIN,
    STATE_PAD
} program_state;

struct TextBuffer
{
    int cursor;
    int ahead_cursor;
    Sint64 update_hint_cursor_ID; //TODO: Maybe migrate to a c++ compiler so we can have default arguments instead of this
    Sint16 update_hint_cursor_charPos;
    int x;
    int line_y;
    int y_padding;
    int line;
    insertID activeInsertID;
    DynamicArray_uint32 text;
    DynamicArray_uint32 ID_table;
    DynamicArray_uint32 author_table;
    DynamicArray_uint32 charPos_table;
};
typedef struct TextBuffer TextBuffer;

typedef struct network_data
{
    int own_socket;
    int wait_for_init;
    DynamicArray_uint32 send_queue;
    int send_now;
} network_data;

Sint32
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


/*void
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
}*/

int
string_compare (char *buffer1, int length1, char *buffer2, int length2)
{
    int i;
    int length = fmin(length1, length2);
    for (i=0; i<length; i++)
    {
        if (buffer1[i] != buffer2[i])
        {
            return 0;
        }
    }
    return 1;
}

void
DynamicArray_uint32_to_DynamicArray_char ( DynamicArray_uint32 *in_text, DynamicArray_char *out_text )
{
    int i;
    for (i=0; i<in_text->length; i++)
    {
        addToDynamicArray_char(out_text, (char) in_text->array[i]);
    }
}

void
add_string_to_utf32_text ( DynamicArray_uint32 *text, char *string)
{
    int i;
    for (i=0; string[i]; i++)
    {
        addToDynamicArray_uint32(text, string[i]);
    }
}

long //so we can have all the unsigned ints *and* return -1 on not finding an insert
getInsertByID (TextInsertSet *set, Uint32 selfID)
{
    unsigned int i;
    for (i=0; i<set->length; i++)
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
check_base32_crc8_0x97 ( char *data, size_t total_length )
//NOTE: data *will* be modified to replace the second-to-last char with the decoded crc value for checking.
{
    Uint8 crc_value = ( (data[total_length-2]-65)<<4 ) + (data[total_length-1]-65);
    data[total_length-2] = crc_value;
    return check_crc8_0x97(data, total_length-1);
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

    int i;
    for (i=0; i<5; i++)
    {
        if ( (input[i] < 42) || (input[i] > 126) )
        {
            printf("The Base85 Uint32 decoder encountered an invalid input string.\n");
            return 0;//TODO: error handling
        }
    }

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

    append_base85_enc_uint32( insert->selfID, output );
    append_base85_enc_uint32( insert->parentID, output );
    append_base85_enc_uint32( insert->author, output );
    append_base85_enc_uint32( (insert->charPos << 16) + insert->length + (insert->lock << 31), output );

    //copy over the text
    int i;
    for (i=0; i < insert->length; i++)
    {
        append_base85_enc_uint32( (insert->content)[i], output);
    }
}

Sint64
unserialize_insert ( TextInsertSet *set, char *string, size_t maxlength, size_t *return_insert_length)
{

    size_t total_length = 0;

    if (maxlength < 20)
    {
        printf("Serialized insert string is definitely too short, cannot even read the header!\n");
        return -1;
    }

    TextInsert insert;

    insert.selfID = base85_dec_uint32(string);
    string += 5;
    total_length += 5;

    insert.parentID = base85_dec_uint32(string);
    string += 5;
    total_length += 5;

    insert.author = base85_dec_uint32(string);
    string += 5;
    total_length += 5;

    Uint32 mix = base85_dec_uint32(string);
    insert.charPos = (mix>>16)&0xFFFF;
    insert.length = mix&0xFFFF;
    insert.lock = (mix>>31)&1;
    string += 5;
    total_length += 5;

    total_length += insert.length*5;

    if (maxlength < total_length)
    {
        printf("Serialized insert string is too short for insert content.\n");
        return -1;
    }

    insert.content = malloc(insert.length*sizeof(Uint32));
    int i;
    for (i=0; i<insert.length; i++)
    {
        insert.content[i] = base85_dec_uint32(string+i*5);
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
        if (insert.selfID >= ID_start && insert.selfID <= ID_end)
        {
            printf("Insert unserialization failed due to invalid insert ID: The insert would be newly created, but its ID lies within our ID range.\n");
            return -1;
        }
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

    if ( send(network->own_socket, data, length, 0) < 0 )
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

void
send_init_request (network_data *network)
{
    char init_request[] = "[len]inrq";
    send_data(init_request, 9, network);
}

void
enqueue_insert ( TextInsert *insert, network_data *network )
{
    //check if it is already enqueued
    int i;
    int enqueued = 0;
    for (i=0; i < network->send_queue.length; i++)
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
        printf("Enqueueing insert %lu at %lu.\n", (long unsigned int) insert->selfID, (long unsigned int) insert);

        addToDynamicArray_uint32(&network->send_queue, insert->selfID);
    }
}

int
send_insert ( TextInsert *insert, network_data *network )
{
    DynamicArray_char message, messagelength;
    initDynamicArray_char(&message);
    initDynamicArray_char(&messagelength);

    addStringToDynamicArray_char(&message, "[len]data");
    serialize_insert(insert, &message);

    Uint8 crc_value = crc8_0x97(&message.array[5], message.length-5);
    addStringToDynamicArray_char(&message, "..");
    base32_enc_crc(crc_value, &message.array[message.length-2]);

    send_data(message.array, message.length, network);

    enqueue_insert(insert, network);

    free(message.array);
    return 0;
}


void
serialize_document ( TextInsertSet *set, DynamicArray_char *output )
{
    int i;
    for (i=0; i < set->length; i++)
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
save_document ( TextInsertSet *set, DynamicArray_uint32 *pad_with )
{
    if (pad_with->length > 0)
    {
        DynamicArray_char filename, output;
        initDynamicArray_char(&filename); //defer free(filename.array);
        initDynamicArray_char(&output); //defer free(output.array);

        addStringToDynamicArray_char(&filename, "pads/");
        DynamicArray_uint32_to_DynamicArray_char(pad_with, &filename);
        addToDynamicArray_char(&filename, 0);

        FILE *savefile = fopen(filename.array, "w");

        append_base85_enc_uint32(author_ID, &output);
        append_base85_enc_uint32(ID_start, &output);
        append_base85_enc_uint32(ID_end, &output);
        serialize_document(set, &output);

        fwrite(output.array, 1, output.length, savefile);
        fclose(savefile);
        free(filename.array);
        free(output.array);
    }
}

void
load_document ( TextInsertSet *set, DynamicArray_uint32 *pad_with )
{
    if (pad_with->length > 0)
    {
        DynamicArray_char filename, content;
        initDynamicArray_char(&filename); //defer free(filename.array);
        initDynamicArray_char(&content); //defer free(content.array);

        addStringToDynamicArray_char(&filename, "pads/");
        DynamicArray_uint32_to_DynamicArray_char(pad_with, &filename);
        addToDynamicArray_char(&filename, 0);

        FILE *savefile = fopen(filename.array, "r");
        if (savefile)
        {
            fseek(savefile, 0, SEEK_END);
            long length = ftell(savefile);
            fseek(savefile, 0, SEEK_SET);

            content.array = realloc(content.array, length);
            content.allocated_length = length;
            content.length = length;

            fread(content.array, 1, length, savefile);

            fclose(savefile);

            author_ID = base85_dec_uint32(content.array);
            ID_start = base85_dec_uint32(content.array+5);
            ID_end = base85_dec_uint32(content.array+10);
            unserialize_document(set, content.array+15, length-15);

            free(filename.array);
            free(content.array);
        }
    }
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
number_of_linewraps (DynamicArray_uint32 *text, int offset, int left_padding, FT_Face fontface)
{
    int i;
    Uint32 character;
    int linewraps = 0;
    int x = left_padding;
    int word_length = 0;

    for (i = offset; (i < text->length) && (text->array[i] != 10); i++)
    {
        character = text->array[i];
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

int
seek_to_line (DynamicArray_uint32 *text, int line)
{
    if (line == 0)
    {
        return 0;
    }

    int newlines = 0;
    int i;
    for (i=0; i<text->length; i++)
    {
        if (text->array[i] == 10)
        {
            newlines++;

            if (newlines == line)
            {
                i++;
                break;
            }
        }
    }
    
    return i;
}

int get_line_nr (DynamicArray_uint32 *text, int cursor)
{
    int line_nr = 0;
    int i;
    for (i=0; i<cursor; i++)
    {
        if (text->array[i] == 10)
        {
            line_nr++;
        }
    }

    return line_nr;
}

void
draw_text (TextBuffer *buffer, Uint32 *text, Uint32 *pixels, char show_cursor, FT_Face fontface, int set_cursor_x, int set_cursor_y)
{
    Uint32 character;
    int x = buffer->x;
    int y = buffer->line_y + buffer->y_padding;
    int zero_x = x;
    int error;
    int height = (int) fontface->size->metrics.height / 64;
    y += height;

    FT_UInt glyph_index, previous_glyph_index = 0;

    int i = seek_to_line(&buffer->text, buffer->line);
    for (; i < buffer->text.length; i++)
    {
        character = text[i];
        int linewrap = 0;

        if (character == 10) {
            linewrap = 1;

            if ( show_cursor == 1 && i == buffer->ahead_cursor )
            {
                draw_cursor(x, y, pixels, fontface);
            }
        }

        else
        {
            if (character == 32)
            {
                int lk_i = i;
                int lookahead_x = x;
                error = FT_Load_Char(fontface, text[lk_i], FT_LOAD_DEFAULT);
                lookahead_x += fontface->glyph->advance.x / 64;

                lk_i++;

                for (; (lk_i < buffer->text.length) && (text[lk_i] != 32) && (text[lk_i] != 10); lk_i++)
                {
                    error = FT_Load_Char(fontface, text[lk_i], FT_LOAD_RENDER);
                    lookahead_x += fontface->glyph->advance.x / 64;

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

            glyph_index = FT_Get_Char_Index(fontface, character);
            if (previous_glyph_index)
            {
                FT_Vector kerning;
                error = FT_Get_Kerning(fontface, previous_glyph_index, glyph_index, FT_KERNING_DEFAULT, &kerning);
                x += kerning.x / 64;
            }
            previous_glyph_index = glyph_index;
            error = FT_Load_Glyph ( fontface, glyph_index, FT_LOAD_RENDER );

            FT_Bitmap bitmap = fontface->glyph->bitmap;
            int advance = fontface->glyph->advance.x >> 6;

            if ( bitmap.pixel_mode != FT_PIXEL_MODE_GRAY )
            {
                printf("Not the right Freetype glyph bitmap pixel mode! Sorry, it ran on my computer...\n");
                return;
            }

            if ( bitmap.pitch < 0 )
            {
                printf("Freetype glyph bitmap pitch is negative. Surely wasn't expecting that...\n");
                return;
            }


            int target_x, target_y;
            target_x = x + fontface->glyph->bitmap_left;
            target_y = y - fontface->glyph->bitmap_top;

            int row;
            int col;

            //draw underline
            if (buffer->author_table.length)
            {
                Uint32 underline_color = 0xFF;
                if (buffer->author_table.array[i] != author_ID)
                {
                    //underline_color += (91 << 24) + (67 << 16) + (10 << 8);
                    underline_color += (151 << 24) + (113 << 16) + (24 << 8);
                }
                else
                {
                    //underline_color += (16 << 24) + (75 << 16) + (106 << 8);
                    underline_color += (33 << 24) + (126 << 16) + (174 << 8);
                }

                for (col = 0; col < advance; col++)
                {
                    if ( (y+2 < window_height) && (y+1 >= 0) && (target_x+col < window_width) && (target_x+col >= 0) )
                    {
                        SETPIXEL(x+col, y+1, underline_color);
                        SETPIXEL(x+col, y+2, underline_color);
                    }
                }
            }



            //copy glyph into pixel array

            unsigned char *glyhp_buffer = (unsigned char *) (bitmap.buffer);
            for ( row = 0; row < bitmap.rows; row++ )
            {
                for ( col = 0; col < bitmap.width; col++ )
                {
                    if ( (target_y+row < window_height) && (target_y+row > 0) && (target_x+col < window_width) && (target_x+col > 0) )
                    {
                        Uint32 color = *( glyhp_buffer + row * (bitmap.pitch) + col );
                        if (color > 0)
                        {
                            SETPIXEL(target_x+col, target_y+row, (color<<24)+(color<<16)+(color<<8)+255);
                        }
                    }
                }
            }

            if ( show_cursor == 1 && i == buffer->ahead_cursor )
            {
                draw_cursor(x, y, pixels, fontface);
            }

            x += advance;

            if ( (set_cursor_x >= 0) && (set_cursor_x <= x-(advance>>1)) && (set_cursor_y <= y) )
            {
                buffer->cursor = i;//TODO: what's with this?
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

    if (set_cursor_x >= 0)
    {
        buffer->cursor = i;
    }

    if ( show_cursor == 1 && i == buffer->ahead_cursor )
    {
        draw_cursor(x, y, pixels, fontface);
    }
}

void
utf8_to_utf32 ( char *in, DynamicArray_uint32 *out )
{

    int i;
    Uint32 utfchar;
    for (i=0; in[i]; i++)
    {
        if ( (in[i] & 0x80) == 0)
        {
            utfchar = in[i];
        }

        else if ( (in[i] & 0x40) == 0)
        {
            printf("Why is there a continuation byte here?\n");
        }

        else if ( (in[i] & 0x20) == 0)
        {
            utfchar = in[i] & 0x1f;

            utfchar <<= 6;
            i++;
            utfchar += in[i] & 0x3f;
        }

        else if ( (in[i] & 0x10) == 0)
        {
            utfchar = in[i] & 0x0f;

            utfchar <<= 6;
            i++;
            utfchar += in[i] & 0x3f;

            utfchar <<= 6;
            i++;
            utfchar += in[i] & 0x3f;
        }

        else if ( (in[i] & 0x08) == 0)
        {
            utfchar = in[i] & 0x07;

            utfchar <<= 6;
            i++;
            utfchar += in[i] & 0x3f;

            utfchar <<= 6;
            i++;
            utfchar += in[i] & 0x3f;

            utfchar <<= 6;
            i++;
            utfchar += in[i] & 0x3f;
        }

        else
        {
            printf("What is that fifth continuation doing here?? (This should never happen)\n");
        }

        addToDynamicArray_uint32(out, utfchar);
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

extern void *
start_backend (Uint16 own_port, Uint16 other_port, TextBuffer *textbuffer_ptr);

void
rust_text_input (const Uint8 *text, Sint32 length, void *ffi_box_ptr);

void
rust_try_sync_text (void *ffi_box_ptr);

void
rust_blocking_sync_text (void *ffi_box_ptr);

void
rust_sync_unlock (void *ffi_box_ptr);

void
rust_send_cursor (Uint32 cursor, void *ffi_box_ptr);

void
render_text (TextInsertSet *set, Uint32 parentID, Uint8 charPos, TextBuffer *buffer, Uint32 cursor_ID, Uint8 cursor_charPos, DynamicArray_uint32 *ID_stack)
//buffer.text needs to be initialized; buffer.ID_table (needs to be initialized too) stores the ID of the insertion mark which contains each character, buffer.charPos_table stores the index of the character within its insertion mark. TODO: this is terribly inefficient with memory. fix sometime.
{
    int i;
    DynamicArray_ulong IDs;
    initDynamicArray_ulong(&IDs);

    for (i=0; i<set->length; i++)
    {
        TextInsert insert = *(set->array+i);
        if (insert.parentID == parentID && insert.charPos == charPos)
        {
            int j;
            int valid = 1;
            for (j=0; j<ID_stack->length; j++)
            {
                if (ID_stack->array[j] == insert.selfID)
                {
                    printf("render_text has detected a cyclic dependency between inserts. This should never happen, as it does not conform to the protocol specification.\n");
                    valid = 0;
                }
            }
            if (valid)
            {
                addToDynamicArray_ulong(&IDs, insert.selfID);
            }
        }
    }

    //sort all the inserts
    if (IDs.length > 0)
    {
        quicksort(IDs.array, 0, IDs.length-1);
    }


    //render them in order

    for (i=0; i<IDs.length; i++)
    {
        //get insert
        TextInsert *current_insert = set->array+getInsertByID(set, IDs.array[i]);

        addToDynamicArray_uint32(ID_stack, current_insert->selfID); //push

        //draw it
        int pos;
        for (pos=0; pos<current_insert->length; pos++)
        {
            //render the inserts before this character position
            render_text(set, current_insert->selfID, pos, buffer, cursor_ID, cursor_charPos, ID_stack);

            //stick the appropriate letter on the back
            if (current_insert->content[pos] != 127)
            {
                addToDynamicArray_uint32(&buffer->text, current_insert->content[pos]);
                addToDynamicArray_uint32(&buffer->ID_table, current_insert->selfID);
                addToDynamicArray_uint32(&buffer->author_table, current_insert->author);
                addToDynamicArray_uint32(&buffer->charPos_table, pos);
            }

            //check for the cursor
            if ( ( current_insert->selfID == cursor_ID ) && ( pos == cursor_charPos ) )
            {
                buffer->cursor = buffer->text.length;
            }
        }

        render_text(set, current_insert->selfID, current_insert->length, buffer, cursor_ID, cursor_charPos, ID_stack);

        ID_stack->length--; //pop
    }

    free(IDs.array);
}

void
update_buffer (TextInsertSet *set, TextBuffer *buffer)
{
    //convert cursor to (ID, charPos) format
    Uint32 cursor_ID;
    Uint8 cursor_charPos;

    if (buffer->update_hint_cursor_ID >= 0)
    {
        cursor_ID = buffer->update_hint_cursor_ID;
        cursor_charPos = buffer->update_hint_cursor_charPos;
        buffer->update_hint_cursor_ID = -1;
        buffer->update_hint_cursor_charPos = -1;
    }

    else
    {
        if (buffer->cursor > 0)
        {
            cursor_ID = buffer->ID_table.array[buffer->cursor - 1];
            cursor_charPos = buffer->charPos_table.array[buffer->cursor - 1];
        }

        else
        {
            cursor_ID = 0;
            cursor_charPos = 0;
        }
    }

    //reset all relevant arrays
    buffer->text.length = 0;
    buffer->ID_table.length = 0;
    buffer->author_table.length = 0;
    buffer->charPos_table.length = 0;

    DynamicArray_uint32 ID_stack;
    initDynamicArray_uint32(&ID_stack);

    //render_text(set, 0, 0, buffer, cursor_ID, cursor_charPos);
    render_text(set, 0, 0, buffer, cursor_ID, cursor_charPos, &ID_stack);

    if (cursor_ID == 0)
    {
        buffer->cursor = 0;
    }

    free(ID_stack.array);
}

void
update_login_buffer (TextBuffer *buffer, DynamicArray_uint32 *username, DynamicArray_uint32 *password, DynamicArray_uint32 *pad_with)
{
    int i;
    buffer->text.length = 0;
    add_string_to_utf32_text(&buffer->text, "username: ");
    concatDynamicArrays_uint32(&buffer->text, username);
    add_string_to_utf32_text(&buffer->text, "\npassword: ");
    for (i=0; i<password->length; i++)
    {
        addToDynamicArray_uint32(&buffer->text, 42); //42 is '*'
    }
    add_string_to_utf32_text(&buffer->text, "\npad with: ");
    concatDynamicArrays_uint32(&buffer->text, pad_with);
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
insert_letter (TextInsertSet *set, TextBuffer *buffer, Uint32 letter, network_data *network)
{

    TextInsert *active_insert = NULL;
    if (buffer->activeInsertID)
    {
        active_insert = set->array + getInsertByID(set, buffer->activeInsertID);
    }

    if ( (buffer->activeInsertID) && (active_insert->length < 255) )
    {
        active_insert->content = realloc(active_insert->content, (active_insert->length+1)*4);
        active_insert->content[active_insert->length] = letter;
        active_insert->length++;

        enqueue_insert(active_insert, network);
        network->send_now = 1;

        buffer->update_hint_cursor_ID = buffer->activeInsertID;
        buffer->update_hint_cursor_charPos = active_insert->length - 1;
    }

    else
    {
        buffer->activeInsertID = 0;

        int pos = buffer->cursor;
        Uint32 insert_ID;
        Uint8 charPos;
        if ( pos == 0 )
        {
            if (set->length == 0)
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
            if (pos == buffer->ID_table.length )
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
        new_insert.content = malloc(4);
        new_insert.content[0] = letter;

        addToTextInsertSet(set, new_insert);
        TextInsert *new_insert_pointer = set->array + set->length-1; //NOTE: not thread safe!
        buffer->activeInsertID = new_insert.selfID;
        
        enqueue_insert(new_insert_pointer, network);
        network->send_now = 1;

        buffer->update_hint_cursor_ID = new_insert.selfID;
        buffer->update_hint_cursor_charPos = 0;

    }

    update_buffer(set, buffer);

    return 0;
}

int
delete_letter ( TextInsertSet *set, TextBuffer *buffer, network_data *network )
{
    if (buffer->cursor < buffer->ID_table.length)
    {
        insertID insert_ID = buffer->ID_table.array[buffer->cursor];
        TextInsert *insert = set->array + getInsertByID(set, insert_ID);
        Uint8 inner_pos = buffer->charPos_table.array[buffer->cursor];

        insert->content[inner_pos] = 127;

        enqueue_insert(insert, network);
        network->send_now = 1;
    }
    update_buffer(set, buffer);
    return 0;
}


void ahead_insert_letter ( TextBuffer *buffer, Uint32 letter )
{
    insertIntoDynamicArray_uint32(&buffer->text, letter, buffer->ahead_cursor);
}

void ahead_delete_letter ( TextBuffer *buffer )
{
    deleteFromDynamicArray_uint32(&buffer->text, buffer->ahead_cursor);
}


void
login_insert_letter ( TextBuffer *buffer, DynamicArray_uint32 *username, DynamicArray_uint32 *password, DynamicArray_uint32 *pad_with, Uint32 letter )
{
    int line_nr = get_line_nr(&buffer->text, buffer->cursor);

    if (letter>127)
    {
        return; //only ascii
    }

    if (line_nr == 0)
    {
        int insert_pos = buffer->cursor - 10;
        if (insert_pos < 0)
        {
            insert_pos = username->length;
        }
        insertIntoDynamicArray_uint32(username, letter, insert_pos);
        buffer->cursor = insert_pos + 10 + 1;
        buffer->ahead_cursor = buffer->cursor;
    }
    
    else if (line_nr == 1)
    {
        int password_line_offset = seek_to_line(&buffer->text, 1);
        int insert_pos = buffer->cursor - 10 - password_line_offset;
        if (insert_pos < 0)
        {
            insert_pos = password->length;
        }
        insertIntoDynamicArray_uint32(password, letter, insert_pos);
        buffer->cursor = insert_pos + 10 + password_line_offset + 1;
        buffer->ahead_cursor = buffer->cursor;
    }
    else if (line_nr == 2)
    {
        int pad_with_line_offset = seek_to_line(&buffer->text, 2);
        int insert_pos = buffer->cursor - 10 - pad_with_line_offset;
        if (insert_pos < 0)
        {
            insert_pos = pad_with->length;
        }
        insertIntoDynamicArray_uint32(pad_with, letter, insert_pos);
        buffer->cursor = insert_pos + 10 + pad_with_line_offset + 1;
        buffer->ahead_cursor = buffer->cursor;
    }

    update_login_buffer(buffer, username, password, pad_with);
}

void
login_delete_letter ( TextBuffer *buffer, DynamicArray_uint32 *username, DynamicArray_uint32 *password, DynamicArray_uint32 *pad_with )
{
    int line_nr = get_line_nr(&buffer->text, buffer->cursor);

    if (line_nr == 0)
    {
        int delete_pos = buffer->cursor - 10 - 1;
        if (delete_pos >= 0)
        {
            deleteFromDynamicArray_uint32(username, delete_pos);
            buffer->cursor--;
            buffer->ahead_cursor = buffer->cursor;
        }
    }

    else if (line_nr == 1)
    {
        int password_line_offset = seek_to_line(&buffer->text, 1);
        int delete_pos = buffer->cursor - 10 - password_line_offset - 1;
        if (delete_pos >= 0)
        {
            deleteFromDynamicArray_uint32(password, delete_pos);
            buffer->cursor--;
            buffer->ahead_cursor = buffer->cursor;
        }
    }

    else if (line_nr == 2)
    {
        int pad_with_line_offset = seek_to_line(&buffer->text, 2);
        int delete_pos = buffer->cursor - 10 - pad_with_line_offset - 1;
        if (delete_pos >= 0)
        {
            deleteFromDynamicArray_uint32(pad_with, delete_pos);
            buffer->cursor--;
            buffer->ahead_cursor = buffer->cursor;
        }
    }
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
    buffer.ahead_cursor = 0;
    buffer.update_hint_cursor_ID = -1;
    buffer.update_hint_cursor_charPos = -1;
    buffer.activeInsertID = 0;
    buffer.x = 10;
    buffer.line_y = 0;
    buffer.y_padding = 10;
    buffer.line = 0;

    initDynamicArray_uint32(&buffer.text);
    initDynamicArray_uint32(&buffer.ID_table);
    initDynamicArray_uint32(&buffer.author_table);
    initDynamicArray_uint32(&buffer.charPos_table);

    program_state = STATE_LOGIN;
    add_string_to_utf32_text(&buffer.text, "username: \npassword: \npad with: ");
    DynamicArray_uint32 username, password, pad_with;
    initDynamicArray_uint32(&username);
    initDynamicArray_uint32(&password);
    initDynamicArray_uint32(&pad_with);


    //Start rust backend
#ifdef SWITCH
    void *ffi_box_ptr = start_backend(2002, 2001, &buffer);
#else
    void *ffi_box_ptr = start_backend(2001, 2002, &buffer);
#endif
    rust_sync_unlock(ffi_box_ptr);


    //main loop
    int quit=0;
    int i;
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
                    DynamicArray_uint32 utf32_encoded;
                    initDynamicArray_uint32(&utf32_encoded);
                    utf8_to_utf32(e.text.text, &utf32_encoded);
                    if (program_state == STATE_PAD)
                    {
                        //TODO (maybe): more efficient multi-letter insert
                        for (i=0; i<utf32_encoded.length; i++)
                        {
                            ahead_insert_letter(&buffer, utf32_encoded.array[i]);
                            buffer.ahead_cursor++;
                        }
                        blink_timer = 0;
                        rust_text_input(e.text.text, get_string_length(e.text.text), ffi_box_ptr);
                    }
                    else if (program_state == STATE_LOGIN)
                    {
                        for (i=0; i<utf32_encoded.length; i++)
                        {
                            login_insert_letter(&buffer, &username, &password, &pad_with, utf32_encoded.array[i]);
                        }
                    }

                } break;

                case SDL_KEYDOWN:
                {
                    switch (e.key.keysym.sym)
                    {
                        case SDLK_RETURN:
                        {
                            if (program_state == STATE_PAD)
                            {
                                ahead_insert_letter(&buffer, 10);
                                buffer.ahead_cursor++;
                                Uint8 enter = 10;
                                rust_text_input(&enter, 1, ffi_box_ptr);
                            }
                            else if (program_state == STATE_LOGIN)
                            {
                                if (username.length > 0 && pad_with.length > 0)
                                {
                                    buffer.cursor = buffer.ahead_cursor = 0;
                                    buffer.text.length = 0;
                                    program_state = STATE_PAD;

                                    load_document(&set, &pad_with);
                                    update_buffer(&set, &buffer);
                                }
                                else
                                {
                                    int line_nr = get_line_nr(&buffer.text, buffer.cursor);
                                    buffer.cursor = buffer.ahead_cursor = seek_to_line(&buffer.text, line_nr+1);
                                }
                            }
                        } break;

                        case SDLK_BACKSPACE:
                        {
                            if (buffer.cursor > 0)
                            {
                                if (program_state == STATE_PAD)
                                {
                                    buffer.ahead_cursor--;
                                    ahead_delete_letter (&buffer);
                                    Uint8 backspace = 127;
                                    rust_text_input(&backspace, 1, ffi_box_ptr);
                                }
                                else if (program_state == STATE_LOGIN)
                                {
                                    login_delete_letter(&buffer, &username, &password, &pad_with);
                                    update_login_buffer(&buffer, &username, &password, &pad_with);
                                }
                            }
                        } break;

                        case SDLK_RIGHT:
                        {
                            rust_blocking_sync_text(ffi_box_ptr);

                            if (e.key.keysym.mod & KMOD_SHIFT) //seek to next word
                            {
                                int i;
                                //skip to next white space
                                for (i=buffer.cursor; (i < buffer.text.length) && (buffer.text.array[i] != 10) && (buffer.text.array[i] != 32); i++);
                                //skip to the end of the whitespace
                                for (; (i < buffer.text.length) && ( (buffer.text.array[i] == 10) || (buffer.text.array[i] == 32) ); i++);
                                buffer.cursor = i;
                            }

                            else
                            {
                                if (buffer.cursor < buffer.text.length)
                                {
                                    buffer.cursor++;
                                }
                            }

                            buffer.activeInsertID = 0;
                            buffer.ahead_cursor = buffer.cursor;
                            rust_send_cursor(buffer.cursor, ffi_box_ptr);
                            rust_sync_unlock(ffi_box_ptr);
                        } break;

                        case SDLK_LEFT:
                        {
                            rust_blocking_sync_text(ffi_box_ptr);

                            if (e.key.keysym.mod & KMOD_SHIFT) //seek to previous word
                            {
                                    int i = buffer.cursor-1;
                                    for (; (i >= 0) && (buffer.text.array[i] != 10) && (buffer.text.array[i] != 32); i--);
                                    for (; (i >= 0) && ( (buffer.text.array[i] == 10) || (buffer.text.array[i] == 32) ); i--);
                                    buffer.cursor = i+1;
                            }

                            else
                            {
                                if (buffer.cursor > 0)
                                {
                                    buffer.cursor--;
                                }
                            }

                            buffer.ahead_cursor = buffer.cursor;
                            buffer.activeInsertID = 0;
                            rust_send_cursor(buffer.cursor, ffi_box_ptr);
                            rust_sync_unlock(ffi_box_ptr);
                        } break;

                        case SDLK_UP:
                        {
                            rust_blocking_sync_text(ffi_box_ptr);

                            int i;
                            int first = 0;
                            for (i=buffer.cursor; (i > 0); i--)
                            {
                                if (buffer.text.array[i-1] == 10)
                                {
                                    if (first)
                                    {
                                        break;
                                    }
                                    else
                                    {
                                        first = 1;
                                    }
                                }
                            }

                            buffer.cursor = i;
                            buffer.ahead_cursor = buffer.cursor;
                            buffer.activeInsertID = 0;
                            rust_send_cursor(buffer.cursor, ffi_box_ptr);
                            rust_sync_unlock(ffi_box_ptr);
                        } break;

                        case SDLK_DOWN:
                        {
                            rust_blocking_sync_text(ffi_box_ptr);

                            int i;
                            for (i=buffer.cursor; (i < buffer.text.length-1) && (buffer.text.array[i] != 10); i++);
                            buffer.cursor = i+1;
                            buffer.ahead_cursor = buffer.cursor;
                            buffer.activeInsertID = 0;
                            rust_send_cursor(buffer.cursor, ffi_box_ptr);
                            rust_sync_unlock(ffi_box_ptr);
                        } break;

                        case SDLK_v:
                        {
                            if (e.key.keysym.mod & KMOD_CTRL)
                            {
                                char *clipboard_content = SDL_GetClipboardText();
                                if (clipboard_content)
                                {
                                    DynamicArray_uint32 utf32_encoded;
                                    initDynamicArray_uint32(&utf32_encoded);
                                    utf8_to_utf32(clipboard_content, &utf32_encoded);
                                    if (program_state == STATE_PAD)
                                    {
                                        for (i=0; i<utf32_encoded.length; i++)
                                        {
                                            ahead_insert_letter(&buffer, utf32_encoded.array[i]);
                                            buffer.ahead_cursor++;
                                        }
                                        blink_timer = 0;
                                        rust_text_input(clipboard_content, get_string_length(clipboard_content), ffi_box_ptr);
                                    }
                                    else if (program_state == STATE_LOGIN)
                                    {
                                        for (i=0; i<utf32_encoded.length; i++)
                                        {
                                            login_insert_letter(&buffer, &username, &password, &pad_with, utf32_encoded.array[i]);
                                        }
                                    }

                                    free(clipboard_content);
                                }
                            }
                        } break;

                        case SDLK_ESCAPE:
                        {
                            quit = 1;
                        } break;
                    }

                    blink_timer = 0;

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

                    if (buffer.line_y >= 0) //TODO: might require further adaptation with the top padding
                    {
                        if (buffer.line == 0)
                        {
                            buffer.line_y = 0;
                        }

                        else
                        {
                            int previous_line_offset = seek_to_line(&buffer.text, buffer.line-1);
                            int y_offset = (number_of_linewraps(&buffer.text, previous_line_offset, buffer.x, fontface)+1) * line_height;
                            buffer.line--;
                            buffer.line_y -= y_offset;
                        }
                    }

                    else
                    {
                        int current_line_offset = seek_to_line(&buffer.text, buffer.line);
                        int current_line_height = (number_of_linewraps(&buffer.text, current_line_offset, buffer.x, fontface)+1) * line_height;
                        
                        if (-buffer.line_y > current_line_height)
                        {
                            int nr_of_lines = get_line_nr(&buffer.text, buffer.text.length);
                            if (buffer.line < nr_of_lines)
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


        //drawing

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

        if ((click_x != -1) || (click_y != -1))
        {
            rust_blocking_sync_text(ffi_box_ptr);
        }
        if (blink_timer < 128)
        {
            draw_text(&buffer, buffer.text.array, pixels, 1, fontface, click_x, click_y);
        }
        else
        {
            draw_text(&buffer, buffer.text.array, pixels, 0, fontface, click_x, click_y);
        }
        buffer.ahead_cursor = buffer.cursor;
        if ((click_x != -1) || (click_y != -1))
        {
            rust_send_cursor(buffer.cursor, ffi_box_ptr);
            rust_sync_unlock(ffi_box_ptr);
            click_x = click_y = -1;
        }

        //SDL_UpdateTexture(texture, NULL, pixels, window_width*sizeof(Uint32));
        SDL_UnlockTexture(texture);
        SDL_RenderClear(renderer);
        SDL_RenderCopy(renderer, texture, NULL, NULL);
        SDL_RenderPresent(renderer);
        


        //check FIFO
        //{
        //    char *base85_length = malloc(5);
        //    if (recv(network.own_socket, base85_length, 5, MSG_PEEK) == 5)
        //    {
        //        Uint32 length = base85_dec_uint32(base85_length);

        //        char *input_unsliced = malloc(length+5);
        //        ssize_t actual_length = recv(network.own_socket, input_unsliced, length+5, 0)-5;
        //        if (actual_length != length)
        //        {
        //            printf("Given message length and actual message length did not match. Message is not processed.\n");
        //        }
        //        else
        //        {
        //            char *input = input_unsliced + 5;

        //            printf("Received message of length %i: %.*s\n", length, length, input);

        //            if (string_compare(input, length, "Init", 4))
        //            {
        //                if (network.wait_for_init)
        //                {
        //                    if (length == 4+5+5+2)
        //                    {
        //                        if (check_base32_crc8_0x97(input, length) == 0)
        //                        {
        //                            author_ID = ID_start = base85_dec_uint32(input+4);
        //                            ID_end = base85_dec_uint32(input+9);

        //                            network.wait_for_init = 0;
        //                        }
        //                    }

        //                    else
        //                    {
        //                        printf("Invalid data length!\n");
        //                    }

        //                }
        //            }

        //            else if (string_compare(input, length, "deny", 4))
        //            {
        //                if (network.wait_for_init)
        //                {
        //                    network.wait_for_init = 0;
        //                }
        //            }

        //            else if (string_compare(input, length, "inrq", 4))
        //            {
        //                const struct sockaddr_un other_address = { .sun_family = AF_UNIX, .sun_path = "/tmp/deca_socket_2" };
        //                connect(network.own_socket, (const struct sockaddr *) &other_address, sizeof(other_address));
        //                send_init(&network);
        //            }

        //            else if (string_compare(input, length, "data", 4))
        //            {
        //                if (check_base32_crc8_0x97(input, length) == 0)
        //                {
        //                    size_t insert_length;
        //                    Sint64 insert_ID = unserialize_insert(&set, input+4, length-4, &insert_length);
        //                    update_buffer(&set, &buffer);

        //                    if(insert_ID >= 0)
        //                    {
        //                        //ACK
        //                        char ack[14] = "*****ack *****";
        //                        base85_enc_uint32(insert_ID, ack+9);
        //                        send_data(ack, 14, &network);
        //                    }
        //                }
        //            }

        //            else if (string_compare(input, length, "ack ", 4))
        //            {
        //                if (length == 4+5)
        //                {
        //                    int i;
        //                    Uint32 ack_id = base85_dec_uint32(input+4);
        //                    for (i=0; i < network.send_queue.length; i++)
        //                    {
        //                        if ( network.send_queue.array[i] == ack_id )
        //                        {
        //                            deleteFromDynamicArray_uint32(&network.send_queue, i);
        //                            break;
        //                        }
        //                    }
        //                }
        //            }
        //        }

        //        free(input_unsliced);
        //    }
        //    free(base85_length);

        //    //check whether we have to take back the cursor after a deletion
        //    if (buffer.cursor > buffer.text.length)
        //    {
        //        buffer.cursor = buffer.text.length;
        //    }
        //}

        ////resend un-ACKed inserts
        //if (resend_timer >= 500 || network.send_now)
        //{
        //    resend_timer = 0;
        //    network.send_now = 0;

        //    if (network.send_queue.length > 0)
        //    {
        //        Uint32 insert_ID = network.send_queue.array[0];
        //        TextInsert *resend_insert = set.array + getInsertByID(&set, insert_ID);
        //        //printf("Resending insert %lu at %lu.\n", (long unsigned int) resend_insert->selfID, (long unsigned int) resend_insert);
        //        send_insert(resend_insert, &network);

        //        deleteFromDynamicArray_uint32(&network.send_queue, 0);
        //        addToDynamicArray_uint32(&network.send_queue, insert_ID);
        //    }

        //    if (network.wait_for_init)
        //    {
        //        send_init_request(&network);
        //    }
        //}


        blink_timer+=9;
        resend_timer += 30;
        SDL_Delay(30);//TODO: how big should the delay be?

        rust_try_sync_text(ffi_box_ptr);
        rust_sync_unlock(ffi_box_ptr);

    }

    save_document(&set, &pad_with);

    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();

    //kill the rust thread
    Uint8 quit_signal[4] = {255, 255, 255, 255};
    rust_text_input(&quit_signal[0], 4, ffi_box_ptr);

    free(buffer.text.array);
    free(buffer.ID_table.array);
    free(buffer.charPos_table.array);

    for (i=0; i<set.length; i++)
    {
        free( set.array[i].content );
    }

    free(set.array);

    FT_Done_FreeType(ft_library);

    //close(network.own_socket);

    return 0;
}
