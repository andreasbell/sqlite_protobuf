#include "protodec.h"

#include <cstring>
#include <stdlib.h>

#define TAG_BITS 3
#define MAX_VARINT_64BYTES 10
#define MAX_VARINT_32BYTES 5

struct Fields
{
    Field* data;
    size_t size;
    size_t capacity;
};

static inline void append(Fields *fields, const Field *field)
{
    if (fields->size >= fields->capacity)
    {
        fields->capacity = fields->capacity == 0 ? 16 : fields->capacity * 2;
        fields->data = (Field* )realloc(fields->data, sizeof(Field) * fields->capacity);
    }
    memset(&fields->data[fields->size], 0, sizeof(Field));
    fields->data[fields->size] = *field;
    fields->size++;
}

static int decodeField(Field *field, Buffer *in);

size_t getSize(const Buffer* buffer) 
{
    return buffer->end - buffer->start; 
}

static inline uint32_t getTag(uint32_t fieldNumber, WireType wireType)
{
    return (fieldNumber << TAG_BITS) | wireType;
}

static inline int getWireType(uint32_t tag)
{
    return tag & ((1 << TAG_BITS) - 1);
}

static inline int getFieldNumber(uint32_t tag)
{
    return tag >> TAG_BITS;
}

int getWireType(const Field& field)
{
    return getWireType(field.tag);
}

int getFieldNumber(const Field& field)
{
    return getFieldNumber(field.tag);
}

static inline Field* getSubFields(Field* field)
{
    return field->subFieldsSize > 0 ? field + field->subFieldsOffset : nullptr;
}

static inline int compareFields(const void *a, const void *b)
{
    const Field* fieldA = *(const Field**)a;
    const Field* fieldB = *(const Field**)b;
    return fieldA->tag == fieldB->tag ? 0 : fieldA->tag > fieldB->tag ? 1 : -1;
}

static inline Field** getSubFieldsSorted(Field* field)
{
    Field** subFieldsSorted = (Field**)malloc(sizeof(Field*) * field->subFieldsSize);
    Field* subFields = getSubFields(field);
    for (size_t i = 0; i < field->subFieldsSize; i++){subFieldsSorted[i] = &subFields[i];}
    qsort(subFieldsSorted, field->subFieldsSize, sizeof(Field*), compareFields);
    return subFieldsSorted;
}

Field* getSubField(Field* field, uint32_t fieldNumber, WireType wireType, int64_t index)
{    
    uint32_t tag = getTag(fieldNumber, wireType);
    Field* subFields = getSubFields(field);
    if (index >= 0) // Positive index, iterate forward through list
    {
        for (size_t i = 0; i < field->subFieldsSize; i++)
        {
            if (subFields[i].tag == tag && index-- == 0)
            {
                return &subFields[i];
            }
        }
    }
    else // Negative index iterate backward through list
    {
        for (size_t i = 1; i <= field->subFieldsSize; i++)
        {
            if (subFields[field->subFieldsSize - i].tag == tag && ++index == 0)
            {
                return &subFields[field->subFieldsSize - i];
            }
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
    if (in->start + sizeof(int64_t) > in->end)
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

    if (in->start + sizeof(int32_t) > in->end)
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

    if (!ptr || ptr + length > in->end)
    {
        return DECODE_ERROR;
    }

    field->value.start = ptr;
    field->value.end = ptr + length;
    in->start = field->value.end;

    return DECODE_OK;
}

static inline int decodePacked(Fields *fields, size_t parentIndex, Field *field)
{
    Field subField = {0};
    Buffer b;
    uint32_t fieldNumber = getFieldNumber(field->tag);

    // Decode as packed VARINT
    subField.tag = getTag(fieldNumber, WIRETYPE_VARINT);
    b = field->value;
    int fieldCount = 0;
    while (b.start < b.end)
    {
        if (DECODE_OK == decodeVarint(&subField, &b))
        {
            append(fields, &subField);
            fields->data[parentIndex].subFieldsSize++;
            fieldCount++;
        }
        else
        {
            // Error when decoding subfields, clean up
            fields->data[parentIndex].subFieldsSize -= fieldCount;
            fields->size -= fieldCount;
            break;
        }
    }

    // Decode as packed I64
    subField.tag = getTag(fieldNumber, WIRETYPE_I64);
    b = field->value;
    if ((b.end - b.start) % sizeof(int64_t) == 0)
    {
        while (b.start < b.end)
        {
            decodeFixed64(&subField, &b);
            append(fields, &subField);
            fields->data[parentIndex].subFieldsSize++;
        }
    }

    // Decode as packed I32
    subField.tag = getTag(fieldNumber, WIRETYPE_I32);
    b = field->value;
    if ((b.end - b.start) % sizeof(int32_t) == 0)
    {
        while (b.start < b.end)
        {
            decodeFixed32(&subField, &b);
            append(fields, &subField);
            fields->data[parentIndex].subFieldsSize++;
        }
    }

    return DECODE_OK;
}

static inline int decodeSubField(Fields *fields, size_t parentIndex, bool packed)
{
    Field subField = {0};
    Buffer b = fields->data[parentIndex].value;

    while (b.start < b.end)
    {
        if (DECODE_OK == decodeField(&subField, &b))
        {
            append(fields, &subField);
            fields->data[parentIndex].subFieldsSize++;

            if (packed && WIRETYPE_LEN == getWireType(subField.tag))
            {
                decodePacked(fields, parentIndex, &subField);
            }
        }
        else
        {
            // Error when decoding subfields, clean up
            fields->size -= fields->data[parentIndex].subFieldsSize;
            fields->data[parentIndex].subFieldsSize = 0;
            return DECODE_ERROR;
        }
    }

    return DECODE_OK;
}

static inline int decodeGroup(Field *field, Buffer *in)
{
    // Initialize field
    field->value.start = in->start;
    field->value.end = in->start;

    if (getWireType(field->tag) == WIRETYPE_EGROUP) return DECODE_ERROR;

    // Iterate through sub fields until we reach end of group
    Field subField = {0};
    while (DECODE_OK == decodeField(&subField, in))
    {
        field->value.end = in->start;
    }

    if (getTag(getFieldNumber(field->tag), WIRETYPE_EGROUP) == subField.tag)
    {
        in->start = subField.value.end;
        return DECODE_OK;
    }

    return DECODE_ERROR;
}

static inline int decodeField(Field *field, Buffer *in)
{
    int64_t tag;

    // Read tag from buffer
    const uint8_t *ptr = readVarint(in, &tag, MAX_VARINT_32BYTES);

    // Check validity of tag
    if (getFieldNumber(tag) == 0 || !ptr) return DECODE_ERROR;

    // Initialize field
    field->tag = (uint32_t)tag;
    in->start = ptr;
    
    switch (getWireType(field->tag))
    {
    case WIRETYPE_VARINT:
        return decodeVarint(field, in);

    case WIRETYPE_I64:
        return decodeFixed64(field, in);

    case WIRETYPE_LEN:
        return decodeString(field, in); 

    case WIRETYPE_I32:
        return decodeFixed32(field, in);

    case WIRETYPE_SGROUP:
    case WIRETYPE_EGROUP:
        return decodeGroup(field, in);
    }

    return DECODE_ERROR;
}

Field* decodeProtobuf(Buffer in, bool packed)
{
    // Create list for storing decoded fields
    Fields fields = {0};

    // Set root field
    Field root = {0};
    root.tag = getTag(0, WIRETYPE_LEN);
    root.value = in;
    append(&fields, &root);

    // Breadth first decode of fields 
    for (size_t i = 0; i < fields.size; i++)
    {
        Field *parent = &fields.data[i];
        parent->subFieldsSize = 0;
        parent->subFieldsOffset = fields.size - i;

        if (getWireType(parent->tag) == WIRETYPE_LEN || getWireType(parent->tag) == WIRETYPE_SGROUP)
        {
            decodeSubField(&fields, i, packed);
        }
    }

    return fields.data;
}

static inline void base64Encode(const Buffer &in, std::ostream &os)
{
    int val = 0, valb = -6, size = 0;
    for (size_t i = 0; i < getSize(&in); i++)
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
    for (size_t i = 0; i < getSize(&b); i++)
    {
        if (!isprint(b.start[i]))
        {
            return false;
        }
    }
    return true;
}

void toJson(Field *field, std::ostream &os, bool showType)
{
    if (field->subFieldsSize != 0)
    {
        os << "{";
        Field** subFields = getSubFieldsSorted(field);
        size_t i = 0;

        while (i < field->subFieldsSize)
        {
            uint32_t tag = subFields[i]->tag;
            os << "\"" << getFieldNumber(tag);
            if (showType)
            {
                os << "_" << getWireType(tag); 
            }
            os << "\":";
            if (i + 1 < field->subFieldsSize && tag == subFields[i+1]->tag)
            {
                os << "[";
                while (true)
                {
                    toJson(subFields[i++], os, showType);
                    if (i < field->subFieldsSize && tag == subFields[i]->tag)
                    {
                        os << ",";
                    }
                    else
                    {
                        break;
                    }
                }
                os << "]";
            }
            else
            {
                toJson(subFields[i++], os, showType);
            }
            if (i < field->subFieldsSize)
            {
                os << ",";
            }
        }
        os << "}";

        free(subFields);
    }
    else if (getWireType(field->tag) == WIRETYPE_VARINT)
    {
        int64_t number;
        getInt64(&field->value, &number, 0); // Guess type is signed 64 bit int
        os << number;
    }
    else if (getWireType(field->tag) == WIRETYPE_I64)
    {
        double number;
        getDouble(&field->value, &number, 0); // Guess type is double
        os << number;
    }
    else if (getWireType(field->tag) == WIRETYPE_I32)
    {
        float number;
        getFloat(&field->value, &number, 0); // Guess type is float
        os << number;
    }
    else
    {
        os << "\"";
        if (isPrintable(field->value))
        {
            // Write buffer directly to json
            os.write((const char *)field->value.start, getSize(&field->value));
        }
        else
        {
            // Write base64 encoded buffer to json
            base64Encode(field->value, os);
        }
        os << "\"";
    }
}

static inline int getVarint(const Buffer *in, int64_t *out, int64_t index, size_t maxBytes)
{
    int64_t number;
    int64_t length = 0;
    Buffer b;

    // Find number of varints in buffer
    b = *in;
    while(b.start < b.end)
    {
        b.start = readVarint(&b, &number, maxBytes);
        if (b.start == nullptr)
        {
            return DECODE_ERROR;
        }
        length++;
    }

    index = index < 0 ? index + length : index; // Wrap arround

    if (index < 0 || index >= length)
    {
        return DECODE_ERROR;
    }

    // Find varint with given index
    b = *in;
    while(true)
    {
        b.start = readVarint(&b, &number, maxBytes);
        if(index <= 0){
            break;
        }
        index--;
    }

    *out = number;
    return DECODE_OK;
}

int getInt32(const Buffer *in, int32_t *out, int64_t index)
{
    int64_t number;
    if(DECODE_OK != getVarint(in, &number, index, MAX_VARINT_32BYTES))
    {
        return DECODE_ERROR;
    }

    *out = (int32_t)number;
    return DECODE_OK;
}

int getInt64(const Buffer *in, int64_t *out, int64_t index)
{   
    int64_t number;
    if(DECODE_OK != getVarint(in, &number, index, MAX_VARINT_64BYTES))
    {
        return DECODE_ERROR;
    }
    
    *out = (int64_t)number;
    return DECODE_OK;
}

int getUint32(const Buffer *in, uint32_t *out, int64_t index)
{
    int64_t number;
    if(DECODE_OK != getVarint(in, &number, index, MAX_VARINT_32BYTES))
    {
        return DECODE_ERROR;
    }

    *out = (uint32_t)number;
    return DECODE_OK;
}

int getUint64(const Buffer *in, uint64_t *out, int64_t index)
{
    int64_t number;
    if(DECODE_OK != getVarint(in, &number, index, MAX_VARINT_64BYTES))
    {
        return DECODE_ERROR;
    }

    *out = (uint64_t)number;
    return DECODE_OK;
}

int getSint32(const Buffer *in, int32_t *out, int64_t index)
{
    int64_t number;
    if(DECODE_OK != getVarint(in, &number, index, MAX_VARINT_32BYTES))
    {
        return DECODE_ERROR;
    }

    *out = (int32_t)number;
    *out = (*out >> 1) ^ -(*out & 1); // zigzag decoding
    return DECODE_OK;
}

int getSint64(const Buffer *in, int64_t *out, int64_t index)
{
    int64_t number;
    if(DECODE_OK != getVarint(in, &number, index, MAX_VARINT_64BYTES))
    {
        return DECODE_ERROR;
    }

    *out = (int64_t)number;
    *out = (*out >> 1) ^ -(*out & 1); // zigzag decoding
    return DECODE_OK;
}

int getBool(const Buffer *in, bool *out, int64_t index)
{
    int64_t number;
    if(DECODE_OK != getVarint(in, &number, index, MAX_VARINT_32BYTES))
    {
        return DECODE_ERROR;
    }

    *out = (bool)number;
    return DECODE_OK;
}


static inline int getI64(const Buffer *in, void *out, int64_t index)
{
    int64_t length = in->end - in->start;
    
    index = index < 0 ? index*sizeof(uint64_t) + length : index*sizeof(uint64_t); // Wrap arround
    
    if (index < 0 || index >= length || length % sizeof(uint64_t) != 0)
    {
        return DECODE_ERROR;
    }

    uint64_t *result =(uint64_t*) out;
    *result = (*result << 8) | in->start[index + 7];
    *result = (*result << 8) | in->start[index + 6];
    *result = (*result << 8) | in->start[index + 5];
    *result = (*result << 8) | in->start[index + 4];
    *result = (*result << 8) | in->start[index + 3];
    *result = (*result << 8) | in->start[index + 2];
    *result = (*result << 8) | in->start[index + 1];
    *result = (*result << 8) | in->start[index + 0];
    return DECODE_OK;
}

int getFixed64(const Buffer *in, uint64_t *out, int64_t index)
{
    return getI64(in, out, index);
}

int getSfixed64(const Buffer *in, int64_t *out, int64_t index)
{
    return getI64(in, out, index);
}

int getDouble(const Buffer *in, double *out, int64_t index)
{
    return getI64(in, out, index);
}

static inline int getI32(const Buffer *in, void *out, int64_t index)
{
    int64_t length = in->end - in->start;
    
    index = index < 0 ? index*sizeof(uint32_t) + length : index*sizeof(uint32_t); // Wrap arround
    
    if (index < 0 || index >= length || length % sizeof(uint32_t) != 0)
    {
        return DECODE_ERROR;
    }

    uint32_t *result =(uint32_t*) out;
    *result = (*result << 8) | in->start[index + 3];
    *result = (*result << 8) | in->start[index + 2];
    *result = (*result << 8) | in->start[index + 1];
    *result = (*result << 8) | in->start[index + 0];
    return DECODE_OK;
}

int getFixed32(const Buffer *in, uint32_t *out, int64_t index)
{
    return getI32(in, out, index);
}

int getSfixed32(const Buffer *in, int32_t *out, int64_t index)
{
    return getI32(in, out, index);
}

int getFloat(const Buffer *in, float *out, int64_t index)
{
    return getI32(in, out, index);
}
