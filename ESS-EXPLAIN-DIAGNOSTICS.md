# ESS EXPLAIN estimator diagnostics

Enable the research-only extension for one command with:

```sql
EXPLAIN (ESS_DIAGNOSTICS, FORMAT JSON)
SELECT ...;
```

The option defaults to off and requires JSON format. Off mode leaves the
existing EXPLAIN output unchanged. Collection is active only during planning
of the opted-in EXPLAIN query; events are reset for each rewritten query.

The JSON plan object gains an `ESS Diagnostics` object with:

* `Status`: `complete`, `incomplete`, or `empty`. `incomplete` means a
  selected MCV statistic has no matching numeric estimate record, or a
  numeric estimate has no matching selected-stat record. `empty` means no
  estimator events were produced.
* `Event Count`: number of records in `Events`.
* `Events`: ordered event objects with one-based `sequence` and `type`.

`base_estimate` events include `relation_oid`, `rte_index`, `rel_tuples`,
`selectivity`, `nrows_preclamp`, and `rows_postclamp`. The pre-clamp value is
the product immediately before `clamp_row_est()`; it is distinct from
`Plan Rows`. `mcv_selection` events identify selected statistic OIDs,
`mcv_clause` events identify claimed clause indexes, and `mcv_estimate`
events contain `clause_count`, `simple_sel`, `mcv_sel`, `mcv_basesel`,
`mcv_totalsel`, and `stat_sel`. Floating-point fields use `%.17g` decimal
formatting and are emitted as JSON numbers. If a planner hook supplies a
non-finite value, diagnostics raises an error instead of emitting invalid JSON
tokens such as `NaN` or `Infinity`.

For example, a client can read the event array from the first planned query as
`result[0]["ESS Diagnostics"]["Events"]`. Match base estimates by relation
OID and range-table index. Match MCV records by relation OID and statistic OID;
use event sequence to preserve planner order. Do not substitute `Plan Rows`
when a pre-clamp field is absent. Treat `incomplete` as an explicit failure
for consumers that require complete MCV numeric data.

The MCV numeric values are captured where the existing trace computes the
implicitly-ANDed extended-statistics estimate. MCV selections used only by
other clause forms can therefore be reported as `incomplete`; the new
interface does not synthesize those numeric values. The existing server-log
trace remains available for comparison and fallback.
