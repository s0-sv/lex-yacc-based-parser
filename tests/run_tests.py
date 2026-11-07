import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
BIN = ROOT / "build" / "minicc"
TIMEOUT = 60


def run(cmd, stdin=""):
    return subprocess.run(cmd, input=stdin, capture_output=True, text=True, timeout=TIMEOUT)


def check_program(path, tmp):
    reference = tmp / f"{path.stem}.gcc"
    built = run(["gcc", "-std=gnu99", "-w", "-O0", str(path), "-o", str(reference), "-lm"])
    if built.returncode:
        return [f"gcc: {built.stderr}"]
    expected = run([str(reference)])
    problems = []

    interp = run([str(BIN), "--run", str(path)])
    if (interp.stdout, interp.returncode) != (expected.stdout, expected.returncode):
        problems.append(describe("інтерпретатор", expected, interp))

    native_bin = tmp / f"{path.stem}.native"
    compiled = run([str(BIN), "--build", "-o", str(native_bin), str(path)])
    if compiled.returncode:
        problems.append(f"збірка асемблера: {compiled.stderr}")
    else:
        native = run([str(native_bin)])
        if (native.stdout, native.returncode) != (expected.stdout, expected.returncode):
            problems.append(describe("асемблер", expected, native))
    return problems


def describe(name, expected, actual):
    want = expected.stdout.splitlines()
    got = actual.stdout.splitlines()
    for i in range(max(len(want), len(got))):
        left = want[i] if i < len(want) else "<кінець>"
        right = got[i] if i < len(got) else "<кінець>"
        if left != right:
            return f"{name}: рядок {i + 1}: очікувалось {left!r}, отримано {right!r}; {actual.stderr}"
    return f"{name}: код виходу {actual.returncode} замість {expected.returncode}; {actual.stderr}"


def check_error(path):
    result = run([str(BIN), "--check", str(path)])
    if result.returncode == 0:
        return ["очікувалась помилка, але аналіз пройшов"]
    if "помилка" not in result.stderr:
        return [f"немає діагностики: {result.stderr}"]
    return []


def inline_checks(tmp):
    cases = {
        "коментарі": ("int main(void) { /* a\n b */ int x = 1; // c\n return x + 1; }\n", 2, ""),
        "макрос у макросі": ("#define A B\n#define B 4\nint main(void) { return A; }\n", 4, ""),
        "конкатенація рядків": ('#include <stdio.h>\nint main(void) { printf("a" "b\\n"); return 0; }\n', 0, "ab\n"),
        "порожня програма в main": ("int main(void) { }\n", 0, ""),
        "оператори в рядку": ('#include <stdio.h>\nint main(void) { puts("/* no */ // no"); return 0; }\n', 0, "/* no */ // no\n"),
        "екранування": ('#include <stdio.h>\nint main(void) { printf("%d %d\\n", \'\\n\', \'\\x41\'); return 0; }\n', 0, "10 65\n"),
    }
    problems = []
    for name, (source, code, out) in cases.items():
        path = tmp / "inline.c"
        path.write_text(source)
        result = run([str(BIN), "--run", str(path)])
        if result.returncode != code or result.stdout != out:
            problems.append(f"{name}: код {result.returncode}, вивід {result.stdout!r}, {result.stderr}")
    stdin_path = tmp / "stdin.c"
    stdin_path.write_text('#include <stdio.h>\nint main(void) { int a; double b; char s[16]; scanf("%d %lf %s", &a, &b, s); printf("%d %.2f %s\\n", a, b, s); return 0; }\n')
    result = run([str(BIN), "--run", str(stdin_path)], "12 3.5 word\n")
    if result.stdout != "12 3.50 word\n":
        problems.append(f"scanf: {result.stdout!r} {result.stderr}")
    runtime = {
        "ділення на нуль": "int main(void) { int a = 0; return 1 / a; }\n",
        "нульовий вказівник": "int main(void) { int *p = 0; return *p; }\n",
        "нескінченний цикл": "int main(void) { while (1) { } }\n",
        "нескінченна рекурсія": "int f(int n) { return f(n + 1); }\nint main(void) { return f(0); }\n",
    }
    for name, source in runtime.items():
        path = tmp / "runtime.c"
        path.write_text(source)
        result = run([str(BIN), "--run", "--max-steps", "1000000", str(path)])
        if result.returncode != 70 or "помилка виконання" not in result.stderr:
            problems.append(f"{name}: код {result.returncode}, {result.stderr}")
    return problems


def main():
    failed = 0
    total = 0
    with tempfile.TemporaryDirectory() as raw:
        tmp = Path(raw)
        for path in sorted(list((ROOT / "tests" / "cases").glob("*.c")) + list((ROOT / "examples").glob("*.c"))):
            total += 1
            problems = check_program(path, tmp)
            print(("ok  " if not problems else "FAIL"), path.relative_to(ROOT))
            for p in problems:
                print("    ", p)
            failed += bool(problems)
        for path in sorted((ROOT / "tests" / "errors").glob("*.c")):
            total += 1
            problems = check_error(path)
            print(("ok  " if not problems else "FAIL"), path.relative_to(ROOT))
            for p in problems:
                print("    ", p)
            failed += bool(problems)
        total += 1
        problems = inline_checks(tmp)
        print(("ok  " if not problems else "FAIL"), "вбудовані перевірки")
        for p in problems:
            print("    ", p)
        failed += bool(problems)
    print(f"{total - failed}/{total} пройдено")
    sys.exit(1 if failed else 0)


main()
