#include <iostream>
#include <string>
#include <cstring>
#include "protodec.h"

#define ASSERT(condition) { if (!(condition)) {std::cout << " Function: " << __FUNCTION__ << " failed on line: " << __LINE__ << std::endl; return 1;}}

namespace utils
{
    void appendVarint(uint64_t n, std::string& buf) 
    {
        char val;
        do {
            val = (n & 0xFF);
            if (n > 0b01111111) {val = val | 0b10000000;}
            n = n >> 7;
            buf.append(1, val);
        } while (n);
    }

    template<typename T>
    void appendI64(T n, std::string& buf)
    {
        uint64_t* tmp = (uint64_t*)&n; 
        for (int i = 0; i < sizeof(uint64_t); i++)
        {
            char c = (char)(*tmp >> 8*i);
            buf.append(&c, sizeof(char));
        }
    }

    template<typename T>
    void appendI32(T n, std::string& buf)
    {
        uint32_t* tmp = (uint32_t*)&n; 
        for (int i = 0; i < sizeof(uint32_t); i++)
        {
            char c = (char)(*tmp >> 8*i);
            buf.append(&c, sizeof(char));
        }
    }

    void appendStr(const std::string& str, std::string& buf)
    {
        buf.append(str);
    }

    std::string encodeInt(uint32_t fieldNumber, int64_t num)
    {
        std::string buf;
        appendVarint((fieldNumber << 3) | 0, buf); // Add tag with wiretype 0 (VARINT)
        appendVarint(num, buf); // Add number
        return buf;
    }

    std::string encodeStr(uint32_t fieldNumber, const std::string& str)
    {
        std::string buf;
        appendVarint((fieldNumber << 3) | 2, buf); // Add tag with wiretype 2 (LEN)
        appendVarint(str.size(), buf); // Add lengt of string
        appendStr(str, buf); // Add string
        return buf;
    }

    std::string encodeDouble(uint32_t fieldNumber, double num)
    {
        std::string buf;
        appendVarint((fieldNumber << 3) | 1, buf); // Add tag with wiretype 1 (I64)
        appendI64(num, buf); // Add number
        return buf;
    }

    std::string encodeFloat(uint32_t fieldNumber, float num)
    {
        std::string buf;
        appendVarint((fieldNumber << 3) | 5, buf); // Add tag with wiretype 5 (I32)
        appendI32(num, buf); // Add number
        return buf;
    }

    std::string encodeGroup(uint32_t fieldNumber, const std::string& str)
    {
        std::string buf;
        appendVarint((fieldNumber << 3) | 3, buf); // Add tag with wiretype 3 (SGROUP)
        buf.append(str); // Add string
        appendVarint((fieldNumber << 3) | 4, buf); // Add tag with wiretype 4 (EGROUP)
        return buf;
    }
}

int test_varint1(void)
{
    std::string data;
    Buffer buffer;
    int64_t in = 0;
    int64_t out = 0;

    for (int i = 0; i < 64; i++)
    {
        // Test all possible bit lengths
        in = in << 1 | 1;

        data.append(utils::encodeInt(i+1, in));

        buffer.start = (const uint8_t*)data.c_str();
        buffer.end = buffer.start + data.length();
        Field field = decodeProtobuf(buffer);
        //toJson(&field, std::cout); std::cout << std::endl;

        Field* f = field.getSubField(i+1, WIRETYPE_VARINT, 0);

        ASSERT(f != nullptr);
        ASSERT(getInt64(&f->value, &out, 0) != 0);
        ASSERT(out == in);
    }

    return 0;
}

int test_varint2(void)
{
    std::string data;
    Buffer buffer;
    int64_t in = 0;
    int64_t out = 0;

    for (int i = 0; i <= 512; i++)
    {
        // Test sequence that spans positive, negative, zero, and varint size change
        in = i - 256;

        data.append(utils::encodeInt(i+1, in));

        buffer.start = (const uint8_t*)data.c_str();
        buffer.end = buffer.start + data.length();
        Field field = decodeProtobuf(buffer);
        //toJson(&field, std::cout); std::cout << std::endl;

        Field* f = field.getSubField(i+1, WIRETYPE_VARINT, 0);

        ASSERT(f != nullptr);
        ASSERT(getInt64(&f->value, &out, 0) != 0);
        ASSERT(out == in);
    }

    return 0;
}

int test_i64(void)
{
    std::string data;
    Buffer buffer;
    double out = 0;

    double values[] = {0.0, -123.456, 3.14159265, 1e100, -1e100};

    for (int i = 0; i < sizeof(values)/sizeof(double); i++)
    {
        data.append(utils::encodeDouble(i+1, values[i]));

        buffer.start = (const uint8_t*)data.c_str();
        buffer.end = buffer.start + data.length();
        Field field = decodeProtobuf(buffer);
        //toJson(&field, std::cout); std::cout << std::endl;

        Field* f = field.getSubField(i+1, WIRETYPE_I64, 0);

        ASSERT(f != nullptr);
        ASSERT(getDouble(&f->value, &out, 0) != 0);
        ASSERT(out == values[i]);

    }

    return 0;
}

int test_len(void)
{
    std::string data;
    std::string str;
    Buffer buffer;

    for (int i = 0; i < 255; i++)
    {
        //data.clear();
        str.push_back((char)i);

        data.append(utils::encodeStr(i+1, str));

        buffer.start = (const uint8_t*)data.c_str();
        buffer.end = buffer.start + data.length();
        Field field = decodeProtobuf(buffer);
        //toJson(&field, std::cout); std::cout << std::endl;

        Field* f = field.getSubField(i+1, WIRETYPE_LEN, 0);

        ASSERT(f != nullptr);
        ASSERT(memcmp(f->value.start, str.c_str(), str.length()) == 0);
    }

    return 0;
}

int test_i32(void)
{
    std::string data;
    Buffer buffer;
    float out = 0;

    float values[] = {0.0, -123.456, 3.14159265, 1e10, -1e10};

    for (int i = 0; i < sizeof(values)/sizeof(float); i++)
    {
        data.append(utils::encodeFloat(i+1, values[i]));

        buffer.start = (const uint8_t*)data.c_str();
        buffer.end = buffer.start + data.length();
        Field field = decodeProtobuf(buffer);
        //toJson(&field, std::cout); std::cout << std::endl;

        Field* f = field.getSubField(i+1, WIRETYPE_I32, 0);

        ASSERT(f != nullptr);
        ASSERT(getFloat(&f->value, &out, 0) != 0);
        ASSERT(out == values[i]);

    }

    return 0;
}

int test_group(void)
{
    Buffer buffer;

    std::string subData = utils::encodeInt(1, 42);
    std::string data = utils::encodeGroup(1, subData);

    buffer.start = (const uint8_t*)data.c_str();
    buffer.end = buffer.start + data.length();
    Field field = decodeProtobuf(buffer);
    //toJson(&field, std::cout); std::cout << std::endl;

    Field* f = field.getSubField(1, WIRETYPE_SGROUP, 0);

    ASSERT(f != nullptr);
    ASSERT(memcmp(f->value.start, subData.c_str(), subData.length()) == 0);

    return 0;
}

int test_subfield(void)
{
    Buffer buffer;

    std::string subData = utils::encodeInt(1, 42);
    std::string data = utils::encodeStr(1, subData);

    buffer.start = (const uint8_t*)data.c_str();
    buffer.end = buffer.start + data.length();
    Field field = decodeProtobuf(buffer);
    //toJson(&field, std::cout); std::cout << std::endl;

    Field* f = field.getSubField(1, WIRETYPE_LEN, 0);

    ASSERT(f != nullptr);
    ASSERT(memcmp(f->value.start, subData.c_str(), subData.length()) == 0);

    return 0;
}

int test_repeated_varint(void)
{
    std::string data;
    Buffer buffer;
    int64_t out;
    Field *f;

    // Create protobuf with repeted varints
    for (int i = 0; i < 64; i++)
    {
        data.append(utils::encodeInt(1, 1LL << i));
    }

    buffer.start = (const uint8_t*)data.c_str();
    buffer.end = buffer.start + data.length();
    Field field = decodeProtobuf(buffer);
    //toJson(&field, std::cout); std::cout << std::endl;

    for (int i = 0; i < 64; i++)
    {
        // Positve index
        f = field.getSubField(1, WIRETYPE_VARINT, i);

        ASSERT(f != nullptr);
        ASSERT(getInt64(&f->value, &out, 0) != 0);
        ASSERT(out == 1LL << i);

        // Negative index
        f = field.getSubField(1, WIRETYPE_VARINT,  -(i + 1));

        ASSERT(f != nullptr);
        ASSERT(getInt64(&f->value, &out, 0) != 0);
        ASSERT(out == 1LL << (63 - i));
    }

    // Positive index out of bounds
    f = field.getSubField(1, WIRETYPE_VARINT, 64);
    ASSERT(f == nullptr);

    // Negative index out of bounds
    f = field.getSubField(1, WIRETYPE_VARINT, -65);
    ASSERT(f == nullptr);

    return 0;
}

int test_packed_varint(void)
{
    std::string data;
    Buffer buffer;
    int64_t out;
    Field *f;

    // Create protobuf with packed varints
    for (int i = 0; i < 64; i++)
    {
        utils::appendVarint(1LL << i, data);
    }
    data = utils::encodeStr(1, data);

    buffer.start = (const uint8_t*)data.c_str();
    buffer.end = buffer.start + data.length();
    Field field = decodeProtobuf(buffer);
    //toJson(&field, std::cout); std::cout << std::endl;

    f = field.getSubField(1, WIRETYPE_LEN, 0);
    ASSERT(f != nullptr);

    for (int i = 0; i < 64; i++)
    {
        // Positve index
        ASSERT(getInt64(&f->value, &out, i) != 0);
        ASSERT(out == 1LL << i);

        // Negative index
        ASSERT(getInt64(&f->value, &out, -(i + 1)) != 0);
        ASSERT(out == 1LL << (63 - i));
    }

    // Positive index out of bounds
    ASSERT(getInt64(&f->value, &out, 64) == 0);

    // Negative index out of bounds
    ASSERT(getInt64(&f->value, &out, -65) == 0);

    return 0;
}

int test_repeated_i32(void)
{
    std::string data;
    Buffer buffer;
    float out;
    Field *f;

    // Create protobuf with repeted i32
    for (int i = 0; i < 100; i++)
    {
        data.append(utils::encodeFloat(1, i));
    }

    buffer.start = (const uint8_t*)data.c_str();
    buffer.end = buffer.start + data.length();
    Field field = decodeProtobuf(buffer);
    //toJson(&field, std::cout); std::cout << std::endl;

    for (int i = 0; i < 100; i++)
    {
        // Positve index
        f = field.getSubField(1, WIRETYPE_I32, i);

        ASSERT(f != nullptr);
        ASSERT(getFloat(&f->value, &out, 0) != 0);
        ASSERT(out == i);

        // Negative index
        f = field.getSubField(1, WIRETYPE_I32,  -(i + 1));

        ASSERT(f != nullptr);
        ASSERT(getFloat(&f->value, &out, 0) != 0);
        ASSERT(out == 99 - i);
    }

    // Positive index out of bounds
    f = field.getSubField(1, WIRETYPE_I32, 100);
    ASSERT(f == nullptr);

    // Negative index out of bounds
    f = field.getSubField(1, WIRETYPE_I32, -101);
    ASSERT(f == nullptr);

    return 0;
}

int test_packed_i32(void)
{
    std::string data;
    Buffer buffer;
    uint32_t out;
    Field *f;

    // Create protobuf with packed i32
    for (uint32_t i = 0; i < 100; i++)
    {
        utils::appendI32(i, data);
    }
    data = utils::encodeStr(1, data);

    buffer.start = (const uint8_t*)data.c_str();
    buffer.end = buffer.start + data.length();
    Field field = decodeProtobuf(buffer);
    //toJson(&field, std::cout); std::cout << std::endl;

    f = field.getSubField(1, WIRETYPE_LEN, 0);
    ASSERT(f != nullptr);

    for (int i = 0; i < 100; i++)
    {
        // Positve index
        ASSERT(getFixed32(&f->value, &out, i) != 0);
        ASSERT(out == i);

        // Negative index
        ASSERT(getFixed32(&f->value, &out, -(i + 1)) != 0);
        ASSERT(out == 99 - i);
    }

    // Positive index out of bounds
    ASSERT(getFixed32(&f->value, &out, 100) == 0);

    // Negative index out of bounds
    ASSERT(getFixed32(&f->value, &out, -101) == 0);

    return 0;
}

int test_repeated_i64(void)
{
    std::string data;
    Buffer buffer;
    double out;
    Field *f;

    // Create protobuf with repeted i64
    for (int i = 0; i < 100; i++)
    {
        data.append(utils::encodeDouble(1, i));
    }

    buffer.start = (const uint8_t*)data.c_str();
    buffer.end = buffer.start + data.length();
    Field field = decodeProtobuf(buffer);
    //toJson(&field, std::cout); std::cout << std::endl;

    for (int i = 0; i < 100; i++)
    {
        // Positve index
        f = field.getSubField(1, WIRETYPE_I64, i);

        ASSERT(f != nullptr);
        ASSERT(getDouble(&f->value, &out, 0) != 0);
        ASSERT(out == i);

        // Negative index
        f = field.getSubField(1, WIRETYPE_I64,  -(i + 1));

        ASSERT(f != nullptr);
        ASSERT(getDouble(&f->value, &out, 0) != 0);
        ASSERT(out == 99 - i);
    }

    // Positive index out of bounds
    f = field.getSubField(1, WIRETYPE_I64, 100);
    ASSERT(f == nullptr);

    // Negative index out of bounds
    f = field.getSubField(1, WIRETYPE_I64, -101);
    ASSERT(f == nullptr);

    return 0;
}

int test_packed_i64(void)
{
    std::string data;
    Buffer buffer;
    uint64_t out;
    Field *f;

    // Create protobuf with packed i64
    for (uint64_t i = 0; i < 100; i++)
    {
        utils::appendI64(i, data);
    }
    data = utils::encodeStr(1, data);

    buffer.start = (const uint8_t*)data.c_str();
    buffer.end = buffer.start + data.length();
    Field field = decodeProtobuf(buffer);
    //toJson(&field, std::cout); std::cout << std::endl;

    f = field.getSubField(1, WIRETYPE_LEN, 0);
    ASSERT(f != nullptr);

    for (int i = 0; i < 100; i++)
    {
        // Positve index
        ASSERT(getFixed64(&f->value, &out, i) != 0);
        ASSERT(out == i);

        // Negative index
        ASSERT(getFixed64(&f->value, &out, -(i + 1)) != 0);
        ASSERT(out == 99 - i);
    }

    // Positive index out of bounds
    ASSERT(getFixed64(&f->value, &out, 100) == 0);

    // Negative index out of bounds
    ASSERT(getFixed64(&f->value, &out, -101) == 0);

    return 0;
}

int test_repeated_len(void)
{
    std::string data;
    Buffer buffer;
    double out;
    Field *f;

    std::string str = "Hello World!";

    // Create protobuf with repeted string
    for (int i = 0; i < 100; i++)
    {
        data.append(utils::encodeStr(1, str));
    }

    buffer.start = (const uint8_t*)data.c_str();
    buffer.end = buffer.start + data.length();
    Field field = decodeProtobuf(buffer);
    //toJson(&field, std::cout); std::cout << std::endl;

    for (int i = 0; i < 100; i++)
    {
        // Positve index
        f = field.getSubField(1, WIRETYPE_LEN, i);
        ASSERT(f != nullptr);
        ASSERT(memcmp(f->value.start, str.c_str(), str.length()) == 0);

        // Negative index
        f = field.getSubField(1, WIRETYPE_LEN,  -(i + 1));
        ASSERT(f != nullptr);
        ASSERT(memcmp(f->value.start, str.c_str(), str.length()) == 0);
    }

    // Positive index out of bounds
    f = field.getSubField(1, WIRETYPE_LEN, 100);
    ASSERT(f == nullptr);

    // Negative index out of bounds
    f = field.getSubField(1, WIRETYPE_LEN, -101);
    ASSERT(f == nullptr);

    return 0;
}

int test_type_int32(void)
{
    uint8_t data[] = {0x08, 0xd6, 0xff, 0xff, 0xff, 0x0f};
    int32_t expected = -42;
    int32_t result = 0;

    Buffer buffer;
    buffer.start = data;
    buffer.end = buffer.start + sizeof(data);
    Field field = decodeProtobuf(buffer);
    Field *f = field.getSubField(1, WIRETYPE_VARINT, 0);
    
    ASSERT(f != nullptr);
    ASSERT(getInt32(&f->value, &result, 0) != 0);
    ASSERT(result == expected);

    return 0;
}

int test_type_int64(void)
{
    uint8_t data[] = {0x10, 0xd6, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x01};
    int64_t expected = -42;
    int64_t result = 0;

    Buffer buffer;
    buffer.start = data;
    buffer.end = buffer.start + sizeof(data);
    Field field = decodeProtobuf(buffer);
    Field *f = field.getSubField(2, WIRETYPE_VARINT, 0);
    
    ASSERT(f != nullptr);
    ASSERT(getInt64(&f->value, &result, 0) != 0);
    ASSERT(result == expected);

    return 0;
}

int test_type_uint32(void)
{
    uint8_t data[] = {0x18, 0xff, 0xff, 0xff, 0xff, 0x0f};
    uint32_t expected = 4294967295;
    uint32_t result = 0;

    Buffer buffer;
    buffer.start = data;
    buffer.end = buffer.start + sizeof(data);
    Field field = decodeProtobuf(buffer);
    Field *f = field.getSubField(3, WIRETYPE_VARINT, 0);
    
    ASSERT(f != nullptr);
    ASSERT(getUint32(&f->value, &result, 0) != 0);
    ASSERT(result == expected);

    return 0;
}

int test_type_uint64(void)
{
    uint8_t data[] = {0x20, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x01};
    uint64_t expected = 18446744073709551615;
    uint64_t result = 0;

    Buffer buffer;
    buffer.start = data;
    buffer.end = buffer.start + sizeof(data);
    Field field = decodeProtobuf(buffer);
    Field *f = field.getSubField(4, WIRETYPE_VARINT, 0);
    
    ASSERT(f != nullptr);
    ASSERT(getUint64(&f->value, &result, 0) != 0);
    ASSERT(result == expected);

    return 0;
}

int test_type_sint32(void)
{
    uint8_t data[] = {0x28, 0x53};
    int32_t expected = -42;
    int32_t result = 0;

    Buffer buffer;
    buffer.start = data;
    buffer.end = buffer.start + sizeof(data);
    Field field = decodeProtobuf(buffer);
    Field *f = field.getSubField(5, WIRETYPE_VARINT, 0);
    
    ASSERT(f != nullptr);
    ASSERT(getSint32(&f->value, &result, 0) != 0);
    ASSERT(result == expected);

    return 0;
}

int test_type_sint64(void)
{
    uint8_t data[] = {0x30, 0x53};
    int64_t expected = -42;
    int64_t result = 0;

    Buffer buffer;
    buffer.start = data;
    buffer.end = buffer.start + sizeof(data);
    Field field = decodeProtobuf(buffer);
    Field *f = field.getSubField(6, WIRETYPE_VARINT, 0);
    
    ASSERT(f != nullptr);
    ASSERT(getSint64(&f->value, &result, 0) != 0);
    ASSERT(result == expected);

    return 0;
}

int test_type_bool(void)
{
    uint8_t data[] = {0x38, 0x01};
    bool expected = true;
    bool result = false;

    Buffer buffer;
    buffer.start = data;
    buffer.end = buffer.start + sizeof(data);
    Field field = decodeProtobuf(buffer);
    Field *f = field.getSubField(7, WIRETYPE_VARINT, 0);
    
    ASSERT(f != nullptr);
    ASSERT(getBool(&f->value, &result, 0) != 0);
    ASSERT(result == expected);

    return 0;
}

int test_type_fixed64(void)
{
    uint8_t data[] = {0x41, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
    uint64_t expected = 18446744073709551615;
    uint64_t result = 0;

    Buffer buffer;
    buffer.start = data;
    buffer.end = buffer.start + sizeof(data);
    Field field = decodeProtobuf(buffer);
    Field *f = field.getSubField(8, WIRETYPE_I64, 0);
    
    ASSERT(f != nullptr);
    ASSERT(getFixed64(&f->value, &result, 0) != 0);
    ASSERT(result == expected);

    return 0;
}

int test_type_sfixed64(void)
{
    uint8_t data[] = {0x49, 0xd6, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
    int64_t expected = -42;
    int64_t result = 0;

    Buffer buffer;
    buffer.start = data;
    buffer.end = buffer.start + sizeof(data);
    Field field = decodeProtobuf(buffer);
    Field *f = field.getSubField(9, WIRETYPE_I64, 0);
    
    ASSERT(f != nullptr);
    ASSERT(getSfixed64(&f->value, &result, 0) != 0);
    ASSERT(result == expected);

    return 0;
}

int test_type_double(void)
{
    uint8_t data[] = {0x51, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x45, 0xc0};
    double expected = -42;
    double result = 0;

    Buffer buffer;
    buffer.start = data;
    buffer.end = buffer.start + sizeof(data);
    Field field = decodeProtobuf(buffer);
    Field *f = field.getSubField(10, WIRETYPE_I64, 0);
    
    ASSERT(f != nullptr);
    ASSERT(getDouble(&f->value, &result, 0) != 0);
    ASSERT(result == expected);

    return 0;
}

int test_type_fixed32(void)
{
    uint8_t data[] = {0x5d, 0xff, 0xff, 0xff, 0xff};
    uint32_t expected = 4294967295;
    uint32_t result = 0;

    Buffer buffer;
    buffer.start = data;
    buffer.end = buffer.start + sizeof(data);
    Field field = decodeProtobuf(buffer);
    Field *f = field.getSubField(11, WIRETYPE_I32, 0);
    
    ASSERT(f != nullptr);
    ASSERT(getFixed32(&f->value, &result, 0) != 0);
    ASSERT(result == expected);

    return 0;
}

int test_type_sfixed32(void)
{
    uint8_t data[] = {0x65, 0xd6, 0xff, 0xff, 0xff};
    int32_t expected = -42;
    int32_t result = 0;

    Buffer buffer;
    buffer.start = data;
    buffer.end = buffer.start + sizeof(data);
    Field field = decodeProtobuf(buffer);
    Field *f = field.getSubField(12, WIRETYPE_I32, 0);
    
    ASSERT(f != nullptr);
    ASSERT(getSfixed32(&f->value, &result, 0) != 0);
    ASSERT(result == expected);

    return 0;
}

int test_type_float(void)
{
    uint8_t data[] = {0x6d, 0x00, 0x00, 0x28, 0xc2};
    float expected = -42;
    float result = 0;

    Buffer buffer;
    buffer.start = data;
    buffer.end = buffer.start + sizeof(data);
    Field field = decodeProtobuf(buffer);
    Field *f = field.getSubField(13, WIRETYPE_I32, 0);
    
    ASSERT(f != nullptr);
    ASSERT(getFloat(&f->value, &result, 0) != 0);
    ASSERT(result == expected);

    return 0;
}


int main(int argc, char *argv[])
{  
    // Create list of tests
    int (*tests[])(void) = {
        test_varint1,
        test_varint2,
        test_i64,
        test_len,
        test_i32,
        test_group,
        test_subfield,
        test_repeated_varint,
        test_packed_varint,
        test_repeated_i32,
        test_packed_i32,
        test_repeated_i64,
        test_packed_i64,
        test_repeated_len,
        test_type_int32,
        test_type_int64,
        test_type_uint32,
        test_type_uint64,
        test_type_sint32,
        test_type_sint64,
        test_type_bool,
        test_type_fixed64,
        test_type_sfixed64,
        test_type_double,
        test_type_fixed32,
        test_type_sfixed32,
        test_type_float
    };

    // Run tests
    const int numTests = sizeof(tests)/sizeof(nullptr);
    int error = 0;

    if (argc == 1) // Run all tests
    {
        for (int i = 0; i < numTests; i++)
        {
            error |= tests[i]();
        }
    }

    else if (argc > 1) // Run subset of tests
    {
        for (int i = 1; i < argc; i++)
        {
            int j = atoi(argv[i]);
            if (j >= 0 && j < numTests)
            {
                error |= tests[j]();
            }
            else
            {
                error |= 1;
            }
        }
    }

    return error;
}