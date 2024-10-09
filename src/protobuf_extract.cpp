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
            uint32_t fieldNumber;
            int32_t fieldIndex;
        };

        // Create a global cache for saving data
        #define PROTOBUF_CACHE_BUFFER_SIZE 4096

        struct Cache
        {
            // Protobuf decode cache
            Field field;
            size_t length;
            uint8_t buffer[PROTOBUF_CACHE_BUFFER_SIZE];
        };

        Cache cache = {0};

        std::string string_from_sqlite3_value(sqlite3_value *value)
        {
            const char *text = static_cast<const char *>(sqlite3_value_blob(value));
            size_t text_size = static_cast<size_t>(sqlite3_value_bytes(value));
            return std::string(text, text_size);
        }
        
        Path* path_from_string(const std::string &pathString)
        {
            // Check that the path begins with $, representing the root of the tree
            if (pathString.length() == 0 || pathString[0] != '$')
            {
                return nullptr;
            }

            size_t capacity = 1; // Start with 1 as initial capacity
            size_t length = 0; // Initialize path length 0
            Path* path = (Path*)sqlite3_malloc64(sizeof(Path) * capacity);

            int fieldNumber, fieldIndex;

            // Parse the path string and traverse the message
            size_t fieldStart = pathString.find(".", 0);
            while(fieldStart < pathString.size())
            {
                size_t fieldEnd = pathString.find(".", fieldStart + 1);
                size_t indexStart = pathString.find("[", fieldStart + 1);
                size_t indexEnd = pathString.find("]", fieldStart + 1);

                fieldEnd = (fieldEnd == std::string::npos) ? pathString.size() : fieldEnd;

                if (indexStart < fieldEnd && indexEnd < fieldEnd) // Both field and index supplied
                {
                    fieldNumber = std::atoi(pathString.substr(fieldStart + 1, indexStart - fieldStart - 1).c_str());
                    fieldIndex  = std::atoi(pathString.substr(indexStart + 1, indexEnd - indexStart - 1).c_str());
                }
                else // Only field supplied
                {
                    fieldNumber = std::atoi(pathString.substr(fieldStart + 1, fieldEnd - fieldStart - 1).c_str());
                    fieldIndex  = 0; 
                }

                // Add path entry to end of list
                if (path != nullptr)
                {
                    path[length].fieldNumber = fieldNumber;
                    path[length].fieldIndex = fieldIndex;
                    length++;
                    if (length >= capacity)
                    {
                        // Double the capacity of the path vector
                        capacity = capacity * 2;
                        path = (Path*)sqlite3_realloc64(path, sizeof(Path) * capacity);
                    }
                }

                // Move path ponter forward
                fieldStart = fieldEnd;
            }

            // Set fieldNumber to the reserved number 0 to indicate end of path 
            if (path != nullptr) {path[length].fieldNumber = 0;}

            return path;          
        }

        enum Type 
        {
            // SPECIAL TYPES
            TYPE_UNKNOWN,
            TYPE_BUFFER,
            // WIRETYPE VARINT
            TYPE_INT32,
            TYPE_INT64,
            TYPE_UINT32,
            TYPE_UINT64,
            TYPE_SINT32,
            TYPE_SINT64,
            TYPE_BOOL,
            TYPE_ENUM,
            // WIRETYPE I64
            TYPE_FIXED64,
            TYPE_SFIXED64,
            TYPE_DOUBLE,
            // WIRETYPE LEN
            TYPE_STRING,
            TYPE_BYTES,
            // WIRETYPE I32
            TYPE_FIXED32, 
            TYPE_SFIXED32, 
            TYPE_FLOAT,
        };

        Type type_from_string(const std::string &type)
        {
            
            switch (type.length())
            {
            case 0: // ""
                return TYPE_BUFFER;
            case 4: // bool
                if (type == "bool") {return TYPE_BOOL;}
                if (type == "enum") {return TYPE_ENUM;}
                return TYPE_UNKNOWN;
            case 5: // bytes, int32, int64, float
                if (type == "bytes") {return TYPE_BYTES;}
                if (type == "int32") {return TYPE_INT32;}
                if (type == "int64") {return TYPE_INT64;}
                if (type == "float") {return TYPE_FLOAT;}
                return TYPE_UNKNOWN;
            case 6: // string, uint32, uint64, sint32, sint64, double
                if (type == "string") {return TYPE_STRING;}
                if (type == "uint32") {return TYPE_UINT32;}
                if (type == "uint64") {return TYPE_UINT64;}
                if (type == "sint32") {return TYPE_SINT32;}
                if (type == "sint64") {return TYPE_SINT64;}
                if (type == "double") {return TYPE_DOUBLE;}
                return TYPE_UNKNOWN;
            case 7: // fixed64, fixed32
                if (type == "fixed64") {return TYPE_FIXED64;}
                if (type == "fixed32") {return TYPE_FIXED32;}
                return TYPE_UNKNOWN;
            case 8: // sfixed64, sfixed32
                if (type == "sfixed64") {return TYPE_SFIXED64;}
                if (type == "sfixed32") {return TYPE_SFIXED32;}
                return TYPE_UNKNOWN;
            default:
                return TYPE_UNKNOWN;
            }
        }

        /// Return the element (or elements)
        ///
        ///     SELECT protobuf_extract(data, "$.1.2[0].3", type);
        ///
        /// @returns a Protobuf-encoded BLOB or the appropriate SQL datatype
        static void protobuf_extract(sqlite3_context *context, int argc, sqlite3_value **argv)
        {

            // Look up type from aux data
            Type type;
            Type* typePtr = (Type*)sqlite3_get_auxdata(context, 2);
            if (typePtr == nullptr)
            {
                const std::string typeString = string_from_sqlite3_value(argv[2]);
                type = type_from_string(typeString);
                typePtr = (Type*)sqlite3_malloc64(sizeof(Type));
                if (typePtr != nullptr)
                {
                    *typePtr = type;
                    // Set type aux data since we no longer need the type pointer
                    sqlite3_set_auxdata(context, 2, typePtr, sqlite3_free);
                }
            }
            else
            {
                type = *typePtr;
            }

            // Check validity of type
            if (type == TYPE_UNKNOWN)
            {
                sqlite3_result_error(context, "Type not valid, try type '' or check documentation", -1);
                return;
            }

            // Look up path from aux data
            bool setPathAuxData = false;
            Path* path = (Path*)sqlite3_get_auxdata(context, 1);
            if (path == nullptr)
            {
                const std::string pathString = string_from_sqlite3_value(argv[1]);
                path = path_from_string(pathString);
                setPathAuxData = true;
            }

            // Check validity of path
            if (path == nullptr){
                sqlite3_result_error(context, "Path not valid, path should start with $", -1);
                return;
            }
            
            // Load protobuf data into a buffer
            Buffer buffer;
            size_t length = static_cast<size_t>(sqlite3_value_bytes(argv[0]));
            buffer.start = static_cast<const uint8_t *>(sqlite3_value_blob(argv[0]));
            buffer.end = buffer.start + length;

            // Look up message in cache
            Field* root = nullptr;
            if (cache.length != 0 && cache.length == length && memcmp(&cache.buffer, buffer.start, length) == 0)
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
            
            // Traverse path to the desired field
            Field *field = root;
            Field *parent = nullptr;
            for (size_t i = 0; path[i].fieldNumber != 0; i++)
            {
                parent = field;
                field = nullptr;
                if (path[i+1].fieldNumber != 0) // Not at end of path
                {
                    if (field == nullptr) {field = parent->getSubField(path[i].fieldNumber, WIRETYPE_LEN, path[i].fieldIndex);}
                    if (field == nullptr) {field = parent->getSubField(path[i].fieldNumber, WIRETYPE_SGROUP, path[i].fieldIndex);}
                }
                else
                {
                    switch (type)
                    {
                    case TYPE_BUFFER:
                        // We don't know the wire type, so try all until one succeeds
                        if (field == nullptr) {field = parent->getSubField(path[i].fieldNumber, WIRETYPE_LEN, path[i].fieldIndex);}
                        if (field == nullptr) {field = parent->getSubField(path[i].fieldNumber, WIRETYPE_SGROUP, path[i].fieldIndex);}
                        if (field == nullptr) {field = parent->getSubField(path[i].fieldNumber, WIRETYPE_VARINT, path[i].fieldIndex);}
                        if (field == nullptr) {field = parent->getSubField(path[i].fieldNumber, WIRETYPE_I64, path[i].fieldIndex);}
                        if (field == nullptr) {field = parent->getSubField(path[i].fieldNumber, WIRETYPE_I32, path[i].fieldIndex);}
                        break;
                    case TYPE_STRING:
                    case TYPE_BYTES:
                        field = parent->getSubField(path[i].fieldNumber, WIRETYPE_LEN, path[i].fieldIndex);
                        break;
                    case TYPE_INT32:
                    case TYPE_INT64:
                    case TYPE_UINT32:
                    case TYPE_UINT64:
                    case TYPE_SINT32:
                    case TYPE_SINT64:
                    case TYPE_BOOL:
                    case TYPE_ENUM:
                        field = parent->getSubField(path[i].fieldNumber, WIRETYPE_VARINT, path[i].fieldIndex);
                        break;
                    case TYPE_FIXED64:
                    case TYPE_SFIXED64:
                    case TYPE_DOUBLE:
                        field = parent->getSubField(path[i].fieldNumber, WIRETYPE_I64, path[i].fieldIndex);
                        break;
                    case TYPE_FIXED32:
                    case TYPE_SFIXED32:
                    case TYPE_FLOAT:
                        field = parent->getSubField(path[i].fieldNumber, WIRETYPE_I32, path[i].fieldIndex);
                        break;
                    default:
                        field = nullptr;
                        break;
                    }
                }

                if (field == nullptr) {break;}
            }

            // Set path aux data, needs to be done after path no longer is needed (see sqlite documentation)
            if (setPathAuxData)
            {
                sqlite3_set_auxdata(context, 1, path, sqlite3_free);
            }

            // Create result buffer pointing to correct memmory address
            if (field == nullptr) {return;}
            Buffer result;
            result.start = field->value.start + (buffer.start - root->value.start);
            result.end = field->value.end + (buffer.start - root->value.start);

            // Extract data from buffer based on selected type
            int32_t valueInt32 = 0;
            int64_t valueInt64 = 0;
            uint32_t valueUint32 = 0;
            uint64_t valueUint64 = 0;
            double valueDouble = 0;
            float valueFloat = 0;
            bool valueBool = 0;

            switch (type)
            {
            case TYPE_BUFFER:
                sqlite3_result_blob(context, (char *)result.start, result.size(), SQLITE_STATIC);
                return;
            case TYPE_STRING:
                sqlite3_result_text(context, (char *)result.start, result.size(), SQLITE_STATIC);
                return;
            case TYPE_BYTES:
                sqlite3_result_blob(context, (char *)result.start, result.size(), SQLITE_STATIC);
                return;
            case TYPE_ENUM:
            case TYPE_INT32:
                if (getInt32(&result, &valueInt32)) {sqlite3_result_int(context, valueInt32);}
                return;
            case TYPE_INT64:
                if (getInt64(&result, &valueInt64)) {sqlite3_result_int64(context, valueInt64);}
                return;
            case TYPE_UINT32:
                if (getUint32(&result, &valueUint32)) {sqlite3_result_int64(context, valueUint32);}
                return;
            case TYPE_UINT64:
                if (getUint64(&result, &valueUint64)) {sqlite3_result_int64(context, valueUint64);}
                if (valueUint64 > INT64_MAX) {sqlite3_log(SQLITE_WARNING,"Protobuf type is unsigned, but SQLite does not support unsigned types. Value %llu doesn't fit in an int64.", valueUint64);}
                return;
            case TYPE_SINT32:
                if (getSint32(&result, &valueInt32)) {sqlite3_result_int(context, valueInt32);}
                return;
            case TYPE_SINT64:
                if (getSint64(&result, &valueInt64)) {sqlite3_result_int64(context, valueInt64);}
                return;
            case TYPE_BOOL:
                if (getBool(&result, &valueBool)) {sqlite3_result_int(context, valueBool ? 1 : 0);}
                return;
            case TYPE_FIXED64:
                if (getFixed64(&result, &valueUint64)) {sqlite3_result_int64(context, valueUint64);}
                if (valueUint64 > INT64_MAX) {sqlite3_log(SQLITE_WARNING,"Protobuf type is unsigned, but SQLite does not support unsigned types. Value %llu doesn't fit in an int64.", valueUint64);}
                return;
            case TYPE_SFIXED64:
                if (getSfixed64(&result, &valueInt64)) {sqlite3_result_int64(context, valueInt64);}
                return;
            case TYPE_DOUBLE:
                if (getDouble(&result, &valueDouble)) {sqlite3_result_double(context, valueDouble);}
                return;
            case TYPE_FIXED32:
                if (getFixed32(&result, &valueUint32)) {sqlite3_result_int64(context, valueUint32);}
                return;
            case TYPE_SFIXED32:
                if (getSfixed32(&result, &valueInt32)) {sqlite3_result_int(context, valueInt32);}
                return;
            case TYPE_FLOAT:
                if (getFloat(&result, &valueFloat)) {sqlite3_result_double(context, valueFloat);}
                return;
            default:
                return;
            }
        }
    } // namespace

    int register_protobuf_extract(sqlite3 *db, char **pzErrMsg, const sqlite3_api_routines *pApi)
    {
        return sqlite3_create_function(db, "protobuf_extract", 3, SQLITE_UTF8 | SQLITE_DETERMINISTIC, 0, protobuf_extract, 0, 0);
    }

} // namespace sqlite_protobuf