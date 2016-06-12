#![allow(non_snake_case)]
#![allow(dead_code)]
//#![allow(unused_variables)]
#![allow(unused_imports)]

mod sync;
use sync::OneThreadTent;
use sync::spsc_255::{self, Producer, Consumer};

mod tnetstring;

mod crc;

use crc::crc;

use std::{mem, net, str, char, thread, process};

use std::os::raw::{c_int, c_long, c_ulong};
use std::sync::atomic::{AtomicBool, AtomicUsize, Ordering};

use std::vec::Vec;
use std::collections::vec_deque::VecDeque;
use std::rc::Rc;
use std::sync::{Arc, Condvar, Mutex};
use std::cell::{Cell, RefCell, Ref, RefMut};

use std::ops::Deref;
use std::sync::mpsc;
use std::time::Duration;

extern crate libc;

fn deserialize_u16 (buffer: &[u8]) -> u16
{
    if buffer.len() < 2
    {
        panic!("deserialize_u32 needs a slice that is at least 2 bytes long, but it got only {}.", buffer.len());
    }

    let result = ((buffer[0] as u16)<<8) + (buffer[1] as u16);
    return result;
}

fn deserialize_u32 (buffer: &[u8]) -> u32
{
    if buffer.len() < 4
    {
        panic!("deserialize_u32 needs a slice that is at least 4 bytes long, but it got only {}.", buffer.len());
    }

    let result = ((buffer[0] as u32)<<24) + ((buffer[1] as u32)<<16) + ((buffer[2] as u32)<<8) + (buffer[3] as u32);
    return result;
}

fn serialize_u16 (number: u16, buffer: &mut Vec<u8>)
{
    buffer.push((number>>8) as u8);
    buffer.push(number as u8);
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
            if let Some(parent) = get_insert_by_ID(other.parent, &*set)
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

    fn get_number_of_deleted_chars (&self) -> u8
    {
        let mut number = 0;
        for character in self.content.iter()
        {
            if *character == 127 as char
            {
                number += 1;
            }
        }

        return number;
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
        buffer.push(self.charPos);
        buffer.extend_from_slice(content_string.as_bytes());
    }

    fn send(&self, network: &mut NetworkState)
    {
        let mut buffer: Vec<u8> = Vec::with_capacity(20+self.content.len());
        buffer.push('i' as u8);
        serialize_u16(0, &mut buffer); //pad ID placeholder
        self.serialize(&mut buffer);
        network.send(&buffer[..]);
    }

    fn deserialize (buffer: &[u8], set: &mut TextInsertSet, backend_state: &ProtocolBackendState) -> Option<(usize, bool)>
    {
        if buffer.len() < 13
        {
            println!("TextInsert::deserialize says: given buffer is too short, cannot read header.");
            return None;
        }

        let mut new_insert_created = false;
        let mut insert_index = 0;

        let ID = deserialize_u32(&buffer[0..4]);
        let parent = deserialize_u32(&buffer[4..8]);
        let author = deserialize_u32(&buffer[8..12]);

        let charPos = buffer[12];

        let mut raw_content: Vec<u8> = Vec::with_capacity(buffer.len()-13);
        raw_content.extend_from_slice(&buffer[13..]);
        match String::from_utf8(raw_content)
        {
            Ok(content_string) => 
            {
                let mut content: Vec<char> = Vec::with_capacity(254);
                for character in content_string.chars()
                {
                    content.push(character);
                }
                content.shrink_to_fit();

                if content.len() > 255
                {
                    return None;
                }

                match get_insert_by_ID_index(ID, &*set)
                {
                    Some(index) =>
                    {
                        insert_index = index;
                        let mut old_insert = &mut set[index];

                        if content.len() >= old_insert.content.len() //TODO: assume this is an update for now, actually we would need authenticity checking (and copying over 127s?)
                        {
                            //old_insert.ID = ID;
                            //old_insert.parent = parent;
                            //old_insert.author = author;
                            //old_insert.charPos = charPos;
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
                            new_insert_created = true;

                            insert_index = set.len();
                            set.push(
                                TextInsert
                                {
                                    ID: ID,
                                    parent: parent,
                                    author: author,
                                    charPos: charPos,
                                    content: content,
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
        return Some((insert_index, new_insert_created));
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
    cursor_globalPos: usize, //position of the cursor in the buffer
    active_insert: Option<u32>,
    needs_updating: bool
}

#[derive(Debug)]
struct ProtocolBackendState
{
    start_ID: u32,
    end_ID: u32,
    author_ID: u32
}

#[derive(Clone, Copy, Debug)]
enum SendType
{
    Full,
    Append { position: u8 },
    Delete { start_pos: u8, end_pos: u8 }
}

#[derive(Clone, Copy, Debug)]
struct SendQueueEntry
{
    ID: u32,
    kind: SendType
}


#[derive(Debug)]
struct NetworkState
{
    socket: net::UdpSocket,
    other_address: net::SocketAddr,
    send_queue: VecDeque<SendQueueEntry>,
    cheap_queue: VecDeque<(u32, Vec<u8>)>,
    cheap_counter: u32
}

impl NetworkState
{
    fn send(&self, data: &[u8])
    {
        let checkvalue = crc(data);
        let mut checked_buffer = Vec::with_capacity(4+data.len());
        serialize_u32(checkvalue, &mut checked_buffer);
        checked_buffer.extend_from_slice(data);

        match self.socket.send_to(&checked_buffer[..], &self.other_address)
        {
            Err(error) => println!("Failed to send data: {:?}", error),
            _ => ()
        }
    }

    fn send_cheap (&mut self, data: &[u8])
    {
        let message_id = self.cheap_counter;
        self.cheap_counter += 1;

        let mut augmented_message = Vec::with_capacity(data.len()+1);
        augmented_message.push('c' as u8);
        serialize_u32(message_id, &mut augmented_message);
        augmented_message.extend_from_slice(data);
        self.cheap_queue.push_back((message_id, augmented_message));
    }

    fn send_acka (&self, ID: u32, length: u8)
    {
        let mut ack_buffer = Vec::new();
        ack_buffer.push('A' as u8);
        serialize_u16(0, &mut ack_buffer);
        serialize_u32(ID, &mut ack_buffer);
        ack_buffer.push(length);
        self.send(&ack_buffer[..]);
    }

    fn get_queue_entry (&self, ID: u32) -> Option<usize>
    {
        for (index, entry) in self.send_queue.iter().enumerate()
        {
            if entry.ID == ID
            {
                return Some(index);
            }
        }
        return None;
    }

    fn enqueue_full (&mut self, ID: u32)
    {
        if let Some(index) = self.get_queue_entry(ID)
        {
            let entry = &mut self.send_queue[index];
            match entry.kind
            {
                SendType::Full => (),
                SendType::Append {..} | SendType::Delete {..} => entry.kind = SendType::Full
            }
        }

        else
        {
            self.send_queue.push_back( SendQueueEntry { ID: ID, kind: SendType::Full });
        }
    }

    fn enqueue_append (&mut self, ID: u32, position: u8)
    {
        if let Some(index) = self.get_queue_entry(ID)
        {
            let entry = self.send_queue[index];
            match entry.kind
            {
                SendType::Full => (),
                SendType::Delete {..} => self.send_queue.push_back( SendQueueEntry { ID: ID, kind: SendType::Append { position: position } }),
                SendType::Append {..} => ()
            }
        }

        else
        {
            self.send_queue.push_back( SendQueueEntry { ID: ID, kind: SendType::Append { position: position } });
        }
    }

    fn enqueue_delete (&mut self, ID: u32, start: u8, end: u8)
    {
        let mut append_new = false;

        if let Some(index) = self.get_queue_entry(ID)
        {
            let entry = &mut self.send_queue[index];
            match entry.kind
            {
                SendType::Full => (),
                SendType::Delete { ref mut start_pos, ref mut end_pos } =>
                {
                    *start_pos = start;
                    *end_pos = end;
                },
                SendType::Append {..} => append_new = true
            }
        }
        
        else
        {
            append_new = true;
        }

        if append_new
        {
            self.send_queue.push_back( SendQueueEntry { ID: ID, kind: SendType::Delete { start_pos: start, end_pos: end }});
        }
    }

    fn resend(&mut self, set: &TextInsertSet)
    {
        if let Some(entry) = self.send_queue.pop_front()
        {
            if let Some(insert) = get_insert_by_ID(entry.ID, set)
            {
                self.send_queue.push_back(entry);

                match entry.kind
                {
                    SendType::Full => insert.send(self),
                    SendType::Append { position } =>
                    {
                        if position < insert.content.len() as u8
                        {
                            let mut buffer = Vec::new();
                            buffer.push('a' as u8);
                            serialize_u16(0, &mut buffer);
                            serialize_u32(insert.ID, &mut buffer);
                            buffer.push(position);

                            let mut utf8_content = String::new();
                            for character in &insert.content[position as usize..]
                            {
                                utf8_content.push(*character);
                            }

                            buffer.extend_from_slice(utf8_content.as_bytes());

                            self.send(&buffer[..]);
                        }
                    },
                    SendType::Delete { start_pos, end_pos } =>
                    {
                        let mut buffer = Vec::new();
                        buffer.push('d' as u8);
                        serialize_u16(0, &mut buffer);
                        serialize_u32(insert.ID, &mut buffer);
                        buffer.push(start_pos);
                        buffer.push(end_pos);

                        self.send(&buffer[..]);
                    }
                }
            }
            else
            {
                println!("Warning: the insert send queue contained an unknown insert ID.");
            }
        }

        if let Some((msg_id, msg)) = self.cheap_queue.pop_front()
        {
            self.send(&msg[..]);
            self.cheap_queue.push_back((msg_id, msg));
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
    ahead_cursor: c_int,
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
}

unsafe impl Send for ThreadPointerWrapper {}

#[derive(PartialEq, Eq)]
enum FrontendSyncstate
{
    lockedsync,
    unsynced,
    syncing
}

#[derive(PartialEq, Eq)]
enum BackendSyncstate
{
    lockedsync,
    unsynced,
    syncing
}

pub struct FFIData
{
    sender: Producer,
    receiver: Consumer,
    tent: OneThreadTent,
    state: FrontendSyncstate,
    is_buffer_locked: Arc<AtomicBool>
}


fn render_text(set: &TextInsertSet, text_buffer: &mut TextBufferInternal)
{
    let mut ID_stack: Vec<u32> = Vec::new();

    let mut temp_cursor_ID = false;
    if text_buffer.cursor_ID.is_none()
    {
        let cursor = text_buffer.cursor_globalPos;
        if cursor > 0
        {
            text_buffer.cursor_ID = Some(text_buffer.ID_table[cursor-1]);
            text_buffer.cursor_charPos = Some(text_buffer.charPos_table[cursor-1] +1);
        }
        else
        {
            text_buffer.cursor_ID = Some(0);
            text_buffer.cursor_charPos = Some(0);
        }
        temp_cursor_ID = true;
    }


    text_buffer.text.clear();
    text_buffer.ID_table.clear();
    text_buffer.author_table.clear();
    text_buffer.charPos_table.clear();

    render_text_internal(&set, 0, 0, &mut *text_buffer, &mut ID_stack);

    if text_buffer.cursor_ID.unwrap() == 0 //edge case
    {
        text_buffer.cursor_globalPos = 0;
    }

    if temp_cursor_ID
    {
        text_buffer.cursor_ID = None;
        text_buffer.cursor_charPos = None;
    }

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
            if (Some(insert.ID) == text_buffer.cursor_ID) & (Some(position as u8) == text_buffer.cursor_charPos) //NOTE: maybe keep information on where the cursor is attached. would only be necessary for deleting to the right, normal backspace and insertion should be attached to the left.
            {
                text_buffer.cursor_globalPos = text_buffer.text.len();
            }

            render_text_internal(set, insert.ID, position as u8, &mut *text_buffer, ID_stack);
            if *character != 127 as char
            {
                text_buffer.text.push(*character);
                text_buffer.ID_table.push(insert.ID);
                text_buffer.author_table.push(insert.author);
                text_buffer.charPos_table.push(position as u8);
            }
        }

        if (Some(insert.ID) == text_buffer.cursor_ID) & (Some(insert.content.len() as u8) == text_buffer.cursor_charPos)
        {
            text_buffer.cursor_globalPos = text_buffer.text.len();
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

fn synchronize_buffers ( text_buffer: &TextBufferInternal, c_pointers: &ThreadPointerWrapper, is_buffer_locked: &Arc<AtomicBool> )
{
    assert!(is_buffer_locked.load(Ordering::Acquire) == true);

    unsafe
    {

        let mut c_text_buffer = &mut *c_pointers.text_buffer;
        c_text_buffer.cursor = text_buffer.cursor_globalPos as c_int;
        c_text_buffer.ahead_cursor = c_text_buffer.cursor;

        expandDynamicArray_uint32(&mut c_text_buffer.text, text_buffer.text.len());
        c_text_buffer.text.length = text_buffer.text.len() as c_long;
        for (offset, character) in text_buffer.text.iter().enumerate()
        {
            *c_text_buffer.text.array.offset(offset as isize) = *character as u32;
        }

        expandDynamicArray_uint32(&mut c_text_buffer.author_table, text_buffer.author_table.len());
        c_text_buffer.author_table.length = text_buffer.author_table.len() as c_long;
        for (offset, author_ID) in text_buffer.author_table.iter().enumerate()
        {
            *c_text_buffer.author_table.array.offset(offset as isize) = *author_ID;
        }
    }

    is_buffer_locked.store(false, Ordering::Release);
}

#[no_mangle]
pub unsafe extern fn start_backend (own_port: u16, other_port: u16, textbuffer_ptr: *mut TextBuffer) -> *mut FFIData
{
    start_backend_safe(own_port, other_port, textbuffer_ptr)
}

fn start_backend_safe (own_port: u16, other_port: u16, c_text_buffer_ptr: *mut TextBuffer) -> *mut FFIData
{
	
	let (input_sender, input_receiver): (Producer, Consumer) = spsc_255::new();
    let (tent, other_thread_tent) = OneThreadTent::new();
    let is_buffer_synchronized = Arc::new(AtomicBool::new(true));
    let is_buffer_locked = Arc::new(AtomicBool::new(false));
    let is_buffer_synchronized_clone = is_buffer_synchronized.clone();
    let is_buffer_locked_clone = is_buffer_locked.clone();

    let c_pointers = ThreadPointerWrapper { text_buffer: c_text_buffer_ptr };

    let mut syncstate = BackendSyncstate::lockedsync;
    let (sender, syncstate_receiver) = spsc_255::new();
	
	thread::Builder::new().name("Rust".to_string()).spawn(move ||
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
                active_insert: None,
                needs_updating: false
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
                send_queue: VecDeque::new(),
                cheap_queue: VecDeque::new(),
                cheap_counter: 0
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
                        println!("Received data.\n{:?}\n", &buffer[4..bytes]);

                        let checkvalue = deserialize_u32(&buffer[0..4]);
                        if crc(&buffer[4..bytes]) == checkvalue
                        {
                            if 'R' as u8 == buffer[4]
                            {
                                if backend_state.is_none()
                                {
                                    if bytes == 4+1+4+4
                                    {
                                        let start_ID = deserialize_u32(&buffer[5..9]);
                                        let end_ID = deserialize_u32(&buffer[9..13]);
                                        backend_state = Some(ProtocolBackendState { start_ID: start_ID, end_ID: end_ID, author_ID: start_ID } );
                                    }
                                }
                            }

                            else if 'r' as u8 == buffer[4]
                            {
                                //send init
                                match backend_state
                                {
                                    None => backend_state = Some(ProtocolBackendState { start_ID: 1, end_ID: 1025, author_ID: 1 } ), //TODO: find a better scheme for deciding initialization
                                    Some(ProtocolBackendState {end_ID, ..}) =>
                                    {
                                        let mut send_buffer: Vec<u8> = Vec::new();
                                        send_buffer.push('R' as u8);
                                        serialize_u32(end_ID+1, &mut send_buffer);
                                        serialize_u32(end_ID+1025, &mut send_buffer);
                                        network.send(&send_buffer[..]);
                                    }
                                }
                            }

                            else if 'i' as u8 == buffer[4]
                            {
                                if bytes >= 20
                                {
                                    match backend_state
                                    {
                                        Some(ref backend_state_unpacked) =>
                                        {
                                            match TextInsert::deserialize(&buffer[7..bytes], &mut set, &backend_state_unpacked) //start at 7 to ignore pad ID for now
                                            {
                                                Some((insert_index, new_insert_created)) =>
                                                {
                                                    let insert = &set[insert_index];
                                                    text_buffer.needs_updating = true;
                                                    let mut ack_buffer = Vec::with_capacity(8);
                                                    ack_buffer.push('I' as u8);
                                                    serialize_u16(0, &mut ack_buffer);
                                                    serialize_u32(insert.ID, &mut ack_buffer);
                                                    ack_buffer.push(insert.content.len() as u8);
                                                    ack_buffer.push(insert.get_number_of_deleted_chars());
                                                    network.send(&ack_buffer[..]);
                                                    println!("Deserialized insert.");
                                                },
                                                None => ()
                                            }
                                        },
                                        None => println!("Received data without being initialized first.")
                                    }
                                }
                            }

                            else if 'I' as u8 == buffer[4]
                            {
                                if bytes == 4+1+2+4+1+1
                                {
                                    let ack_ID = deserialize_u32(&buffer[7..11]);
                                    let ack_content_length = buffer[11];
                                    let ack_deleted_chars = buffer[12];
                                    if let Some(index) = network.send_queue.iter().position(|&x| x.ID == ack_ID)
                                    {
                                        if let Some(insert) = get_insert_by_ID(ack_ID, &set)
                                        {
                                            if (ack_content_length >= insert.content.len() as u8) & (ack_deleted_chars >= insert.get_number_of_deleted_chars())
                                            {
                                                network.send_queue.remove(index);
                                            }
                                        }
                                    }
                                }
                            }

                            else if 'a' as u8 == buffer[4]
                            {
                                println!("Received apnd.");
                                if bytes >= 4+1+2+4+1
                                {
                                    if backend_state.is_some()
                                    {
                                        let insert_ID = deserialize_u32(&buffer[7..11]);

                                        if let Some(mut insert) = get_insert_by_ID_mut(insert_ID, &mut set)
                                        {
                                            let data_length = bytes-4-1-2-4-1;
                                            let append_start = buffer[11] as usize;

                                            if data_length+append_start <= insert.content.len()
                                            {
                                                //no new data
                                            }

                                            else
                                            {
                                                let mut data;
                                                if append_start < insert.content.len()
                                                {
                                                    data = Some(&buffer[insert.content.len()-append_start +12 .. bytes]);
                                                }
                                                else if append_start == insert.content.len()
                                                {
                                                    data = Some(&buffer[12..bytes]);
                                                }
                                                else
                                                {
                                                    println!("Warning: received append that was too far ahead"); //TODO: change this when resend insert requests are there
                                                    data = None;
                                                }

                                                if let Some(data) = data
                                                {
                                                    if let Ok(utf8_data) = str::from_utf8(data)
                                                    {
                                                        for character in utf8_data.chars()
                                                        {
                                                            insert.content.push(character);
                                                        }
                                                    }
                                                    text_buffer.needs_updating = true;
                                                }
                                            }

                                            network.send_acka(insert.ID, insert.content.len() as u8);
                                            println!("Sent ack apnd");
                                        }
                                    }
                                }
                            }

                            else if 'A' as u8 == buffer[4]
                            {
                                if bytes == 4+1+2+4+1
                                {
                                    let insert_ID = deserialize_u32(&buffer[7..11]);
                                    let acknowledged_position = buffer[11];

                                    if let Some(entry_index) = network.get_queue_entry(insert_ID)
                                    {
                                        if let Some(insert) = get_insert_by_ID(insert_ID, &set)
                                        {
                                            {
                                                let entry = &mut network.send_queue[entry_index];
                                                match entry.kind
                                                {
                                                    SendType::Append { ref mut position } =>
                                                    {
                                                        if *position < acknowledged_position
                                                        {
                                                            *position = acknowledged_position;

                                                            if *position > insert.content.len() as u8
                                                            {
                                                                *position = insert.content.len() as u8;
                                                                println!("Received an acka with append position out of bounds.");
                                                            }
                                                        }
                                                    },
                                                    _ => ()
                                                }
                                            }

                                            if insert.content.len() == acknowledged_position as usize
                                            {
                                                network.send_queue.remove(entry_index);
                                            }
                                        }
                                    }
                                }
                            }

                            else if 'd' as u8 == buffer[4]
                            {
                                if bytes == 4+1+2+4+1+1
                                {
                                    if backend_state.is_some()
                                    {
                                        let insert_ID = deserialize_u32(&buffer[7..11]);
                                        let start_pos = buffer[11];
                                        let end_pos = buffer[12];

                                        if let Some(mut insert) = get_insert_by_ID_mut(insert_ID, &mut set)
                                        {
                                            if (start_pos <= end_pos) & (end_pos <= insert.content.len() as u8)
                                            {
                                                for i in start_pos as usize ..end_pos as usize
                                                {
                                                    insert.content[i] = 127 as char;
                                                    text_buffer.needs_updating = true;
                                                }

                                                let mut ack_buffer = Vec::new();
                                                ack_buffer.push('D' as u8);
                                                serialize_u16(0, &mut ack_buffer);
                                                serialize_u32(insert.ID, &mut ack_buffer);
                                                ack_buffer.push(start_pos);
                                                ack_buffer.push(end_pos);
                                                network.send(&ack_buffer[..]);
                                            }
                                        }
                                    }
                                }
                            }

                            else if 'D' as u8 == buffer[4]
                            {
                                if bytes == 4+1+2+4+1+1
                                {
                                    let insert_ID = deserialize_u32(&buffer[7..11]);
                                    let start_pos = buffer[11];
                                    let end_pos = buffer[12];

                                    if let Some(index) = network.get_queue_entry(insert_ID)
                                    {
                                        let entry = network.send_queue[index];
                                        match entry.kind
                                        {
                                            SendType::Delete { start_pos: stored_start_pos, end_pos: stored_end_pos } =>
                                            {
                                                if (stored_start_pos == start_pos) & (stored_end_pos == end_pos)
                                                {
                                                    network.send_queue.remove(index);
                                                }
                                            },
                                            _ => ()
                                        }
                                    }
                                }
                            }

                            else if buffer[4] == 'c' as u8
                            {
                                let message_id = deserialize_u32(&buffer[5..9]);
                                let mut ack = Vec::with_capacity(5);
                                ack.push('C' as u8);
                                serialize_u32(message_id, &mut ack);

                                network.send(&ack[..]);
                            }

                            else if buffer[4] == 'C' as u8
                            {
                                let acknowledged_message_id = deserialize_u32(&buffer[5..9]);
                                let mut delete_index = None;
                                for i in 0..network.cheap_queue.len()
                                {
                                    if network.cheap_queue[i].0 == acknowledged_message_id
                                    {
                                        delete_index = Some(i);
                                    }
                                }

                                if let Some(index) = delete_index
                                {
                                    network.cheap_queue.remove(index);
                                }

                                let message = &buffer[9..bytes];
                                //message interpretation code goes here
                            }
                        }
                    }
                },
				Err(_) => ()
			}

            //resend un-ACKed inserts
            network.resend(&set);
			
			//check input queue
			{
				let mut new_text_raw: Vec<u8> = Vec::new();
				while let Some(byte) = input_receiver.pop()
				{
					new_text_raw.push(byte);
				}

                if new_text_raw.len() > 0
                {
                    let new_text = str::from_utf8(&new_text_raw[..]).expect("Got invalid UTF-8 from SDL!");

                    //println!("Received text!");

                    let mut text_iter = new_text.chars();
                    while let Some(character) = text_iter.next()
                    {
                        if character == 127 as char
                        {
                            match syncstate
                            {
                                BackendSyncstate::lockedsync => panic!("Can't insert or delete in lockedsync mode"),
                                _ => ()
                            }

                            if new_text.len() > 1
                            {
                                if text_buffer.needs_updating
                                {
                                    render_text(&set, &mut text_buffer); //make sure we get the correct cursor position for deleting...
                                    //text_buffer.needs_updating = false; //TODO: revisit whats going on with the cursor position here
                                }
                            }

                            if text_buffer.cursor_globalPos > 0
                            {
                                text_buffer.cursor_globalPos -= 1;
                                delete_character(text_buffer.cursor_globalPos, &mut set, &mut network, &mut text_buffer);
                                text_buffer.needs_updating = true;

                                text_buffer.active_insert = None;
                                text_buffer.cursor_ID = None; //TODO: add sorting inserts by timestamp + ID (render_text), enable setting cursor_ID and charPos here
                                text_buffer.cursor_charPos = None;

                                text_buffer.needs_updating = true;
                            }
                        }

                        else if character == 31 as char //ASCII unit separator: sent to indicate that the previous stream of text is terminated and cursor position must be updated
                        {
                            assert!(syncstate == BackendSyncstate::lockedsync, "Tried to update cursor position (ASCII 31) while not synchronized!");
                            let mut new_cursor_pos: usize = 0;

                            let byte =
                                if let Some(byte) = text_iter.next() { (byte as u8) as usize }
                                else { input_receiver.blocking_pop() as usize };

                            new_cursor_pos = byte;

                            let byte =
                                if let Some(byte) = text_iter.next() { (byte as u8) as usize }
                                else { input_receiver.blocking_pop() as usize };

                            new_cursor_pos += byte<<8;

                            let byte =
                                if let Some(byte) = text_iter.next() { (byte as u8) as usize }
                                else { input_receiver.blocking_pop() as usize };

                            new_cursor_pos += byte<<16;

                            let byte =
                                if let Some(byte) = text_iter.next() { (byte as u8) as usize }
                                else { input_receiver.blocking_pop() as usize };

                            new_cursor_pos += byte<<32;

                            assert!(new_cursor_pos <= text_buffer.text.len());
                            println!("new cursor position: {}", new_cursor_pos);
                            text_buffer.cursor_globalPos = new_cursor_pos;

                            text_buffer.active_insert = None;
                            text_buffer.cursor_ID = None;
                            text_buffer.cursor_charPos = None;
                        }

                        else if character == 22 as char //ASCII 'SYN'
                        {
                            match syncstate
                            {
                                BackendSyncstate::lockedsync => panic!("Can't sync while in lockedsync mode"),
                                BackendSyncstate::unsynced =>
                                {
                                    assert!(input_receiver.len() == 0);
                                    synchronize_buffers(&text_buffer, &c_pointers, &is_buffer_locked);
                                    syncstate = BackendSyncstate::lockedsync;
                                    assert!(sender.push('s' as u8));
                                },
                                _ => ()
                            }
                        }

                        else if character == 2 as char //ASCII 'STX'
                        {
                            if syncstate == BackendSyncstate::lockedsync
                            {
                                syncstate = BackendSyncstate::unsynced;
                            }
                            else
                            {
                                panic!("Trying to unlock from other than lockedsync mode");
                            }
                        }

                        else
                        {
                            match backend_state
                            {
                                None => println!("Got some keypresses, but weren't initialized."), //TODO: find better solution
                                
                                Some(ref mut inner_backend_state) =>
                                {
                                    match syncstate
                                    {
                                        BackendSyncstate::lockedsync => panic!("Can't insert or delete in lockedsync mode"),
                                        _ => ()
                                    }

                                    insert_character(&mut set, character, &mut network, &mut text_buffer, inner_backend_state);
                                    text_buffer.needs_updating = true;
                                }
                            }
                        }
                    }
                }
			}

            if (text_buffer.needs_updating) & (syncstate == BackendSyncstate::unsynced)
            {
                render_text(&set, &mut text_buffer);//TODO: initialize with correct cursor position
                assert!(sender.push('r' as u8) == true);
                text_buffer.needs_updating = false;

                println!("Newly rendered text: {:?}", &text_buffer.text);
                println!("Data: {:?}", &set);
            }

            if syncstate == BackendSyncstate::unsynced
            {
                //TODO: wait a bit here?
                if let Some(message) = input_receiver.peek()
                {
                    if message == 22
                    {
                        input_receiver.pop();

                        //synchronize
                        synchronize_buffers(&text_buffer, &c_pointers, &is_buffer_locked);
                        syncstate = BackendSyncstate::lockedsync;
                        assert!(sender.push('s' as u8));
                    }
                }
            }


            if backend_state.is_none() & (own_port < other_port)
            {
                network.send(&['r' as u8][..]); //retry init
            }
		}
	}).expect("Could not start the backend thread. Good bye.");
	
    let return_box = Box::new( FFIData
                                {
                                    sender: input_sender,
                                    receiver: syncstate_receiver,
                                    state: FrontendSyncstate::lockedsync,
                                    tent: other_thread_tent,
                                    is_buffer_locked: is_buffer_locked_clone
                                });
	return Box::into_raw(return_box);
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
                network.enqueue_append(active_insert.ID, active_insert.content.len() as u8);
                active_insert.content.push(character);
                text_buffer.cursor_ID = Some(active_insert.ID);
                text_buffer.cursor_charPos = Some(active_insert.content.len() as u8);
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
        if let (Some(ID), Some(charPos)) = (text_buffer.cursor_ID, text_buffer.cursor_charPos)
        {
            (ID, charPos)
        }

        else
        {
            if position == 0
            {
                if set.len() == 0
                {
                    (0, 0)
                }
                else
                {
                    (text_buffer.ID_table[position], text_buffer.charPos_table[position])
                }
            }
            else
            {
                if position == text_buffer.ID_table.len()
                {
                    (text_buffer.ID_table[position-1], text_buffer.charPos_table[position-1] + 1)
                }
                else
                {
                    let left_insert = get_insert_by_ID(text_buffer.ID_table[position-1], &*set).expect("insert_character says: ID_table contains at least one ID that is does not belong to any insert currently in the insert set.");

                    let right_insert = get_insert_by_ID(text_buffer.ID_table[position], &*set).expect("insert_character says: ID_table contains at least one ID that is does not belong to any insert currently in the insert set.");


                    if left_insert.is_ancestor_of(right_insert, &*set)
                    {
                        (right_insert.ID, text_buffer.charPos_table[position])
                    }
                    else
                    {
                        (left_insert.ID, text_buffer.charPos_table[position-1] +1)
                    }
                }
            }
        };

        let new_insert = 
            TextInsert
            {
                ID: state.start_ID,
                parent: parent,
                author: state.author_ID,
                charPos: parent_charPos,
                content: vec![character]
            };
        

        state.start_ID += 1;

        text_buffer.cursor_ID = Some(new_insert.ID);
        text_buffer.cursor_charPos = Some(1);

        text_buffer.active_insert = Some(new_insert.ID);
        network.send_queue.push_back(SendQueueEntry { ID: new_insert.ID, kind: SendType::Full });

        set.push(new_insert);
        //TODO: send_now?
    }
                

}

fn delete_character (position: usize, set: &mut TextInsertSet, network: &mut NetworkState, text_buffer: &mut TextBufferInternal)
{
    if position < text_buffer.ID_table.len()
    {
        let mut insert = get_insert_by_ID_mut(text_buffer.ID_table[position], &mut *set).expect("delete_character says: ID_table contains at least one ID that is not associated to any insert in our vector. This should not happen.");

        insert.content[text_buffer.charPos_table[position] as usize] = 127 as char;
        network.enqueue_delete(insert.ID, text_buffer.charPos_table[position], text_buffer.charPos_table[position]+1);
        //TODO: send_now
    }
}




#[no_mangle]
pub unsafe extern fn rust_text_input (text: *const u8, length: i32, box_ptr: *mut FFIData) //only for input, not command sending!!!
{
	let mut ffi = Box::from_raw(box_ptr);
    assert!(ffi.state == FrontendSyncstate::unsynced);

    for i in 0..length as isize
    {
        while ffi.sender.push(*text.offset(i)) == false
        {
            println!("rust_text_input says: keypress buffer has run full");
        }
    }
	mem::forget(ffi);
}

fn frontend_finish_sync (ffi: &mut FFIData)
{
    ffi.is_buffer_locked.store(true, Ordering::Release);
    if ffi.state == FrontendSyncstate::syncing
    {
        loop
        {
            if let Some(msg) = ffi.receiver.pop()
            {
                if msg == 's' as u8
                {
                    ffi.state = FrontendSyncstate::lockedsync;
                    break;
                }
                else if msg == 'f' as u8
                {
                    ffi.state = FrontendSyncstate::unsynced;
                    break;
                }
                else if msg == 'r' as u8
                {
                }
                else
                {
                    println!("WTF?!");
                }
            }
        }
    }

    else
    {
        println!("frontend_finish sync was called while not in syncing state.");
    }

    assert!(ffi.is_buffer_locked.load(Ordering::Acquire) == false);
}

#[no_mangle]
pub unsafe extern fn rust_try_sync_text (ffi_data: *mut FFIData)
{
    let mut ffi = Box::from_raw(ffi_data);
    if ffi.state == FrontendSyncstate::unsynced
    {
        if let Some(msg) = ffi.receiver.pop()
        {
            if msg == 'r' as u8
            {
                while !ffi.sender.push(22) {} //ASCII 'SYN'
                ffi.state = FrontendSyncstate::syncing;
                frontend_finish_sync(&mut *ffi);
            }
            else
            {
                println!("Frontend thread received a non-'sync ready' message while in state 'unsynced'. This should never happen.");
            }
        }
    }
    mem::forget(ffi);
}

#[no_mangle]
pub unsafe extern fn rust_blocking_sync_text (ffi_data: *mut FFIData)
{
    let mut ffi = Box::from_raw(ffi_data);

    if ffi.state == FrontendSyncstate::unsynced
    {
        while !ffi.sender.push(22) {} //ASCII 'SYN'
        ffi.state = FrontendSyncstate::syncing;
        frontend_finish_sync(&mut *ffi);
    }
    mem::forget(ffi);
}

#[no_mangle]
pub unsafe extern fn rust_sync_unlock (ffi_data: *mut FFIData)
{
    let mut ffi = Box::from_raw(ffi_data);
    if ffi.state == FrontendSyncstate::lockedsync
    {
        while !ffi.sender.push(2) {} //ASCII 'STX'
        ffi.state = FrontendSyncstate::unsynced;
    }
    else
    {
        //println!("Trying to unlock from non-unlock state. Nothing will happen.")
    }
    mem::forget(ffi);
}

#[no_mangle]
pub unsafe extern fn rust_send_cursor (cursor: u32, ffi_data: *mut FFIData)
{
    let mut ffi = Box::from_raw(ffi_data);
    if ffi.state == FrontendSyncstate::lockedsync
    {
        while !ffi.sender.push(31) {} //ASCII 'US'
        while !ffi.sender.push(cursor as u8) {}
        while !ffi.sender.push((cursor >> 8) as u8) {}
        while !ffi.sender.push((cursor >> 16) as u8) {}
        while !ffi.sender.push((cursor >> 24) as u8) {} //TODO: blocking push function
    }
    else
    {
        println!("Warning: trying to update cursor while not in lockedsync");
    }
    mem::forget(ffi);
}
