#![allow(non_snake_case)]
#![allow(dead_code)]
#![allow(unused_variables)]

use std::os::raw::{c_int, c_long, c_ulong};
use std::vec::Vec;
use std::thread;
use std::net;
use std::collections::vec_deque::VecDeque;
use std::rc::Rc;
use std::cell::{RefCell, Ref, RefMut};
use std::ops::Deref;
use std::sync::{Arc, Mutex};
use std::time::Duration;
use std::sync::mpsc;
extern crate libc;
extern crate crc;
use crc::crc32::checksum_ieee;

fn compare_slices (a: &[u8], b: &[u8]) -> bool
{
    if a.len() == b.len()
    {
        for i in 0..a.len()
        {
            if a[i] != b[i]
            {
                return false;
            }
        }
        return true;
    }
    else
    {
        return false;
    }
}

fn deserialize_u32 (buffer: &[u8]) -> u32
{
    if buffer.len() < 4
    {
        panic!("deserialize_u32 needs a slice that is at least 4 bytes long, but it got only {}.", buffer.len());
    }

    let result = (buffer[0] as u32)<<24 + (buffer[1] as u32)<<16 + (buffer[2] as u32)<<8 + (buffer[3] as u32);
    return result;
}

fn serialize_u32 (number: u32, buffer: &mut Vec<u8>)
{
    buffer.push((number>>24) as u8);
    buffer.push((number>>16) as u8);
    buffer.push((number>>8) as u8);
    buffer.push(number as u8);
}


#[derive(Debug)]
struct TextInsert
{
    ID: u32,
    parent: u32,
    author: u32,
    charPos: u8,
    lock: u8,
    content: String
}

impl TextInsert
{
    fn is_ancestor_of_ID(&self, other_ID: u32, set: &TextInsertSet) -> bool
    {
        let selfID = self.ID;
        match get_insert_by_ID(other_ID, &*set)
        {
            Some(&TextInsert { parent, ..}) if parent == self.ID => true,
            Some(&TextInsert { parent, ..}) if parent == 0 => false,
            Some(&TextInsert { parent, ..}) => self.is_ancestor_of_ID(parent, &*set),
            None => {println!("TextInsert.is_ancestor_of could not find the insert {}", other_ID); false}
        }
    }

    fn is_ancestor_of(&self, other: &TextInsert, set: &TextInsertSet) -> bool
    {
        if other.parent == self.ID
        {
            true
        }
        else if other.parent == 0
        {
            false
        }
        else
        {
            if let Some(& ref parent) = get_insert_by_ID(other.parent, &*set) //the &mut ref parent is for converting the mutable to an immutable borrow
            {
                self.is_ancestor_of(parent, &*set)
            }
            else
            {
                println!("TextInsert.is_ancestor_of could not find the ancestor of {}, which is {}.", other.ID, other.parent);
                false
            }
        }
    }

}


type TextInsertSet = Vec<TextInsert>;


#[derive(Debug)]
struct TextBufferInternal
{
    text: String,
    ID_table: Vec<u32>,
    author_table: Vec<u32>,
    charPos_table: Vec<u8>,
    cursor_ID: Option<u32>, //ID of the insert where the parent is
    cursor_charPos: Option<u8>, //character position of the cursor inside the insert
    cursor_globalPos: u32, //position of the cursor in the buffer
    active_insert: Option<u32>
}

#[derive(Debug)]
struct ProtocolBackendState
{
    start_ID: u32,
    end_ID: u32,
    author_ID: u32
}

#[derive(Debug)]
struct NetworkState
{
    socket: net::UdpSocket,
    other_address: net::SocketAddr,
    send_queue: VecDeque<u32>
}

impl NetworkState
{
    fn send(&self, data: &[u8])
    {
        match self.socket.send_to(data, &self.other_address)
        {
            Err(error) => println!("Failed to send data: {:?}", error),
            _ => ()
        }
    }
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

pub struct ThreadPointerWrapper
{
    text_buffer: *mut TextBuffer,
    sync_bit: *mut u8
}

unsafe impl Send for ThreadPointerWrapper {}

fn initialize_crc()
{
    let poly: u8 = 0x97;
    let mut crc_table = [0u8; 256];

    let mut i = 0u8;
    while i <= 255
    {
        let mut indexbyte = i;
        let mut j = 0u8;
        while j <= 7
        {
            if (indexbyte >> 7) == 1
            {
                indexbyte <<= 1;
                indexbyte ^= poly;
            }
            else
            {
                indexbyte <<= 1;
            }

            j += 1;
        }
        crc_table[i as usize] = indexbyte;

        i += 1;
    }
}



fn render_text(set: &TextInsertSet, text_buffer: &mut TextBufferInternal)
{
    let mut ID_stack: Vec<u32> = Vec::new();
    let mut cursor_new_pos = 0;

    text_buffer.text.clear();
    text_buffer.ID_table.clear();
    text_buffer.author_table.clear();
    text_buffer.charPos_table.clear();

    render_text_internal(&set, 0, 0, &mut *text_buffer, &mut ID_stack);

}

fn render_text_internal(set: &TextInsertSet, parentID: u32, charPos: u8, text_buffer: &mut TextBufferInternal, ID_stack: &mut Vec<u32>)
{
    let mut inserts: Vec<&TextInsert> = Vec::new();
    for i in 0..set.len()
    {
        let insert = &set[i];
        if (insert.parent == parentID) & (insert.charPos == charPos)
        {
            if ID_stack.contains(&insert.ID)
            {
                println!("render_text has detected a cyclic dependency between inserts. This should never happen, as it does not conform to the protocol specification.");
            }
            else
            {
                inserts.push(insert);
            }
        }
    }
    inserts.sort_by(|a, b| a.ID.cmp(&b.ID));

    for insert in inserts
    {
        ID_stack.push(insert.ID);

        for (position, character) in insert.content.chars().enumerate()
        {
            render_text_internal(set, insert.ID, position as u8, &mut *text_buffer, ID_stack);
            if character != 127 as char
            {
                text_buffer.text.push(character);
                text_buffer.ID_table.push(insert.ID);
                text_buffer.author_table.push(insert.author);
                text_buffer.charPos_table.push(position as u8);
            }

            if (Some(insert.ID) == text_buffer.cursor_ID) & (Some(position as u8) == text_buffer.cursor_charPos)
            {
                text_buffer.cursor_globalPos = text_buffer.text.len() as u32;
            }
        }

        render_text_internal(set, insert.ID, insert.content.len() as u8, &mut *text_buffer, ID_stack);

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
            array.allocated_length = new_length as c_long;
            return 0;
        }
    }
    return 0;
}

#[no_mangle]
pub unsafe extern fn start_backend (own_port: u16, other_port: u16, sync_bit: *mut u8, textbuffer_ptr: *mut TextBuffer) -> *mut mpsc::Sender<u8>
{
    start_backend_safe(own_port, other_port, sync_bit, textbuffer_ptr)
}

fn start_backend_safe (own_port: u16, other_port: u16, sync_bit: *mut u8, c_text_buffer_ptr: *mut TextBuffer) -> *mut mpsc::Sender<u8>
{
	
	let (input_sender, input_receiver): (mpsc::Sender<u8>, mpsc::Receiver<u8>) = mpsc::channel();
	let input_sender_box = Box::new(input_sender);

    let c_pointers = ThreadPointerWrapper { text_buffer: c_text_buffer_ptr, sync_bit: sync_bit };
	
	thread::spawn(move ||
	{
        //set up data structures
        let mut set: TextInsertSet = Vec::new();
        let mut backend_state: Option<ProtocolBackendState> = None;
        let mut text_buffer =
            TextBufferInternal
            {
                text: String::new(),
                ID_table: Vec::new(),
                author_table: Vec::new(),
                charPos_table: Vec::new(),
                cursor_ID: None,
                cursor_charPos: None,
                cursor_globalPos: 0,
                active_insert: None
            }
        ;

        //network
		let mut own_socket = net::UdpSocket::bind(("127.0.0.1", own_port)).expect("Socket fail!"); //TODO: set nonblocking (this is only possible in rusts nightly right now)
		own_socket.set_read_timeout(Some(Duration::new(0, 1)));
		own_socket.set_write_timeout(Some(Duration::new(0, 1))); //I hope this works as nonblocking for now
        let mut network =
            NetworkState
            {
                socket: own_socket,
                other_address: net::SocketAddr::V4(net::SocketAddrV4::new(net::Ipv4Addr::new(127, 0, 0, 1), other_port)),
                send_queue: VecDeque::new()
            }
        ;
		
		const buffer_length: usize = 10000;
		let mut buffer = [0u8; buffer_length];

		loop
		{
			//check network
			match network.socket.recv_from(&mut buffer)
			{
				Ok((bytes, address)) => 
                {
                    if address.port() == other_port //TODO: check IP address
                    {
                        //length?
                        if compare_slices("init".as_bytes(), &buffer[0..4])
                        {
                            if backend_state.is_none()
                            {
                                if bytes == 4+4+4+4
                                {
                                    if checksum_ieee(&buffer[0..12]) == deserialize_u32(&buffer[12..16])
                                    {
                                        let start_ID = deserialize_u32(&buffer[4..8]);
                                        let end_ID = deserialize_u32(&buffer[8..12]);
                                        backend_state = Some(ProtocolBackendState { start_ID: start_ID, end_ID: end_ID, author_ID: start_ID } );
                                    }
                                }
                            }
                        }

                        else if compare_slices("inrq".as_bytes(), &buffer[0..4])
                        {
                            //send init
                            match backend_state
                            {
                                None => backend_state = Some(ProtocolBackendState { start_ID: 1, end_ID: 1025, author_ID: 1 } ), //TODO: find a better scheme for deciding initialization
                                Some(ProtocolBackendState {end_ID, ..}) =>
                                {
                                    let mut send_buffer: Vec<u8> = Vec::new();
                                    send_buffer.extend_from_slice("init".as_bytes());
                                    serialize_u32(end_ID+1, &mut send_buffer);
                                    serialize_u32(end_ID+1025, &mut send_buffer);
                                    let checksum: u32 = checksum_ieee(&send_buffer[..]);
                                    serialize_u32(checksum, &mut send_buffer);
                                    network.send(&send_buffer[..]);
                                }
                            }
                        }

                        else if compare_slices("data".as_bytes(), &buffer[0..4])
                        {
                        }

                        else if compare_slices("ack ".as_bytes(), &buffer[0..4])
                        {
                            if bytes == 4+4
                            {
                                let ack_ID = deserialize_u32(&buffer[4..8]);
                                if let Some(index) = network.send_queue.iter().position(|&x| x == ack_ID)
                                {
                                    network.send_queue.remove(index);
                                }
                            }
                        }
                    }
                },
				Err(error) => ()
			}
			
			//check input queue
			{
				let mut new_text_raw: Vec<u8> = Vec::new();
				while let Ok(byte) = input_receiver.try_recv()
				{
					new_text_raw.push(byte);
				}
				let mut new_text = String::from_utf8(new_text_raw).expect("Got invalid UTF-8 from SDL!");
				if new_text.len() > 0
				{
                    println!("Received text!: {}", new_text);
					for character in new_text.drain(..)
					{
                        if character == 127 as char
                        {
                            //delete_letter
                        }
                        else
                        {
                            match backend_state
                            {
                                Some(ref mut inner_state) => insert_character(&mut set, character, &mut network, &mut text_buffer, inner_state),
                                None => println!("Tried to insert a character while being uninitialized. The character will not be inserted.")
                            }
                        }
					}

                    render_text(&set, &mut text_buffer);
                    println!("Newly rendered text: {}", &text_buffer.text);
                    println!("Data: {:?}", &set);

                    unsafe
                    {
                        *c_pointers.sync_bit = 1;
                        while *c_pointers.sync_bit == 1
                        {
                        }
                    }
					
					//copy
					
					
				}
			}
		}
	});
	
	return Box::into_raw(input_sender_box);
}

fn get_insert_by_ID (ID: u32, set: &TextInsertSet) -> Option<&TextInsert>
{
    for insert in set
    {
        if insert.ID == ID
        {
            return Some(insert);
        }
    }

    return None;
}

fn get_insert_by_ID_mut (ID: u32, set: &mut TextInsertSet) -> Option<&mut TextInsert>
{
    for insert in set
    {
        if insert.ID == ID
        {
            return Some(insert);
        }
    }

    return None;
}

fn insert_character<'a> (set: &mut TextInsertSet, character: char, network: &mut NetworkState, text_buffer: &mut TextBufferInternal, state: &mut ProtocolBackendState)
{
    let mut make_new_insert = false;
    if let Some(active_insert_ID) = text_buffer.active_insert
    {
        if let Some(active_insert) = get_insert_by_ID_mut(active_insert_ID, &mut *set)
        {
            if active_insert.content.len() < 255
            {
                active_insert.content.push(character);
                network.send_queue.push_back(active_insert_ID);
                //TODO: update hints?
            }
            else
            {
                make_new_insert = true;
            }
        }
        else
        {
            println!("insert_character says: active_insert was not found in insert set (ID: {})", active_insert_ID);
            make_new_insert = false;
        }
    }

    else
    {
        make_new_insert = true;
    }

    if make_new_insert == true
    {
        let position = text_buffer.cursor_globalPos;

        let (parent, parent_charPos): (u32, u8) =
        if position == 0
        {
            if set.len() == 0
            {
                (0, 0)
            }
            else
            {
                (text_buffer.ID_table[(position-1) as usize], text_buffer.charPos_table[(position-1) as usize] + 1)
            }
        }
        else
        {
            if position == text_buffer.ID_table.len() as u32
            {
                (text_buffer.ID_table[(position-1) as usize], text_buffer.charPos_table[(position-1) as usize] + 1)
            }
            else
            {
                let left_insert = get_insert_by_ID(text_buffer.ID_table[(position-1) as usize], &*set).expect("insert_character says: ID_table contains at least one ID that is does not belong to any insert currently in the insert set.");

                let right_insert = get_insert_by_ID(text_buffer.ID_table[position as usize], &*set).expect("insert_character says: ID_table contains at least one ID that is does not belong to any insert currently in the insert set.");


                if left_insert.is_ancestor_of(right_insert, &*set)
                {
                    (right_insert.ID, text_buffer.charPos_table[position as usize])
                }
                else
                {
                    (left_insert.ID, text_buffer.charPos_table[(position-1) as usize])
                }
            }
        };

        let new_insert = 
            TextInsert
            {
                ID: state.start_ID,
                parent: parent,
                author: state.start_ID,
                charPos: parent_charPos,
                lock: 0,
                content: character.to_string()
            };
        

        state.start_ID += 1;

        text_buffer.cursor_ID = Some(new_insert.ID);
        text_buffer.cursor_charPos = Some(0);

        text_buffer.active_insert = Some(new_insert.ID);
        network.send_queue.push_back(new_insert.ID);

        set.push(new_insert);
        //TODO: send_now?
    }
                

}


#[no_mangle]
pub unsafe extern fn rust_text_input (text: *const u8, sender_box_ptr: *mut mpsc::Sender<u8>) -> *mut mpsc::Sender<u8>
{
	let mut sender_box = Box::from_raw(sender_box_ptr);
	{
		let sender = &*sender_box;
		let mut i = 0;
		loop
		{
			if *text.offset(i) == 0
			{
				break;
			}
			else
			{
				match sender.send(*text.offset(i))
				{
					Err(error) => println!("Failed to send keypress to the rust thread: {}", error),
					_ => ()
				}
			}
            i += 1;
		}
	}
	let new_box_pointer = Box::into_raw(sender_box);
	return new_box_pointer;
}
