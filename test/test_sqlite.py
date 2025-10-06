#!/usr/bin/env python3
import sqlite3
import struct

def varint(num):
    if num < 0:
        num += (1 << 64)
    buffer = b""
    while True:
        if (num >> 7) > 0:
            buffer += bytes([(num & 0b01111111) | 0b10000000])
        else:
            buffer += bytes([num & 0b01111111])
            break
        num = num >> 7
    return buffer

def encode_int(fieldNumber, num):
    buffer = varint((fieldNumber << 3) | 0) # Add tag with wiretype 0 (VARINT)
    buffer += varint(num) # Add number
    return buffer

def encode_str(fieldNumber, string):
    buffer = varint((fieldNumber << 3) | 2) # Add tag with wiretype 2 (LEN)
    buffer += varint(len(string)) # Add lengt of string
    buffer += string # Add string
    return buffer

def encode_i64(fieldNumber, num):
    buffer = varint((fieldNumber << 3) | 1) # Add tag with wiretype 1 (I64)
    if isinstance(num, float):
        buffer += struct.pack("<d", num)
    else:
        buffer += struct.pack("<q", num)
    return buffer

def encode_i32(fieldNumber, num):
    buffer = varint((fieldNumber << 3) | 5) # Add tag with wiretype 5 (I32)
    if isinstance(num, float):
        buffer += struct.pack("<f", num)
    else:
        buffer += struct.pack("<i", num)
    return buffer

def encode_group(fieldNumber, string):
    buffer = varint((fieldNumber << 3) | 3) # Add tag with wiretype 3 (SGROUP)
    buffer += string # Add string
    buffer += varint((fieldNumber << 3) | 4)# Add tag with wiretype 4 (EGROUP)
    return buffer

def test_protobuf_to_json(db):
    cur = db.cursor()
    
    # Singel string
    input = encode_str(1, b"Hello World!")
    expected = '{"1":"Hello World!"}'

    res = cur.execute("SELECT protobuf_to_json(?);", [input])
    output = res.fetchone()[0]
    assert output == expected

    # Repeated string
    input = encode_str(2, b"Hello") + encode_str(2, b"World!")
    expected = '{"2":["Hello","World!"]}'

    res = cur.execute("SELECT protobuf_to_json(?);", [input])
    output = res.fetchone()[0]
    assert output == expected

    # Single int
    for i in range(0, 256):
        input = encode_int(i + 1, i - 50)
        expected = '{{"{}":{}}}'.format(i + 1, i - 50)

        res = cur.execute("SELECT protobuf_to_json(?);", [input])
        output = res.fetchone()[0]
        assert output == expected

    # Repeated int
    input = encode_int(3, -1) + encode_int(3, 0) + encode_int(3, 1)
    expected = '{"3":[-1,0,1]}'

    res = cur.execute("SELECT protobuf_to_json(?);", [input])
    output = res.fetchone()[0]
    assert output == expected
    
    # Singel float
    input = encode_i32(1, 3.1415)
    expected = '{"1":3.1415}'

    res = cur.execute("SELECT protobuf_to_json(?);", [input])
    output = res.fetchone()[0]
    assert output == expected

    # Repeated float
    input = encode_i32(1, -1.0) + encode_i32(1, 2.0)
    expected = '{"1":[-1,2]}'

    res = cur.execute("SELECT protobuf_to_json(?);", [input])
    output = res.fetchone()[0]
    assert output == expected
    
    # Singel float
    input = encode_i64(1, 3.1415)
    expected = '{"1":3.1415}'

    res = cur.execute("SELECT protobuf_to_json(?);", [input])
    output = res.fetchone()[0]
    assert output == expected

    # Repeated float
    input = encode_i64(1, -1.0) + encode_i64(1, 2.0)
    expected = '{"1":[-1,2]}'

    res = cur.execute("SELECT protobuf_to_json(?);", [input])
    output = res.fetchone()[0]
    assert output == expected

    # Compound message
    input = b""
    input += encode_str(1, b"A")
    input += encode_group(2, encode_int(1, -1))
    input += encode_str(3, encode_i64(1, 1.23))
    input += encode_i32(4, float("nan"))
    input += encode_i32(4, float("inf"))
    expected = '{"1":"A","2":{"1":-1},"3":{"1":1.23},"4":[nan,inf]}'

    res = cur.execute("SELECT protobuf_to_json(?);", [input])
    output = res.fetchone()[0]
    assert output == expected

def test_protobuf_extract(db):
    cur = db.cursor()
    input = b""

    # Extract string
    input += encode_str(1, b"This is a string")
    res = cur.execute("SELECT protobuf_extract(?, '$.1', 'string');", [input])
    assert res.fetchone()[0] == "This is a string"

    # Extract bytes
    input += encode_str(2, b"These are bytes")
    res = cur.execute("SELECT protobuf_extract(?, '$.2', 'bytes');", [input])
    assert res.fetchone()[0] == b"These are bytes"

    # Extract bool
    input += encode_int(3, True)
    res = cur.execute("SELECT protobuf_extract(?, '$.3', 'bool');", [input])
    assert res.fetchone()[0]

    # Extract int32
    input += encode_int(4, 0xffffffff)
    res = cur.execute("SELECT protobuf_extract(?, '$.4', 'int32');", [input])
    assert res.fetchone()[0] == -1

    # Extract uint32
    input += encode_int(5, 0xffffffff)
    res = cur.execute("SELECT protobuf_extract(?, '$.5', 'uint32');", [input])
    assert res.fetchone()[0] == 0xffffffff

    # Extract int64
    input += encode_int(6, -2)
    res = cur.execute("SELECT protobuf_extract(?, '$.6', 'int64');", [input])
    assert res.fetchone()[0] == -2

    # Extract uint64
    input += encode_int(7, 0xffffffffffffffff)
    res = cur.execute("SELECT protobuf_extract(?, '$.7', 'uint64');", [input])
    assert res.fetchone()[0] == -1 # negative value due to uint64 unsupported by sqlite

    # Extract sint32
    num = -12345678
    input += encode_int(8, (num << 1) ^ (num >> 31))
    res = cur.execute("SELECT protobuf_extract(?, '$.8', 'sint32');", [input])
    assert res.fetchone()[0] == num

    # Extract sint64
    num = 12345678
    input += encode_int(9, (num << 1) ^ (num >> 63))
    res = cur.execute("SELECT protobuf_extract(?, '$.9', 'sint64');", [input])
    assert res.fetchone()[0] == num

    # Extract fixed32
    input += encode_i32(10, -1)
    res = cur.execute("SELECT protobuf_extract(?, '$.10', 'fixed32');", [input])
    assert res.fetchone()[0] == 0xffffffff

    # Extract fixed64
    input += encode_i64(11, 100)
    res = cur.execute("SELECT protobuf_extract(?, '$.11', 'fixed64');", [input])
    assert res.fetchone()[0] == 100

    # Extract sfixed32
    input += encode_i32(12, -1)
    res = cur.execute("SELECT protobuf_extract(?, '$.12', 'sfixed32');", [input])
    assert res.fetchone()[0] == -1

    # Extract sfixed64
    input += encode_i64(13, -1)
    res = cur.execute("SELECT protobuf_extract(?, '$.13', 'sfixed64');", [input])
    assert res.fetchone()[0] == -1

    # Extract enum as value
    input += encode_int(14, 123)
    res = cur.execute("SELECT protobuf_extract(?, '$.14', 'enum');", [input])
    assert res.fetchone()[0] == 123

    # Extract enum as key
    res = cur.execute("SELECT protobuf_extract(?, '$.14', 'enum MY_ENUM {Alice=0; Bob=123; Charlie=1000;}');", [input])
    assert res.fetchone()[0] == "Bob"

    # Extract repeated
    for i in range(100):
        input += encode_str(15, str(i).encode("utf-8"))
    for i in range(100):
        res = cur.execute(f"SELECT protobuf_extract(?, '$.15[{i}]', 'string');", [input])
        assert res.fetchone()[0] == str(i)
    for i in range(100):
        res = cur.execute(f"SELECT protobuf_extract(?, '$.15[{-i-1}]', 'string');", [input])
        assert res.fetchone()[0] == str(100-i-1)
    res = cur.execute("SELECT protobuf_extract(?, '$.15[100]', 'string');", [input])
    assert res.fetchone()[0] is None
    res = cur.execute("SELECT protobuf_extract(?, '$.15[-101]', 'string');", [input])
    assert res.fetchone()[0] is None

    # Extract packed repeated varint
    input += encode_str(16, bytes([i for i in range(100)]))
    for i in range(100):
        res = cur.execute(f"SELECT protobuf_extract(?, '$.16[{i}]', 'int32');", [input])
        assert res.fetchone()[0] == i
    for i in range(100):
        res = cur.execute(f"SELECT protobuf_extract(?, '$.16[{-i-1}]', 'int32');", [input])
        assert res.fetchone()[0] == 100-i-1
    res = cur.execute("SELECT protobuf_extract(?, '$.16[100]', 'int32');", [input])
    assert res.fetchone()[0] is None
    res = cur.execute("SELECT protobuf_extract(?, '$.16[-101]', 'int32');", [input])
    assert res.fetchone()[0] is None

    # Extract packed repeated i32
    input += encode_str(17, struct.pack("<100i", *range(100)))
    for i in range(100):
        res = cur.execute(f"SELECT protobuf_extract(?, '$.17[{i}]', 'fixed32');", [input])
        assert res.fetchone()[0] == i
    for i in range(100):
        res = cur.execute(f"SELECT protobuf_extract(?, '$.17[{-i-1}]', 'fixed32');", [input])
        assert res.fetchone()[0] == 100-i-1
    res = cur.execute("SELECT protobuf_extract(?, '$.17[100]', 'fixed32');", [input])
    assert res.fetchone()[0] is None
    res = cur.execute("SELECT protobuf_extract(?, '$.17[-101]', 'fixed32');", [input])
    assert res.fetchone()[0] is None

    # Extract packed repeated i64
    input += encode_str(18, struct.pack("<100q", *range(100)))
    for i in range(100):
        res = cur.execute(f"SELECT protobuf_extract(?, '$.18[{i}]', 'fixed64');", [input])
        assert res.fetchone()[0] == i
    for i in range(100):
        res = cur.execute(f"SELECT protobuf_extract(?, '$.18[{-i-1}]', 'fixed64');", [input])
        assert res.fetchone()[0] == 100-i-1
    res = cur.execute("SELECT protobuf_extract(?, '$.18[100]', 'fixed64');", [input])
    assert res.fetchone()[0] is None
    res = cur.execute("SELECT protobuf_extract(?, '$.18[-101]', 'fixed64');", [input])
    assert res.fetchone()[0] is None

    # Extract float
    input += encode_i32(19, float("inf"))
    res = cur.execute("SELECT protobuf_extract(?, '$.19', 'float');", [input])
    assert res.fetchone()[0] == float("inf")

    # Extract float
    input += encode_i64(20, float("inf"))
    res = cur.execute("SELECT protobuf_extract(?, '$.20', 'double');", [input])
    assert res.fetchone()[0] == float("inf")
    
    # Extract from group
    input += encode_group(21, encode_str(1, b"I am in a group"))
    res = cur.execute("SELECT protobuf_extract(?, '$.21.1', 'string');", [input])
    assert res.fetchone()[0] == "I am in a group"

    # Extract from sub message
    input += encode_str(22, encode_str(1, b"I am in a sub message"))
    res = cur.execute("SELECT protobuf_extract(?, '$.22.1', 'string');", [input])
    assert res.fetchone()[0] == "I am in a sub message"

    # Extract raw buffer
    input += encode_str(23, encode_str(1, b"I am a nested message"))
    res = cur.execute("SELECT protobuf_extract(?, '$.23', '');", [input])
    res = cur.execute("SELECT protobuf_extract(?, '$.1', '');", [res.fetchone()[0]])
    assert res.fetchone()[0] == b"I am a nested message"

    # Extract nested repeated
    input += encode_str(24, encode_str(1, b"a") + encode_str(1, b"b"))
    input += encode_str(24, encode_str(1, b"c") + encode_str(1, b"d"))
    res = cur.execute("SELECT protobuf_extract(?, '$.24.1', 'string');", [input])
    assert res.fetchone()[0] == "a"
    res = cur.execute("SELECT protobuf_extract(?, '$.24.1[1]', 'string');", [input])
    assert res.fetchone()[0] == "b"
    res = cur.execute("SELECT protobuf_extract(?, '$.24[1].1', 'string');", [input])
    assert res.fetchone()[0] == "c"
    res = cur.execute("SELECT protobuf_extract(?, '$.24[1].1[1]', 'string');", [input])
    assert res.fetchone()[0] == "d"

    # Extract none existant path
    res = cur.execute("SELECT protobuf_extract(?, '$.100', '');", [input])
    assert res.fetchone()[0] is None
    res = cur.execute("SELECT protobuf_extract(?, '$.-1', '');", [input])
    assert res.fetchone()[0] is None
    res = cur.execute("SELECT protobuf_extract(?, '$.1.2.3', '');", [input])
    assert res.fetchone()[0] is None

    # Extract invalid / malformed path
    try:
        res = cur.execute("SELECT protobuf_extract(?, '', 'string');", [input])
        assert False # Should not reach this
    except sqlite3.OperationalError as e:
        assert str(e) == "Path not valid, path should start with $"

    try:
        res = cur.execute("SELECT protobuf_extract(?, 'abcd', 'string');", [input])
        assert False # Should not reach this
    except sqlite3.OperationalError as e:
        assert str(e) == "Path not valid, path should start with $"

def test_protobuf_each(db):
    cur = db.cursor()

    # Make input buffer
    input = b""
    input += encode_str(1, b"Hello")
    input += encode_str(1, b"World")
    input += encode_str(1, b"!")
    input += encode_int(2, 0)
    input += encode_int(2, 1)
    input += encode_i32(3, 2)
    input += encode_i32(3, 3)
    input += encode_i64(4, 4)
    input += encode_i64(4, 5)

    # Extract all (without path)
    res = cur.execute("SELECT tag, field, wiretype, value, parent, buffer, root FROM protobuf_each(?)", [input])
    output = res.fetchall()
    assert len(output) == 20
    assert output[-1][0] == 33
    assert output[-1][1] == 4
    assert output[-1][2] == 1
    assert output[-1][3] == b"\x05\x00\x00\x00\x00\x00\x00\x00"
    assert output[-1][4] == input
    assert output[-1][5] == input
    assert output[-1][6] == "$"

    # Extract all (with path)
    res = cur.execute("SELECT tag, field, wiretype, value, parent, buffer, root FROM protobuf_each(?, '$')", [input])
    output = res.fetchall()
    assert len(output) == 20
    assert output[-1][0] == 33
    assert output[-1][1] == 4
    assert output[-1][2] == 1
    assert output[-1][3] == b"\x05\x00\x00\x00\x00\x00\x00\x00"
    assert output[-1][4] == input
    assert output[-1][5] == input
    assert output[-1][6] == "$"

    # Extract all strings
    res = cur.execute("SELECT * FROM protobuf_each(?, '$') WHERE wiretype = 2 and field = 1", [input])
    assert res.fetchone()[3] == b"Hello"
    assert res.fetchone()[3] == b"World"
    assert res.fetchone()[3] == b"!"
    assert res.fetchone() is None

    # Extract all field 2
    res = cur.execute("SELECT * FROM protobuf_each(?, '$') WHERE field = 2", [input])
    assert res.fetchone()[1:4] == (2, 0, b"\x00")
    assert res.fetchone()[1:4] == (2, 0, b"\x01")
    assert res.fetchone() is None

    # Extract all field 3
    res = cur.execute("SELECT * FROM protobuf_each(?, '$') WHERE field = 3", [input])
    assert res.fetchone()[1:4] == (3, 5, b"\x02\x00\x00\x00")
    assert res.fetchone()[1:4] == (3, 5, b"\x03\x00\x00\x00")
    assert res.fetchone() is None

    # Extract all field 4
    res = cur.execute("SELECT * FROM protobuf_each(?, '$') WHERE field = 4", [input])
    assert res.fetchone()[1:4] == (4, 1, b"\x04\x00\x00\x00\x00\x00\x00\x00")
    assert res.fetchone()[1:4] == (4, 1, b"\x05\x00\x00\x00\x00\x00\x00\x00")
    assert res.fetchone() is None

    # Extract all fields from submessage
    input += encode_str(5, encode_str(1, b"I") + encode_str(1, b"am") + encode_str(1, b"a") + encode_str(1, b"sub message"))
    res = cur.execute("SELECT * FROM protobuf_each(?, '$.5') WHERE wiretype = 2", [input])
    assert res.fetchone()[1:4] == (1, 2, b"I")
    assert res.fetchone()[1:4] == (1, 2, b"am")
    assert res.fetchone()[1:4] == (1, 2, b"a")
    assert res.fetchone()[1:4] == (1, 2, b"sub message")
    assert res.fetchone() is None

    # Extract all fields from group
    input += encode_group(6, encode_str(1, b"I") + encode_str(1, b"am") + encode_str(1, b"a") + encode_str(1, b"group"))
    res = cur.execute("SELECT * FROM protobuf_each(?, '$.6') WHERE wiretype = 2", [input])
    assert res.fetchone()[1:4] == (1, 2, b"I")
    assert res.fetchone()[1:4] == (1, 2, b"am")
    assert res.fetchone()[1:4] == (1, 2, b"a")
    assert res.fetchone()[1:4] == (1, 2, b"group")
    assert res.fetchone() is None

    # Extract none existant path
    res = cur.execute("SELECT * FROM protobuf_each(?, '$.100')", [input])
    assert res.fetchone() is None
    res = cur.execute("SELECT * FROM protobuf_each(?, '$.-1')", [input])
    assert res.fetchone() is None
    res = cur.execute("SELECT * FROM protobuf_each(?, '$.1.2.3')", [input])
    assert res.fetchone() is None

    # Extract invalid / malformed path
    try:
        res = cur.execute("SELECT * FROM protobuf_each(?, 'abcd')", [input])
        assert False # Should not reach this
    except sqlite3.OperationalError as e:
        assert str(e) == "Path not valid, path should start with $"

def test_malformed_input(db):
    cur = db.cursor()

    # Try extracting from empty buffer
    res = cur.execute("SELECT protobuf_extract(?, '$', '');", [b""])
    assert res.fetchone()[0] is None

    # Try extracting from truncated buffer (str wiretype)
    input = encode_str(1, b"Hello")[0:-1]
    res = cur.execute("SELECT protobuf_extract(?, '$.1', '');", [input])
    assert res.fetchone()[0] is None

    # Try extracting from truncated buffer (i32 wiretype)
    input = encode_i32(1, 1234)[0:-1]
    res = cur.execute("SELECT protobuf_extract(?, '$.1', '');", [input])
    assert res.fetchone()[0] is None

    # Try extracting from truncated buffer (i64 wiretype)
    input = encode_i64(1, 1234)[0:-1]
    res = cur.execute("SELECT protobuf_extract(?, '$.1', '');", [input])
    assert res.fetchone()[0] is None

    # Try extracting from truncated buffer (varint wiretype)
    input = encode_int(1, 1234)[0:-1]
    res = cur.execute("SELECT protobuf_extract(?, '$.1', '');", [input])
    assert res.fetchone()[0] is None

def main():
    # Load data base and sqlite_protobuf extension
    db = sqlite3.connect("test.db")
    db.enable_load_extension(True)
    db.load_extension("./sqlite_protobuf")

    # Run tests
    test_protobuf_to_json(db)
    test_protobuf_extract(db)
    test_protobuf_each(db)
    test_malformed_input(db)


if __name__ == "__main__":
    main()