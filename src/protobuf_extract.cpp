#include "protobuf_extract.h"
#include "sqlite3ext.h"

#include <string>
#include <cstring>

#include "protodec.h"

namespace sqlite_protobuf
{
    SQLITE_EXTENSION_INIT3

    namespace
    {
        struct Path
        {
            std::string pathString;
            std::vector<int> fieldNumber;
            std::vector<int> fieldIndex;
        };

        // Create a global cache for saving data
        #define PROTOBUF_CACHE_BUFFER_SIZE 4096
        #define PROTOBUF_CACHE_PATH_SIZE 64

        struct Cache
        {
            // Protobuf decode cache
            Field field;
            size_t length;
            uint8_t buffer[PROTOBUF_CACHE_BUFFER_SIZE];

            // Path parse cache
            size_t index;
            Path path[PROTOBUF_CACHE_PATH_SIZE];
        };

        Cache cache = {0};

        std::string string_from_sqlite3_value(sqlite3_value *value)
        {
            const char *text = static_cast<const char *>(sqlite3_value_blob(value));
            size_t text_size = static_cast<size_t>(sqlite3_value_bytes(value));
            return std::string(text, text_size);
        }
        
        Path path_from_string(const std::string &path)
        {
            Path parsedPath;
            int fieldNumber, fieldIndex;

            parsedPath.pathString = path;

            // Parse the path string and traverse the message
            size_t fieldStart = path.find(".", 0);
            while(fieldStart < path.size())
            {
                size_t fieldEnd = path.find(".", fieldStart + 1);
                size_t indexStart = path.find("[", fieldStart + 1);
                size_t indexEnd = path.find("]", fieldStart + 1);

                fieldEnd = (fieldEnd == std::string::npos) ? path.size() : fieldEnd;

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

                // Add path entry to end of list
                parsedPath.fieldNumber.push_back(fieldNumber);
                parsedPath.fieldIndex.push_back(fieldIndex);

                // Move path ponter forward
                fieldStart = fieldEnd;
            }

            return parsedPath;          
        }

        /// Return the element (or elements)
        ///
        ///     SELECT protobuf_extract(data, "$.1.2[0].3", type);
        ///
        /// @returns a Protobuf-encoded BLOB or the appropriate SQL datatype
        static void protobuf_extract(sqlite3_context *context, int argc, sqlite3_value **argv)
        {
            //sqlite3_result_int64(context, 0);
            //return;

            sqlite3_value *data = argv[0];
            const std::string path = string_from_sqlite3_value(argv[1]);
            const std::string type = string_from_sqlite3_value(argv[2]);

            // Check that the path begins with $, representing the root of the tree
            if (path.length() == 0 || path[0] != '$')
            {
                sqlite3_result_error(context, "Invalid path", -1);
                return;
            }
            
            // Load protobuf data into a buffer
            Buffer buffer;
            size_t length = static_cast<size_t>(sqlite3_value_bytes(data));
            buffer.start = static_cast<const uint8_t *>(sqlite3_value_blob(data));
            buffer.end = buffer.start + length;

            // Look up message in cache
            Field* root = nullptr;
            if (cache.length == length && memcmp(&cache.buffer, buffer.start, length) == 0)
            {
                // Chache hit -> use the decoded field from cache
                root = &cache.field;
            }
            else if (length <= PROTOBUF_CACHE_BUFFER_SIZE)
            {
                // Chache miss and buffer fits in cache -> decode protobuf and cache result
                memcpy(cache.buffer, buffer.start, length);
                cache.field = decodeProtobuf(buffer, true);
                cache.length = length;
                root = &cache.field;
            }
            else
            {
                // Chache miss, but buffer does not fit in cache -> decode protobuf and invalidate cache
                cache.length = 0;
                cache.field = decodeProtobuf(buffer, true);
                root = &cache.field;
            }

            // Look up path in cache
            Path* parsedPath = nullptr;
            for (size_t i = 0; i < PROTOBUF_CACHE_PATH_SIZE; i++)
            {
                if (cache.path[i].pathString == path)
                {
                    parsedPath = &cache.path[i];
                    break;
                }
            }

            // cache miss -> parse path and add to cache
            if (parsedPath == nullptr)
            {
                cache.index = (cache.index + 1) % PROTOBUF_CACHE_PATH_SIZE;
                cache.path[cache.index] = path_from_string(path);
                parsedPath = &cache.path[cache.index];
            }
            
            // Traverse path to the desired field
            Field *field = root;
            for (size_t i = 0; i < parsedPath->fieldNumber.size() - 1; i++)
            {
                field = field->getSubField(parsedPath->fieldNumber[i], WIRETYPE_LEN, parsedPath->fieldIndex[i]);
                if (field == nullptr) {return;}
            }
            // Traverse last field based on type
            int fieldNumber = parsedPath->fieldNumber[parsedPath->fieldNumber.size() - 1];
            int fieldIndex = parsedPath->fieldIndex[parsedPath->fieldIndex.size() - 1];

            Buffer result;

            // Extract raw buffer
            if (type == "")
            {
                Field *parent = field;
                field = nullptr;

                // We don't know the wire type, so try all untill one succeeds
                if (field == nullptr) {field = parent->getSubField(fieldNumber, WIRETYPE_LEN, fieldIndex);}
                if (field == nullptr) {field = parent->getSubField(fieldNumber, WIRETYPE_VARINT, fieldIndex);}
                if (field == nullptr) {field = parent->getSubField(fieldNumber, WIRETYPE_I64, fieldIndex);}
                if (field == nullptr) {field = parent->getSubField(fieldNumber, WIRETYPE_I32, fieldIndex);}
                if (field == nullptr) {return;}

                // Point result buffer to correct memmory address
                result.start = field->value.start + (buffer.start - root->value.start);
                result.end = field->value.end + (buffer.start - root->value.start);

                sqlite3_result_blob(context, (char *)result.start, result.size(), SQLITE_STATIC);
                return;
            }

            // Extract extract WIRETYPE_LEN
            if (type == "string")
            {
                // Extract correct wire type
                field = field->getSubField(fieldNumber, WIRETYPE_LEN, fieldIndex);
                if(field == nullptr){return;}
                
                // Point result buffer to correct memmory address
                result.start = field->value.start + (buffer.start - root->value.start);
                result.end = field->value.end + (buffer.start - root->value.start);
                
                // Return result
                sqlite3_result_text(context, (char *)result.start, result.size(), SQLITE_STATIC);
                return;
            }
            if (type == "bytes")
            {
                // Extract correct wire type
                field = field->getSubField(fieldNumber, WIRETYPE_LEN, fieldIndex);
                if(field == nullptr){return;}
                
                // Point result buffer to correct memmory address
                result.start = field->value.start + (buffer.start - root->value.start);
                result.end = field->value.end + (buffer.start - root->value.start);
                
                // Return result
                sqlite3_result_blob(context, (char *)result.start, result.size(), SQLITE_STATIC);
                return;
            }

            // Extract WIRETYPE_VARINT
            if (type == "int32")
            {
                // Extract correct wire type
                field = field->getSubField(fieldNumber, WIRETYPE_VARINT, fieldIndex);
                if(field == nullptr){return;}
                
                // Point result buffer to correct memmory address
                result.start = field->value.start + (buffer.start - root->value.start);
                result.end = field->value.end + (buffer.start - root->value.start);

                int32_t value;
                if (getInt32(&result, &value))
                {
                    sqlite3_result_int64(context, value);
                    return;
                }
            }
            if (type == "int64")
            {
                // Extract correct wire type
                field = field->getSubField(fieldNumber, WIRETYPE_VARINT, fieldIndex);
                if(field == nullptr){return;}
                
                // Point result buffer to correct memmory address
                result.start = field->value.start + (buffer.start - root->value.start);
                result.end = field->value.end + (buffer.start - root->value.start);

                int64_t value;
                if (getInt64(&result, &value))
                {
                    sqlite3_result_int64(context, value);
                    return;
                }
            }
            if (type == "uint32")
            {
                // Extract correct wire type
                field = field->getSubField(fieldNumber, WIRETYPE_VARINT, fieldIndex);
                if(field == nullptr){return;}
                
                // Point result buffer to correct memmory address
                result.start = field->value.start + (buffer.start - root->value.start);
                result.end = field->value.end + (buffer.start - root->value.start);

                uint32_t value;
                if (getUint32(&result, &value))
                {
                    sqlite3_result_int64(context, value);
                    return;
                }
            }
            if (type == "uint64")
            {
                // Extract correct wire type
                field = field->getSubField(fieldNumber, WIRETYPE_VARINT, fieldIndex);
                if(field == nullptr){return;}
                
                // Point result buffer to correct memmory address
                result.start = field->value.start + (buffer.start - root->value.start);
                result.end = field->value.end + (buffer.start - root->value.start);

                uint64_t value;
                if (getUint64(&result, &value))
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
                // Extract correct wire type
                field = field->getSubField(fieldNumber, WIRETYPE_VARINT, fieldIndex);
                if(field == nullptr){return;}
                
                // Point result buffer to correct memmory address
                result.start = field->value.start + (buffer.start - root->value.start);
                result.end = field->value.end + (buffer.start - root->value.start);

                int32_t value;
                if (getSint32(&result, &value))
                {
                    sqlite3_result_int64(context, value);
                    return;
                }
            }
            if (type == "sint64")
            {
                // Extract correct wire type
                field = field->getSubField(fieldNumber, WIRETYPE_VARINT, fieldIndex);
                if(field == nullptr){return;}
                
                // Point result buffer to correct memmory address
                result.start = field->value.start + (buffer.start - root->value.start);
                result.end = field->value.end + (buffer.start - root->value.start);

                int64_t value;
                if (getSint64(&result, &value))
                {
                    sqlite3_result_int64(context, value);
                    return;
                }
            }
            if (type == "bool")
            {
                // Extract correct wire type
                field = field->getSubField(fieldNumber, WIRETYPE_VARINT, fieldIndex);
                if(field == nullptr){return;}
                
                // Point result buffer to correct memmory address
                result.start = field->value.start + (buffer.start - root->value.start);
                result.end = field->value.end + (buffer.start - root->value.start);

                bool value;
                if (getBool(&result, &value))
                {
                    sqlite3_result_int64(context, value ? 1 : 0);
                    return;
                }
            }

            // Extract WIRETYPE_I64
            if (type == "fixed64")
            {
                // Extract correct wire type
                field = field->getSubField(fieldNumber, WIRETYPE_I64, fieldIndex);
                if(field == nullptr){return;}
                
                // Point result buffer to correct memmory address
                result.start = field->value.start + (buffer.start - root->value.start);
                result.end = field->value.end + (buffer.start - root->value.start);

                uint64_t value;
                if (getFixed64(&result, &value))
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
                // Extract correct wire type
                field = field->getSubField(fieldNumber, WIRETYPE_I64, fieldIndex);
                if(field == nullptr){return;}
                
                // Point result buffer to correct memmory address
                result.start = field->value.start + (buffer.start - root->value.start);
                result.end = field->value.end + (buffer.start - root->value.start);
                
                int64_t value;
                if (getSfixed64(&result, &value))
                {
                    sqlite3_result_int64(context, value);
                    return;
                }
            }
            if (type == "double")
            {
                // Extract correct wire type
                field = field->getSubField(fieldNumber, WIRETYPE_I64, fieldIndex);
                if(field == nullptr){return;}
                
                // Point result buffer to correct memmory address
                result.start = field->value.start + (buffer.start - root->value.start);
                result.end = field->value.end + (buffer.start - root->value.start);

                double value;
                if (getDouble(&result, &value))
                {
                    sqlite3_result_double(context, value);
                    return;
                }
            }

            // Extract WIRETYPE_I32
            if (type == "fixed32")
            {
                // Extract correct wire type
                field = field->getSubField(fieldNumber, WIRETYPE_I32, fieldIndex);
                if(field == nullptr){return;}
                
                // Point result buffer to correct memmory address
                result.start = field->value.start + (buffer.start - root->value.start);
                result.end = field->value.end + (buffer.start - root->value.start);

                uint32_t value;
                if (getFixed32(&result, &value))
                {
                    sqlite3_result_int64(context, value);
                    return;
                }
            }
            if (type == "sfixed32")
            {
                // Extract correct wire type
                field = field->getSubField(fieldNumber, WIRETYPE_I32, fieldIndex);
                if(field == nullptr){return;}
                
                // Point result buffer to correct memmory address
                result.start = field->value.start + (buffer.start - root->value.start);
                result.end = field->value.end + (buffer.start - root->value.start);

                int32_t value;
                if (getSfixed32(&result, &value))
                {
                    sqlite3_result_int64(context, value);
                    return;
                }
            }
            if (type == "float")
            {
                // Extract correct wire type
                field = field->getSubField(fieldNumber, WIRETYPE_I32, fieldIndex);
                if(field == nullptr){return;}
                
                // Point result buffer to correct memmory address
                result.start = field->value.start + (buffer.start - root->value.start);
                result.end = field->value.end + (buffer.start - root->value.start);

                float value;
                if (getFloat(&result, &value))
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