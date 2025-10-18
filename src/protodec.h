#pragma once

#include <cstdint>
#include <iostream>

#define DECODE_OK 0
#define DECODE_ERROR 1

enum WireType
{
    // int32, int64, uint32, uint64, sint32, sint64, bool, enum
    WIRETYPE_VARINT = 0,
    // fixed64, sfixed64, double
    WIRETYPE_I64 = 1,
    // string, bytes, embedded messages, packed repeated fields
    WIRETYPE_LEN = 2,
    // group start (deprecated)
    WIRETYPE_SGROUP = 3,
    // group end (deprecated)
    WIRETYPE_EGROUP = 4,
    // fixed32, sfixed32, float
    WIRETYPE_I32 = 5,
};

struct Buffer
{
    const uint8_t *start; // Pointer to start of buffer
    const uint8_t *end;   // Ponter to end of buffer
};

size_t getSize(const Buffer* buffer);

struct Field
{
    uint32_t tag;
    Buffer value;
    size_t subFieldsOffset;
    size_t subFieldsSize;
};

int getWireType(const Field& field);
int getFieldNumber(const Field& field);
Field* getSubField(Field* field, uint32_t fieldNumber, WireType wireType, int64_t index);

/**
 * @brief Decode protobuf message
 *
 * @param[in] data protobuf data buffer message
 * @param[in] len protobuf message length
 * @param[in] packed try decoding packed fields
 * @return Field containing decoded protobuf message
 */
Field* decodeProtobuf(Buffer in, bool packed = false);

/**
 * @brief Convert Field into JSON
 *
 * @param[in] field protobuf field
 * @param[out] os string stream with json string
 * @param[in] showType show wire type along with field number
 */
void toJson(Field *field, std::ostream &os, bool showType = false);

/**
 * @brief Get specific type form buffer
 *
 * @param[in] in protobuf buffer
 * @param[out] out decoded value
 * @return int success
 */
int getInt32(const Buffer *in, int32_t *out, int64_t index);
int getInt64(const Buffer *in, int64_t *out, int64_t index);
int getUint32(const Buffer *in, uint32_t *out, int64_t index);
int getUint64(const Buffer *in, uint64_t *out, int64_t index);
int getSint32(const Buffer *in, int32_t *out, int64_t index);
int getSint64(const Buffer *in, int64_t *out, int64_t index);
int getBool(const Buffer *in, bool *out, int64_t index);
int getFixed64(const Buffer *in, uint64_t *out, int64_t index);
int getSfixed64(const Buffer *in, int64_t *out, int64_t index);
int getDouble(const Buffer *in, double *out, int64_t index);
int getFixed32(const Buffer *in, uint32_t *out, int64_t index);
int getSfixed32(const Buffer *in, int32_t *out, int64_t index);
int getFloat(const Buffer *in, float *out, int64_t index);