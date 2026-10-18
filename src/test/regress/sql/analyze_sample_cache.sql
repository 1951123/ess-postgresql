-- Research-only binary ANALYZE sample capture/replay.
-- These names are confined to the disposable regression instance.
\set samplefile '/tmp/ess-analyze-sample-cache.bin'
\set corruptfile '/tmp/ess-analyze-sample-cache-corrupt.bin'
\setenv ESS_SAMPLEFILE :samplefile
\setenv ESS_CORRUPTFILE :corruptfile

SET client_min_messages = warning;
SHOW ess.analyze_sample_export;
SHOW ess.analyze_sample_import;

CREATE TABLE ess_analyze_sample_cache_test
(
    id integer,
    category text,
    bucket integer
);
INSERT INTO ess_analyze_sample_cache_test
SELECT i, 'category-' || (i % 17), i % 5
FROM generate_series(1, 2000) AS s(i);
ALTER TABLE ess_analyze_sample_cache_test
    ALTER COLUMN category SET STATISTICS 1,
    ALTER COLUMN bucket SET STATISTICS 1;
CREATE STATISTICS ess_analyze_sample_cache_mcv (mcv)
    ON category, bucket FROM ess_analyze_sample_cache_test;
ALTER STATISTICS ess_analyze_sample_cache_mcv SET STATISTICS 1;

SET ess.analyze_sample_export = :'samplefile';
ANALYZE ess_analyze_sample_cache_test;
SELECT (pg_stat_file(:'samplefile')).size > 0 AS sample_file_written;
SET ess.analyze_sample_export = '';

CREATE TEMP TABLE ess_analyze_sample_cache_ordinary_before AS
SELECT a.attname,
       md5(row_to_json(s)::text) AS stat_hash
FROM pg_statistic s
JOIN pg_attribute a ON a.attrelid = s.starelid AND a.attnum = s.staattnum
WHERE s.starelid = 'ess_analyze_sample_cache_test'::regclass
ORDER BY a.attnum;
CREATE TEMP TABLE ess_analyze_sample_cache_mcv_before AS
SELECT md5(stxdmcv::text) AS mcv_hash
FROM pg_statistic_ext e
JOIN pg_statistic_ext_data d ON d.stxoid = e.oid
WHERE e.stxrelid = 'ess_analyze_sample_cache_test'::regclass
  AND e.stxname = 'ess_analyze_sample_cache_mcv';

SET ess.analyze_sample_import = :'samplefile';
ANALYZE ess_analyze_sample_cache_test;
SELECT NOT EXISTS (
    SELECT * FROM ess_analyze_sample_cache_ordinary_before
    EXCEPT
    SELECT a.attname,
           md5(row_to_json(s)::text)
    FROM pg_statistic s
    JOIN pg_attribute a ON a.attrelid = s.starelid AND a.attnum = s.staattnum
    WHERE s.starelid = 'ess_analyze_sample_cache_test'::regclass
) AS ordinary_statistics_replayed;
SELECT NOT EXISTS (
    SELECT * FROM ess_analyze_sample_cache_mcv_before
    EXCEPT
    SELECT md5(stxdmcv::text)
    FROM pg_statistic_ext e
    JOIN pg_statistic_ext_data d ON d.stxoid = e.oid
    WHERE e.stxrelid = 'ess_analyze_sample_cache_test'::regclass
      AND e.stxname = 'ess_analyze_sample_cache_mcv'
) AS pairwise_mcv_replayed;

-- A second replay must use the same tuple bytes/order, not resample.
ANALYZE ess_analyze_sample_cache_test;
SELECT NOT EXISTS (
    SELECT * FROM ess_analyze_sample_cache_ordinary_before
    EXCEPT
    SELECT a.attname,
           md5(row_to_json(s)::text)
    FROM pg_statistic s
    JOIN pg_attribute a ON a.attrelid = s.starelid AND a.attnum = s.staattnum
    WHERE s.starelid = 'ess_analyze_sample_cache_test'::regclass
) AS second_replay_identical;

-- The two paths are mutually exclusive at ANALYZE execution time.
SET ess.analyze_sample_export = :'samplefile';
ANALYZE ess_analyze_sample_cache_test;
SET ess.analyze_sample_export = '';

-- A format-version mismatch is rejected.
\! cp "$ESS_SAMPLEFILE" "$ESS_CORRUPTFILE"
\! dd if=/dev/zero of="$ESS_CORRUPTFILE" bs=1 seek=8 count=1 conv=notrunc status=none
SET ess.analyze_sample_import = :'corruptfile';
ANALYZE ess_analyze_sample_cache_test;
SET ess.analyze_sample_import = :'samplefile';

-- Corruption is rejected rather than falling back to a fresh sample.
\! cp "$ESS_SAMPLEFILE" "$ESS_CORRUPTFILE"
\! truncate -s 1 "$ESS_CORRUPTFILE"
SET ess.analyze_sample_import = :'corruptfile';
ANALYZE ess_analyze_sample_cache_test;
SET ess.analyze_sample_import = :'samplefile';

-- A descriptor change is rejected.
ALTER TABLE ess_analyze_sample_cache_test ADD COLUMN added_column integer;
ANALYZE ess_analyze_sample_cache_test;
SET ess.analyze_sample_import = '';
ALTER TABLE ess_analyze_sample_cache_test DROP COLUMN added_column;

-- A request larger than the captured target is rejected as insufficient.
ALTER TABLE ess_analyze_sample_cache_test
    ALTER COLUMN category SET STATISTICS 1000;
SET ess.analyze_sample_import = :'samplefile';
ANALYZE ess_analyze_sample_cache_test;
ALTER TABLE ess_analyze_sample_cache_test
    ALTER COLUMN category SET STATISTICS 1;
SET ess.analyze_sample_import = '';

-- Partitioned/inherited relations are outside the binary sample scope.
CREATE TABLE ess_analyze_sample_cache_partitioned (id integer)
    PARTITION BY RANGE (id);
SET ess.analyze_sample_export = :'samplefile';
ANALYZE ess_analyze_sample_cache_partitioned;
SET ess.analyze_sample_export = '';
DROP TABLE ess_analyze_sample_cache_partitioned;

DROP TABLE ess_analyze_sample_cache_test;
\! rm -f "$ESS_SAMPLEFILE" "$ESS_CORRUPTFILE"
