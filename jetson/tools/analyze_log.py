#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""SAVER CSV 로그를 측정 mark 구간별로 분석한다.

Python 3.6 및 표준 라이브러리만 사용한다.
결측값은 통계에서 제외하며 절대로 0으로 대체하지 않는다.
"""
import argparse
import csv
import json
import math
import os
import sys


META_FIELDS = {
    "ts_ms", "source", "mark_label", "mark_note", "mode", "state", "substate",
    "decision", "recommendation", "err_code",
}
COMPRESSION_FIELDS = (
    "cp_rate_cpm", "cp_depth_mm", "cp_force_N", "cp_motor_i_A",
)


def _is_false(value):
    return str(value).strip().lower() in ("0", "false", "no", "off")


def number(row, field):
    """CSV 값을 float로 읽는다. 빈 값과 *_ok=false는 None이다."""
    raw = row.get(field)
    if raw is None or str(raw).strip() == "":
        return None
    ok_field = field + "_ok"
    if ok_field in row and _is_false(row.get(ok_field)):
        return None
    try:
        value = float(raw)
    except (TypeError, ValueError):
        return None
    if not math.isfinite(value):
        return None
    return value


def timestamp_ms(row):
    return number(row, "ts_ms")


def read_segments(path):
    """mark 행은 다음 구간의 시작이다. mark 이전 행은 unmarked로 보존한다."""
    with open(path, "r", newline="", encoding="utf-8-sig") as fp:
        reader = csv.DictReader(fp)
        fieldnames = reader.fieldnames or []
        segments = []
        current = None
        unmarked = []
        for row in reader:
            if row.get("source") == "mark":
                if current is not None:
                    segments.append(current)
                elif unmarked:
                    segments.append({"label": "unmarked", "note": "",
                                     "mark_ts_ms": timestamp_ms(unmarked[0]),
                                     "rows": unmarked})
                    unmarked = []
                current = {
                    "label": (row.get("mark_label") or row.get("decision") or "mark").strip(),
                    "note": (row.get("mark_note") or "").strip(),
                    "mark_ts_ms": timestamp_ms(row),
                    "rows": [],
                }
            elif current is None:
                unmarked.append(row)
            else:
                current["rows"].append(row)
        if current is not None:
            segments.append(current)
        elif unmarked:
            segments.append({"label": "unmarked", "note": "",
                             "mark_ts_ms": timestamp_ms(unmarked[0]),
                             "rows": unmarked})
    return fieldnames, segments


def summarize(values):
    values = [v for v in values if v is not None]
    if not values:
        return None
    mean = sum(values) / float(len(values))
    variance = sum((v - mean) ** 2 for v in values) / float(len(values))
    return {
        "n": len(values), "min": min(values), "max": max(values),
        "mean": mean, "stddev": math.sqrt(variance),
    }


def numeric_summaries(fieldnames, rows):
    result = []
    for field in fieldnames:
        if field in META_FIELDS or field.endswith("_ok"):
            continue
        summary = summarize([number(row, field) for row in rows])
        if summary is not None:
            result.append((field, summary))
    return result


def _timed_values(rows, field):
    values = []
    for row in rows:
        ts = timestamp_ms(row)
        value = number(row, field)
        if ts is not None and value is not None:
            values.append((ts, value))
    values.sort(key=lambda item: item[0])
    return values


def vacuum_metrics(segment, target_kpa, tolerance_kpa):
    values = _timed_values(segment["rows"], "vac_kPa")
    result = {"samples": len(values), "reach_s": None, "leak_kpa_min": None}
    if not values or target_kpa is None:
        return result

    tolerance = abs(tolerance_kpa or 0.0)
    boundary = target_kpa + tolerance
    reached_at = None
    for ts, value in values:
        if value <= boundary:
            reached_at = ts
            break
    if reached_at is None:
        return result

    started_at = segment.get("mark_ts_ms")
    if started_at is None:
        started_at = values[0][0]
    result["reach_s"] = max(0.0, (reached_at - started_at) / 1000.0)

    # 목표에 도달한 뒤 압력이 0 쪽으로 상승하는 기울기를 누설률로 본다.
    leak_values = [(ts, value) for ts, value in values if ts >= reached_at]
    if len(leak_values) < 2:
        return result
    x0 = leak_values[0][0]
    xs = [(ts - x0) / 60000.0 for ts, value in leak_values]
    ys = [value for ts, value in leak_values]
    x_mean = sum(xs) / float(len(xs))
    y_mean = sum(ys) / float(len(ys))
    denominator = sum((x - x_mean) ** 2 for x in xs)
    if denominator > 0:
        result["leak_kpa_min"] = (
            sum((x - x_mean) * (y - y_mean) for x, y in zip(xs, ys)) /
            denominator
        )
    return result


def compression_cycles(rows):
    """cp_rate_cpm 적분 위상으로 20 Hz 요약 샘플을 사이클별로 묶는다."""
    samples = []
    for row in rows:
        ts = timestamp_ms(row)
        rate = number(row, "cp_rate_cpm")
        if ts is None or rate is None or rate <= 0:
            continue
        if not any(number(row, field) is not None for field in COMPRESSION_FIELDS[1:]):
            continue
        samples.append((ts, rate, row))
    samples.sort(key=lambda item: item[0])
    if not samples:
        return []

    grouped = []
    phase = 0.0
    previous_ts = samples[0][0]
    previous_rate = samples[0][1]
    current_index = 0
    current_rows = []
    for ts, rate, row in samples:
        if current_rows:
            elapsed_s = max(0.0, (ts - previous_ts) / 1000.0)
            phase += ((previous_rate + rate) / 2.0) * elapsed_s / 60.0
        index = int(phase)
        if index != current_index and current_rows:
            grouped.append(current_rows)
            current_rows = []
            current_index = index
        current_rows.append(row)
        previous_ts = ts
        previous_rate = rate
    if current_rows:
        grouped.append(current_rows)

    cycles = []
    for index, cycle_rows in enumerate(grouped, 1):
        start = timestamp_ms(cycle_rows[0])
        end = timestamp_ms(cycle_rows[-1])
        cycles.append({
            "cycle": index,
            "duration_s": (end - start) / 1000.0 if end is not None and start is not None else None,
            "rate_mean": _mean(cycle_rows, "cp_rate_cpm"),
            "depth_max": _maximum(cycle_rows, "cp_depth_mm"),
            "force_max": _maximum(cycle_rows, "cp_force_N"),
            "current_max": _maximum(cycle_rows, "cp_motor_i_A"),
        })
    return cycles


def _mean(rows, field):
    values = [number(row, field) for row in rows]
    values = [v for v in values if v is not None]
    return sum(values) / float(len(values)) if values else None


def _maximum(rows, field):
    values = [number(row, field) for row in rows]
    values = [v for v in values if v is not None]
    return max(values) if values else None


def load_vacuum_config(path):
    if not path or not os.path.exists(path):
        return None, None
    with open(path, "r", encoding="utf-8") as fp:
        data = json.load(fp)
    return data.get("vacuum_target_kPa"), data.get("vacuum_tol_kPa")


def _fmt(value):
    return "—" if value is None else "%.3f" % value


def print_table(headers, rows):
    text_rows = [[str(value) for value in row] for row in rows]
    widths = [len(str(header)) for header in headers]
    for row in text_rows:
        for index, value in enumerate(row):
            widths[index] = max(widths[index], len(value))
    pattern = "  ".join("%%-%ds" % width for width in widths)
    print(pattern % tuple(headers))
    print(pattern % tuple("-" * width for width in widths))
    for row in text_rows:
        print(pattern % tuple(row))


def analyze(path, target_kpa=None, tolerance_kpa=None):
    fieldnames, segments = read_segments(path)
    print("로그: %s" % path)
    print("구간: %d개" % len(segments))
    if target_kpa is not None:
        print("음압 목표: %.3f kPa, 허용오차: %.3f kPa" %
              (target_kpa, abs(tolerance_kpa or 0.0)))

    for index, segment in enumerate(segments, 1):
        print("\n[%d] %s" % (index, segment["label"]))
        if segment["note"]:
            print("조건: %s" % segment["note"])
        print("데이터 행: %d" % len(segment["rows"]))

        summaries = numeric_summaries(fieldnames, segment["rows"])
        if summaries:
            rows = []
            for field, summary in summaries:
                rows.append((field, summary["n"], _fmt(summary["min"]),
                             _fmt(summary["max"]), _fmt(summary["mean"]),
                             _fmt(summary["stddev"])))
            print_table(("field", "n", "min", "max", "mean", "stddev"), rows)
        else:
            print("수치 데이터 없음")

        vacuum = vacuum_metrics(segment, target_kpa, tolerance_kpa)
        if vacuum["samples"]:
            if target_kpa is None:
                print("음압: 목표값 미지정으로 도달 시간·누설률 계산 안 함")
            elif vacuum["reach_s"] is None:
                print("음압: 목표 허용범위에 도달하지 않음")
            else:
                print("음압: 목표 도달 %.3f s, 누설률 %s kPa/min" %
                      (vacuum["reach_s"], _fmt(vacuum["leak_kpa_min"])))

        cycles = compression_cycles(segment["rows"])
        if cycles:
            rows = []
            for cycle in cycles:
                rows.append((cycle["cycle"], _fmt(cycle["duration_s"]),
                             _fmt(cycle["rate_mean"]), _fmt(cycle["depth_max"]),
                             _fmt(cycle["force_max"]), _fmt(cycle["current_max"])))
            print("압박 사이클:")
            print_table(("cycle", "duration_s", "rate_mean", "depth_max",
                         "force_max", "current_max"), rows)
    return segments


def main(argv=None):
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    default_config = os.path.join(root, "config.json")
    ap = argparse.ArgumentParser(description="SAVER CSV 측정 로그 분석")
    ap.add_argument("csv_path")
    ap.add_argument("--config", default=default_config,
                    help="기본값: jetson/config.json (없으면 무시)")
    ap.add_argument("--vacuum-target-kpa", type=float, default=None)
    ap.add_argument("--vacuum-tol-kpa", type=float, default=None)
    a = ap.parse_args(argv)

    try:
        config_target, config_tolerance = load_vacuum_config(a.config)
        target = a.vacuum_target_kpa
        tolerance = a.vacuum_tol_kpa
        if target is None:
            target = config_target
        if tolerance is None:
            tolerance = config_tolerance
        analyze(a.csv_path, target, tolerance)
    except (OSError, ValueError, csv.Error, json.JSONDecodeError) as exc:
        print("분석 실패: %s" % exc, file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
