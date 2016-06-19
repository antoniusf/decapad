use std::char;

pub struct Utf8StreamConverter
{
    codepoint: u32,
    byte_length: usize,
}

impl Utf8StreamConverter
{
    pub fn new () -> Utf8StreamConverter
    {
        Utf8StreamConverter { codepoint: 0, byte_length: 0 }
    }

    pub fn input (&mut self, byte: u8) -> Option<char>
    {
        if self.byte_length == 0
        {
            if (byte>>7) == 0
            {
                return Some(byte as char);
            }

            else if (byte>>6) == 2
            {
                println!("UTF8-converter says: Un-aligned stream!");
                return None;
            }

            else if (byte>>5) == 0b110
            {
                self.byte_length = 1;
                self.codepoint = (byte & 0b11111) as u32;
                return None;
            }

            else if (byte>>4) == 0b1110
            {
                self.byte_length = 2;
                self.codepoint = (byte & 0b1111) as u32;
                return None;
            }

            else if (byte>>3) == 0b11110
            {
                self.byte_length = 3;
                self.codepoint = (byte & 0b111) as u32;
                return None;
            }

            else
            {
                return None;
            }
        }

        else
        {
            if (byte>>6) == 2
            {
                self.byte_length -= 1;
                self.codepoint <<= 6;
                self.codepoint += (byte & 0b111111) as u32;

                if self.byte_length == 0
                {
                    return char::from_u32(self.codepoint);
                }

                return None;
            }

            else
            {
                println!("UTF8-converter says: Invalid number of continuation bytes!");
                return None;
            }
        }
    }
}


#[test]
fn test_utf8_converter ()
{
    let mut chars = Vec::new();
    let mut string = String::new();
    for &pre_character in [0x5f, 0xd1c3, 0x10050, 0x42, 0x5e2, 0x100].iter()
    {
        let character = char::from_u32(pre_character);
        chars.push(character.unwrap());
        string.push(character.unwrap());
    }

    let mut converter = Utf8StreamConverter::new();
    let mut chars_new = Vec::new();

    for byte in string.bytes()
    {
        if let Some(character) = converter.input(byte)
        {
            chars_new.push(character);
        }
    }

    assert!(&chars[..] == &chars_new[..]);
}


