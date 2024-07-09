#include "protobuf_extract.h"
#include "sqlite3ext.h"

#include <string>

#include "protodec.h"

namespace sqlite_protobuf
{
    SQLITE_EXTENSION_INIT3

    namespace
    {

        std::string string_from_sqlite3_value(sqlite3_value *value)
        {
            const char *text = static_cast<const char *>(sqlite3_value_blob(value));
            size_t text_size = static_cast<size_t>(sqlite3_value_bytes(value));
            return std::string(text, text_size);
        }

        /// Return the element (or elements)
        ///
        ///     SELECT protobuf_extract(data, "$.1.2[0].3", type);
        ///
        /// @returns a Protobuf-encoded BLOB or the appropriate SQL datatype
        ///
        /// If `default` is provided, it is returned instead of the protobuf field's
        /// default value.
        static void protobuf_extract(sqlite3_context *context, int argc, sqlite3_value **argv)
        {
            sqlite3_value *data = argv[0];
            const std::string path = string_from_sqlite3_value(argv[1]);
            const std::string type = string_from_sqlite3_value(argv[2]);

            // Check that the path begins with $, representing the root of the tree
            if (path.length() == 0 || path[0] != '$')
            {
                sqlite3_result_error(context, "Invalid path", -1);
                return;
            }

            Buffer buffer;
            buffer.start = static_cast<const uint8_t *>(sqlite3_value_blob(data));
            buffer.end = buffer.start + static_cast<size_t>(sqlite3_value_bytes(data));

            // Deserialize the message
            Field root = decodeProtobuf(buffer, true);
            Field *field = &root;

            int fieldNumber, fieldIndex;
            
            // Parse the path string and traverse the message
            size_t fieldStart = path.find(".", 0);
            while(fieldStart < path.size())
            {
                size_t fieldEnd = path.find(".", fieldStart + 1);
                size_t indexStart = path.find("[", fieldStart + 1);
                size_t indexEnd = path.find("]", fieldStart + 1);

                fieldEnd = fieldEnd == std::string::npos ? path.size() : fieldEnd;

                if (indexStart < fieldEnd && indexEnd < fieldEnd) // Both field and index supplied
                {
                    fieldNumber = std::atoi(path.substr(fieldStart + 1, indexStart - fieldStart - 1).c_str());
                    fieldIndex  = std::atoi(path.substr(indexStart + 1, indexEnd - indexStart - 1).c_str());
                }
                else // Only field supplied
                {
                    fieldNumber = std::atoi(path.substr(fieldStart + 1, fieldEnd - fieldStart - 1).c_str());
                    fieldIndex  = 0; 
                }

                // Move path ponter forward
                fieldStart = fieldEnd;


                if (fieldStart < path.size()) // Traverse subfields if not at the end
                {
                    field = field->getSubField(fieldNumber, WIRETYPE_LEN, fieldIndex);
                    if (field == nullptr) {return;}
                }
                else // We are at end of path string, get field matcing type
                {
                    Field *parent = field;
                    field = nullptr;
                    
                    if (type == "" || type == "string" || type == "bytes")
                    {
                        field = parent->getSubField(fieldNumber, WIRETYPE_LEN, fieldIndex);
                        if(field) {break;}
                    }
                    if(type == "" || type == "int32" || type == "int64" || type == "uint32" || type == "uint64" || type == "sint32" || type == "sint64" || type == "bool")
                    {
                        field = parent->getSubField(fieldNumber, WIRETYPE_VARINT, fieldIndex);
                        if(field) {break;}
                    }
                    if(type == "" || type == "fixed64" || type == "sfixed64" || type == "double")
                    {
                        field = parent->getSubField(fieldNumber, WIRETYPE_I64, fieldIndex);
                        if(field) {break;}
                    }
                    if(type == "" || type == "fixed32" || type == "sfixed32" || type == "float")
                    {
                        field = parent->getSubField(fieldNumber, WIRETYPE_I32, fieldIndex);
                        if(field) {break;}
                    }
                    if (field == nullptr) {return;}
                }
            }

            // Extract raw buffer
            if (type == "")
            {
                sqlite3_result_blob(context, (char *)field->value.start, field->value.size(), SQLITE_STATIC);
                return;
            }

            // Extract extract length delimited types
            if (type == "string")
            {
                sqlite3_result_text(context, (char *)field->value.start, field->value.size(), SQLITE_STATIC);
                return;
            }
            if (type == "bytes")
            {
                sqlite3_result_blob(context, (char *)field->value.start, field->value.size(), SQLITE_STATIC);
                return;
            }

            // Extract varints
            if (type == "")
            {
                sqlite3_result_blob(context, (char *)field->value.start, field->value.size(), SQLITE_STATIC);
                return;
            }
            if (type == "int32")
            {
                int32_t value;
                if (getInt32(&field->value, &value))
                {
                    sqlite3_result_int64(context, value);
                    return;
                }
            }
            if (type == "int64")
            {
                int64_t value;
                if (getInt64(&field->value, &value))
                {
                    sqlite3_result_int64(context, value);
                    return;
                }
            }
            if (type == "uint32")
            {
                uint32_t value;
                if (getUint32(&field->value, &value))
                {
                    sqlite3_result_int64(context, value);
                    return;
                }
            }
            if (type == "uint64")
            {
                uint64_t value;
                if (getUint64(&field->value, &value))
                {
                    if (value > INT64_MAX)
                    {
                        sqlite3_log(SQLITE_WARNING,
                                    "Protobuf type is unsigned, but SQLite does not support "
                                    "unsigned types. Value %llu doesn't fit in an int64.",
                                    value);
                    }
                    sqlite3_result_int64(context, value);
                    return;
                }
            }
            if (type == "sint32")
            {
                int32_t value;
                if (getSint32(&field->value, &value))
                {
                    sqlite3_result_int64(context, value);
                    return;
                }
            }
            if (type == "sint64")
            {
                int64_t value;
                if (getSint64(&field->value, &value))
                {
                    sqlite3_result_int64(context, value);
                    return;
                }
            }
            if (type == "bool")
            {
                bool value;
                if (getBool(&field->value, &value))
                {
                    sqlite3_result_int64(context, value ? 1 : 0);
                    return;
                }
            }

            // Extract fixed 64
            if (type == "")
            {
                sqlite3_result_blob(context, (char *)field->value.start, field->value.size(), SQLITE_STATIC);
                return;
            }
            if (type == "fixed64")
            {
                uint64_t value;
                if (getFixed64(&field->value, &value))
                {
                    if (value > INT64_MAX)
                    {
                        sqlite3_log(SQLITE_WARNING,
                                    "Protobuf type is unsigned, but SQLite does not support "
                                    "unsigned types. Value %llu doesn't fit in an int64.",
                                    value);
                    }
                    sqlite3_result_int64(context, value);
                    return;
                }
            }
            if (type == "sfixed64")
            {
                int64_t value;
                if (getSfixed64(&field->value, &value))
                {
                    sqlite3_result_int64(context, value);
                    return;
                }
            }
            if (type == "double")
            {
                double value;
                if (getDouble(&field->value, &value))
                {
                    sqlite3_result_double(context, value);
                    return;
                }
            }

            // Extract fixed32
            if (type == "")
            {
                sqlite3_result_blob(context, (char *)field->value.start, field->value.size(), SQLITE_STATIC);
                return;
            }
            if (type == "fixed32")
            {
                uint32_t value;
                if (getFixed32(&field->value, &value))
                {
                    sqlite3_result_int64(context, value);
                    return;
                }
            }
            if (type == "sfixed32")
            {
                int32_t value;
                if (getSfixed32(&field->value, &value))
                {
                    sqlite3_result_int64(context, value);
                    return;
                }
            }
            if (type == "float")
            {
                float value;
                if (getFloat(&field->value, &value))
                {
                    sqlite3_result_double(context, value);
                    return;
                }
            }

            return;
        }
    } // namespace

    int register_protobuf_extract(sqlite3 *db, char **pzErrMsg, const sqlite3_api_routines *pApi)
    {
        return sqlite3_create_function(db, "protobuf_extract", 3, SQLITE_UTF8 | SQLITE_DETERMINISTIC, 0, protobuf_extract, 0, 0);
    }

} // namespace sqlite_protobuf