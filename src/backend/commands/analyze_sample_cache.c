/*-------------------------------------------------------------------------
 *
 * analyze_sample_cache.c
 *		Binary export/import for ANALYZE acquisition samples.
 *
 * This is deliberately an internal, PostgreSQL-native format.  It is tied to
 * the server binary and relation tuple descriptor; it is not a data export
 * format and does not contain computed statistics.  The feature is intended
 * only for trusted local research files.
 *
 *-------------------------------------------------------------------------
 */
#include "postgres.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "access/heapam.h"
#include "access/htup_details.h"
#include "access/tableam.h"
#include "catalog/namespace.h"
#include "commands/analyze_sample_cache.h"
#include "miscadmin.h"
#include "port/pg_crc32c.h"
#include "storage/bufmgr.h"
#include "storage/fd.h"
#include "utils/elog.h"
#include "utils/lsyscache.h"
#include "utils/memutils.h"
#include "utils/rel.h"

#define ESS_ANALYZE_SAMPLE_MAGIC "ESSANL01"
#define ESS_ANALYZE_SAMPLE_MAGIC_LEN 8
#define ESS_ANALYZE_SAMPLE_FORMAT_VERSION 1

char *ess_analyze_sample_export = "";
char *ess_analyze_sample_import = "";

static void ess_sample_write_bytes(FILE *file, const void *data, size_t len,
								   pg_crc32c *crc, const char *path);
static void ess_sample_read_bytes(FILE *file, void *data, size_t len,
								  pg_crc32c *crc, const char *path);
static void ess_sample_write_u32(FILE *file, uint32 value, pg_crc32c *crc,
								 const char *path);
static uint32 ess_sample_read_u32(FILE *file, pg_crc32c *crc,
								  const char *path);
static void ess_sample_write_string(FILE *file, const char *value,
								   pg_crc32c *crc, const char *path);
static char *ess_sample_read_string(FILE *file, pg_crc32c *crc,
									const char *path);
static char *ess_sample_relation_identity(Relation relation);
static void ess_sample_check_descriptor(Relation relation, uint32 natts,
										FILE *file, pg_crc32c *crc,
										const char *path);
static void ess_sample_file_error(const char *path, const char *operation);
static void ess_sample_data_error(const char *detail);
static void ess_sample_check_u32_size(uint32 value, Size element_size,
									 const char *what, const char *path);

static void
ess_sample_file_error(const char *path, const char *operation)
{
	ereport(ERROR,
			(errcode_for_file_access(),
			 errmsg("could not %s ANALYZE sample file \"%s\": %m",
					 operation, path)));
}

static void
ess_sample_data_error(const char *detail)
{
	ereport(ERROR,
			(errcode(ERRCODE_DATA_CORRUPTED),
			 errmsg("invalid ANALYZE sample file: %s", detail)));
}

static void
ess_sample_write_bytes(FILE *file, const void *data, size_t len,
								pg_crc32c *crc, const char *path)
{
	if (len > 0 && fwrite(data, 1, len, file) != len)
		ess_sample_file_error(path, "write");
	if (len > 0)
		COMP_CRC32C(*crc, data, len);
}

static void
ess_sample_read_bytes(FILE *file, void *data, size_t len,
							 pg_crc32c *crc, const char *path)
{
	if (len > 0 && fread(data, 1, len, file) != len)
	{
		if (ferror(file))
			ess_sample_file_error(path, "read");
		ess_sample_data_error("truncated file");
	}
	if (len > 0)
		COMP_CRC32C(*crc, data, len);
}

static void
ess_sample_write_u32(FILE *file, uint32 value, pg_crc32c *crc,
							 const char *path)
{
	ess_sample_write_bytes(file, &value, sizeof(value), crc, path);
}

static uint32
ess_sample_read_u32(FILE *file, pg_crc32c *crc, const char *path)
{
	uint32	value;

	ess_sample_read_bytes(file, &value, sizeof(value), crc, path);
	return value;
}

static void
ess_sample_write_string(FILE *file, const char *value, pg_crc32c *crc,
							 const char *path)
{
	size_t	len = strlen(value);

	if (len > UINT32_MAX)
		ereport(ERROR,
				(errcode(ERRCODE_PROGRAM_LIMIT_EXCEEDED),
				 errmsg("ANALYZE sample relation identity is too long")));
	ess_sample_write_u32(file, (uint32) len, crc, path);
	ess_sample_write_bytes(file, value, len, crc, path);
}

static char *
ess_sample_read_string(FILE *file, pg_crc32c *crc, const char *path)
{
	uint32	len = ess_sample_read_u32(file, crc, path);
	char   *value;

	if (len > MaxAllocSize - 1)
		ereport(ERROR,
				(errcode(ERRCODE_DATA_CORRUPTED),
				 errmsg("ANALYZE sample file string is too large"),
				 errdetail("file \"%s\" contains a string of length %u",
						   path, len)));
	value = (char *) palloc((Size) len + 1);
	ess_sample_read_bytes(file, value, len, crc, path);
	value[len] = '\0';
	return value;
}

static char *
ess_sample_relation_identity(Relation relation)
{
	const char *namespace_name = get_namespace_name(RelationGetNamespace(relation));
	const char *relation_name = RelationGetRelationName(relation);

	return psprintf("%zu:%s%zu:%s",
					strlen(namespace_name), namespace_name,
					strlen(relation_name), relation_name);
}

static void
ess_sample_check_u32_size(uint32 value, Size element_size, const char *what,
							  const char *path)
{
	if (value > MaxAllocSize / element_size)
		ereport(ERROR,
				(errcode(ERRCODE_PROGRAM_LIMIT_EXCEEDED),
				 errmsg("ANALYZE sample file %s is too large", what),
				 errdetail("file \"%s\" requests %u elements", path, value)));
}

static void
ess_sample_check_descriptor(Relation relation, uint32 natts, FILE *file,
								pg_crc32c *crc, const char *path)
{
	uint32	 i;

	if (natts > INT_MAX || relation->rd_att->natts != (int) natts)
		ereport(ERROR,
				(errcode(ERRCODE_DATATYPE_MISMATCH),
				 errmsg("ANALYZE sample file column count does not match relation"),
				 errdetail("file has %u columns, relation has %d",
						   natts, relation->rd_att->natts)));

	for (i = 0; i < natts; i++)
	{
		Form_pg_attribute attr = TupleDescAttr(relation->rd_att, i);
		Oid		typid = ess_sample_read_u32(file, crc, path);
		int32	typmod;
		Oid		collation;
		uint8	dropped;
		char   *attname;

		ess_sample_read_bytes(file, &typmod, sizeof(typmod), crc, path);
		collation = ess_sample_read_u32(file, crc, path);
		ess_sample_read_bytes(file, &dropped, sizeof(dropped), crc, path);
		attname = ess_sample_read_string(file, crc, path);

		if (attr->atttypid != typid || attr->atttypmod != typmod ||
			attr->attcollation != collation ||
			attr->attisdropped != (dropped != 0) ||
			strcmp(NameStr(attr->attname), attname) != 0)
			ereport(ERROR,
					(errcode(ERRCODE_DATATYPE_MISMATCH),
					 errmsg("ANALYZE sample file column %u does not match relation",
							i + 1)));
		pfree(attname);
	}
}

bool
ess_analyze_sample_export_enabled(void)
{
	return ess_analyze_sample_export != NULL &&
		ess_analyze_sample_export[0] != '\0';
}

bool
ess_analyze_sample_import_enabled(void)
{
	return ess_analyze_sample_import != NULL &&
		ess_analyze_sample_import[0] != '\0';
}

void
ess_analyze_sample_check_relation(Relation relation)
{
	if (!ess_analyze_sample_export_enabled() &&
		!ess_analyze_sample_import_enabled())
		return;

	if (ess_analyze_sample_export_enabled() &&
		ess_analyze_sample_import_enabled())
		ereport(ERROR,
				(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
				 errmsg("ess.analyze_sample_export and ess.analyze_sample_import cannot be enabled together")));

	if (relation->rd_rel->relkind != RELKIND_RELATION ||
		relation->rd_rel->relhassubclass ||
		relation->rd_tableam != GetHeapamTableAmRoutine())
		ereport(ERROR,
				(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
				 errmsg("ESS ANALYZE sample export/replay supports only a single heap table"),
				 errdetail("relation \"%s\" is inherited, partitioned, or does not use the heap table access method",
						   RelationGetRelationName(relation))));
}

void
ess_analyze_sample_export_file(Relation relation, const char *path,
								   HeapTuple *tuples, int tuple_count,
								   int sample_target, double totalrows)
{
	FILE	   *file;
	pg_crc32c	crc;
	uint32		i;
	uint32		natts = (uint32) relation->rd_att->natts;
	uint32		server_version = PG_VERSION_NUM;
	uint32		relation_pages = RelationGetNumberOfBlocks(relation);
	char	   *relation_identity;

	if (tuple_count <= 0 || sample_target <= 0 || tuple_count > sample_target ||
		!isfinite(totalrows) || totalrows < 0.0 ||
		totalrows < (double) tuple_count)
		ereport(ERROR,
				(errcode(ERRCODE_DATA_CORRUPTED),
				 errmsg("invalid ANALYZE sample metadata for export"),
				 errdetail("sample rows=%d, target=%d, totalrows=%.17g",
						   tuple_count, sample_target, totalrows)));

	file = AllocateFile(path, PG_BINARY_W);
	if (file == NULL)
		ess_sample_file_error(path, "open");

	INIT_CRC32C(crc);
	relation_identity = ess_sample_relation_identity(relation);
	ess_sample_write_bytes(file, ESS_ANALYZE_SAMPLE_MAGIC,
							   ESS_ANALYZE_SAMPLE_MAGIC_LEN, &crc, path);
	ess_sample_write_u32(file, ESS_ANALYZE_SAMPLE_FORMAT_VERSION, &crc, path);
	ess_sample_write_u32(file, server_version, &crc, path);
	ess_sample_write_string(file, PG_VERSION, &crc, path);
	ess_sample_write_u32(file, BLCKSZ, &crc, path);
	ess_sample_write_u32(file, MAXIMUM_ALIGNOF, &crc, path);
	ess_sample_write_u32(file, sizeof(HeapTupleHeaderData), &crc, path);
	ess_sample_write_u32(file, sizeof(HeapTupleData), &crc, path);
	ess_sample_write_u32(file, sizeof(ItemPointerData), &crc, path);
	ess_sample_write_u32(file, relation->rd_id, &crc, path);
	ess_sample_write_u32(file, relation->rd_rel->relfilenode, &crc, path);
	ess_sample_write_u32(file, relation_pages, &crc, path);
	ess_sample_write_string(file, relation_identity, &crc, path);
	ess_sample_write_u32(file, natts, &crc, path);
	ess_sample_write_u32(file, (uint32) sample_target, &crc, path);
	ess_sample_write_u32(file, (uint32) tuple_count, &crc, path);
	ess_sample_write_bytes(file, &totalrows, sizeof(totalrows), &crc, path);

	for (i = 0; i < natts; i++)
	{
		Form_pg_attribute attr = TupleDescAttr(relation->rd_att, i);
		uint8	dropped = attr->attisdropped ? 1 : 0;

		ess_sample_write_u32(file, attr->atttypid, &crc, path);
		ess_sample_write_bytes(file, &attr->atttypmod, sizeof(attr->atttypmod),
							   &crc, path);
		ess_sample_write_u32(file, attr->attcollation, &crc, path);
		ess_sample_write_bytes(file, &dropped, sizeof(dropped), &crc, path);
		ess_sample_write_string(file, NameStr(attr->attname), &crc, path);
	}

	for (i = 0; i < (uint32) tuple_count; i++)
	{
		uint32	len = tuples[i]->t_len;

		if (len < sizeof(HeapTupleHeaderData) || len > MaxAllocSize)
			ereport(ERROR,
					(errcode(ERRCODE_DATA_CORRUPTED),
					 errmsg("invalid tuple length in ANALYZE sample")));
		ess_sample_write_u32(file, len, &crc, path);
		ess_sample_write_bytes(file, &tuples[i]->t_self,
							   sizeof(tuples[i]->t_self), &crc, path);
		ess_sample_write_u32(file, tuples[i]->t_tableOid, &crc, path);
		ess_sample_write_bytes(file, tuples[i]->t_data, len, &crc, path);
	}

	FIN_CRC32C(crc);
	if (fwrite(&crc, sizeof(crc), 1, file) != 1)
		ess_sample_file_error(path, "write");
	if (FreeFile(file) != 0)
		ess_sample_file_error(path, "close");
	pfree(relation_identity);
}

SampleCache *
ess_analyze_sample_import_file(Relation relation, const char *path, int targrows)
{
	SampleCache *cache;
	FILE	   *file;
	pg_crc32c	crc;
	pg_crc32c	stored_crc;
	char		magic[ESS_ANALYZE_SAMPLE_MAGIC_LEN];
	uint32		version;
	uint32		server_version;
	char	   *server_version_string;
	uint32		stored_blcksz;
	uint32		stored_alignof;
	uint32		stored_tuple_header_size;
	uint32		stored_tuple_size;
	uint32		stored_item_pointer_size;
	char	   *relation_name;
	char	   *relation_identity;
	uint32		natts;
	uint32		i;
	int			trailing;

	if (targrows <= 0)
		ereport(ERROR,
				(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
				 errmsg("invalid ANALYZE sample target %d", targrows)));

	file = AllocateFile(path, PG_BINARY_R);
	if (file == NULL)
		ess_sample_file_error(path, "open");

	INIT_CRC32C(crc);
	ess_sample_read_bytes(file, magic, sizeof(magic), &crc, path);
	if (memcmp(magic, ESS_ANALYZE_SAMPLE_MAGIC, sizeof(magic)) != 0)
		ess_sample_data_error("invalid magic");

	version = ess_sample_read_u32(file, &crc, path);
	if (version != ESS_ANALYZE_SAMPLE_FORMAT_VERSION)
		ereport(ERROR,
				(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
				 errmsg("unsupported ANALYZE sample file format version %u", version)));

	server_version = ess_sample_read_u32(file, &crc, path);
	server_version_string = ess_sample_read_string(file, &crc, path);
	if (server_version != PG_VERSION_NUM ||
		strcmp(server_version_string, PG_VERSION) != 0)
		ereport(ERROR,
				(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
				 errmsg("ANALYZE sample file PostgreSQL version is incompatible"),
				 errdetail("file is for %u (%s), server is %u (%s)",
						   server_version, server_version_string,
						   PG_VERSION_NUM, PG_VERSION)));
	pfree(server_version_string);

	stored_blcksz = ess_sample_read_u32(file, &crc, path);
	stored_alignof = ess_sample_read_u32(file, &crc, path);
	stored_tuple_header_size = ess_sample_read_u32(file, &crc, path);
	stored_tuple_size = ess_sample_read_u32(file, &crc, path);
	stored_item_pointer_size = ess_sample_read_u32(file, &crc, path);
	if (stored_blcksz != BLCKSZ || stored_alignof != MAXIMUM_ALIGNOF ||
		stored_tuple_header_size != sizeof(HeapTupleHeaderData) ||
		stored_tuple_size != sizeof(HeapTupleData) ||
		stored_item_pointer_size != sizeof(ItemPointerData))
		ereport(ERROR,
				(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
				 errmsg("ANALYZE sample file binary layout is incompatible")));

	cache = (SampleCache *) palloc0(sizeof(SampleCache));
	cache->format_version = version;
	cache->relation_oid = ess_sample_read_u32(file, &crc, path);
	cache->relation_filenode = ess_sample_read_u32(file, &crc, path);
	cache->relation_pages = ess_sample_read_u32(file, &crc, path);
	relation_name = ess_sample_read_string(file, &crc, path);
	relation_identity = ess_sample_relation_identity(relation);
	if (cache->relation_oid != RelationGetRelid(relation) ||
		cache->relation_filenode != relation->rd_rel->relfilenode ||
		cache->relation_pages != RelationGetNumberOfBlocks(relation) ||
		strcmp(relation_name, relation_identity) != 0)
		ereport(ERROR,
				(errcode(ERRCODE_OBJECT_NOT_IN_PREREQUISITE_STATE),
				 errmsg("ANALYZE sample file relation identity does not match"),
				 errdetail("file relation \"%s\" does not match the current table",
						   relation_name)));
	cache->relation_name = relation_name;
	pfree(relation_identity);

	natts = ess_sample_read_u32(file, &crc, path);
	if (natts > INT_MAX)
		ereport(ERROR,
				(errcode(ERRCODE_PROGRAM_LIMIT_EXCEEDED),
				 errmsg("ANALYZE sample file has too many columns"),
				 errdetail("file \"%s\" contains %u columns", path, natts)));
	cache->natts = (int) natts;
	cache->sample_target = ess_sample_read_u32(file, &crc, path);
	cache->tuple_count = ess_sample_read_u32(file, &crc, path);
	ess_sample_read_bytes(file, &cache->totalrows, sizeof(cache->totalrows),
						 &crc, path);

	if (cache->sample_target == 0 || cache->sample_target < cache->tuple_count ||
		cache->tuple_count == 0 || !isfinite(cache->totalrows) ||
		cache->totalrows < (double) cache->tuple_count ||
		cache->sample_target < (uint32) targrows ||
		(cache->totalrows > (double) cache->tuple_count &&
		 cache->tuple_count < (uint32) targrows))
		ereport(ERROR,
				(errcode(ERRCODE_PROGRAM_LIMIT_EXCEEDED),
				 errmsg("ANALYZE sample file does not contain enough rows"),
				 errdetail("file has %u rows for target %u; current request needs %d",
						   cache->tuple_count, cache->sample_target, targrows)));

	ess_sample_check_descriptor(relation, natts, file, &crc, path);
	if (cache->tuple_count > INT_MAX)
		ereport(ERROR,
				(errcode(ERRCODE_PROGRAM_LIMIT_EXCEEDED),
				 errmsg("ANALYZE sample file contains too many rows"),
				 errdetail("file \"%s\" contains %u rows", path,
						   cache->tuple_count)));
	ess_sample_check_u32_size(cache->tuple_count, sizeof(HeapTuple),
							 "tuple array", path);
	cache->tuples = (HeapTuple *) palloc(sizeof(HeapTuple) * cache->tuple_count);
	for (i = 0; i < cache->tuple_count; i++)
	{
		uint32		len = ess_sample_read_u32(file, &crc, path);
		HeapTuple	tuple;

		if (len < sizeof(HeapTupleHeaderData) || len > MaxAllocSize)
			ereport(ERROR,
					(errcode(ERRCODE_DATA_CORRUPTED),
					 errmsg("invalid tuple length in ANALYZE sample file"),
					 errdetail("tuple %u has length %u", i, len)));
		tuple = (HeapTuple) palloc(sizeof(HeapTupleData));
		tuple->t_len = len;
		ess_sample_read_bytes(file, &tuple->t_self, sizeof(tuple->t_self),
							  &crc, path);
		tuple->t_tableOid = ess_sample_read_u32(file, &crc, path);
		if (tuple->t_tableOid != RelationGetRelid(relation))
			ereport(ERROR,
					(errcode(ERRCODE_DATATYPE_MISMATCH),
					 errmsg("ANALYZE sample tuple table identity does not match")));
		tuple->t_data = (HeapTupleHeader) palloc(len);
		ess_sample_read_bytes(file, tuple->t_data, len, &crc, path);
		cache->tuples[i] = tuple;
	}

	if (fread(&stored_crc, sizeof(stored_crc), 1, file) != 1)
	{
		if (ferror(file))
			ess_sample_file_error(path, "read");
		ess_sample_data_error("truncated checksum");
	}
	FIN_CRC32C(crc);
	if (stored_crc != crc)
		ereport(ERROR,
				(errcode(ERRCODE_DATA_CORRUPTED),
				 errmsg("checksum mismatch in ANALYZE sample file \"%s\"",
						path)));
	trailing = fgetc(file);
	if (trailing != EOF)
		ereport(ERROR,
				(errcode(ERRCODE_DATA_CORRUPTED),
				 errmsg("unexpected data after ANALYZE sample file checksum"),
				 errdetail("file \"%s\" has trailing bytes", path)));
	if (ferror(file))
		ess_sample_file_error(path, "read");
	if (FreeFile(file) != 0)
		ess_sample_file_error(path, "close");

	return cache;
}
