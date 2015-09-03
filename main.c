#include <stdio.h>
#include <ft2build.h>
#include FT_FREETYPE_H
#include <SDL2/SDL.h>
#include "main.h"
#include "dynamic_array.h"

#define SETPIXEL(x, y, value) ( *(pixels+(x)+(y)*WINDOW_WIDTH) = (value) )

//TODO(tony): find a better way for this
#define WINDOW_WIDTH 640
#define WINDOW_HEIGHT 400


//one character is 8x6; start in the top left corner, then scan vertically
const char characters[] = {
    63, 72, 72, 72, 63, 0, //A
    127, 73, 73, 73, 54, 0,//B
    62, 65, 65, 65, 65, 0,//C
    127, 65, 65, 65, 62, 0,//D
    127, 73, 73, 73, 65, 0,//E
    127, 72, 72, 72, 64, 0,//F
    49, 73, 73, 73, 70, 0,//S (for typing ASDF)
    0, 0, 0, 0, 0, 0, 0,//Space
};

unsigned int
get_string_length (char *buffer) //*not* counting the 0 at the end
{
    unsigned int length = 0;
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
    unsigned int length1, length2, i;
    length1 = get_string_length(*buffer1);
    length2 = get_string_length(*buffer2);
    *buffer1 = realloc(*buffer1, length1+length2+1);
    for (i=0; i<=length2; i++) //the <= is so we copy over the 0 byte from buffer2, too
    {
        *(*buffer1+length1+i) = *(*buffer2+i);
    }
    return;
}

void
draw_text (TextBuffer *buffer, char *text, unsigned int x, unsigned int y, Uint32 *pixels, char show_cursor, FT_Face fontface)
{
    unsigned short inverted_pos = buffer->cursor + 1; //NOTE(tony): what if the buffer is too long?
    if (!show_cursor)
    {
        inverted_pos = 0;
    }
    char character;
    char *data;
    int i;
    char *index = text;
    char *inverted_char = text+inverted_pos;
    unsigned int zero_x = x;
    int error;
    int height = (int) fontface->size->metrics.height / 64;

    while ((character=(*index++)))
    {
        if (character == 10) {
            x = zero_x;
            y += height;
        }
        else {
            error = FT_Load_Char( fontface, character, FT_LOAD_RENDER );

            FT_Bitmap bitmap = fontface->glyph->bitmap;

            if ( bitmap.pixel_mode != FT_PIXEL_MODE_GRAY )
            {
                printf("Not the right Freetype glyph bitmap pixel mode! Sorry, it ran on my computer...\n");
                return;
            }

            else
            {
                //copy glyph into pixel array

                unsigned int target_x, target_y;
                target_x = x + fontface->glyph->bitmap_left;
                target_y = y - fontface->glyph->bitmap_top;

                if ( bitmap.pitch < 0 )
                {
                    printf("Freetype glyph bitmap pitch is negative. Surely wasn't expecting that...\n");
                    return;
                }

                unsigned int row;
                unsigned int col;
                unsigned char *buffer = (unsigned char *) (bitmap.buffer);
                for ( row = 0; row < bitmap.rows; row++ )
                {
                    for ( col = 0; col < bitmap.width; col++ )
                    {
                        Uint32 color = *( buffer + row * (bitmap.pitch) + col );
                        SETPIXEL(target_x+col, target_y+row, (color<<24)+(color<<16)+(color<<8)+255);
                    }
                }
            }

            if ( index == inverted_char )
            {
                int i;
                for ( i=0; i<height; i++ )
                {
                    SETPIXEL(x, y-i+height/8, 0xFFFFFFFF);
                }
            }

            x += fontface->glyph->advance.x >> 6;
        }
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

long //so we can have all the unsigned ints *and* return -1 on not finding an insert
getInsertByID (TextInsertSet *set, unsigned long selfID)
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
render_text (TextInsertSet *set, unsigned long parentID, unsigned short charPos, DynamicArray_char *output_buffer, DynamicArray_ulong *ID_table, DynamicArray_ulong *charPos_table)//output_buffer needs to be initialized; ID_table (needs to be initialized too) stores the ID of the insertion mark which contains each character. TODO: this is terribly inefficient with memory. fix sometime.
{
    unsigned int i;
    TextInsert current_insert;
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
        current_insert = set->array[getInsertByID(set, IDs.array[i])];

        //draw it
        int pos;
        for (pos=0; pos<current_insert.length; pos++)
        {
            //render the inserts before this character position
            render_text(set, current_insert.selfID, pos, output_buffer, ID_table, charPos_table);

            //stick the appropriate letter on the back
            if (current_insert.content[pos] != 127)
            {
                addToDynamicArray_char(output_buffer, current_insert.content[pos]);
                addToDynamicArray_ulong(ID_table, current_insert.selfID);
                addToDynamicArray_ulong(charPos_table, pos);
            }
        }

        render_text(set, current_insert.selfID, current_insert.length, output_buffer, ID_table, charPos_table);
    }

    free(IDs.array);
}


int
insert_letter (TextBuffer *buffer, TextInsertSet *set, DynamicArray_ulong *ID_table, DynamicArray_ulong *charPos_table, char letter)
{

    if (buffer->activeInsertID)
    {
        TextInsert *insert = set->array + getInsertByID(set, buffer->activeInsertID);
        insert->content = realloc(insert->content, insert->length+1);
        insert->content[insert->length] = letter;
        insert->length++;
    }

    else
    {

        unsigned short pos = buffer->cursor;
        unsigned long insert_ID = ID_table->array[pos];

        unsigned short charPos = 0;
        charPos = charPos_table->array[pos];


        TextInsert new_insert;
        new_insert.selfID = set->used_length + 1;
        new_insert.parentID = insert_ID;
        new_insert.charPos = charPos;
        new_insert.lock = 0;
        new_insert.length = 1;
        new_insert.content = malloc(1);
        new_insert.content[0] = letter;

        addToTextInsertSet(set, new_insert);
        buffer->activeInsertID = new_insert.selfID;

    }

    buffer->cursor++;
    return 0;
}

int
delete_letter ( TextInsertSet *set, DynamicArray_ulong *ID_table, DynamicArray_ulong *charPos_table, unsigned short pos )
{
    if (pos < ID_table->used_length)
    {
        unsigned long insert_ID = ID_table->array[pos];
        unsigned short inner_pos = 0;
        inner_pos = charPos_table->array[pos];

        TextInsert *insert = set->array + getInsertByID(set, insert_ID);
        insert->content[inner_pos] = 127;
    }
    return 0;
}


int main (void)
{

    //Freetype Setup


    int error;
    
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

    //SDL Setup

    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        printf("SDL initialization failed: %s", SDL_GetError());
        return 1;
    }

    SDL_Window *window = SDL_CreateWindow("congame 3d test", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, WINDOW_WIDTH, WINDOW_HEIGHT, 0);
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

    SDL_Texture *texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_STREAMING, WINDOW_WIDTH, WINDOW_HEIGHT);

    Uint32 *pixels = malloc(WINDOW_WIDTH*WINDOW_HEIGHT*sizeof(Uint32));


    //Logic
    TextInsertSet set;
    initTextInsertSet(&set);
    {
        TextInsert start_insert;
        start_insert.parentID = 0;
        start_insert.selfID = 1;
        start_insert.charPos = 0;
        start_insert.lock = 0;
        start_insert.length = 1;
        start_insert.content = malloc(1);
        start_insert.content[0] = 65; //<- WHEN DEBUGGING, WATCH THIS!!!
        addToTextInsertSet(&set, start_insert);
    }


    DynamicArray_char output_buffer;
    DynamicArray_ulong ID_table;
    DynamicArray_ulong charPos_table;

    initDynamicArray_char(&output_buffer);
    initDynamicArray_ulong(&ID_table);
    initDynamicArray_ulong(&charPos_table);

    render_text(&set, 0, 0, &output_buffer, &ID_table, &charPos_table);
    addToDynamicArray_char(&output_buffer, 0);


    //main loop
    int quit=0;
    //int i;
    //int x, y;


    TextBuffer buffer;
    buffer.cursor = 0;
    buffer.activeInsertID = 0;

    //short unsigned cursor = 0;
    char unsigned blink_timer = 0;

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
                    insert_letter(&buffer, &set, &ID_table, &charPos_table, e.text.text[0]);
                    blink_timer = 0;

                    //update buffer
                    output_buffer.used_length = 0;
                    ID_table.used_length = 0;
                    charPos_table.used_length = 0;
                    render_text(&set, 0, 0, &output_buffer, &ID_table, &charPos_table);
                    addToDynamicArray_char(&output_buffer, 0);
                } break;

                case SDL_KEYDOWN:
                {
                    switch (e.key.keysym.sym)
                    {
                        case SDLK_RETURN:
                        {
                            insert_letter(&buffer, &set, &ID_table, &charPos_table, 10);
                        } break;

                        case SDLK_BACKSPACE:
                        {
                            if (buffer.cursor > 0)
                            {
                                buffer.cursor--;
                                delete_letter (&set, &ID_table, &charPos_table, buffer.cursor);
                            }
                        } break;

                        case SDLK_RIGHT:
                        {
                            //if (buffer.cursor < buffer.length-1)
                            //{
                                buffer.cursor++;
                                buffer.activeInsertID = 0;
                            //}
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

                    //update buffer
                    output_buffer.used_length = 0;
                    ID_table.used_length = 0;
                    charPos_table.used_length = 0;
                    render_text(&set, 0, 0, &output_buffer, &ID_table, &charPos_table);
                    addToDynamicArray_char(&output_buffer, 0);
                    //printf("Rendered text: %s\n # Inserts: %i\n", output_buffer.array, set.used_length);

                } break;

                default:
                    break;
            }
        }

        {
            Uint32 *pointer = pixels;
            while(pointer < pixels+WINDOW_WIDTH*WINDOW_HEIGHT)
            {
                *pointer = 0;
                pointer++;
            }
        }

        if (blink_timer < 128)
        {
            draw_text(&buffer, output_buffer.array, 10, 100, pixels, 1, fontface);
        }
        else
        {
            draw_text(&buffer, output_buffer.array, 10, 100, pixels, 0, fontface);
        }
        SDL_UpdateTexture(texture, NULL, pixels, WINDOW_WIDTH*sizeof(Uint32));
        SDL_RenderClear(renderer);
        SDL_RenderCopy(renderer, texture, NULL, NULL);
        SDL_RenderPresent(renderer);
        
        blink_timer+=9;
        SDL_Delay(30);
    }

    SDL_DestroyWindow(window);
    SDL_Quit();
    free(pixels);

    FT_Done_FreeType(ft_library);

    return 0;
}
