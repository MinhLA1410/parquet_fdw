#include "common.hpp"

extern "C"
{
#include "postgres.h"
#include "fmgr.h"
#include "catalog/pg_type.h"
#include "utils/builtins.h"
#include "utils/date.h"
#include "utils/memutils.h"
#include "utils/memdebug.h"
#include "utils/timestamp.h"
}

#if PG_VERSION_NUM < 130000
#define MAXINT8LEN 25
#endif

/*
 * exc_palloc
 *      C++ specific memory allocator that utilizes postgres allocation sets.
 */
void *
exc_palloc(std::size_t size)
{
	/* duplicates MemoryContextAllocZero to avoid increased overhead */
	void	   *ret;
	MemoryContext context = CurrentMemoryContext;

	AssertArg(MemoryContextIsValid(context));

	if (!AllocSizeIsValid(size))
		throw std::bad_alloc();

	context->isReset = false;

	ret = context->methods->alloc(context, size);
	if (unlikely(ret == NULL))
		throw std::bad_alloc();

	VALGRIND_MEMPOOL_ALLOC(context, ret, size);

	return ret;
}

Oid
to_postgres_type(int arrow_type)
{
    switch (arrow_type)
    {
        case arrow::Type::BOOL:
            return BOOLOID;
        case arrow::Type::INT8:
        case arrow::Type::INT16:
            return INT2OID;
        case arrow::Type::INT32:
            return INT4OID;
        case arrow::Type::INT64:
            return INT8OID;
        case arrow::Type::FLOAT:
            return FLOAT4OID;
        case arrow::Type::DOUBLE:
            return FLOAT8OID;
        case arrow::Type::STRING:
            return TEXTOID;
        case arrow::Type::BINARY:
            return BYTEAOID;
        case arrow::Type::TIMESTAMP:
            return TIMESTAMPOID;
        case arrow::Type::DATE32:
            return DATEOID;
        default:
            return InvalidOid;
    }
}

/*
 * bytes_to_postgres_type
 *      Convert min/max values from column statistics stored in parquet file as
 *      plain bytes to postgres Datum.
 */
Datum
bytes_to_postgres_type(const char *bytes, Size len, const arrow::DataType *arrow_type)
{
    switch(arrow_type->id())
    {
        case arrow::Type::BOOL:
            return BoolGetDatum(*(bool *) bytes);
        case arrow::Type::INT8:
            return Int16GetDatum(*(int8 *) bytes);
        case arrow::Type::INT16:
            return Int16GetDatum(*(int16 *) bytes);
        case arrow::Type::INT32:
            return Int32GetDatum(*(int32 *) bytes);
        case arrow::Type::INT64:
            return Int64GetDatum(*(int64 *) bytes);
        case arrow::Type::FLOAT:
            return Float4GetDatum(*(float *) bytes);
        case arrow::Type::DOUBLE:
            return Float8GetDatum(*(double *) bytes);
        case arrow::Type::STRING:
            return CStringGetTextDatum(bytes);
        case arrow::Type::BINARY:
            return PointerGetDatum(cstring_to_text_with_len(bytes, len));
        case arrow::Type::TIMESTAMP:
            {
                TimestampTz ts;
                auto tstype = (arrow::TimestampType *) arrow_type;

                to_postgres_timestamp(tstype, *(int64 *) bytes, ts);
                return TimestampGetDatum(ts);
            }
            break;
        case arrow::Type::DATE32:
            return DateADTGetDatum(*(int32 *) bytes +
                                   (UNIX_EPOCH_JDATE - POSTGRES_EPOCH_JDATE));
        default:
            return PointerGetDatum(NULL);
    }
}

/*
 * XXX Currently only supports ascii strings
 */
char *
tolowercase(const char *input, char *output)
{
    int i = 0;

    Assert(strlen(input) < NAMEDATALEN - 1);

    do
    {
        output[i] = tolower(input[i]);
    }
    while (input[i++]);

    return output;
}

arrow::Type::type
get_arrow_list_elem_type(arrow::DataType *type)
{
    auto children = type->fields();

    Assert(children.size() == 1);
    return children[0]->type()->id();
}
