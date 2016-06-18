#![allow(non_camel_case_types)]

use std::thread;
use std::sync::{Arc, Condvar, Mutex};
use std::sync::atomic::{AtomicUsize, Ordering};
use std::cell::Cell;

pub mod spsc_255
{
    use std::sync::Arc;
    use std::sync::atomic::{AtomicUsize, Ordering};
    use std::cell::Cell;

    struct Spsc255Internal
    {
        buffer: [Cell<u8>; 256],
        pop_index: AtomicUsize,
        push_index: AtomicUsize,
    }

    pub struct Consumer
    {
        queue: Arc<Spsc255Internal>
    }

    pub struct Producer
    {
        queue: Arc<Spsc255Internal>
    }

    pub fn new() -> (Producer, Consumer)
    {
        let queue = Arc::new(
            Spsc255Internal
            {
                buffer: [Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8), Cell::new(0u8)], //sigh.
                pop_index: AtomicUsize::new(0),
                push_index: AtomicUsize::new(0)
            }
        );

        return (Producer {queue: queue.clone()}, Consumer{queue: queue.clone()});
    }

    impl Producer
    {
        pub fn push (&self, item: u8) -> bool
        {
            let push_index: u8 = self.queue.push_index.load(Ordering::Relaxed) as u8;
            if push_index.wrapping_add(1) != self.queue.pop_index.load(Ordering::Acquire) as u8
            {
                self.queue.buffer[push_index as usize].set(item);
                self.queue.push_index.store(push_index.wrapping_add(1) as usize, Ordering::Release);
                return true;
            }
            else
            {
                return false;
            }
        }

        pub fn blocking_push (&self, item: u8)
        {
            while !self.push(item) {}
        }
    }

    unsafe impl Send for Producer {} //TODO: formal proof?

    impl Consumer
    {
        pub fn pop (&self) -> Option<u8>
        {
            let pop_index: u8 = self.queue.pop_index.load(Ordering::Relaxed) as u8;
            if pop_index != self.queue.push_index.load(Ordering::Acquire) as u8
            {
                let value = self.queue.buffer[pop_index as usize].get();
                self.queue.pop_index.store(pop_index.wrapping_add(1) as usize, Ordering::Release);
                return Some(value);
            }
            else
            {
                return None;
            }
        }

        pub fn peek (&self) -> Option<u8>
        {
            let peek_index: u8 = self.queue.pop_index.load(Ordering::Relaxed) as u8;
            if peek_index != self.queue.push_index.load(Ordering::Acquire) as u8
            {
                let value = self.queue.buffer[peek_index as usize].get();
                return Some(value);
            }
            else
            {
                return None;
            }
        }

        pub fn blocking_pop (&self) -> u8
        {
            loop
            {
                if let Some(value) = self.pop()
                {
                    return value;
                }
            }
        }

        /// Returns a minimum bound of the current length of the queue. In most cases, the value is probably exact, but (due to threading) it is also possible that the queue is longer that.
        /// If do need an exact value, you should probably make sure that the other thread does not push in between and use an atomic fence to ensure you are seeing the most updated push_index.
        pub fn len (&self) -> u8
        {
            return (self.queue.push_index.load(Ordering::Relaxed) as u8) - (self.queue.pop_index.load(Ordering::Relaxed) as u8)
        }
    }

    unsafe impl Send for Consumer {} //TODO: formal proof?
}


#[derive(PartialEq)]
enum TentState
{
    Empty,
    Occupied,
    Woken_Up
}

struct OneThreadTentInside
{
    condvar: Condvar,
    state: Mutex<TentState>
}

///Two threads go camping, but only one thread can sleep in the tent at any time. This is so that both threads can't sleep at the same time with no one to wake them up.
pub struct OneThreadTent
{
    arc: Arc<OneThreadTentInside>
}

impl OneThreadTent
{
    ///Makes a new tent and gives two references back to you, so two threads can sleep in it.
    pub fn new() -> (OneThreadTent, OneThreadTent)
    {
        let arc = Arc::new(
            OneThreadTentInside
            {
                condvar: Condvar::new(),
                state: Mutex::new(TentState::Empty)
            }
        );
        return (OneThreadTent {arc: arc.clone()}, OneThreadTent {arc: arc.clone()})
    }

    ///Tries to sleep in the tent. Does not go to sleep if the tent is occupied. Returns true if it has slept, and false if it doesn't.
    pub fn sleep(&self) -> bool
    {
        let mut state = self.arc.state.lock().unwrap();
        if *state == TentState::Empty
        {
            *state = TentState::Occupied;
            while *state != TentState::Woken_Up
            {
                state = self.arc.condvar.wait(state).unwrap();
            }
            //get out of the tent
            *state = TentState::Empty;

            return true;
        }
        
        else
        {
            println!("OneThreadTent.sleep (called from thread {:?}) says: Can't go to sleep, there's already someone in the tent!", thread::current().name());
            return false;
        }
    }

    ///Wakes the other thread up if it is sleeping in the tent.
    pub fn wake_up(&self) -> bool
    {
        let mut state = self.arc.state.lock().unwrap();
        if *state == TentState::Occupied
        {
            *state = TentState::Woken_Up;
            self.arc.condvar.notify_one();
            return true;
        }

        else
        {
            println!("OneThreadTent.wake_up (called from thread {:?})says: The tent is empty, there is no one to wake up!", thread::current().name());
            return false;
        }
    }

    ///Tells you if the tent is occupied by the other thread (if you can call this function, your thread is _not_ sleeping in the tent, so you know it's the other one).
    pub fn is_occupied(&self) -> bool
    {
        let state = self.arc.state.lock().unwrap();
        if (*state == TentState::Occupied) | (*state == TentState::Woken_Up)
        {
            return true;
        }
        else
        {
            return false;
        }
    }
}

impl Drop for OneThreadTent
{
    fn drop(&mut self) //TODO: maybe make a fourth TentState for dropped, because threads might wait on the other thread waking up
    {
        self.wake_up(); //Wake up any sleeping threads
        let mut state = self.arc.state.lock().unwrap();
        if *state == TentState::Empty
        {
            *state = TentState::Occupied; //go to sleep forever...
        }
        else
        {
            println!("Warning: a tent was dropped while it was occupied. This may result in the other thread sleeping indefinitely.");
        }
    }
}
