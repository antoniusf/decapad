#![allow(non_snake_case)]
#![allow(dead_code)]
//#![allow(unused_variables)]
#![allow(unused_imports)]

use std::os::raw::{c_int, c_long, c_ulong};
use std::vec::Vec;
use std::{str, char};
use std::thread;
use std::net;
use std::collections::vec_deque::VecDeque;
use std::rc::Rc;
use std::cell::{Cell, RefCell, Ref, RefMut};
use std::ops::Deref;
use std::sync::{Arc, Mutex};
use std::sync::atomic::{AtomicUsize, Ordering};
use std::sync::mpsc;
use std::time::Duration;
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


enum KeyEvent
{
    Character(char),
    Other(u8)
}


#[derive(Debug)]
struct TextInsert
{
    ID: u32,
    parent: u32,
    author: u32,
    charPos: u8,
    lock: u8,
    content: Vec<char>
}

impl TextInsert
{
    fn is_ancestor_of_ID(&self, other_ID: u32, set: &TextInsertSet) -> bool
    {
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

    fn serialize (&self, buffer: &mut Vec<u8>)
    {
        let mut content_string = String::with_capacity(self.content.len()); //convert to UTF-8
        for character in self.content.iter()
        {
            content_string.push(*character);
        }

        serialize_u32(self.ID, buffer);
        serialize_u32(self.parent, buffer);
        serialize_u32(self.author, buffer);
        serialize_u32((self.charPos as u32)<<16 + (content_string.len() as u16), buffer);//NOTE: the u16 is necessary here because while character is one element of our content vector, it can be more than one byte in the utf-8 string (but not more than 255, which is why u16 should be sufficient).
        buffer.extend_from_slice(content_string.as_bytes());
    }

    fn send(&self, network: NetworkState)
    {
        let mut buffer: Vec<u8> = Vec::with_capacity(16+self.content.len());
        self.serialize(&mut buffer);
        network.send(&buffer[..]);
    }

    fn deserialize (buffer: &[u8], set: &mut TextInsertSet, backend_state: &ProtocolBackendState) -> Option<usize>
    {
        if buffer.len() < 16
        {
            println!("TextInsert::deserialize says: given buffer is too short, cannot read header.");
            return None;
        }

        let ID = deserialize_u32(&buffer[0..4]);
        let parent = deserialize_u32(&buffer[4..8]);
        let author = deserialize_u32(&buffer[8..12]);

        let mix = deserialize_u32(&buffer[12..16]);
        let charPos = (mix>>16) as u8;
        let length = (mix as u16) as usize;

        if buffer.len() < 16+length
        {
            println!("TextInsert::deserialize says: given buffer is too for the content it should contain.");
            return None;
        }

        let mut raw_content: Vec<u8> = Vec::with_capacity(length);
        raw_content.extend_from_slice(&buffer[16..16+length]);
        match String::from_utf8(raw_content)
        {
            Ok(content_string) => 
            {
                let mut content: Vec<char> = Vec::with_capacity(255);
                for character in content_string.chars()
                {
                    content.push(character);
                }
                content.shrink_to_fit(); //TODO: is this good or no good?

                match get_insert_by_ID_index(ID, &*set)
                {
                    Some(index) =>
                    {
                        let mut old_insert = &mut set[index];

                        if content.len() >= old_insert.content.len() //TODO: assume this is an update for now, actually we would need authenticity checking (and copying over 127s?)
                        {
                            old_insert.ID = ID;
                            old_insert.parent = parent;
                            old_insert.author = author;
                            old_insert.charPos = charPos;
                            old_insert.content = content;
                        }
                        else
                        {
                            for (old_character, new_character) in old_insert.content.iter_mut().zip(content.iter())
                            {
                                if *new_character == 127 as char
                                {
                                    *old_character = 127 as char
                                }
                            }
                        }
                    },

                    None =>
                    {
                        if (ID >= backend_state.start_ID) & (ID <= backend_state.end_ID)
                        {
                            println!("Insert unserialization failed due to invalid insert ID: The insert would be newly created, but its ID lies within our ID range.");
                            return None;
                        }
                        else
                        {
                            set.push(
                                TextInsert
                                {
                                    ID: ID,
                                    parent: parent,
                                    author: author,
                                    charPos: charPos,
                                    content: content,
                                    lock: 0 //TODO: remove lock?
                                }
                            );
                        }

                    }
                }

            },

            Err(_) =>
            {
                println!("TextInsert::deserialize got invalid UTF-8.");
                return None;
            }
        }
        return Some(16 + length);
    }


}


type TextInsertSet = Vec<TextInsert>;


#[derive(Debug)]
struct TextBufferInternal
{
    text: Vec<char>,
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

struct Spsc_255
{
    buffer: [Cell<u8>; 256],
    pop_index: AtomicUsize,
    push_index: AtomicUsize,
}

pub struct Consumer
{
    queue: Arc<Spsc_255>
}

pub struct Producer
{
    queue: Arc<Spsc_255>
}

impl Spsc_255
{
    fn new() -> (Producer, Consumer)
    {
        let queue = Arc::new(
            Spsc_255
            {
                buffer: [Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8)], //sigh.
                pop_index: AtomicUsize::new(0),
                push_index: AtomicUsize::new(0)
            }
        );

        return (Producer {queue: queue.clone()}, Consumer{queue: queue.clone()});
    }
}

impl Producer
{
    fn push (&self, item: u8) -> bool
    {
        let push_index = self.queue.push_index.load(Ordering::Relaxed);
        if push_index as u8 != (self.queue.pop_index.load(Ordering::Acquire) as u8) - 1
        {
            self.queue.buffer[push_index].set(item);
            self.queue.push_index.store(((push_index as u8) + 1) as usize, Ordering::Release);
            return true;
        }
        else
        {
            return false;
        }
    }
}

impl Consumer
{
    fn pop (&self) -> Option<u8>
    {
        let pop_index = self.queue.push_index.load(Ordering::Relaxed);
        if pop_index as u8 == self.queue.push_index.load(Ordering::Acquire) as u8
        {
            let value = self.queue.buffer[pop_index].get();
            self.queue.pop_index.store(((pop_index as u8) +1) as usize, Ordering::Release);
            return Some(value);
        }
        else
        {
            return None;
        }
    }

    /// Returns a minimum bound of the current length of the queue. In most cases, the value is probably exact, but (due to threading) it is also possible that the queue is longer that.
    /// If do need an exact value, you should probably make sure that the other thread does not push in between and use an atomic fence to ensure you are seeing the most updated push_index.
    fn len (&self) -> u8
    {
        return (self.queue.push_index.load(Ordering::Relaxed) as u8) - (self.queue.pop_index.load(Ordering::Relaxed) as u8)
    }
}

unsafe impl Send for Consumer {} //TODO: formal proof?

fn render_text(set: &TextInsertSet, text_buffer: &mut TextBufferInternal)
{
    let mut ID_stack: Vec<u32> = Vec::new();

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

        for (position, character) in insert.content.iter().enumerate()
        {
            render_text_internal(set, insert.ID, position as u8, &mut *text_buffer, ID_stack);
            if *character != 127 as char
            {
                text_buffer.text.push(*character);
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
pub unsafe extern fn start_backend (own_port: u16, other_port: u16, sync_bit: *mut u8, textbuffer_ptr: *mut TextBuffer) -> *mut Producer
{
    start_backend_safe(own_port, other_port, sync_bit, textbuffer_ptr)
}

fn start_backend_safe (own_port: u16, other_port: u16, sync_bit: *mut u8, c_text_buffer_ptr: *mut TextBuffer) -> *mut Producer
{
	
	let (input_sender, input_receiver): (Producer, Consumer) = Spsc_255::new();
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
                text: Vec::new(),
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
		
		const BUFFER_LENGTH: usize = 10000;
		let mut buffer = [0u8; BUFFER_LENGTH];

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
                            match backend_state
                            {
                                Some(ref backend_state_unpacked) =>
                                {
                                    TextInsert::deserialize(&buffer[4..bytes], &mut set, &backend_state_unpacked);
                                },
                                None => println!("Received data without being initialized first.")
                            }
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
				Err(_) => ()
			}
			
			//check input queue
			{
				let mut new_text_raw: Vec<u8> = Vec::new();
				while let Some(byte) = input_receiver.pop()
				{
					new_text_raw.push(byte);
				}

                match backend_state
                {
                    Some(ref mut inner_backend_state) =>
                    {
                        let mut read_index = 0;
                        let mut new_text: Vec<KeyEvent> = Vec::new();
                        while read_index < new_text_raw.len()
                        {
                            let possible_next_control_byte = new_text_raw[read_index..].iter().position(|&x| x==255u8); //NOTE: we are using 255 as a control byte so we can send non-character-keypresses over the same queue. 255 is invalid utf-8, so we can use it without limiting its utility as a utf-8 character queue. The 255 control byte is followed by a second byte not to be interpreted as utf-8, which designates what other key has been pressed. After that, normal utf-8 starts again.

                            match possible_next_control_byte
                            {
                                Some(next_control_byte) =>
                                {
                                    match str::from_utf8(&new_text_raw[read_index..next_control_byte])
                                    {
                                        Ok(string_slice) => new_text.extend(string_slice.chars().map(|character| KeyEvent::Character(character))),
                                        Err(_) => panic!("Got invalid UTF-8 from SDL!")
                                    }

                                    new_text.push(KeyEvent::Other(new_text_raw[next_control_byte+1]));
                                    read_index = next_control_byte +2;
                                }

                                None =>
                                {
                                    match str::from_utf8(&new_text_raw[read_index..])
                                    {
                                        Ok(string_slice) => new_text.extend(string_slice.chars().map(|character| KeyEvent::Character(character))),
                                        Err(_) => panic!("Got invalid UTF-8 from SDL!")
                                    }
                                    read_index = new_text_raw.len();
                                }
                            }
                        }

                        if new_text.len() > 0
                        {
                            println!("Received text!");
                            for event in new_text.drain(..)
                            {
                                match event
                                {
                                    KeyEvent::Character(character) => insert_character(&mut set, character, &mut network, &mut text_buffer, inner_backend_state),
                                    KeyEvent::Other(other_event) =>
                                    {
                                        if other_event == 127
                                        {
                                            //delete_letter
                                        }
                                        //...
                                    }
                                }
                            }

                            render_text(&set, &mut text_buffer);
                            println!("Newly rendered text: {:?}", &text_buffer.text);
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
                    },

                    None => println!("Got some keypresses, but weren't initialized.")
					
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

fn get_insert_by_ID_index (ID: u32, set: &TextInsertSet) -> Option<usize>
{
    for (index, insert) in set.iter().enumerate()
    {
        if insert.ID == ID
        {
            return Some(index);
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

    if make_new_insert
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
                content: vec![character]
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
pub unsafe extern fn rust_text_input (text: *const u8, sender_box_ptr: *mut Producer) -> *mut Producer
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
				if sender.push(*text.offset(i)) == false
                {
                    println!("rust_text_input says: keypress buffer has run full");
                }
			}
            i += 1;
		}
	}
	let new_box_pointer = Box::into_raw(sender_box);
	return new_box_pointer;
}
