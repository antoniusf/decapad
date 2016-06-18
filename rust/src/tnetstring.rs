use std::str;

#[derive(Debug, PartialEq, Eq)]
pub enum Data
{
    Bool (bool),
    Integer (isize),
    String (String),
    List (Vec<Data>),
    Dict (Vec<(Data, Data)>),
}

fn int_to_string (mut value: isize) -> Vec<u8>
{
    let mut value_string: Vec<u8> = Vec::new();
    if value < 0
    {
        value_string.push('-' as u8);
        value = -value;
    }

    else if value == 0
    {
        value_string.push('0' as u8);
    }

    while value > 0
    {
        let digit = value%10;
        let digit_ascii_value = digit + 48;
        value_string.insert(0, digit_ascii_value as u8);
        value /= 10;
    }

    return value_string;
}

pub fn encode (data: &Data, result: &mut Vec<u8>)
{
    match *data
    {
        Data::Bool (true) => result.extend_from_slice("4:true!".as_bytes()),
        Data::Bool (false) => result.extend_from_slice("5:false!".as_bytes()),
        Data::Integer (value) =>
        {
            let value_string = int_to_string(value);
            let length = int_to_string(value_string.len() as isize);
            result.extend_from_slice(&length[..]);
            result.push(':' as u8);
            result.extend_from_slice(&value_string[..]);
            result.push('#' as u8);
        },

        Data::String (ref value) =>
        {
            let length = int_to_string(value.as_bytes().len() as isize);
            result.extend_from_slice(&length[..]);
            result.push(':' as u8);
            result.extend_from_slice(value.as_bytes());
            result.push(',' as u8);
        },

        Data::List (ref list) =>
        {
            let mut list_string: Vec<u8> = Vec::new();
            for item in list.iter()
            {
                encode(item, &mut list_string);
            }

            let length = int_to_string(list_string.len() as isize);
            result.extend_from_slice(&length[..]);
            result.push(':' as u8);
            result.extend_from_slice(&list_string[..]);
            result.push(']' as u8);
        },

        Data::Dict (ref dict) =>
        {
            let mut dict_string: Vec<u8> = Vec::new();
            for &(ref key, ref value) in dict.iter()
            {
                encode(key, &mut dict_string);
                encode(value, &mut dict_string);
            }

            let length = int_to_string(dict_string.len() as isize);
            result.extend_from_slice(&length[..]);
            result.push(':' as u8);
            result.extend_from_slice(&dict_string[..]);
            result.push('}' as u8);
        },
    }
}

pub fn encode_string_dict ( dict: Vec<(&str, Data)>, result: &mut Vec<u8> )
{

    let mut tnet_dict = Vec::new();

    for (key, value) in dict
    {
        tnet_dict.push((Data::String(key.to_string()), value));
    }

    encode(&Data::Dict(tnet_dict), result);
}

#[test]
fn test_encode_string_dict ()
{

    let mut result = Vec::new();

    encode_string_dict( vec![
                            ("first", Data::Bool(true)),
                            ("second", Data::Integer(2)),
                            ("third", Data::String("hello".to_string()))
                            ], &mut result);

    assert!("44:5:first,4:true!6:second,1:2#5:third,5:hello,}".as_bytes() == &result[..]);
}

fn decode_integer (string: &mut &[u8], terminator: u8) -> Result<isize, &'static str>
{
    let mut result: isize = 0;

    while string.len() > 0
    {
        let character = string[0];
        *string = &&string[1..];

        if character == terminator
        {
            return Ok(result);
        }

        let digit = character as isize - 48;
        if (digit >= 0) & (digit <= 9)
        {
            result = result*10 + digit as isize;
        }
        else
        {
            return Err("Non-digit character found before hitting terminator");
        }
    }

    return Ok(result);
}

#[test]
fn test_decode_integer()
{
    assert!(decode_integer(&mut "37 ".as_bytes(), ' ' as u8).unwrap() == 37);
    assert!(decode_integer(&mut "1024# Hello World".as_bytes(), '#' as u8).unwrap() == 1024);
    assert!(decode_integer(&mut "002048".as_bytes(), 32).unwrap() == 2048);
    assert!(decode_integer(&mut "25:blablafoo".as_bytes(), ' ' as u8).is_err());

    let test = &mut "25:blablafoo".as_bytes();
    assert!(decode_integer(test, ':' as u8).unwrap() == 25);
    assert!(*test == "blablafoo".as_bytes());
}

pub fn decode (string: &mut &[u8]) -> Result<Data, &'static str>
{
    let length: isize;
    match decode_integer(string, ':' as u8)
    {
        Ok(value) => length = value,
        Err(_) => return Err("Found invalid length")
    }

    if length < 0
    {
        return Err("Found negative length");
    }

    let length = length as usize;
    let mut data = &string[..length];
    let type_char = string[length];
    *string = &string[length+1..];

    match type_char as char
    {
        '!' =>
        {
            if data == "true".as_bytes()
            {
                return Ok(Data::Bool(true));
            }
            else if data == "false".as_bytes()
            {
                return Ok(Data::Bool(false));
            }
            else
            {
                return Err("Bool value was neither true nor false");
            }
        },

        '#' =>
        {
            match decode_integer(&mut data, 32)
            {
                Ok(value) => return Ok(Data::Integer(value)),
                Err(error) => return Err(error)
            }
        },

        ',' =>
        {
            match str::from_utf8(data)
            {
                Ok(value) => return Ok(Data::String(value.to_string())),
                Err(Utf8Error) => return Err("String was not valid utf-8")
            }
        },

        ']' =>
        {
            let mut list = Vec::new();
            while data.len() > 0
            {
                match decode(&mut data)
                {
                    Ok(value) => list.push(value),
                    Err(error) => return Err(error)
                }
            }
            return Ok(Data::List(list));
        },

        '}' =>
        {
            let mut dict = Vec::new();
            while data.len() > 0
            {
                match decode(&mut data)
                {
                    Ok(key) =>
                    {
                        match decode(&mut data)
                        {
                            Ok(value) => dict.push((key, value)),
                            Err(error) => return Err(error)
                        }
                    },

                    Err(error) => return Err(error)
                }
            }

            return Ok(Data::Dict(dict));
        },
        
        _ => return Err("Invalid type tag")
    }
}



#[test]
fn test_encode_decode()
{
    let mut list = Vec::new();
    //list.push(Data::Bool(true));
    let mut dict = Vec::new();
    dict.push((Data::String("Hello".to_string()), Data::String("World".to_string())));
    dict.push((Data::String("timeout".to_string()), Data::Integer(42)));
    dict.push((Data::Integer(0), Data::Bool(false)));
    list.push(Data::Dict(dict));
    list.push(Data::String("tnetstrings are awesome".to_string()));
    list.push(Data::Integer(5));
    let data = Data::List(list);

    let mut encoded_data = Vec::new();
    encode(&data, &mut encoded_data);
    println!("Tadaaa: {}", str::from_utf8(&encoded_data[..]).unwrap());
    assert_eq!(&encoded_data[..], "78:43:5:Hello,5:World,7:timeout,2:42#1:0#5:false!}23:tnetstrings are awesome,1:5#]".as_bytes());

    assert_eq!(decode(&mut &encoded_data[..]).unwrap(), data);
    println!("{:?}", decode(&mut &encoded_data[..]));
}

pub fn get_field<'a, 'b> (name: &'a str, data: &'b Data) -> Option<&'b Data>
{

    if let &Data::Dict(ref dict) = data
    {
        for &(ref key, ref value) in dict.iter()
        {
            if let &Data::String(ref string) = key
            {
                if &string[..] == name
                {
                    return Some(value);
                }
            }
        }

        return None;
    }

    return None;
}

#[test]
fn test_get_field()
{
    let mut dict = Vec::new();
    dict.push((Data::String("first".to_string()), Data::Bool(true)));
    dict.push((Data::String("second".to_string()), Data::Integer(2)));
    dict.push((Data::String("third".to_string()), Data::String("hello".to_string())));

    let dict_data = Data::Dict(dict);

    assert!( get_field("first", &dict_data) == Some(&Data::Bool(true)));
    assert!( get_field("second", &dict_data) == Some(&Data::Integer(2)));
    assert!( get_field("third", &dict_data) == Some(&Data::String("hello".to_string())));
}
