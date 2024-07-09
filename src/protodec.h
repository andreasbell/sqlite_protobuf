#pragma once

#include <map>
#include <ctype.h>
#include <vector>
#include <iostream>

enum WireType
{
    // int32, int64, uint32, uint64, sint32, sint64, bool, enum
    WIRETYPE_VARINT = 0,
    // fixed64, sfixed64, double
    WIRETYPE_I64 = 1,
    // string, bytes, embedded messages, packed repeated fields
    WIRETYPE_LEN = 2,
    // group start (deprecated) not implemented
    WIRETYPE_SGROUP = 3,
    // group end (deprecated) not implemented
    WIRETYPE_EGROUP = 4,
    // fixed32, sfixed32, float
    WIRETYPE_I32 = 5,
};

struct Buffer
{
    const uint8_t *start; // Pointer to start of buffer
    const uint8_t *end;   // Ponter to end of buffer

    size_t size() const { return this->end - this->start; }
};

struct Field
{
    uint32_t tag;
    uint32_t wireType;
    uint32_t fieldNum;
    uint32_t depth;
    Buffer value;
    Field* parent;
    std::vector<Field> subFields;

    std::map<uint32_t, std::vector<Field *>> subFieldMap();
    Field* getSubField(uint32_t fieldNumber, WireType wireType, int64_t index);
};

/**
 * @brief Decode protobuf message
 *
 * @param[in] data protobuf data buffer message
 * @param[in] len protobuf message length
 * @param[in] packed try decoding packed fields
 * @return Field containing decoded protobuf message
 */
Field decodeProtobuf(const Buffer &in, bool packed = false);

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
int getInt32(const Buffer *in, int32_t *out);
int getInt64(const Buffer *in, int64_t *out);
int getUint32(const Buffer *in, uint32_t *out);
int getUint64(const Buffer *in, uint64_t *out);
int getSint32(const Buffer *in, int32_t *out);
int getSint64(const Buffer *in, int64_t *out);
int getBool(const Buffer *in, bool *out);
int getFixed64(const Buffer *in, uint64_t *out);
int getSfixed64(const Buffer *in, int64_t *out);
int getDouble(const Buffer *in, double *out);
int getFixed32(const Buffer *in, uint32_t *out);
int getSfixed32(const Buffer *in, int32_t *out);
int getFloat(const Buffer *in, float *out);