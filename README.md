# Protobuf Extension for SQLite
This project implements a [run-time loadable extension][ext] for [SQLite][]. It
allows SQLite to perform queries that can extract field values out of stored
Protobuf messages. 

This exension works without the need for protobuf definitions (`.proto` files). This has the benifit of making the extension flexible and simple to use, however the downside is that the extension is not able to perfectly decode messages, and makes some guesses as to the data types of the different message fields. 

[ext]: https://www.sqlite.org/loadext.html
[SQLite]: https://www.sqlite.org/

## Building source code
To build the extension run the following command in the root directory
- `mkdir build`
- `cd build`
- `conan install ..`
- `cmake .. -DCMAKE_BUILD_TYPE=Release`
- `cmake --build . --config=release`

building the source code will generate a library `sqlite_protobuf.dll` or `sqlite_protobuf.so` (depending on operating system).

## Using extension

### protobuf\_extract(_protobuf_, _path_, _type_)
This deserializes the `protobuf` messge, and returns the element at the desired `path` as the desired `type`. The `path` must begin with `$`, which refers to the root object, followed by zero or more field designations `.field_number` or `.field_number[index]`, here the `field_number` refers to the field number in the protobuf message, and `index` refers to the index, when a field is repeated. Negative indexes are allowed. If an index is out of bounds, or the field does not exist the function returns `null` rather than throwing an error.

    SELECT protobuf_extract(protobuf, "$.1[2].3", "int32") AS value FROM messgaes;

The return type of this function depends user specified `type`, if a value can not be decoded into the desired type, an error is thrown. Valid types include
- "int32" : extracts `int32` and `sfixed32` data as INTEGER
- "int64" : extracts `int64` and `sfixed64` data as INTEGER
- "uint32" : extracts `uint32` and `fixed32` data as INTEGER
- "uint64" : extracts `uint64` and `fixed64` data as INTEGER, note sqlite only supports signed 64 bit numbers.
- "sint32" : extracts `sint32` data as INTEGER
- "sint64" : extracts `sint64` data as INTEGER
- "bool" : extracts `bool` data as INTEGER
- "float" : extracts `float` data as REAL
- "double" : extracts `double` data as REAL
- "string" : extracts `string` data as TEXT
- "bytes" : extracts `bytes` data as BLOB
- "" : extracts raw protobuf data as BLOB

### protobuf_to_json(_protobuf_)
This function deserializes the protobuf message and returns a json representation of the message. Note that the protobuf deserialization makes guesses for the value types, hence the values may not always be as expected.

    SELECT protobuf_to_json(protobuf) AS json FROM messgaes;

The returned json string will look something like this:

    {"1":{"4":123},"2":"hello world","4":[1, 2, 3]}

Note that the json string is compacted into a single line without whitespaces, which is efficient, but can make the string less human readable.
