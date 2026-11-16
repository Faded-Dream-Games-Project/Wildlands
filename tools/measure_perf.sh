#!/usr/bin/env bash
# Wildlands runtime performance sampler (development tool).
# Usage: tools/measure_perf.sh <label> <binary> [args...]
#
# Runs the game, samples /proc/<pid> (process + per-thread CPU, RSS,
# thread count) every 200 ms, and prints a summary line. FPS is implied
# by the demo's frame counter (frames / elapsed) for --frames runs.
set -u
LABEL="${1:?usage: measure_perf.sh <label> <binary> [args...]}"
shift
BIN="${1:?binary}"
shift
HZ=$(getconf CLK_TCK)
LOG=$(mktemp /tmp/wl-perf-log.XXXXXX)

START=$(date +%s.%N)
"$BIN" "$@" >"$LOG" 2>&1 &
PID=$!

# per-thread CPU counters at start and (updated) during the run
declare -A T_START T_LAST
for f in /proc/$PID/task/*/stat; do
    [ -r "$f" ] || continue
    tid=${f#/proc/$PID/task/}; tid=${tid%/stat}
    read -r _ _ _ _ _ _ _ _ _ _ _ _ _ u s rest < "$f" 2>/dev/null || continue
    T_START[$tid]=$((u + s))
    T_LAST[$tid]=$((u + s))
done

cpu_sum=0; n=0; peak=0; rss_max=0; thr_max=0
pu=""; ps=""; pt=""

while kill -0 "$PID" 2>/dev/null; do
    read -r _ _ _ _ _ _ _ _ _ _ _ _ _ u s rest < "/proc/$PID/stat" 2>/dev/null || break
    now=$(date +%s.%N)
    if [ -n "$pt" ]; then
        read -r cpu dt < <(awk -v u="$u" -v s="$s" -v pu="$pu" -v ps="$ps" \
                               -v t1="$now" -v t0="$pt" -v hz="$HZ" 'BEGIN{
            dt = t1 - t0
            dc = (u + s - pu - ps) / hz
            if (dt > 0.05 && dc >= 0) printf "%.3f %.3f", dc / dt * 100, dt
        }')
        if [ -n "${cpu:-}" ]; then
            cpu_sum=$(awk -v a="$cpu_sum" -v c="$cpu" 'BEGIN{print a + c}')
            peak=$(awk -v p="$peak" -v c="$cpu" 'BEGIN{print (c > p) ? c : p}')
            n=$((n + 1))
        fi
    fi
    pu=$u; ps=$s; pt=$now

    # per-thread + rss + thread count
    for f in /proc/$PID/task/*/stat; do
        [ -r "$f" ] || continue
        tid=${f#/proc/$PID/task/}; tid=${tid%/stat}
        read -r _ _ _ _ _ _ _ _ _ _ _ _ _ tu ts rest < "$f" 2>/dev/null || continue
        T_LAST[$tid]=$((tu + ts))
    done
    rss=$(awk '/^VmRSS/{print $2}' /proc/$PID/status 2>/dev/null)
    [ -n "$rss" ] && [ "$rss" -gt "$rss_max" ] 2>/dev/null && rss_max=$rss
    nth=$(ls /proc/$PID/task 2>/dev/null | wc -l)
    [ "$nth" -gt "$thr_max" ] && thr_max=$nth
    sleep 0.2
done
wait "$PID" 2>/dev/null
RC=$?
END=$(date +%s.%N)

elapsed=$(awk -v e="$END" -v s="$START" 'BEGIN{printf "%.2f", e - s}')
avg=$(awk -v s="$cpu_sum" -v n="$n" 'BEGIN{if (n > 0) printf "%.1f", s / n; else print "n/a"}')
peakf=$(awk -v p="$peak" 'BEGIN{printf "%.1f", p}')
frames=$(grep -c 'demo pos' "$LOG" 2>/dev/null)
frames=${frames:-0}
# one diag line per 30 frames -> approximate played frames
played=$((frames * 30))

echo "------------------------------------------------------------------"
echo "[$LABEL]"
echo "  exit=$RC  wall=${elapsed}s"
echo "  process cpu: avg ${avg}%  peak ${peakf}%  (100% = one core)"
echo "  peak rss: $((rss_max / 1024)) MiB   threads: $thr_max"
if [ "$played" -gt 0 ] && awk -v e="$elapsed" 'BEGIN{exit !(e > 0.5)}'; then
    echo "  implied fps (frames/wall): $(awk -v f="$played" -v e="$elapsed" 'BEGIN{printf "%.0f", f / e}')"
fi
echo "  per-thread cpu seconds (last observed):"
for tid in "${!T_LAST[@]}"; do
    d=$(( T_LAST[$tid] - T_START[$tid] ))
    [ "$d" -gt 0 ] && echo "    tid $tid: $(awk -v d="$d" -v hz="$HZ" 'BEGIN{printf "%.2f", d / hz}')s"
done | sort -t: -k2 -rn | head -6
echo "  game log: $LOG (kept)"
