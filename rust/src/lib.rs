#![allow(non_snake_case)]
#![allow(dead_code)]
#![allow(unused_variables)]

use std::os::raw::{c_int, c_long, c_ulong};
use std::vec::Vec;
extern crate libc;

#[repr(C)]
pub struct TextInsert
{
    selfID: u32,
    parentID: u32,
    author: u32,
    charPos: u8,
    lock: u8,
    length: u8,
    content: *mut u32
}

#[derive(Debug)]
struct TextInsertInternal
{
    selfID: u32,
    parentID: u32,
    author: u32,
    charPos: u8,
    lock: u8,
    content: Vec<u32>
}

#[repr(C)]
pub struct TextInsertSet
{
    array: *mut TextInsert,
    length: c_long,
    allocated_length: c_long
}

#[repr(C)]
pub struct DynamicArray_ulong
{
    array: *mut c_ulong,
    length: c_long,
    allocated_length: c_long
}

#[repr(C)]
pub struct DynamicArray_uint32
{
    array: *mut u32,
    length: c_long,
    allocated_length: c_long
}

#[repr(C)]
pub struct TextBuffer
{
    cursor: c_int,
    update_hint_cursor_ID: i64,
    update_hint_cursor_charPos: i16,
    x: c_int,
    line_y: c_int,
    y_padding: c_int,
    line: c_int,
    activeInsertID: u32,
    text: DynamicArray_uint32,
    ID_table: DynamicArray_uint32,
    author_table: DynamicArray_uint32,
    charPos_table: DynamicArray_uint32
}

#[no_mangle]
pub unsafe extern fn render_text(set: *mut TextInsertSet, parentID: u32, charPos: u8, buffer: *mut TextBuffer, cursor_ID: u32, cursor_charPos: u8)
{
    let mut internalset: Vec<TextInsertInternal> = Vec::new();
    for i in 0..((&*set).length as isize)
    {
        let insert: &TextInsert = &*((&*set).array.offset(i));
        let mut content: Vec<u32> = Vec::new();
        for j in 0..((&*insert).length as isize)
        {
            content.push(*(insert.content.offset(j)));
        }
        internalset.push(TextInsertInternal { selfID: insert.selfID, parentID: insert.parentID, author: insert.author, charPos: insert.charPos, lock: insert.lock, content: content } );
    }
    println!("{:?}", internalset);
    let mut output: Vec<u32> = Vec::new();
    let mut ID_table: Vec<u32> = Vec::new();
    let mut author_table: Vec<u32> = Vec::new();
    let mut charPos_table: Vec<u32> = Vec::new();
    let mut ID_stack: Vec<u32> = Vec::new();
    let mut cursor_new_pos: c_int = 0;

    render_text_internal(&internalset, 0, 0, &mut output, &mut ID_table, &mut author_table, &mut charPos_table, cursor_ID, cursor_charPos, &mut cursor_new_pos, &mut ID_stack);

    let mut ref_buffer: &mut TextBuffer = &mut*buffer;
    let mut text = &mut ref_buffer.text;
    if (expandDynamicArray_uint32(text, output.len()) == 0)
     & (expandDynamicArray_uint32(&mut ref_buffer.ID_table, output.len()) == 0)
     & (expandDynamicArray_uint32(&mut ref_buffer.author_table, output.len()) == 0)
     & (expandDynamicArray_uint32(&mut ref_buffer.charPos_table, output.len()) == 0)
    {
        text.length = output.len() as i64;
        ref_buffer.ID_table.length = output.len() as i64;
        ref_buffer.author_table.length = output.len() as i64;
        ref_buffer.charPos_table.length = output.len() as i64;

        let copy_text = "Hello World from Rust!".chars();
        println!("output length: {}", output.len());
        for i in 0..output.len()
        {
            println!("{}", output[i]);
            *text.array.offset(i as isize) = output[i];
            *ref_buffer.ID_table.array.offset(i as isize) = ID_table[i];
            *ref_buffer.author_table.array.offset(i as isize) = author_table[i];
            *ref_buffer.charPos_table.array.offset(i as isize) = charPos_table[i];
        }
        ref_buffer.cursor = cursor_new_pos as c_int;
    }
}

fn render_text_internal(set: &Vec<TextInsertInternal>, parentID: u32, charPos: u8, output: &mut Vec<u32>, ID_table: &mut Vec<u32>, author_table: &mut Vec<u32>, charPos_table: &mut Vec<u32>, cursor_ID: u32, cursor_charPos: u8, cursor_new_pos: &mut c_int, ID_stack: &mut Vec<u32>)
{
    let mut inserts: Vec<&TextInsertInternal> = Vec::new();
    for i in 0..set.len()
    {
        let insert = &set[i];
        if (insert.parentID == parentID) & (insert.charPos == charPos)
        {
            if ID_stack.contains(&insert.selfID)
            {
                println!("render_text has detected a cyclic dependency between inserts. This should never happen, as it does not conform to the protocol specification.");
            }
            else
            {
                inserts.push(insert);
            }
        }
    }
    inserts.sort_by(|a, b| a.selfID.cmp(&b.selfID));

    for insert in inserts
    {
        ID_stack.push(insert.selfID);

        for pos in 0..(insert.content.len() as u8)
        {
            render_text_internal(set, insert.selfID, pos, output, ID_table, author_table, charPos_table, cursor_ID, cursor_charPos, cursor_new_pos, ID_stack);
            if insert.content[pos as usize] != 127
            {
                output.push(insert.content[pos as usize]);
                ID_table.push(insert.selfID);
                author_table.push(insert.author);
                charPos_table.push(pos as u32);
            }

            if (insert.selfID == cursor_ID) & (pos == cursor_charPos)
            {
                *cursor_new_pos = output.len() as c_int;
            }
        }

        render_text_internal(set, insert.selfID, insert.content.len() as u8, output, ID_table, author_table, charPos_table, cursor_ID, cursor_charPos, cursor_new_pos, ID_stack);

        ID_stack.pop();
    }

}

unsafe fn expandDynamicArray_uint32 (array: &mut DynamicArray_uint32, new_length: usize) -> i8
{
    if new_length > array.allocated_length as usize
    {
        let new_pointer: *mut u32 = libc::realloc(array.array as *mut libc::c_void, new_length*std::mem::size_of::<u32>()) as *mut u32;
        if new_pointer.is_null()
        {
            return -1;
        }
        else
        {
            array.array = new_pointer;
            array.allocated_length = new_length as i64;
            return 0;
        }
    }
    return 0;
}

