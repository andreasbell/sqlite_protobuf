#include "protodec.h"

#include <cstring>

#define DECODE_ERROR 0
#define DECODE_OK 1

#define TAG_BITS 3
#define MAX_VARINT_64BYTES 10
#define MAX_VARINT_32BYTES 5

static int decodeField(Field *field, Buffer *in, bool packed);
static uint32_t getTag(uint32_t fieldNumber, WireType wireType);

std::map< uint32_t, std::vector<Field *> > Field::subFieldMap()
{
    std::map< uint32_t, std::vector<Field *> > m;
    for (Field &f : this->subFields)
    {
        m[f.tag].push_back(&f);
    }
    return m;
}

Field* Field::getSubField(uint32_t fieldNumber, WireType wireType, int64_t index)
{
    auto m = subFieldMap();
    auto fields = m.find(getTag(fieldNumber, wireType));
    if (fields != m.end())
    {
        // Wrap around for negative indexing
        index = index < 0 ? fields->second.size() + index : index;

        // Check that index is within range
        if (index >= 0 && index < fields->second.size()) 
        {
            return fields->second[index];
        }
    }
    return nullptr;
}

static inline const uint8_t *readVarint(const Buffer *in, int64_t *out, size_t maxBytes)
{
    *out = 0;
    if (!in->start || in->start >= in->end)
    {
        // Invalid input buffer
        return nullptr;
    }
    
    uint64_t byte;
    for (size_t i = 0; i < maxBytes; i++)
    {
        // Check if we have reached end of buffer
        if (in->start + i >= in->end)
        {
            break;
        }

        // Add next 7 bits to MSB of output
        byte = in->start[i] & 0b01111111;
        *out |= byte << (i * 7);

        // Check continuation bit
        if (in->start[i] < 0b10000000)
        {
            // Decoding completed, return pointer to next byte in buffer
            return in->start + i + 1;
        }
    }

    // Error reading varint
    *out = 0;
    return nullptr;
}

static inline uint32_t getTag(Buffer *b)
{
    int64_t tag;
    const uint8_t *ptr = readVarint(b, &tag, MAX_VARINT_32BYTES);
    if (nullptr != ptr)
    {
        // Advance buffer if valid varint
        b->start = ptr;
    }
    return (uint32_t)tag;
}

static inline uint32_t getTag(uint32_t fieldNumber, WireType wireType)
{
    return (fieldNumber << TAG_BITS) | wireType;
}

static inline int getTagWireType(uint32_t tag)
{
    return tag & ((1 << TAG_BITS) - 1);
}

static inline int getTagFieldNumber(uint32_t tag)
{
    return tag >> TAG_BITS;
}

static inline void initField(Field *field, Buffer *in)
{
    memset(field, 0, sizeof(Field));
    field->tag = getTag(in);
    field->fieldNum = getTagFieldNumber(field->tag);
    field->wireType = getTagWireType(field->tag);
}

static inline int decodeVarint(Field *field, Buffer *in)
{
    const uint8_t *p;
    int64_t n;
    if (in->start >= in->end)
    {
        return DECODE_ERROR;
    }

    field->value.start = in->start;
    p = readVarint(in, &n, MAX_VARINT_64BYTES);
    if (!p)
    {
        return DECODE_ERROR;
    }

    in->start = p;
    field->value.end = p;

    return DECODE_OK;
}

static inline int decodeFixed64(Field *field, Buffer *in)
{
    if (in->size() < static_cast<int>(sizeof(int64_t)))
    {
        return DECODE_ERROR;
    }
    field->value.start = in->start;
    in->start += sizeof(int64_t);
    field->value.end = in->start;

    return DECODE_OK;
}

static inline int decodeFixed32(Field *field, Buffer *in)
{
    if (in->size() < static_cast<int>(sizeof(int32_t)))
    {
        return DECODE_ERROR;
    }
    field->value.start = in->start;
    in->start += sizeof(int32_t);
    field->value.end = in->start;

    return DECODE_OK;
}

static inline int decodeString(Field *field, Buffer *in)
{
    int64_t length;
    const uint8_t *ptr = readVarint(in, &length, MAX_VARINT_32BYTES);

    if (!ptr || static_cast<int>(length) > in->size())
    {
        return DECODE_ERROR;
    }

    field->value.start = ptr;
    field->value.end = ptr + length;
    in->start = field->value.end;

    return DECODE_OK;
}

static inline int decodeSubField(Field *field, bool packed)
{
    Buffer b = field->value;

    while (b.start < b.end)
    {
        Field subField;
        initField(&subField, &b);
        subField.depth = field->depth + 1;
        subField.parent = field;

        if (!subField.tag || !subField.fieldNum)
        {
            field->subFields.clear();
            return DECODE_ERROR;
        }

        if (DECODE_OK != decodeField(&subField, &b, packed))
        {
            field->subFields.clear();
            return DECODE_ERROR;
        }

        field->subFields.push_back(subField);
    }

    return DECODE_OK;
}

static inline int decodePacked(Field *field, WireType wireType)
{
    Buffer b = field->value;
    std::vector<Field> fields;

    if (WIRETYPE_VARINT == wireType || WIRETYPE_I32 == wireType || WIRETYPE_I64 == wireType)
    {
        while (b.start < b.end)
        {
            Field subField;
            subField.tag = getTag(field->fieldNum, wireType);
            subField.fieldNum = getTagFieldNumber(subField.tag);
            subField.wireType = getTagWireType(subField.tag);
            subField.depth = field->depth;
            subField.parent = field->parent;

            if (DECODE_OK != decodeField(&subField, &b, true))
            {
                return DECODE_ERROR;
            }
            fields.push_back(subField);
        }

        // field is a length delimited repsesentation of the packed repeted field, so add them to parent
        field->parent->subFields.insert(field->parent->subFields.end(), fields.begin(), fields.end());

        return DECODE_OK;
    }
    return DECODE_ERROR;
}

static inline int decodeField(Field *field, Buffer *in, bool packed)
{
    switch (field->wireType)
    {
    case WIRETYPE_VARINT:
        return decodeVarint(field, in);

    case WIRETYPE_I64:
        return decodeFixed64(field, in);

    case WIRETYPE_LEN:
        // Try deconding as string, should always work for well formed WIRETYPE_LEN
        if (DECODE_OK != decodeString(field, in))
        {
            return DECODE_ERROR;
        }

        // Try decoding as sub fields
        if (DECODE_OK == decodeSubField(field, packed))
        {
            return DECODE_OK;
        }

        // Try decoding as packed repeated fields
        if (packed)
        {
            decodePacked(field, WIRETYPE_VARINT);
            decodePacked(field, WIRETYPE_I64);
            decodePacked(field, WIRETYPE_I32);
        }

        return DECODE_OK;

    case WIRETYPE_I32:
        return decodeFixed32(field, in);

    default:
        return DECODE_ERROR;
    }

    return DECODE_ERROR;
}

Field decodeProtobuf(const Buffer &in, bool packed)
{
    Field field;
    memset(&field, 0, sizeof(Field));
    field.wireType = WIRETYPE_LEN;
    field.value = in;
    decodeSubField(&field, packed);
    return field;
}

static inline void base64Encode(const Buffer &in, std::ostream &os)
{
    int val = 0, valb = -6, size = 0;
    for (size_t i = 0; i < in.size(); i++)
    {
        val = (val << 8) + in.start[i];
        valb += 8;
        while (valb >= 0)
        {
            os << "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/"[(val >> valb) & 0x3F];
            size++;
            valb -= 6;
        }
    }
    if (valb > -6)
    {
        os << "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/"[((val << 8) >> (valb + 8)) & 0x3F];
        size++;
    }
    while (size++ % 4)
    {
        os << '=';
    }
}

static inline bool isPrintable(const Buffer &b)
{
    for (size_t i = 0; i < b.size(); i++)
    {
        if (!std::isprint(b.start[i]))
        {
            return false;
        }
    }
    return true;
}

void toJson(Field *field, std::ostream &os, bool showType)
{
    if (!field->subFields.empty())
    {
        os << "{";
        auto m = field->subFieldMap();
        for (auto it = m.begin(); it != m.end(); it)
        {
            os << "\"" << getTagFieldNumber(it->first);
            if (showType)
            {
                os << "_" << getTagWireType(it->first); 
            }
            os << "\":";
            if (it->second.size() > 1)
            {
                os << "[";
            }
            for (auto f : it->second)
            {
                toJson(f, os, showType);
                if (f != it->second.back())
                {
                    os << ",";
                }
            }
            if (it->second.size() > 1)
            {
                os << "]";
            }
            if (++it != m.end())
            {
                os << ",";
            }
        }
        os << "}";
    }
    else if (field->wireType == WIRETYPE_VARINT)
    {
        int64_t number;
        getInt64(&field->value, &number); // Guess type is signed 64 bit int
        os << number;
    }
    else if (field->wireType == WIRETYPE_I64)
    {
        double number;
        getDouble(&field->value, &number); // Guess type is double
        os << number;
    }
    else if (field->wireType == WIRETYPE_I32)
    {
        float number;
        getFloat(&field->value, &number); // Guess type is float
        os << number;
    }
    else
    {
        os << "\"";
        if (isPrintable(field->value))
        {
            // Write buffer directly to json
            os.write((const char *)field->value.start, field->value.size());
        }
        else
        {
            // Write base64 encoded buffer to json
            base64Encode(field->value, os);
        }
        os << "\"";
    }
}

int getInt32(const Buffer *in, int32_t *out)
{
    int64_t number;
    if (!readVarint(in, &number, MAX_VARINT_32BYTES)) 
    {
        return DECODE_ERROR;
    }

    *out = (int32_t)number;
    return DECODE_OK;
}

int getInt64(const Buffer *in, int64_t *out)
{   
    int64_t number;
    if (!readVarint(in, &number, MAX_VARINT_64BYTES)) 
    {
        return DECODE_ERROR;
    }
    
    *out = (int64_t)number;
    return DECODE_OK;
}

int getUint32(const Buffer *in, uint32_t *out)
{
    int64_t number;
    if (!readVarint(in, &number, MAX_VARINT_32BYTES)) 
    {
        return DECODE_ERROR;
    }

    *out = (uint32_t)number;
    return DECODE_OK;
}

int getUint64(const Buffer *in, uint64_t *out)
{
    int64_t number;
    if (!readVarint(in, &number, MAX_VARINT_64BYTES)) 
    {
        return DECODE_ERROR;
    }

    *out = (uint64_t)number;
    return DECODE_OK;
}

int getSint32(const Buffer *in, int32_t *out)
{
    int64_t number;
    if (!readVarint(in, &number, MAX_VARINT_32BYTES)) 
    {
        return DECODE_ERROR;
    }

    *out = (int32_t)number;
    *out = (*out >> 1) ^ -(*out & 1); // zigzag decoding
    return DECODE_OK;
}

int getSint64(const Buffer *in, int64_t *out)
{
    int64_t number;
    if (!readVarint(in, &number, MAX_VARINT_64BYTES)) 
    {
        return DECODE_ERROR;
    }

    *out = (int64_t)number;
    *out = (*out >> 1) ^ -(*out & 1); // zigzag decoding
    return DECODE_OK;
}

int getBool(const Buffer *in, bool *out)
{
    int64_t number;
    if (!readVarint(in, &number, MAX_VARINT_32BYTES)) 
    {
        return DECODE_ERROR;
    }

    *out = (bool)number;
    return DECODE_OK;
}

int getFixed64(const Buffer *in, uint64_t *out)
{
    if (in->size() < static_cast<int>(sizeof(uint64_t)))
    {
        return DECODE_ERROR;
    }

    memcpy(out, in->start, sizeof(uint64_t));
    return DECODE_OK;
}

int getSfixed64(const Buffer *in, int64_t *out)
{
    if (in->size() < static_cast<int>(sizeof(int64_t)))
    {
        return DECODE_ERROR;
    }

    memcpy(out, in->start, sizeof(int64_t));
    return DECODE_OK;
}

int getDouble(const Buffer *in, double *out)
{
    if (in->size() < static_cast<int>(sizeof(double)))
    {
        return DECODE_ERROR;
    }

    memcpy(out, in->start, sizeof(double));
    return DECODE_OK;
}

int getFixed32(const Buffer *in, uint32_t *out)
{
    if (in->size() < static_cast<int>(sizeof(uint32_t)))
    {
        return DECODE_ERROR;
    }

    memcpy(out, in->start, sizeof(uint32_t));
    return DECODE_OK;
}

int getSfixed32(const Buffer *in, int32_t *out)
{
    if (in->size() < static_cast<int>(sizeof(int32_t)))
    {
        return DECODE_ERROR;
    }

    memcpy(out, in->start, sizeof(int32_t));
    return DECODE_OK;
}

int getFloat(const Buffer *in, float *out)
{
    if (in->size() < static_cast<int>(sizeof(float)))
    {
        return DECODE_ERROR;
    }

    memcpy(out, in->start, sizeof(float));
    return DECODE_OK;
}
