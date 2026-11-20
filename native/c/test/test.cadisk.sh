#!/usr/bin/env bash
# Manual end-to-end test for CA Disk archival and restore via zo.
#
# Prereqs:
#   - CA Disk must be installed and active on the LPAR
#   - DARCHIVE TSO command must be available in your STEPLIB/LNKLST
#   - zo binary must be on PATH (or set ZOWEX env var)
#
# Usage:
#   ./test.cadisk.sh [HLQ]
#
# HLQ defaults to your TSO user ID (uppercased). The test data set will be
# named <HLQ>.CADISK.TEST and is deleted automatically on exit.
#
# Expected total runtime: 30-45 minutes (CA Disk deferred archival schedule)

set -euo pipefail

export _BPXK_AUTOCVT=ON
export _CEE_RUNOPTS="${_CEE_RUNOPTS:-} FILETAG(AUTOCVT,AUTOTAG) POSIX(ON)"
export _TAG_REDIR_ERR=txt
export _TAG_REDIR_IN=txt
export _TAG_REDIR_OUT=txt

ZO="${ZOWEX:-zo}"
HLQ="${1:-$(whoami | tr '[:lower:]' '[:upper:]')}"
DS="${HLQ}.CADISK.TEST"
CONTENT="cadisk archival test content"
MAX_POLLS=23      # 23 x 120s = ~46 minutes max
POLL_INTERVAL=120 # seconds between volser checks

SEPARATOR="=========================================="

pass() { echo "  ..passed!"; }
fail() {
	local msg="$1"
	echo "  ..FAILED: $msg"
	exit 1
}

cleanup() {
	echo ""
	echo "--- Cleanup ---"
	"$ZO" data-set delete "$DS" 2>/dev/null && echo "Deleted $DS" || echo "$DS already gone (expected after archival)"
}
trap cleanup EXIT

echo "$SEPARATOR"
echo " CA Disk Archival / Restore — Manual Test"
echo "$SEPARATOR"
echo "  Data set : $DS"
echo "  Max wait : $((MAX_POLLS * POLL_INTERVAL / 60)) minutes"
echo ""

# ── 1. Create data set ────────────────────────────────────────────────────────
echo "[1/6] Creating data set..."
"$ZO" data-set create "$DS" --dsorg PS
pass

# ── 2. Write content ──────────────────────────────────────────────────────────
echo "[2/6] Writing content..."
echo "$CONTENT" | "$ZO" data-set write "$DS"
pass

# ── 3. Submit deferred archive request ───────────────────────────────────────
echo "[3/6] Submitting DARCHIVE request..."
"$ZO" tso issue "DARCHIVE $DS"
pass

# ── 4. Poll for archival completion ──────────────────────────────────────────
echo "[4/6] Polling for archival completion (CA Disk deferred — may take ~30 min)..."
archived=false
for i in $(seq 1 "$MAX_POLLS"); do
	printf "  Poll %d/%d..." "$i" "$MAX_POLLS"
	list_out=$("$ZO" data-set list "$DS" -a --rfc 2>&1) || true
	if echo "$list_out" | grep -q "ARCIVE"; then
		archived=true
		echo " ARCIVE volser detected"
		break
	fi
	echo " not yet"
	sleep "$POLL_INTERVAL"
done

if [[ "$archived" != "true" ]]; then
	fail "data set was not archived within $((MAX_POLLS * POLL_INTERVAL / 60)) minutes"
fi
pass

# ── 5. Restore ────────────────────────────────────────────────────────────────
echo "[5/6] Restoring archived data set..."
restore_out=$("$ZO" data-set restore "$DS" 2>&1)
echo "  $restore_out"
if ! echo "$restore_out" | grep -qi "restored"; then
	fail "restore command did not report success"
fi
pass

# ── 6. Verify content ────────────────────────────────────────────────────────
echo "[6/6] Verifying restored content..."
view_out=$("$ZO" data-set view "$DS" 2>&1)
if ! echo "$view_out" | grep -Fq "$CONTENT"; then
	fail "content not found after restore (got: $view_out)"
fi
pass

echo ""
echo "$SEPARATOR"
echo " All CA Disk tests passed!"
echo "$SEPARATOR"
