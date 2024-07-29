#include "sqlite_protobuf.h"

#include "sqlite3ext.h"

#include "protobuf_foreach.h"
#include "protobuf_extract.h"
#include "protobuf_json.h"

namespace sqlite_protobuf
{
    SQLITE_EXTENSION_INIT1

    extern "C" int sqlite3_sqliteprotobuf_init(sqlite3 *db,
                                               char **pzErrMsg,
                                               const sqlite3_api_routines *pApi)
    {
        SQLITE_EXTENSION_INIT2(pApi);

        /*
        if (sqlite3_libversion_number() < 3024000)
        {
            *pzErrMsg = sqlite3_mprintf(
                "sqlite_protobuf requires SQLite 3.24.0 or later");
            return SQLITE_ERROR;
        }
        */

        // Run each register_* function and abort if any of them fails
        int (*register_fns[])(sqlite3 *, char **, const sqlite3_api_routines *) = {
            register_protobuf_extract,
            register_protobuf_json,
            register_protobuf_foreach,
        };

        int nfuncs = sizeof(register_fns) / sizeof(register_fns[0]);
        for (int i = 0; i < nfuncs; i++)
        {
            int err = (register_fns[i])(db, pzErrMsg, pApi);
            if (err != SQLITE_OK)
                return err;
        }

        return SQLITE_OK;
    }

} // namespace sqlite_protobuf
