#include "protobuf_json.h"
#include "sqlite3ext.h"

#include <string>
#include <sstream>

#include "protodec.h"

namespace sqlite_protobuf
{
    SQLITE_EXTENSION_INIT3

    namespace
    {

        /// Converts a binary blob of protobuf bytes to a JSON representation of the message.
        ///
        ///     SELECT protobuf_to_json(data, mode);
        ///
        /// @returns a JSON string.
        void protobuf_to_json(sqlite3_context *context, int argc, sqlite3_value **argv)
        {
            if(argc < 1 || argc > 2)
            {
                sqlite3_result_error(context, "Wrong number of arguments", -1);
                return;
            } 

            // Load in arguments
            sqlite3_value *data = argv[0];
            int64_t mode = argc > 1 ? sqlite3_value_int64(argv[1]) : 0;

            // Decode message
            Buffer buffer;
            buffer.start = static_cast<const uint8_t *>(sqlite3_value_blob(data));
            buffer.end = buffer.start + static_cast<size_t>(sqlite3_value_bytes(data));
            Field field = decodeProtobuf(buffer, mode > 1);

            // Convert to json
            std::ostringstream os;
            toJson(&field, os, mode > 0);
            std::string json = os.str();

            // Return result
            sqlite3_result_text(context, json.c_str(), json.length(), SQLITE_TRANSIENT);
            return;
        }

        /// Converts a JSON string to a binary blob of protobuf bytes.
        ///
        ///     SELECT protobuf_of_json(json);
        ///
        /// @returns a protobuf blob.
        void protobuf_of_json(sqlite3_context *context, int argc, sqlite3_value **argv)
        {
            sqlite3_result_error(context, "Not implemented", -1);
            return;
        }

    } // namespace

    int register_protobuf_json(sqlite3 *db, char **pzErrMsg, const sqlite3_api_routines *pApi)
    {
        int rc;

        rc = sqlite3_create_function(db, "protobuf_to_json", -1,
                                     SQLITE_UTF8 | SQLITE_DETERMINISTIC,
                                     nullptr, protobuf_to_json, nullptr, nullptr);
        if (rc != SQLITE_OK)
            return rc;

        return sqlite3_create_function(db, "protobuf_of_json", 1,
                                       SQLITE_UTF8 | SQLITE_DETERMINISTIC,
                                       nullptr, protobuf_of_json, nullptr, nullptr);
    }

} // namespace sqlite_protobuf
