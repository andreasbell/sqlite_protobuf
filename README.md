<meta name="google-site-verification" content="rz4VEh3vcEH7GHpGUxl6uICoXn0fzROEnP-6zq78QOQ" />

# Protobuf Extension for SQLite
This project implements a [run-time loadable extension][ext] for [SQLite][]. It
allows SQLite to perform queries that can extract field values out of stored
Protobuf messages. 

This exension works without the need for protobuf definitions (`.proto` files). This has the benifit of making the extension flexible and simple to use, however the downside is that the extension is not able to perfectly decode messages, and makes some guesses as to the data types of the different message fields. 

[ext]: https://www.sqlite.org/loadext.html
[SQLite]: https://www.sqlite.org/

## Setup instructions

### Download

The following pre-buildt libraries are available for [download](https://github.com/andreasbell/sqlite_protobuf/releases):

![build](https://github.com/andreasbell/sqlite_protobuf/actions/workflows/build.yml/badge.svg) ![release](https://img.shields.io/github/v/release/andreasbell/sqlite_protobuf?display_name=release)


|  | Windows | MacOS | Linux |
|--|--|--|--|
| x86 | [sqlite_protobuf.dll.zip](https://github.com/andreasbell/sqlite_protobuf/releases/latest/download/sqlite_protobuf-windows-x86.zip) |  | [sqlite_protobuf.so.tar.gz](https://github.com/andreasbell/sqlite_protobuf/releases/latest/download/sqlite_protobuf-linux-x86.tar.gz) |
| x86-64 | [sqlite_protobuf.dll.zip](https://github.com/andreasbell/sqlite_protobuf/releases/latest/download/sqlite_protobuf-windows-x64.zip) | [sqlite_protobuf.dylib.zip](https://github.com/andreasbell/sqlite_protobuf/releases/latest/download/sqlite_protobuf-macos-x64.zip) | [sqlite_protobuf.so.tar.gz](https://github.com/andreasbell/sqlite_protobuf/releases/latest/download/sqlite_protobuf-linux-x64.tar.gz) |
| AArch64 (ARM64) | [sqlite_protobuf.dll.zip](https://github.com/andreasbell/sqlite_protobuf/releases/latest/download/sqlite_protobuf-windows-aarch64.zip) | [sqlite_protobuf.dylib.zip](https://github.com/andreasbell/sqlite_protobuf/releases/latest/download/sqlite_protobuf-macos-aarch64.zip) | [sqlite_protobuf.so.tar.gz](https://github.com/andreasbell/sqlite_protobuf/releases/latest/download/sqlite_protobuf-linux-aarch64.tar.gz)

### How to use

From the [SQLite][] CLI you can load the extension with:
```bash
sqlite3 # will open SQLite CLI
> .load sqlite_protobuf
```

In [python][] you can load the extension with:
```python
import sqlite3
db = sqlite3.connect('my_database.db')
db.enable_load_extension(True)
db.load_extension('sqlite_protobuf')
```

In graphical applications like [DB Browser][] and [SQLiteStudio][] the extension can be automatically loaded when the application is started. This can be configured in the application settings.

See the [documentation][ext] on run-time loadable extensions for more
information.

[DB Browser]: https://sqlitebrowser.org/
[SQLiteStudio]: https://sqlitestudio.pl/
[python]: https://www.python.org/

### How to build
If you want to build the library from source, you can do this with the following commands from the root directory of the source code: 
```bash
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . --config=release
```

building the source code will generate a library `sqlite_protobuf.dll`, `sqlite_protobuf.so` or `sqlite_protobuf.dylib` depending on your operating system.

## API

### protobuf_extract(_protobuf_, _path_, _type_)
This function deserializes the `protobuf` message, and returns the element at the desired `path` as the desired `type`. The `path` must begin with `$`, which refers to the root object, followed by zero or more field designations `.field_number` or `.field_number[index]`, here the `field_number` refers to the field number in the protobuf message, and `index` refers to the index, when a field is repeated. Negative indexes are allowed. If an index is out of bounds, or the field does not exist the function returns `NULL` rather than throwing an error.

```sql
SELECT protobuf_extract(protobuf, '$.1[2].3', 'int32') AS value FROM messages;
```

The return type of this function depends on the user specified `type`, if a value can not be decoded into the desired type, the function returns `NULL` rather than throwing an error. Valid types include
- 'int32' : extracts `int32` as INTEGER
- 'int64' : extracts `int64` as INTEGER
- 'uint32' : extracts `uint32` as INTEGER
- 'uint64' : extracts `uint64` as INTEGER, note sqlite only supports signed 64 bit numbers.
- 'sint32' : extracts `sint32` as INTEGER
- 'sint64' : extracts `sint64` as INTEGER
- 'fixed32' : extracts `fixed32` as INTEGER
- 'fixed64' : extracts `fixed64` as INTEGER, note sqlite only supports signed 64 bit numbers.
- 'sfixed32' : extracts `sfixed32` as INTEGER
- 'sfixed64' : extracts `sfixed64` as INTEGER
- 'bool' : extracts `bool` as INTEGER
- 'float' : extracts `float` as REAL
- 'double' : extracts `double` as REAL
- 'string' : extracts `string` data as TEXT
- 'bytes' : extracts `bytes` data as BLOB
- 'enum' : extracts `enum` as INTEGER
- '' : extracts raw protobuf buffer as BLOB

### protobuf_to_json(_protobuf_, _mode_)
This function deserializes the `protobuf` message and returns a json representation of the message. Note that the protobuf deserialization makes guesses for the value types, hence the values may not always be as expected. 

```sql
SELECT protobuf_to_json(protobuf) AS json FROM messages;
```

The returned json string will look something like this:

```json
{"1":{"4":123},"2":"hello world","4":[1, 2, 3]}
```

The optional `mode` can be set to `1` to show the wire type together with the [field number][pb], or `mode` can be set to `2`, to try decoding [packed][packed] fields. The default `mode` is `0`, where only the field numbers are shown, and packed fields are not decoded.

Note that the json string is compacted into a single line without whitespaces, which is efficient, but can make the string less human readable.

[pb]: https://protobuf.dev/programming-guides/encoding/#structure
[packed]: https://protobuf.dev/programming-guides/encoding/#packed

### protobuf_each(_protobuf_, _path_)
This function deserializes the `protobuf` message, and returns a [virtual table][vtab] with all the subfields at the desired `path`. This provides a convenient interface for iterating over fields, including repeated fields. The optional `path` must begin with `$`, which refers to the root object, followed by zero or more field designations `.field_number` or `.field_number[index]`.

```sql
SELECT * FROM protobuf_each(protobuf);
```

The above example will return a table containing all the sub fields of the given `protobuf` message which will look something like this.

| tag | field | wiretype | value | parent |
|-----|-------|----------|-------|--------|
| 10  | 1     | 2        | BLOB  | BLOB   |
| 24  | 3     | 0        | BLOB  | BLOB   |
| ... | ...   | ...      | ...   | ...    |

The `tag`, `field` and `wiretype` columns are all unsigned integers representing the [tag, field number and wire type][structure] of the sub fieleds in the given `protobuf` message. The `value` is the content of sub field given as a binary blob, and the `parent` is a binary blob of the parent of the sub fields we are iterating over.

The `protobuf_each` function is desiged to be used together with other functions such as `protobuf_to_json` and `protobuf_extract`, like in the example below, where we iterate over all sub fields at the path `$.1[2].3`, and run `protobuf_extract(value, '$', 'int32')` to extract the value.

```sql
SELECT protobuf_extract(value, '$', 'int32') AS number FROM protobuf_each(protobuf, '$.1[2].3');
```

This is particularly usefull when we have repeated or packed repeated fields and we want specific values from all the repeated fields. Note that for repeated fields it is usefull to aditionally filter on the field number and [wire type][structure] `WHERE field = 1 AND wiretype = 0` in order to limit the results to only the desired field and type.

[vtab]: https://www.sqlite.org/vtab.html
[structure]: https://protobuf.dev/programming-guides/encoding/#structure

### protobuf_foreach(_protobuf_, _path_)
This is an alias for the `protobuf_each` function.
