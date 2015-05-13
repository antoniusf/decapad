#include <stdio.h>
#include <SDL2/SDL.h>
#include "dynamic_array.h"

#define SETPIXEL(x, y, value) ( *(pixels+(x)+(y)*WINDOW_WIDTH) = (value) )

//TODO(tony): find a better way for this
#define WINDOW_WIDTH 640
#define WINDOW_HEIGHT 400

struct TextBuffer
{
    unsigned short length;
    unsigned short cursor;
    char *buffer;
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

struct TextInsertSet
{
    TextInsert *set;
    unsigned int maxLength;
    unsigned int count;
    int activeInsert;
    int activeTimer;
};
typedef struct TextInsertSet TextInsertSet;


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
    unsigned int i;
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
draw_text (TextBuffer text, unsigned int x, unsigned int y, Uint32 *pixels, char show_cursor)
{
    unsigned short inverted_pos = text.cursor + 1; //NOTE(tony): what if the buffer is too long?
    if (!show_cursor)
    {
        inverted_pos = 0;
    }
    char character;
    char *data;
    int i;
    char *index = text.buffer;
    char *inverted_char = text.buffer+inverted_pos;
    unsigned int zero_x = x;

    while ((character=(*index++)))
    {
        if (character == 9) {
            x = zero_x;
            y += 9;
        }
        else {
            data = ((char *) characters)+(character-1/*-65*/)*6;
            for (i = 0; i<6; i++)
            {
                if (index==inverted_char)
                {
                    char dataval = ~(*data);
                    SETPIXEL(x, y, (dataval & (1<<7))*0xffffffff);
                    SETPIXEL(x, y+1, (dataval & (1<<6))*0xffffffff);
                    SETPIXEL(x, y+2, (dataval & (1<<5))*0xffffffff);
                    SETPIXEL(x, y+3, (dataval & (1<<4))*0xffffffff);
                    SETPIXEL(x, y+4, (dataval & (1<<3))*0xffffffff);
                    SETPIXEL(x, y+5, (dataval & (1<<2))*0xffffffff);
                    SETPIXEL(x, y+6, (dataval & (1<<1))*0xffffffff);
                    SETPIXEL(x, y+7, (dataval & (1<<0))*0xffffffff);
                }
                else
                {
                    SETPIXEL(x, y, (*data & (1<<7))*0xffffffff);
                    SETPIXEL(x, y+1, (*data & (1<<6))*0xffffffff);
                    SETPIXEL(x, y+2, (*data & (1<<5))*0xffffffff);
                    SETPIXEL(x, y+3, (*data & (1<<4))*0xffffffff);
                    SETPIXEL(x, y+4, (*data & (1<<3))*0xffffffff);
                    SETPIXEL(x, y+5, (*data & (1<<2))*0xffffffff);
                    SETPIXEL(x, y+6, (*data & (1<<1))*0xffffffff);
                    SETPIXEL(x, y+7, (*data & (1<<0))*0xffffffff);
                }
                data++;
                x++;
            }
            x++;
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
    for (i=0; i<set->count; i++)
    {
        if ((set->set)[i].selfID == selfID)
        {
            return i;
        }
    }
    return -1;
}

//int
//getInsertByParentID (TextInsertSet *set, unsigned long parentID, unsigned int count) //count is which find you want back
//{
//    unsigned long *IDlist = malloc(128*sizeof(unsigned long));
//    unsigned long *IDpointer = IDlist;
//    unsigned long currentID;
//    unsigned long i;
//    //minID = 0;
//    //maxID = 0;
//
//    //find all IDs
//    for (i=0; i<set->count; i++)
//    {
//        if ( (*(set->set+i)).parentID == parentID )
//        {
//                currentID = (*(set->set+i)).selfID;
//                *IDpointer++ = currentID;
//                //if (currentID < minID) minID = currentID;
//                //else if (currentID > maxID) maxID = currentID;
//        }
//    }
//
//    //sort them
//
//    
//    free(IDlist);
//    return -1;
//}

void
render_text (TextInsertSet *set, unsigned long parentID, unsigned short charPos, DynamicArray_char *output_buffer, DynamicArray_ulong *ID_table)//output_buffer needs to be initialized; ID_table (needs to be initialized too) stores the ID of the insertion mark which contains each character. TODO: this is terribly inefficient with memory. fix sometime.
{
    unsigned int i;
    TextInsert current_insert;
    unsigned long current_selfID = 0 - 1;//I just want the highest value for unsigned long
    DynamicArray_ulong IDs;
    initDynamicArray_ulong(&IDs);

    for (i=0; i<set->count; i++)
    {
        TextInsert insert = *(set->set+i);
        if (insert.parentID == parentID && insert.charPos == charPos)
        {
            addToDynamicArray_ulong(&IDs, insert.selfID);
            //if (insert.selfID < current_selfID)
            //{
            //    current_insert = insert;
            //    current_selfID = insert.selfID;
            //}
        }
    }

    //sort all the inserts
    if (IDs.used_length > 0)
    {
        quicksort(IDs.array, 0, IDs.used_length-1);
    }


    //render them in order
    //char *output_buffer = malloc(1);
    //output_buffer[0] = 0;
    //char *new_buffer;

    for (i=0; i<IDs.used_length; i++)
    {
        //get insert
        current_insert = set->set[getInsertByID(set, IDs.array[i])];

        //draw it
        int pos;
        for (pos=0; pos<current_insert.length; pos++)
        {
            //new_buffer = render_text(set, current_insert.selfID, pos);
            render_text(set, current_insert.selfID, pos, output_buffer, ID_table);
            //string_concat(&output_buffer, &new_buffer);

            //stick the appropriate letter on the back
            if (current_insert.content[i] != 127)
            {
                addToDynamicArray_char(output_buffer, current_insert.content[pos]);
                addToDynamicArray_ulong(ID_table, current_insert.selfID);
                //int length = get_string_length(output_buffer);
                //output_buffer = realloc(output_buffer, length+2);
                //output_buffer[length] = current_insert.content[pos];
                //output_buffer[length+1] = 0;
                printf("%c", current_insert.content[pos]);
            }
        }
    }

    free(IDs.array);
    //return output_buffer;
}


unsigned short
insert_letter (TextBuffer *buffer, char letter)
{
    //if (set.activeInsert == -1) 
    //{
    //    if (set.count < set.maxLength) {
    //        active_insert = *(set.set+set.count);
    //        set.activeInsert = set.count;
    //        set.activeTimer = 5000;
    //        set.count++;
    //        active_insert.selfID = //?;
    //        active_insert.parentID



    unsigned short pos = buffer->cursor;
    char *pointer = buffer->buffer;
    pointer += pos;
    char new_letter = letter;
    char old_letter = *pointer;
    if (!old_letter)
    {
        return 0;
    }
    while (old_letter)
    {
        *pointer = new_letter;
        new_letter = old_letter;
        pointer++;
        old_letter = *pointer;
    }
    buffer->cursor++;
    return 1;
}


int main (void)
{

    //Setup

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
    set.maxLength = 4096;
    set.count = 0;
    set.activeInsert = -1;
    set.activeTimer = 0;
    set.set = malloc(set.maxLength*sizeof(TextInsert));
    {
        TextInsert *start_insert = &(set.set[0]);
        start_insert->parentID = 0;
        start_insert->selfID = 1;
        start_insert->charPos = 0;
        start_insert->lock = 0;
        start_insert->length = 0;
        start_insert->content = NULL;
        set.count++;
    }

    //insert mark test setup
    {
        TextInsert *insert = &(set.set[1]);
        insert->selfID = 2;
        insert->parentID = 0;
        insert->charPos = 0;
        insert->lock = 0;
        insert->length = 6;
        insert->content = malloc(7);
        insert->content[0] = 72; insert->content[1] = 101; insert->content[2] = 108; insert->content[3] = 108; insert->content[4] = 111; insert->content[5] = 32; insert->content[6] = 0;
        set.count++;

        insert = &(set.set[2]);
        insert->selfID = 3;
        insert->parentID = 2;
        insert->charPos = 3;
        insert->lock = 0;
        insert->length = 6;
        insert->content = malloc(7);
        insert->content[0] = 87; insert->content[1] = 111; insert->content[2] = 114; insert->content[3] = 108; insert->content[4] = 100; insert->content[5] = 33; insert->content[6] = 0;
        set.count++;
    }

    //insert mark test
    printf("render_text test:");
    DynamicArray_char output_buffer;
    initDynamicArray_char(&output_buffer);
    DynamicArray_ulong ID_table;
    initDynamicArray_ulong(&ID_table);
    render_text(&set, 0, 0, &output_buffer, &ID_table);
    printf("\n");
    addToDynamicArray_char(&output_buffer, 0);
    char *text = output_buffer.array;
    printf("render_text test: %s\n", text);


    //main loop
    int quit=0;
    //int i;
    //int x, y;


    TextBuffer buffer;
    buffer.length = 20;
    buffer.cursor = 0;
    buffer.buffer = (char *) malloc(21);
    { //clear buffer
        int i;
        for (i = 0; i<20; i++)
        {
            buffer.buffer[i] = 8;
        }
    }
    buffer.buffer[20] = 0;

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

                case SDL_KEYDOWN:
                {
                    switch (e.key.keysym.sym)
                    {
                        case SDLK_a:
                        {
                            insert_letter(&buffer, 1);
                        } break;
                        
                        case SDLK_b:
                        {
                            insert_letter(&buffer, 2);
                        } break;

                        case SDLK_c:
                        {
                            insert_letter(&buffer, 3);
                        } break;

                        case SDLK_d:
                        {
                            insert_letter(&buffer, 4);
                        } break;

                        case SDLK_e:
                        {
                            insert_letter(&buffer, 5);
                        } break;

                        case SDLK_f:
                        {
                            insert_letter(&buffer, 6);
                        } break;

                        case SDLK_s:
                        {
                            insert_letter(&buffer, 7);
                        } break;

                        case SDLK_SPACE:
                        {
                            insert_letter(&buffer, 8);
                        } break;

                        case SDLK_RETURN:
                        {
                            insert_letter(&buffer, 9);
                        } break;

                        case SDLK_BACKSPACE:
                        {
                            if (buffer.cursor > 0)
                            {
                                buffer.cursor--;
                                buffer.buffer[buffer.cursor] = 8;
                            }
                        } break;

                        case SDLK_RIGHT:
                        {
                            if (buffer.cursor < buffer.length-1)
                            {
                                buffer.cursor++;
                            }
                        } break;

                        case SDLK_LEFT:
                        {
                            if (buffer.cursor > 0)
                            {
                                buffer.cursor--;
                            }
                        } break;

                        case SDLK_ESCAPE:
                        {
                            quit = 1;
                        } break;
                    }

                    blink_timer = 0;
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
            draw_text(buffer, 10, 10, pixels, 1);
        }
        else
        {
            draw_text(buffer, 10, 10, pixels, 0);
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

    return 0;
}
