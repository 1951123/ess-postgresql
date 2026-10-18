/*-------------------------------------------------------------------------
 *
 * analyze_sample_cache.h
 *
 * Internal, research-only binary export/import support for ANALYZE samples.
 * The format is tied to this PostgreSQL server build and relation descriptor;
 * it is not a data export format and contains no computed statistics.  The
 * relation filenode/page-count guard is not a content-level frozen-table
 * proof; callers and test harnesses must separately keep table contents
 * stable while comparing estimates.
 *
 *-------------------------------------------------------------------------
 */
#ifndef ANALYZE_SAMPLE_CACHE_H
#define ANALYZE_SAMPLE_CACHE_H

#include "access/htup.h"
#include "access/relation.h"

typedef struct SampleCache
{
	uint32		format_version;
	Oid			relation_oid;
	Oid			relation_filenode;
	uint32		relation_pages;
	char	   *relation_name;
	int			natts;
	uint32		sample_target;
	uint32		tuple_count;
	double		totalrows;
	HeapTuple *tuples;
} SampleCache;

extern PGDLLIMPORT char *ess_analyze_sample_export;
extern PGDLLIMPORT char *ess_analyze_sample_import;

extern bool ess_analyze_sample_export_enabled(void);
extern bool ess_analyze_sample_import_enabled(void);
extern void ess_analyze_sample_check_relation(Relation relation);

extern void ess_analyze_sample_export_file(Relation relation, const char *path,
										HeapTuple *tuples, int tuple_count,
										int sample_target, double totalrows);
extern SampleCache *ess_analyze_sample_import_file(Relation relation,
											const char *path, int targrows);

#endif						/* ANALYZE_SAMPLE_CACHE_H */
