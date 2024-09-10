#include "protobuf_foreach.h"
#include "sqlite3ext.h"

#include <string>
#include <cstring>

#include "protodec.h"

namespace sqlite_protobuf
{
    SQLITE_EXTENSION_INIT3

    std::string string_from_sqlite3_value(sqlite3_value *value)
    {
        const char *text = static_cast<const char *>(sqlite3_value_blob(value));
        size_t text_size = static_cast<size_t>(sqlite3_value_bytes(value));
        return std::string(text, text_size);
    }

    /*
    ** Define virtual table data structure containg data needed for virtual table
    */
    typedef struct ProtobufForeachVtab ProtobufForeachVtab;
    struct ProtobufForeachVtab 
    {
        sqlite3_vtab base;  // Base class - must be first
    };

    /*
    ** Define cursor data structure, used to iterate over virtual table rows
    */
    typedef struct ProtobufForeachCursor ProtobufForeachCursor;
    struct ProtobufForeachCursor 
    {
        sqlite3_vtab_cursor base;   // Base class - must be first
        sqlite3_int64 iRowid;       // The rowid
        std::string path;           // Path to root field
        Field field;                // Decoded protobuf message
        Field *root;                // Root field
    };

    /*
    ** Constructor for ProtobufForeachVtab objects.
    */
    static int protobufForeachConnect(sqlite3 *db, void *pAux, int argc, const char *const*argv, sqlite3_vtab **ppVtab, char **pzErr)
    {
        /* For convenience, define symbolic names for the index to each column. */
        #define PROTOBUF_FOREACH_TAG      0
        #define PROTOBUF_FOREACH_FIELD    1
        #define PROTOBUF_FOREACH_WIRETYPE 2
        #define PROTOBUF_FOREACH_VALUE    3
        #define PROTOBUF_FOREACH_PARENT   4
        #define PROTOBUF_FOREACH_BUFFER   5 // First argument (marked as HIDDEN) protobuf buffer
        #define PROTOBUF_FOREACH_ROOT     6 // Second argument (marked as HIDDEN) root path

        // Tell SQLite what the result set of queries against the virtual table will look like.
        ProtobufForeachVtab *pNew;
        int rc = sqlite3_declare_vtab(db,"CREATE TABLE x(tag,field,wiretype,value,parent,buffer HIDDEN,root HIDDEN)");

        if( rc==SQLITE_OK )
        {
            // Allocate the ProtobufForeachVtab object and initialize all fields
            pNew = (ProtobufForeachVtab *)sqlite3_malloc( sizeof(*pNew) );
            *ppVtab = (sqlite3_vtab*)pNew;
            if( pNew==0 ) return SQLITE_NOMEM;
            memset(pNew, 0, sizeof(*pNew));
        }

        return rc;
    }

    /*
    ** Destructor for ProtobufForeachVtab objects.
    */
    static int protobufForeachDisconnect(sqlite3_vtab *pVtab)
    {
        ProtobufForeachVtab *p = (ProtobufForeachVtab*)pVtab;
        sqlite3_free(p);
        return SQLITE_OK;
    }

    /*
    ** Constructor for a new ProtobufForeachCursor object.
    */
    static int protobufForeachOpen(sqlite3_vtab *p, sqlite3_vtab_cursor **ppCursor)
    {
        ProtobufForeachCursor *pCur;
        pCur = (ProtobufForeachCursor *)sqlite3_malloc( sizeof(*pCur) );
        if( pCur==0 ) return SQLITE_NOMEM;
        memset(pCur, 0, sizeof(*pCur));
        *ppCursor = &pCur->base;
        pCur->path = "$";
        return SQLITE_OK;
    }

    /*
    ** Destructor for a ProtobufForeachCursor.
    */
    static int protobufForeachClose(sqlite3_vtab_cursor *cur)
    {
        ProtobufForeachCursor *pCur = (ProtobufForeachCursor*)cur;
        sqlite3_free(pCur);
        return SQLITE_OK;
    }


    /*
    ** Advance a ProtobufForeachCursor to its next row of output.
    */
    static int protobufForeachNext(sqlite3_vtab_cursor *cur)
    {
        ProtobufForeachCursor *pCur = (ProtobufForeachCursor*)cur;
        pCur->iRowid++;
        return SQLITE_OK;
    }

    /*
    ** Return value at given column and row (ProtobufForeachCursor).
    */
    static int protobufForeachColumn(sqlite3_vtab_cursor *cur, sqlite3_context *ctx, int col)
    {
        ProtobufForeachCursor *pCur = (ProtobufForeachCursor *)cur;
        Field *field = &pCur->root->subFields[pCur->iRowid];
        
        switch (col)
        {
        case PROTOBUF_FOREACH_TAG:
            sqlite3_result_int64(ctx, field->tag);
            break;
        case PROTOBUF_FOREACH_FIELD:
            sqlite3_result_int64(ctx, field->fieldNum);
            break;
        case PROTOBUF_FOREACH_WIRETYPE:
            sqlite3_result_int64(ctx, field->wireType);
            break;
        case PROTOBUF_FOREACH_VALUE:
            sqlite3_result_blob(ctx, (char*)field->value.start, field->value.size(), SQLITE_STATIC);
            break;
        case PROTOBUF_FOREACH_PARENT:
            sqlite3_result_blob(ctx, (char*)pCur->root->value.start, pCur->root->value.size(), SQLITE_STATIC);
            break;
        case PROTOBUF_FOREACH_BUFFER:
            sqlite3_result_blob(ctx, (char*)pCur->field.value.start, pCur->field.value.size(), SQLITE_STATIC);
            break;
        case PROTOBUF_FOREACH_ROOT:
            sqlite3_result_text(ctx, pCur->path.c_str(), pCur->path.size(), SQLITE_TRANSIENT);
            break;
        default:
            break;
        }
        return SQLITE_OK;
    }

    /*
    ** Return the rowid for the current row.
    */
    static int protobufForeachRowid(sqlite3_vtab_cursor *cur, sqlite_int64 *pRowid)
    {
        ProtobufForeachCursor *pCur = (ProtobufForeachCursor*)cur;
        *pRowid = pCur->iRowid;
        return SQLITE_OK;
    }

    /*
    ** Return TRUE if the cursor has been moved off of the last row of output.
    */
    static int protobufForeachEof(sqlite3_vtab_cursor *cur)
    {
        ProtobufForeachCursor *pCur = (ProtobufForeachCursor*)cur;
        return pCur->root == nullptr || pCur->iRowid >= pCur->root->subFields.size();
    }

    /*
    ** This method is called to "rewind" the ProtobufForeachCursor object back
    ** to the first row of output.  This method is always called at least
    ** once prior to any call to protobufForeachColumn() or protobufForeachRowid() or 
    ** protobufForeachEof().
    */
    static int protobufForeachFilter(sqlite3_vtab_cursor *cur, int idxNum, const char *idxStr, int argc, sqlite3_value **argv)
    {

        ProtobufForeachCursor *pCur = (ProtobufForeachCursor *)cur;
        pCur->iRowid = 0;

        // Query strategy 0, no buffer supplied
        if(idxNum==0)
        {
            return SQLITE_OK;
        }

        // Decode protobuf message
        Buffer buffer;
        buffer.start = static_cast<const uint8_t*>(sqlite3_value_blob(argv[0]));
        buffer.end = buffer.start + static_cast<size_t>(sqlite3_value_bytes(argv[0]));
        pCur->field = decodeProtobuf(buffer, true);
        pCur->root = &pCur->field;
        
        // Query strategy 3, path supplied perform search to find root
        if(idxNum==3)
        {
            // Get path from argument
            const std::string path = string_from_sqlite3_value(argv[1]);
            
            if (path.length() == 0) 
            {
                return SQLITE_OK;
            }
            
            if (path[0] != '$')
            {
                sqlite3_free(cur->pVtab->zErrMsg);
                cur->pVtab->zErrMsg = sqlite3_mprintf("Invalid path");
                return SQLITE_ERROR;
            }

            pCur->path = path;

            // Parse the path string and traverse the message
            int fieldNumber, fieldIndex;
            size_t fieldStart = path.find(".", 0);
            while(fieldStart < path.size())
            {
                size_t fieldEnd = path.find(".", fieldStart + 1);
                size_t indexStart = path.find("[", fieldStart + 1);
                size_t indexEnd = path.find("]", fieldStart + 1);

                fieldEnd = fieldEnd == std::string::npos ? path.size() : fieldEnd;

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

                // Move path ponter forward
                fieldStart = fieldEnd;

                pCur->root = pCur->root->getSubField(fieldNumber, WIRETYPE_LEN, fieldIndex);
                if (pCur->root == nullptr) {break;}
            }
        }

        return SQLITE_OK;
    }

    /*
    ** SQLite will invoke this method one or more times while planning a query
    ** that uses the virtual table.  This routine needs to create a query plan 
    ** for each invocation and compute an estimated cost for that plan.
    ** The query strategy here is to look for an equality constraint on the buffer
    ** column.  Without such a constraint, the table cannot operate.  idxNum 
    ** represents the strategy, and is 1 if the constraint is found, 3 if the 
    ** constraint and root are found, and 0 otherwise.
    */
    static int protobufForeachBestIndex(sqlite3_vtab *tab, sqlite3_index_info *pIdxInfo)
    {
        int aIdx[2];          // Index of constraints for BUFFER and ROOT
        int unusableMask = 0; // Mask of unusable BUFFER and ROOT constraints
        int idxMask = 0;      // Mask of usable == constraints BUFFER and ROOT
        const struct sqlite3_index_info::sqlite3_index_constraint *pConstraint;

        // This implementation assumes that BUFFER and ROOT are the last two columns in the table
        aIdx[0] = aIdx[1] = -1;
        pConstraint = pIdxInfo->aConstraint;
        for (int i = 0; i < pIdxInfo->nConstraint; i++, pConstraint++)
        {
            int iCol = pConstraint->iColumn - PROTOBUF_FOREACH_BUFFER;
            int iMask = 1 << iCol;
            if (iCol >= 0)
            {
                if (pConstraint->usable == 0)
                {
                    unusableMask |= iMask;
                }
                else if (pConstraint->op == SQLITE_INDEX_CONSTRAINT_EQ)
                {
                    aIdx[iCol] = i;
                    idxMask |= iMask;
                }
            }
        }
        if (pIdxInfo->nOrderBy > 0 && pIdxInfo->aOrderBy[0].iColumn < 0 && pIdxInfo->aOrderBy[0].desc == 0)
        {
            pIdxInfo->orderByConsumed = 1;
        }

        if ((unusableMask & ~idxMask) != 0)
        {
            // If there are any unusable constraints on BUFFER or ROOT, then reject this entire plan
            return SQLITE_CONSTRAINT;
        }
        if (aIdx[0] < 0)
        {
            // No BUFFER input. Leave estimatedCost at the huge initial value to discourage query planner from using this plan.
            pIdxInfo->idxNum = 0;
        }
        else
        {
            pIdxInfo->estimatedCost = 1.0;
            pIdxInfo->aConstraintUsage[aIdx[0]].argvIndex = 1;
            pIdxInfo->aConstraintUsage[aIdx[0]].omit = 1;
            if (aIdx[1] < 0)
            {
                pIdxInfo->idxNum = 1; // Only BUFFER supplied.  Plan 1
            }
            else
            {
                pIdxInfo->aConstraintUsage[aIdx[1]].argvIndex = 2;
                pIdxInfo->aConstraintUsage[aIdx[1]].omit = 1;
                pIdxInfo->idxNum = 3; // Both BUFFER and ROOT are supplied.  Plan 3
            }
        }
        return SQLITE_OK;
    }

    /*
    ** Define all the methods for the module (virtual table).
    */
    static sqlite3_module protobufForeachModule = {
        /* iVersion    */ 0,
        /* xCreate     */ 0,
        /* xConnect    */ protobufForeachConnect,
        /* xBestIndex  */ protobufForeachBestIndex,
        /* xDisconnect */ protobufForeachDisconnect,
        /* xDestroy    */ 0,
        /* xOpen       */ protobufForeachOpen,
        /* xClose      */ protobufForeachClose,
        /* xFilter     */ protobufForeachFilter,
        /* xNext       */ protobufForeachNext,
        /* xEof        */ protobufForeachEof,
        /* xColumn     */ protobufForeachColumn,
        /* xRowid      */ protobufForeachRowid,
        /* xUpdate     */ 0,
        /* xBegin      */ 0,
        /* xSync       */ 0,
        /* xCommit     */ 0,
        /* xRollback   */ 0,
        /* xFindMethod */ 0,
        /* xRename     */ 0,
        /* xSavepoint  */ //0, // iVersion >= 2 
        /* xRelease    */ //0, // iVersion >= 2
        /* xRollbackTo */ //0, // iVersion >= 2
        /* xShadowName */ //0, // iVersion >= 3
        /* xIntegrity  */ //0, // iVersion >= 4
    };


    int register_protobuf_foreach(sqlite3 *db, char **pzErrMsg, const sqlite3_api_routines *pApi)
    {
        int rc = SQLITE_OK;
        if(rc == SQLITE_OK) {rc = sqlite3_create_module(db, "protobuf_foreach", &protobufForeachModule, 0);}
        if(rc == SQLITE_OK) {rc = sqlite3_create_module(db, "protobuf_each", &protobufForeachModule, 0);}
        return rc;
    }

} // namespace sqlite_protobuf
