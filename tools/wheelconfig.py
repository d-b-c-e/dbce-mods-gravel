r"""Add or update a wheel profile in a Milestone game's WheelConfig.ini.

Milestone's UE4 racers (Gravel, MXGP, MotoGP, Ride, Supercross) only accept
wheels listed in

    <game>\<game>\Config\WindowsNoEditor\WheelConfig.ini

keyed by a section name of <PID><VID> in lowercase hex - the Logitech G920
(VID_046D, PID_C262) is [/Wheel.Config/c262046d]. Anything not listed gets no
wheel option at all. The lists were frozen at release, so no direct-drive base
of any brand is in them.

This clones a shipped block (so every field the parser expects is present, in
order) and substitutes the device-specific values.

AXIS NUMBERING (derived from the shipped data, cross-checked on the G29, the
Fanatec ClubSport base and the ClubSport V3 pedals):

    Axis1=X  Axis2=Y  Axis3=Z  Axis4=Rx  Axis5=Ry  Axis6=Rz
    Axis7=Slider0  Axis8=Slider1

The trailing "&scale&offset" maps the axis's 0..1 travel onto the pedal:
released must give 0.0 and pressed 1.0. A pedal that idles LOW needs
"&1.0&0.0"; one that idles HIGH needs "&-1.0&1.0". Logitech and Fanatec
idle high, MOZA idles low - copying a Logitech block onto a MOZA leaves the
game convinced a pedal is permanently held. Measure with
Measure-WheelAxes.ps1 rather than guessing.

    python wheelconfig.py --ini <path> --product 0006346e --name "MOZA R12" \
        --steer Axis1 --throttle Axis3 --brake Axis6 --handbrake Axis7 \
        --set Wheel_LeftShoulder=Button13 --set Wheel_RightShoulder=Button14

The file is CRLF and the game rewrites it on exit: edit only while the game
is closed. Steam's "verify integrity" restores the stock file.
"""
import argparse
import ctypes
import os
from pathlib import Path
import re
import shutil
import stat
import sys
import tempfile
import uuid

ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
ap.add_argument("--ini", required=True, help="path to WheelConfig.ini")
ap.add_argument("--product", required=True, help="<PID><VID> hex, e.g. 0006346e (= DirectInput guidProduct.Data1)")
ap.add_argument("--name", required=True, help="ProductName shown in the game")
ap.add_argument("--template", default="c262046d", help="section to clone (default: Logitech G920)")
ap.add_argument("--polarity", default="low", choices=["low", "high"], help="pedals idle low (MOZA) or high (Logitech/Fanatec)")
ap.add_argument("--steer", default="Axis1")
ap.add_argument("--throttle", default="")
ap.add_argument("--brake", default="")
ap.add_argument("--clutch", default="")
ap.add_argument("--handbrake", default="")
ap.add_argument("--rotation", type=float, default=900.0)
ap.add_argument("--ffb", type=float, default=0.5)
ap.add_argument("--set", action="append", default=[], metavar="KEY=VALUE", help="any other field, e.g. Wheel_LeftTrigger=Button34")
ap.add_argument("--clear-buttons", action="store_true", help="empty every Button field inherited from the template")
def setf(b, key, val):
    """Replace one unambiguous value span; retain key spelling and suffix bytes."""
    if not re.fullmatch(r"[A-Za-z_][A-Za-z_0-9]*", key) or any(ord(c) < 32 for c in val):
        raise ValueError("invalid key/value; control characters are refused")
    matches = list(re.finditer(rf"(?mi)^([ \t]*{re.escape(key)}[ \t]*=)([^\r\n]*)", b))
    if len(matches) != 1:
        raise ValueError(f"field {key} must occur exactly once")
    m = matches[0]
    old = m.group(2)
    comment = re.search(r"(?:^|[ \t])[;#]", old)
    end = comment.start() if comment else len(old)
    start = len(old[:end]) - len(old[:end].lstrip(" \t"))
    last = len(old[:end].rstrip(" \t"))
    # An empty value may already have whitespace; retain it just once.
    last = max(start, last)
    suffix = old[last:]
    if suffix.startswith((";", "#")) and val:
        suffix = " " + suffix
    return b[:m.start(2)] + old[:start] + val + suffix + b[m.end(2):]


def plan(raw, a):
    """Pure edit, using strict UTF-8 and byte-stable value spans like native/settings."""
    for product in (a.product, a.template):
        if not re.fullmatch(r"[0-9a-fA-F]{8}", product):
            raise ValueError("product and template must be eight hex digits")
    if len(raw) > 1024 * 1024 or b"\0" in raw:
        raise ValueError("oversized or NUL/UTF-16 input is refused")
    bom = b"\xef\xbb\xbf" if raw.startswith(b"\xef\xbb\xbf") else b""
    s = raw[len(bom):].decode("utf-8")  # Never replace damaged/unsupported bytes.
    if "\r\n" not in s or re.search(r"\r(?!\n)", s):
        raise ValueError("expected CRLF input without bare CR")
    sections = []
    offset = 0
    for line in s.splitlines(keepends=True):
        text = line.rstrip("\r\n").strip(" \t")
        if text and not text.startswith((";", "#")):
            if text.startswith("["):
                h = re.fullmatch(r"\[([^\]\r\n]+)\][ \t]*(?:[;#].*)?", text)
                if not h:
                    raise ValueError("malformed section header")
                sections.append((h.group(1).casefold(), offset))
            elif "=" not in text:
                raise ValueError("malformed INI line")
        offset += len(line)
    def section(product):
        name = "/wheel.config/" + product.casefold()
        hits = [i for i, row in enumerate(sections) if row[0] == name]
        if len(hits) > 1:
            raise ValueError(f"duplicate section {product}")
        if not hits:
            return None
        i = hits[0]
        return sections[i][1], sections[i + 1][1] if i + 1 < len(sections) else len(s)
    template, existing = section(a.template), section(a.product)
    if template is None:
        raise ValueError(f"template section {a.template} not found")
    begin, end = existing or template
    block = s[begin:end]
    if not existing:
        block = re.sub(r"(?i)(\[/Wheel\.Config/)[0-9a-f]{8}(\])",
                       lambda m: m.group(1) + a.product.lower() + m.group(2), block, count=1)
    pol = "1.0&0.0" if a.polarity == "low" else "-1.0&1.0"
    for key, value in (("ProductName", a.name), ("Layout", "Generic"),
                       ("ForceFeedback", f"{a.ffb:.6f}"), ("MaxRotationAngle", f"{a.rotation:.6f}"),
                       ("Wheel_Steer", f"{a.steer}&1.0&0.0")):
        block = setf(block, key, value)
    for key, axis in (("Wheel_Accelerator", a.throttle), ("Wheel_Brake", a.brake),
                      ("Wheel_Clutch", a.clutch), ("Wheel_Handbrake", a.handbrake)):
        block = setf(block, key, f"{axis}&{pol}" if axis else "")
    if a.clear_buttons:
        for key in re.findall(r"(?mi)^[ \t]*(Wheel_\w+)[ \t]*=[ \t]*Button\d+", block):
            block = setf(block, key, "")
    explicit = set()
    for kv in a.set:
        key, eq, value = kv.partition("=")
        key = key.strip()
        if not eq or key.casefold() in explicit:
            raise ValueError("--set requires a unique KEY=VALUE")
        explicit.add(key.casefold())
        block = setf(block, key, value.strip())
    if existing:
        s = s[:begin] + block + s[end:]
    else:
        # The complete original is a prefix, including its original EOF/newlines.
        s += ("\r\n" if s.endswith("\n") else "\r\n\r\n") + block
        if not s.endswith("\n"):
            s += "\r\n"
    return bom + s.encode("utf-8"), existing is None, block


def identity(info):
    return info.st_dev, info.st_ino, info.st_size, info.st_mtime_ns


def inspect(path):
    info = path.lstat()
    if (not stat.S_ISREG(info.st_mode) or info.st_nlink != 1 or
            getattr(info, "st_file_attributes", 0) & (stat.FILE_ATTRIBUTE_REPARSE_POINT | stat.FILE_ATTRIBUTE_READONLY)):
        raise ValueError("linked, nonregular or read-only file refused")
    if info.st_size > 1024 * 1024:
        raise ValueError("oversized file refused")
    return info


def replace(temporary, path):
    # Same existing-file primitive as native/settings: retain destination metadata/ACLs.
    if os.name == "nt":
        kernel = ctypes.WinDLL("kernel32", use_last_error=True)
        fn = kernel.ReplaceFileW
        fn.argtypes = [ctypes.c_wchar_p, ctypes.c_wchar_p, ctypes.c_wchar_p,
                       ctypes.c_ulong, ctypes.c_void_p, ctypes.c_void_p]
        fn.restype = ctypes.c_int
        if not fn(str(path), str(temporary), None, 0, None, None):
            raise ctypes.WinError(ctypes.get_last_error())
    else:
        os.replace(temporary, path)


def save(path, original, info, edited, clone):
    """Exclusive temp, flush, identity/content recheck, atomic replace; no truncation."""
    if edited == original:
        return None
    temporary = None
    backup = None
    try:
        fd, temporary = tempfile.mkstemp(prefix="." + path.name + ".dbce-", suffix=".tmp", dir=path.parent)
        with os.fdopen(fd, "wb") as f:
            f.write(edited)
            f.flush()
            os.fsync(f.fileno())
        shutil.copymode(path, temporary)
        if identity(inspect(path)) != identity(info) or path.read_bytes() != original:
            raise ValueError("source changed during preparation; refusing replacement")
        if clone:
            backup = Path(str(path) + ".bak")
            while True:
                try:
                    with backup.open("xb") as f:
                        f.write(original)
                        f.flush()
                        os.fsync(f.fileno())
                    break
                except FileExistsError:
                    backup = Path(str(path) + ".bak." + uuid.uuid4().hex)
        # As with the native helper, independently renaming editors must be excluded.
        replace(temporary, path)
        temporary = None
        return backup
    finally:
        if temporary is not None:
            os.unlink(temporary)  # Only the exact exclusive temp created by this call.


def main(argv=None):
    a = ap.parse_args(argv)
    path = Path(a.ini)
    try:
        info = inspect(path)
        original = path.read_bytes()
        if identity(inspect(path)) != identity(info):
            raise ValueError("source changed during read")
        edited, clone, block = plan(original, a)
        backup = save(path, original, info, edited, clone)
    except (OSError, ValueError, UnicodeError) as e:
        print(f"not written: {e}", file=sys.stderr)
        return 1
    if backup:
        print(f"backup: {backup}")
    count = len(re.findall(rb"\[/Wheel.Config/", edited))
    print(f"written; sections={count} "
          f"bareLF={edited.count(bytes([10])) - edited.count(bytes([13, 10]))}")
    for line in block.splitlines():
        if "=" in line and line.split("=", 1)[1].strip():
            print("   ", line)
    return 0


if __name__ == "__main__":
    sys.exit(main())
