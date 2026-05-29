#!/usr/bin/env bash
# Export enrolled face embeddings from the Python project's Postgres into a
# single JSON file that the C++ app's EmbeddingIndex can load at startup.
#
# Runs a server-side SQL query via `docker exec ... psql` and writes to
# $ROOT/data/embeddings.json by default. Uses JSONB aggregation so the heavy
# lifting happens inside Postgres (fast even with ~3000 rows × 512 dims).
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUT_FILE="${1:-${ROOT_DIR}/data/embeddings.json}"
PG_CONTAINER="${PG_CONTAINER:-parking-security-postgres-1}"
PG_USER="${PG_USER:-parking}"
PG_DB="${PG_DB:-parking_security}"

mkdir -p "$(dirname "${OUT_FILE}")"

# One query, two UNIONed subqueries (employees + students) aggregated into a
# JSONB array where each entry is:
#   { person_id, person_type, display_name, external_id, embeddings: [...] }
read -r -d '' SQL <<'SQL' || true
SELECT jsonb_build_object(
  'people',
  COALESCE(jsonb_agg(row_to_json(r)::jsonb), '[]'::jsonb)
)::text
FROM (
  SELECT
    e.id::text            AS person_id,
    'employee'            AS person_type,
    e.full_name           AS display_name,
    e.employee_number     AS external_id,
    (
      SELECT COALESCE(jsonb_agg(fe.embedding), '[]'::jsonb)
      FROM face_embeddings fe
      WHERE fe.employee_id = e.id
    ) AS embeddings
  FROM employees e
  WHERE e.is_active AND EXISTS (
    SELECT 1 FROM face_embeddings fe WHERE fe.employee_id = e.id
  )
  UNION ALL
  SELECT
    s.id::text            AS person_id,
    'student'             AS person_type,
    COALESCE(s.full_name, s.student_id) AS display_name,
    s.student_id          AS external_id,
    (
      SELECT COALESCE(jsonb_agg(se.embedding), '[]'::jsonb)
      FROM student_embeddings se
      WHERE se.student_id = s.id
    ) AS embeddings
  FROM students s
  WHERE s.is_active AND EXISTS (
    SELECT 1 FROM student_embeddings se WHERE se.student_id = s.id
  )
) r
SQL

echo "Exporting embeddings from ${PG_CONTAINER} → ${OUT_FILE}"
docker exec -i "${PG_CONTAINER}" \
    psql -U "${PG_USER}" -d "${PG_DB}" -At -c "${SQL}" \
    > "${OUT_FILE}"

# Basic sanity report.
python3 - "${OUT_FILE}" <<'PY'
import json, sys
d = json.load(open(sys.argv[1]))
p = d.get("people", [])
total = sum(len(x.get("embeddings", [])) for x in p)
by_type = {}
for x in p:
    by_type[x["person_type"]] = by_type.get(x["person_type"], 0) + 1
print(f"  people: {len(p)}")
print(f"  embeddings (total): {total}")
for k, v in sorted(by_type.items()):
    print(f"  {k}: {v}")
if p and p[0].get("embeddings"):
    print(f"  first embedding length: {len(p[0]['embeddings'][0])}")
PY
