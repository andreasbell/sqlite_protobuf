# Protobuf Extension for SQLite
This project implements a [run-time loadable extension][ext] for [SQLite][sqlite]. It
allows SQLite to perform queries that can extract field values out of stored
Protobuf messages. 

This exension works without the need for protobuf definitions (`.proto` files). This has the benifit of making the extension flexible and simple to use, however the downside is that the extension is not able to perfectly decode messages, and makes some guesses as to the data types of the different message fields. 

[ext]: https://www.sqlite.org/loadext.html
[sqlite]: https://www.sqlite.org/

## Building source code
To build the extension run the following command in the root directory
- `mkdir build`
- `cd build`
- `cmake .. -DCMAKE_BUILD_TYPE=Release`
- `cmake --build . --config=release`

building the source code will generate a library `sqlite_protobuf.dll` or `sqlite_protobuf.so` (depending on operating system).

## Using extension

### protobuf_extract(_protobuf_, _path_, _type_)
This function deserializes the `protobuf` message, and returns the element at the desired `path` as the desired `type`. The `path` must begin with `$`, which refers to the root object, followed by zero or more field designations `.field_number` or `.field_number[index]`, here the `field_number` refers to the field number in the protobuf message, and `index` refers to the index, when a field is repeated. Negative indexes are allowed. If an index is out of bounds, or the field does not exist the function returns `NULL` rather than throwing an error.

    SELECT protobuf_extract(protobuf, "$.1[2].3", "int32") AS value FROM messages;

The return type of this function depends user specified `type`, if a value can not be decoded into the desired type, an error is thrown. Valid types include
- "int32" : extracts `int32` as INTEGER
- "int64" : extracts `int64` as INTEGER
- "uint32" : extracts `uint32` as INTEGER
- "uint64" : extracts `uint64` as INTEGER, note sqlite only supports signed 64 bit numbers.
- "sint32" : extracts `sint32` as INTEGER
- "sint64" : extracts `sint64` as INTEGER
- "fixed32" : extracts `fixed32` as INTEGER
- "fixed64" : extracts `fixed64` as INTEGER, note sqlite only supports signed 64 bit numbers.
- "sfixed32" : extracts `sfixed32` as INTEGER
- "sfixed64" : extracts `sfixed64` as INTEGER
- "bool" : extracts `bool` as INTEGER
- "float" : extracts `float` as REAL
- "double" : extracts `double` as REAL
- "string" : extracts `string` data as TEXT
- "bytes" : extracts `bytes` data as BLOB
- "" : extracts raw protobuf buffer as BLOB

### protobuf_to_json(_protobuf_, _mode_)
This function deserializes the `protobuf` message and returns a json representation of the message. Note that the protobuf deserialization makes guesses for the value types, hence the values may not always be as expected. 

    SELECT protobuf_to_json(protobuf) AS json FROM messages;

The returned json string will look something like this:

    {"1":{"4":123},"2":"hello world","4":[1, 2, 3]}

The optional `mode` can be set to `1` to show the wire type together with the [field number][pb], or `mode` can be set to `2`, to try decoding [packed][packed] fields. The default `mode` is `0`, where only the field numbers are shown, and packed fields are not decoded.

Note that the json string is compacted into a single line without whitespaces, which is efficient, but can make the string less human readable.

[pb]: https://protobuf.dev/programming-guides/encoding/#structure
[packed]: https://protobuf.dev/programming-guides/encoding/#packed

### protobuf_foreach(_protobuf_, _path_)
This function deserializes the `protobuf` message, and returns a [virtual table][vtab] with all the subfields at the desired `path`. This provides a convenient interface for iterating over fields, including repeated fields. The optional `path` must begin with `$`, which refers to the root object, followed by zero or more field designations `.field_number` or `.field_number[index]`.

    SELECT * FROM protobuf_foreach(protobuf);

The above example will return a table containing all the sub fields of the given `protobuf` message which will look something like this.

| tag | field | wiretype | value | parent |
|-----|-------|----------|-------|--------|
| 10  | 1     | 2        | BLOB  | BLOB   |
| 24  | 3     | 0        | BLOB  | BLOB   |
| ... | ...   | ...      | ...   | ...    |

For the columns of the table, the `tag`, `field` and `wiretype` are all unsigned integers represent the [tag, field number and wire type][structure] of the sub fieleds in the given `protobuf` message. The `value` is the content of sub field given as a binary blob, and the `parent` is as a binary blob of the parent for the sub fields we are iterating over.

The `protobuf_foreach` function is desiged to be used together with other functions such as `protobuf_to_json` and `protobuf_extract`, like in the example below, where we iterate over all sub fields at the path `$.1[2].3`, and run `protobuf_extract(value, "$", "int32")` to extract the value.

    SELECT protobuf_extract(value, "$", "int32") AS number FROM protobuf_foreach(protobuf, "$.1[2].3");

This is particularly usefull when we have repeated or packed repeated fields and we want particular values from all the repeated fields. Note that for repeated fields it is usefull to aditionally filter on the field number and [wire type][structure] `WHERE field = 1 AND wiretype = 0` in order to limit the results to only the desired field and type.

[vtab]: https://www.sqlite.org/vtab.html
[structure]: https://protobuf.dev/programming-guides/encoding/#structure