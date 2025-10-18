#include "protobuf_extract.h"
#include "sqlite3ext.h"

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
            Field* field;
            size_t length;
            uint8_t buffer[PROTOBUF_CACHE_BUFFER_SIZE];
        };

        Cache cache = {0};
        
        Path* path_from_value(sqlite3_value *value)
        {
            const char *pathText = (const char *)sqlite3_value_text(value); // NULL terminated cstring
            int pathLength = (int)sqlite3_value_bytes(value);

            // Check that the path begins with $, representing the root of the tree
            if (pathLength == 0 || pathText[0] != '$')
            {
                return nullptr;
            }

            size_t capacity = 1; // Start with 1 as initial capacity
            size_t length = 0; // Initialize path length 0
            Path* path = (Path*)sqlite3_malloc64(sizeof(Path) * capacity);

            // Parse the path string and traverse the message
            int fieldNumber, fieldIndex;
            const char *fieldStart = (const char *)strchr(pathText, '.');
            while(fieldStart)
            {
                const char *fieldEnd   = (const char *)strchr(fieldStart + 1, '.');
                const char *indexStart = (const char *)strchr(fieldStart + 1, '[');
                const char *indexEnd   = (const char *)strchr(fieldStart + 1, ']');

                // Extract field and index
                fieldNumber = atoi(fieldStart + 1);
                fieldIndex  = (indexStart && indexEnd && (indexStart < fieldEnd || fieldEnd == nullptr)) ? atoi(indexStart + 1) : 0;

                // Move path ponter forward
                fieldStart = fieldEnd;

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
            }

            // Set fieldNumber to the reserved number 0 to indicate end of path 
            if (path != nullptr) {path[length].fieldNumber = 0;}

            return path;          
        }

        enum ProtobufType
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

        struct EnumEntry
        {
            int32_t value;      // Numeric value
            size_t keyOffset;   // String Key offset in type string
            size_t keyLength;   // String Key length
        };

        struct Type
        {
            ProtobufType type;
            size_t enumEntiesSize;
            EnumEntry enumEntries[0];
        };
        

        Type* type_from_value(sqlite3_value *value)
        {
            const char *text = (const char *)sqlite3_value_text(value); // NULL terminated cstring
            int textLength = (int)sqlite3_value_bytes(value);

            // Initialize type as unknown
            Type *type = (Type*)sqlite3_malloc64(sizeof(Type));
            type->type = TYPE_UNKNOWN;
            type->enumEntiesSize = 0;
            
            if (textLength == 0) {type->type = TYPE_BUFFER;}
            if (textLength == 4 && memcmp(text,     "bool", textLength) == 0){type->type = TYPE_BOOL;}
            if (textLength == 4 && memcmp(text,     "enum", textLength) == 0){type->type = TYPE_ENUM;}
            if (textLength == 5 && memcmp(text,    "bytes", textLength) == 0){type->type = TYPE_BYTES;}
            if (textLength == 5 && memcmp(text,    "int32", textLength) == 0){type->type = TYPE_INT32;}
            if (textLength == 5 && memcmp(text,    "int64", textLength) == 0){type->type = TYPE_INT64;}
            if (textLength == 5 && memcmp(text,    "float", textLength) == 0){type->type = TYPE_FLOAT;}
            if (textLength == 6 && memcmp(text,   "string", textLength) == 0){type->type = TYPE_STRING;}
            if (textLength == 6 && memcmp(text,   "uint32", textLength) == 0){type->type = TYPE_UINT32;}
            if (textLength == 6 && memcmp(text,   "uint64", textLength) == 0){type->type = TYPE_UINT64;}
            if (textLength == 6 && memcmp(text,   "sint32", textLength) == 0){type->type = TYPE_SINT32;}
            if (textLength == 6 && memcmp(text,   "sint64", textLength) == 0){type->type = TYPE_SINT64;}
            if (textLength == 6 && memcmp(text,   "double", textLength) == 0){type->type = TYPE_DOUBLE;}
            if (textLength == 7 && memcmp(text,  "fixed64", textLength) == 0){type->type = TYPE_FIXED64;}
            if (textLength == 7 && memcmp(text,  "fixed32", textLength) == 0){type->type = TYPE_FIXED32;}
            if (textLength == 8 && memcmp(text, "sfixed64", textLength) == 0){type->type = TYPE_SFIXED64;}
            if (textLength == 8 && memcmp(text, "sfixed32", textLength) == 0){type->type = TYPE_SFIXED32;}

            // Handle enum type with enum definition example: enum MY_ENUM {VALUE_A=0;VALUE_B=1;VALUE_C=2;}
            if(textLength  >= 5 && memcmp(text, "enum", 4) == 0)
            {
                type->type = TYPE_ENUM;

                // find start of protobuf enum definition
                const char *nameStart = (const char *)strchr(text, '{');
                while (nameStart)
                {
                    // Remove leading spaces
                    while (*(++nameStart) == ' '){}
                    
                    // Find start of next value
                    const char *valueStart = (const char *)strchr(nameStart, '=');

                    // Add enum value to list
                    if(nameStart  && valueStart)
                    {

                        // Extract enum name
                        size_t keyLength = valueStart - nameStart;

                        // Add enum value to list
                        if (keyLength > 0)
                        {
                            type = (Type*)sqlite3_realloc64(type, sizeof(Type) + sizeof(EnumEntry) * (type->enumEntiesSize + 1));
                            type->enumEntries[type->enumEntiesSize].value = atoi(valueStart + 1);
                            type->enumEntries[type->enumEntiesSize].keyOffset = (size_t)(nameStart - text);
                            type->enumEntries[type->enumEntiesSize].keyLength = keyLength;
                            type->enumEntiesSize++;
                        }
                    }

                    // Find start of next name
                    nameStart = (const char *)strchr(nameStart, ';');

                }              
            }
            
            return type;
        }

        /// Return the element (or elements)
        ///
        ///     SELECT protobuf_extract(data, "$.1.2[0].3", type);
        ///
        /// @returns a Protobuf-encoded BLOB or the appropriate SQL datatype
        static void protobuf_extract(sqlite3_context *context, int argc, sqlite3_value **argv)
        {
            // Look up path from aux data
            bool setPathAuxData = false;
            Path* path = (Path*)sqlite3_get_auxdata(context, 1);
            if (path == nullptr)
            {
                path = path_from_value(argv[1]);
                setPathAuxData = true;
            }

            // Look up type from aux data
            bool setTypeAuxData = false;
            Type* type = (Type*)sqlite3_get_auxdata(context, 2);
            if (type == nullptr)
            {
                type = type_from_value(argv[2]);
                setTypeAuxData = true;
            }

            // Check validity of type
            if (type->type == TYPE_UNKNOWN)
            {
                sqlite3_result_error(context, "Type not valid, try type '' or check documentation", -1);

                // Set aux data, needs to be done after data no longer is needed (see sqlite documentation)
                if (setPathAuxData){sqlite3_set_auxdata(context, 1, path, sqlite3_free);}
                if (setTypeAuxData){sqlite3_set_auxdata(context, 2, type, sqlite3_free);}
                return;
            }

            // Check validity of path
            if (path == nullptr){
                sqlite3_result_error(context, "Path not valid, path should start with $", -1);
                
                // Set aux data, needs to be done after data no longer is needed (see sqlite documentation)
                if (setPathAuxData){sqlite3_set_auxdata(context, 1, path, sqlite3_free);}
                if (setTypeAuxData){sqlite3_set_auxdata(context, 2, type, sqlite3_free);}
                return;
            }
            
            // Load protobuf data into a buffer
            Buffer buffer;
            int length = (int)sqlite3_value_bytes(argv[0]);
            buffer.start = (const uint8_t *)sqlite3_value_blob(argv[0]);
            buffer.end = buffer.start + length;

            // Look up message in cache
            Field* root = nullptr;
            if (cache.length != 0 && cache.length == length && memcmp(&cache.buffer, buffer.start, length) == 0)
            {
                // Chache hit -> use the decoded field from cache
                root = cache.field;
            }
            else if (length <= PROTOBUF_CACHE_BUFFER_SIZE)
            {
                // Chache miss and buffer fits in cache -> decode protobuf and cache result
                memcpy(cache.buffer, buffer.start, length);
                free(cache.field);
                cache.field = decodeProtobuf(buffer, false);
                cache.length = length;
                root = cache.field;
            }
            else
            {
                // Chache miss, but buffer does not fit in cache -> decode protobuf and invalidate cache
                cache.length = 0;
                free(cache.field);
                cache.field = decodeProtobuf(buffer, false);
                root = cache.field;
            }
            
            // Traverse path to the desired field
            Field *field = root;
            Field *parent = nullptr;
            int32_t index = 0;
            for (size_t i = 0; path[i].fieldNumber != 0; i++)
            {
                parent = field;
                field = nullptr;
                if (path[i+1].fieldNumber != 0) // Not at end of path
                {
                    if (field == nullptr) {field = getSubField(parent, path[i].fieldNumber, WIRETYPE_LEN, path[i].fieldIndex);}
                    if (field == nullptr) {field = getSubField(parent, path[i].fieldNumber, WIRETYPE_SGROUP, path[i].fieldIndex);}
                }
                else
                {
                    switch (type->type)
                    {
                    case TYPE_BUFFER:
                        // We don't know the wire type, so try all until one succeeds
                        if (field == nullptr) {field = getSubField(parent, path[i].fieldNumber, WIRETYPE_LEN, path[i].fieldIndex);}
                        if (field == nullptr) {field = getSubField(parent, path[i].fieldNumber, WIRETYPE_SGROUP, path[i].fieldIndex);}
                        if (field == nullptr) {field = getSubField(parent, path[i].fieldNumber, WIRETYPE_VARINT, path[i].fieldIndex);}
                        if (field == nullptr) {field = getSubField(parent, path[i].fieldNumber, WIRETYPE_I64, path[i].fieldIndex);}
                        if (field == nullptr) {field = getSubField(parent, path[i].fieldNumber, WIRETYPE_I32, path[i].fieldIndex);}
                        break;
                    case TYPE_STRING:
                    case TYPE_BYTES:
                        field = getSubField(parent, path[i].fieldNumber, WIRETYPE_LEN, path[i].fieldIndex);
                        break;
                    case TYPE_INT32:
                    case TYPE_INT64:
                    case TYPE_UINT32:
                    case TYPE_UINT64:
                    case TYPE_SINT32:
                    case TYPE_SINT64:
                    case TYPE_BOOL:
                    case TYPE_ENUM:
                        field = getSubField(parent, path[i].fieldNumber, WIRETYPE_VARINT, path[i].fieldIndex);
                        if (field == nullptr) {field = getSubField(parent, path[i].fieldNumber, WIRETYPE_LEN, 0); index = path[i].fieldIndex;} // Packed repeated
                        break;
                    case TYPE_FIXED64:
                    case TYPE_SFIXED64:
                    case TYPE_DOUBLE:
                        field = getSubField(parent, path[i].fieldNumber, WIRETYPE_I64, path[i].fieldIndex);
                        if (field == nullptr) {field = getSubField(parent, path[i].fieldNumber, WIRETYPE_LEN, 0); index = path[i].fieldIndex;} // Packed repeated
                        break;
                    case TYPE_FIXED32:
                    case TYPE_SFIXED32:
                    case TYPE_FLOAT:
                        field = getSubField(parent, path[i].fieldNumber, WIRETYPE_I32, path[i].fieldIndex);
                        if (field == nullptr) {field = getSubField(parent, path[i].fieldNumber, WIRETYPE_LEN, 0); index = path[i].fieldIndex;} // Packed repeated
                        break;
                    default:
                        field = nullptr;
                        break;
                    }
                }

                if (field == nullptr) {break;}
            }


            if (field == nullptr) 
            {
                // Field not found, return NULL
                sqlite3_result_null(context);
                
                // Set aux data, needs to be done after data no longer is needed (see sqlite documentation)
                if (setPathAuxData){sqlite3_set_auxdata(context, 1, path, sqlite3_free);}
                if (setTypeAuxData){sqlite3_set_auxdata(context, 2, type, sqlite3_free);}
                return;
            }


            // Create result buffer pointing to correct memmory address
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

            switch (type->type)
            {
            case TYPE_BUFFER:
                sqlite3_result_blob(context, (char *)result.start, getSize(&result), SQLITE_STATIC);
                break;
            case TYPE_STRING:
                sqlite3_result_text(context, (char *)result.start, getSize(&result), SQLITE_STATIC);
                break;
            case TYPE_BYTES:
                sqlite3_result_blob(context, (char *)result.start, getSize(&result), SQLITE_STATIC);
                break;
            case TYPE_ENUM:
                if (DECODE_OK == getInt32(&result, &valueInt32, index)) 
                {
                    sqlite3_result_int(context, valueInt32);
                    for (size_t i = 0; i < type->enumEntiesSize; i++)
                    {
                        if (type->enumEntries[i].value == valueInt32)
                        {
                            sqlite3_result_text(context, (char *)sqlite3_value_text(argv[2]) + type->enumEntries[i].keyOffset, type->enumEntries[i].keyLength, SQLITE_STATIC);
                        }
                    }
                }
                break;
            case TYPE_INT32:
                if (DECODE_OK == getInt32(&result, &valueInt32, index)) {sqlite3_result_int(context, valueInt32);}
                break;
            case TYPE_INT64:
                if (DECODE_OK == getInt64(&result, &valueInt64, index)) {sqlite3_result_int64(context, valueInt64);}
                break;
            case TYPE_UINT32:
                if (DECODE_OK == getUint32(&result, &valueUint32, index)) {sqlite3_result_int64(context, valueUint32);}
                break;
            case TYPE_UINT64:
                if (DECODE_OK == getUint64(&result, &valueUint64, index)) {sqlite3_result_int64(context, valueUint64);}
                if (valueUint64 > INT64_MAX) {sqlite3_log(SQLITE_WARNING,"Protobuf type is unsigned, but SQLite does not support unsigned types. Value %llu doesn't fit in an int64.", valueUint64);}
                break;
            case TYPE_SINT32:
                if (DECODE_OK == getSint32(&result, &valueInt32, index)) {sqlite3_result_int(context, valueInt32);}
                break;
            case TYPE_SINT64:
                if (DECODE_OK == getSint64(&result, &valueInt64, index)) {sqlite3_result_int64(context, valueInt64);}
                break;
            case TYPE_BOOL:
                if (DECODE_OK == getBool(&result, &valueBool, index)) {sqlite3_result_int(context, valueBool ? 1 : 0);}
                break;
            case TYPE_FIXED64:
                if (DECODE_OK == getFixed64(&result, &valueUint64, index)) {sqlite3_result_int64(context, valueUint64);}
                if (valueUint64 > INT64_MAX) {sqlite3_log(SQLITE_WARNING,"Protobuf type is unsigned, but SQLite does not support unsigned types. Value %llu doesn't fit in an int64.", valueUint64);}
                break;
            case TYPE_SFIXED64:
                if (DECODE_OK == getSfixed64(&result, &valueInt64, index)) {sqlite3_result_int64(context, valueInt64);}
                break;
            case TYPE_DOUBLE:
                if (DECODE_OK == getDouble(&result, &valueDouble, index)) {sqlite3_result_double(context, valueDouble);}
                break;
            case TYPE_FIXED32:
                if (DECODE_OK == getFixed32(&result, &valueUint32, index)) {sqlite3_result_int64(context, valueUint32);}
                break;
            case TYPE_SFIXED32:
                if (DECODE_OK == getSfixed32(&result, &valueInt32, index)) {sqlite3_result_int(context, valueInt32);}
                break;
            case TYPE_FLOAT:
                if (DECODE_OK == getFloat(&result, &valueFloat, index)) {sqlite3_result_double(context, valueFloat);}
                break;
            default:
                break;
            }

            // Set aux data, needs to be done after data no longer is needed (see sqlite documentation)
            if (setPathAuxData){sqlite3_set_auxdata(context, 1, path, sqlite3_free);}
            if (setTypeAuxData){sqlite3_set_auxdata(context, 2, type, sqlite3_free);}
            return;
        }
    } // namespace

    int register_protobuf_extract(sqlite3 *db, char **pzErrMsg, const sqlite3_api_routines *pApi)
    {
        return sqlite3_create_function(db, "protobuf_extract", 3, SQLITE_UTF8 | SQLITE_DETERMINISTIC, 0, protobuf_extract, 0, 0);
    }

} // namespace sqlite_protobuf