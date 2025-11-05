#!/usr/bin/env python3
import argparse
import os
import re
import shutil
import subprocess
import sys
from datetime import datetime


WORKSPACE_ROOT = "/Volumes/Samsung990/performance/PTPN"
EXAMPLE_DIR = os.path.join(WORKSPACE_ROOT, "example")
COMMON_DOT = os.path.join(EXAMPLE_DIR, "common.dot")
BUILD_DIR = os.path.join(WORKSPACE_ROOT, "build")
LOG_DIR = os.path.join(BUILD_DIR, "logs")
BIN = os.path.join(BUILD_DIR, "PTPN")

# 输出到 example 下的三个分类目录
EX_FIXED = os.path.join(EXAMPLE_DIR, "fixed_time")
EX_MULTI = os.path.join(EXAMPLE_DIR, "multi_cores")
EX_DIFF = os.path.join(EXAMPLE_DIR, "diff_exec")


# 匹配周期任务：{任务名;[周期下界,周期上界];优先级;核心;[执行时间下界,执行时间上界]}
PERIODIC_TASK_REGEX = re.compile(r"\{([^;]+);(\[[^\]]+\]);([^;]+);([^;]+);(\[[^\]]+\])\}")
# 匹配常规任务：{任务名;优先级;核心;[执行时间下界,执行时间上界]}
REGULAR_TASK_REGEX = re.compile(r"\{([^;]+);([^;]+);([^;]+);(\[[^\]]+\])\}")


def ensure_paths():
    os.makedirs(LOG_DIR, exist_ok=True)
    os.makedirs(EX_FIXED, exist_ok=True)
    os.makedirs(EX_MULTI, exist_ok=True)
    os.makedirs(EX_DIFF, exist_ok=True)


def read_text(path: str) -> str:
    with open(path, "r", encoding="utf-8") as f:
        return f.read()


def write_text(path: str, content: str) -> None:
    with open(path, "w", encoding="utf-8") as f:
        f.write(content)


def modify_exec_windows(dot_text: str, mode: str, value=None):
    """
    修改 DOT 文件中所有任务的执行时间窗口（支持周期任务和常规任务）
    mode:
      - fixed_upper: 将 [a,b] -> [b,b]
      - fixed_lower: 将 [a,b] -> [a,a]
      - fixed_mid: 将 [a,b] -> [m,m], m = round((a+b)/2)
      - fixed_value: 将 [a,b] -> [v,v] (value 为 int)
      - scale: 将 [a,b] -> [int(a*factor), int(b*factor)] (value 为 float)
      - replace: 将 [a,b] -> value (value 为字符串, 形如 "[x,y]")
    """
    
    def process_exec_window(execw: str) -> str:
        """处理执行时间窗口字符串"""
        a_b = execw.strip("[]").split(",")
        if len(a_b) != 2:
            return execw
        try:
            a = int(a_b[0])
            b = int(a_b[1])
        except ValueError:
            return execw
        
        if mode == "fixed_upper":
            new_exec = f"[{b},{b}]"
        elif mode == "fixed_lower":
            new_exec = f"[{a},{a}]"
        elif mode == "fixed_mid":
            mval = int(round((a + b) / 2))
            new_exec = f"[{mval},{mval}]"
        elif mode == "fixed_value":
            v = int(value)
            new_exec = f"[{v},{v}]"
        elif mode == "scale":
            factor = float(value)
            new_a = max(0, int(round(a * factor)))
            new_b = max(0, int(round(b * factor)))
            new_exec = f"[{new_a},{new_b}]"
        elif mode == "replace":
            new_exec = str(value)
        else:
            new_exec = execw
        return new_exec
    
    # 处理周期任务（5个属性）
    def repl_periodic(m: re.Match):
        task, period, prio, core, execw = m.groups()
        new_exec = process_exec_window(execw)
        return "{" + ";".join([task, period, prio, core, new_exec]) + "}"
    
    # 处理常规任务（4个属性）
    def repl_regular(m: re.Match):
        task, prio, core, execw = m.groups()
        new_exec = process_exec_window(execw)
        return "{" + ";".join([task, prio, core, new_exec]) + "}"
    
    # 先处理周期任务,再处理常规任务
    result = PERIODIC_TASK_REGEX.sub(repl_periodic, dot_text)
    result = REGULAR_TASK_REGEX.sub(repl_regular, result)
    return result


def run_case(dot_path: str, cpus: int, cores: int, max_states: int = None, tag: str = "") -> int:
    timestamp = datetime.now().strftime("%Y%m%d-%H%M%S")
    base = os.path.splitext(os.path.basename(dot_path))[0]
    tag_part = f"-{tag}" if tag else ""
    log_name = f"{timestamp}-{base}-c{cpus}-k{cores}{tag_part}.log"
    log_path = os.path.join(LOG_DIR, log_name)

    cmd = [BIN, "--cpus", str(cpus), "--cores", str(cores), "--file", dot_path]
    if max_states is not None:
        cmd += ["--max_states", str(max_states)]

    with open(log_path, "w", encoding="utf-8") as logf:
        logf.write("CMD: " + " ".join(cmd) + "\n\n")
        try:
            proc = subprocess.run(cmd, stdout=logf, stderr=subprocess.STDOUT, check=False)
            return proc.returncode
        except FileNotFoundError:
            logf.write("\n错误: 未找到可执行文件 PTPN,请先构建到 build/PTPN\n")
            return 127


def suite_fixed_time(max_states: int = None):
    text = read_text(COMMON_DOT)
    # 生成三种固定方式（统一使用 modify_exec_windows）
    upper_text = modify_exec_windows(text, mode="fixed_upper")  # [a,b] -> [b,b]
    lower_text = modify_exec_windows(text, mode="fixed_lower")  # [a,b] -> [a,a]
    mid_text = modify_exec_windows(text, mode="fixed_mid")      # [a,b] -> [m,m], m=round((a+b)/2)

    out_upper = os.path.join(EX_FIXED, "common_fixed_upper.dot")
    out_lower = os.path.join(EX_FIXED, "common_fixed_lower.dot")
    out_mid = os.path.join(EX_FIXED, "common_fixed_mid.dot")
    write_text(out_upper, upper_text)
    write_text(out_lower, lower_text)
    write_text(out_mid, mid_text)

    rc = 0
    rc |= run_case(out_upper, cpus=1, cores=2, max_states=max_states, tag="fixed-upper")
    rc |= run_case(out_lower, cpus=1, cores=2, max_states=max_states, tag="fixed-lower")
    rc |= run_case(out_mid,   cpus=1, cores=2, max_states=max_states, tag="fixed-mid")
    return rc


def suite_vary_topology(max_states: int = None):
    # 将原始 common.dot 拷贝为多核套件的基准文件
    base_dot = os.path.join(EX_MULTI, "common_multi_cores.dot")
    write_text(base_dot, read_text(COMMON_DOT))
    # 变更 cpus/cores 组合
    cases = [(1, 1), (1, 2), (2, 2), (2, 4)]
    rc = 0
    for c, k in cases:
        rc_case = run_case(base_dot, cpus=c, cores=k, max_states=max_states, tag=f"mc-c{c}-k{k}")
        rc = rc or rc_case
    return rc


def suite_vary_exec_window(max_states: int = None):
    text = read_text(COMMON_DOT)
    variants = {
        "common_exec_38.dot": "[3,8]",    # 原始
        "common_exec_55.dot": "[5,5]",    # 固定执行时间
        "common_exec_26.dot": "[2,6]",    # 收紧
        "common_exec_88.dot": "[8,8]",    # 固定为上界
        "common_exec_8_10.dot": "[8,10]"  # 放宽上界
    }

    rc = 0
    for fname, window in variants.items():
        mod_text = modify_exec_windows(text, mode="replace", value=window)
        out_dot = os.path.join(EX_DIFF, fname)
        write_text(out_dot, mod_text)
        tag = os.path.splitext(fname)[0]
        rc_case = run_case(out_dot, cpus=1, cores=2, max_states=max_states, tag=tag)
        rc = rc or rc_case
    return rc


def main():
    parser = argparse.ArgumentParser(description="PTPN 自动测试脚本")
    parser.add_argument("--suite", choices=["all", "fixed", "topology", "exec"], default="all")
    parser.add_argument("--max_states", type=int, default=None)
    args = parser.parse_args()

    ensure_paths()

    if not os.path.isfile(BIN):
        print(f"未找到可执行文件: {BIN},请先构建项目 (cmake && make)")
        return 127

    results = []
    if args.suite in ("all", "fixed"):
        results.append(("fixed", suite_fixed_time(args.max_states)))
    if args.suite in ("all", "topology"):
        results.append(("topology", suite_vary_topology(args.max_states)))
    if args.suite in ("all", "exec"):
        results.append(("exec", suite_vary_exec_window(args.max_states)))

    print("测试完成,结果如下:")
    ok = True
    for name, rc in results:
        status = "OK" if rc == 0 else f"FAIL({rc})"
        print(f"  - {name}: {status}")
        ok = ok and (rc == 0)

    print(f"日志目录: {LOG_DIR}")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())


