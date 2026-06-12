#!/usr/bin/env python3
"""Prepare a flashed DietPi SD card for Braillatron bootstrap.

Run on your PC after flashing DietPi and *before* the Pi's first boot (or to
repair a card whose root partition already expanded to fill the disk).

Actions:
  1. Find a removable ~32 GB micro SD card (never the system disk).
  2. Expand the root partition to fill the card minus a 768 MB /data tail.
  3. Create /dietpi_skip_partition_resize so DietPi does not re-expand to 100%.
  4. Verify >= 768 MB unallocated space remains at the disk tail (for /data).
  5. Optionally shrink an over-expanded root partition (--shrink-if-needed).

Requires root: sudo python3 deploy/prepare-sd-card.py
"""

from __future__ import annotations

import argparse
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable, Sequence

DATA_TAIL_MB = 768
TAIL_MARGIN_MB = 32
SKIP_FILE = "dietpi_skip_partition_resize"
DIETPI_MARKERS = (
    "boot/dietpi.txt",
    "boot/dietpiEnv.txt",
    "var/lib/dietpi",
)
DIETPI_SETUP_LABEL = "DIETPISETUP"
DIETPI_SETUP_FILES = (
    "dietpi.txt",
    "dietpi-wifi.txt",
    "dietpiEnv.txt",
    "boot.ini",
    "extlinux.conf",
    "Automation_Custom_PreScript.sh",
    "Automation_Custom_Script.sh",
)


@dataclass(frozen=True)
class DiskInfo:
    path: str
    size_bytes: int
    model: str
    removable: bool


@dataclass(frozen=True)
class PartitionInfo:
    path: str
    number: int
    fstype: str | None
    size_bytes: int
    mountpoints: tuple[str, ...]


class PrepareError(Exception):
    pass


def run(
    cmd: Sequence[str],
    *,
    check: bool = True,
    capture: bool = True,
    text: bool = True,
) -> subprocess.CompletedProcess[str]:
    if not capture:
        return subprocess.run(cmd, check=check, text=text)
    return subprocess.run(cmd, check=check, capture_output=capture, text=text)


def require_root() -> None:
    if os.geteuid() != 0:
        raise PrepareError("This script must run as root (use sudo).")


def require_tools(*names: str) -> None:
    missing = [name for name in names if shutil.which(name) is None]
    if missing:
        raise PrepareError(f"Missing required tools: {', '.join(missing)}")


def system_root_disk() -> str | None:
    try:
        proc = run(["findmnt", "-n", "-o", "SOURCE", "/"])
    except subprocess.CalledProcessError:
        return None
    source = proc.stdout.strip()
    if not source:
        return None
    # Strip partition suffix and LUKS mapper suffix.
    source = re.sub(r"p[0-9]+$", "", source)
    source = re.sub(r"\[.*\]$", "", source)
    if source.startswith("/dev/mapper/"):
        try:
            proc = run(["lsblk", "-no", "PKNAME", source])
        except subprocess.CalledProcessError:
            return source
        parent = proc.stdout.strip()
        return f"/dev/{parent}" if parent and not parent.startswith("/dev/") else parent or source
    return source


def lsblk_tree() -> list[dict]:
    proc = run(
        [
            "lsblk",
            "-J",
            "-b",
            "-o",
            "NAME,PATH,SIZE,RM,TYPE,MOUNTPOINTS,FSTYPE,PKNAME,LABEL,MODEL",
        ]
    )
    data = json.loads(proc.stdout)
    return data.get("blockdevices", [])


def flatten(nodes: Iterable[dict]) -> Iterable[dict]:
    for node in nodes:
        yield node
        for child in node.get("children") or []:
            yield from flatten([child])


def normalize_mountpoints(raw: object) -> tuple[str, ...]:
    if not raw:
        return ()
    if isinstance(raw, list):
        points = []
        for item in raw:
            if not item:
                continue
            text = str(item).strip()
            if text:
                points.append(text)
        return tuple(points)
    return (str(raw).strip(),) if str(raw).strip() else ()


def disk_candidates(target_gb: float, tolerance_gb: float) -> list[DiskInfo]:
    system_disk = system_root_disk()
    lo = int((target_gb - tolerance_gb) * 1_000_000_000)
    hi = int((target_gb + tolerance_gb) * 1_000_000_000)
    found: list[DiskInfo] = []

    for node in lsblk_tree():
        if node.get("type") != "disk":
            continue
        path = node.get("path") or ""
        if not path.startswith("/dev/"):
            continue
        if path.startswith("/dev/zram") or path.startswith("/dev/loop"):
            continue
        if system_disk and (path == system_disk or path.startswith(system_disk)):
            continue
        size = int(node.get("size") or 0)
        removable = bool(node.get("rm"))
        model = (node.get("model") or "").strip()
        if not removable and size < hi:
            # USB SD readers often report RM=0; still allow typical card sizes.
            if size < lo or size > hi:
                continue
        elif removable:
            if size < lo or size > hi:
                continue
        else:
            continue
        found.append(DiskInfo(path=path, size_bytes=size, model=model, removable=removable))

    return sorted(found, key=lambda d: d.path)


def partitions_for_disk(disk_path: str) -> list[PartitionInfo]:
    disk_name = Path(disk_path).name
    parts: list[PartitionInfo] = []
    for node in flatten(lsblk_tree()):
        if node.get("type") != "part":
            continue
        if node.get("pkname") != disk_name:
            continue
        path = node.get("path") or ""
        number = int(re.sub(r"^.*p", "", Path(path).name)) if "mmcblk" in disk_name else int(
            re.sub(r"^.*", "", Path(path).name.replace(disk_name, "")) or "0"
        )
        if number <= 0:
            m = re.search(r"p?(\d+)$", Path(path).name)
            number = int(m.group(1)) if m else 0
        parts.append(
            PartitionInfo(
                path=path,
                number=number,
                fstype=(node.get("fstype") or None),
                size_bytes=int(node.get("size") or 0),
                mountpoints=normalize_mountpoints(node.get("mountpoints")),
            )
        )
    return sorted(parts, key=lambda p: p.number)


def parse_mb(value: str) -> int:
    cleaned = value.replace("MB", "").replace("MiB", "").strip()
    return int(float(cleaned))


def parted_script(disk_path: str, *args: str) -> None:
    run(["parted", "-f", "-s", disk_path, *args], capture=False)


def parted_print(disk_path: str, *extra: str) -> str:
    proc = run(["parted", "-m", "-s", disk_path, "unit", "MB", *extra, "print"])
    return proc.stdout


def parted_print_free(disk_path: str) -> str:
    proc = run(["parted", "-m", "-s", disk_path, "unit", "MB", "print", "free"])
    return proc.stdout


def tail_free_mb(disk_path: str) -> int:
    tail = 0
    for line in parted_print_free(disk_path).splitlines():
        line = line.rstrip(";")
        if not line.endswith(":free"):
            continue
        fields = line.split(":")
        if len(fields) < 4:
            continue
        try:
            tail = parse_mb(fields[3])
        except ValueError:
            continue
    return tail


def disk_end_mb(disk_path: str) -> int:
    for line in parted_print(disk_path).splitlines():
        line = line.rstrip(";")
        if not line.startswith(f"{disk_path}:"):
            continue
        fields = line.split(":")
        # Disk line: path:size:transport:logical:physical:table:model
        if len(fields) >= 2:
            try:
                return parse_mb(fields[1])
            except ValueError:
                pass

    try:
        proc = run(["blockdev", "--getsize64", disk_path])
        return int(proc.stdout.strip()) // (1024 * 1024)
    except (subprocess.CalledProcessError, ValueError):
        pass
    raise PrepareError(f"Unable to read disk size for {disk_path}")


def parted_extents(disk_path: str) -> list[tuple[int, int, int, str]]:
    rows: list[tuple[int, int, int, str]] = []
    for line in parted_print(disk_path).splitlines():
        line = line.rstrip(";")
        if not re.match(r"^[0-9]+:", line):
            continue
        fields = line.split(":")
        if len(fields) < 5:
            continue
        try:
            number = int(fields[0])
            start = parse_mb(fields[1])
            end = parse_mb(fields[2])
        except ValueError:
            continue
        fstype = fields[4]
        rows.append((number, start, end, fstype))
    return rows


def is_dietpi_root(root: Path) -> bool:
    return any((root / marker).exists() for marker in DIETPI_MARKERS)


def find_root_partition(disk_path: str) -> PartitionInfo:
    parts = [p for p in partitions_for_disk(disk_path) if p.fstype == "ext4"]
    if not parts:
        raise PrepareError(f"No ext4 partition found on {disk_path}")

    for part in sorted(parts, key=lambda p: p.size_bytes, reverse=True):
        mount = existing_mount(part)
        if mount and is_dietpi_root(mount):
            return part

    with tempfile.TemporaryDirectory(prefix="braillatron-sd-") as tmp:
        for part in sorted(parts, key=lambda p: p.size_bytes, reverse=True):
            mp = Path(tmp) / f"part{part.number}"
            mp.mkdir()
            try:
                run(["mount", "-o", "ro", part.path, str(mp)])
            except subprocess.CalledProcessError:
                continue
            try:
                if is_dietpi_root(mp):
                    return part
            finally:
                run(["umount", str(mp)], capture=False)

    # Fall back to the largest ext4 partition.
    return max(parts, key=lambda p: p.size_bytes)


def existing_mount(part: PartitionInfo) -> Path | None:
    for point in part.mountpoints:
        if point:
            return Path(point)
    return None


def mount_partition(part: PartitionInfo) -> tuple[Path, bool]:
    existing = existing_mount(part)
    if existing:
        return existing, False
    mp = Path(tempfile.mkdtemp(prefix="braillatron-sd-"))
    run(["mount", "-o", "rw", part.path, str(mp)])
    return mp, True


def umount_path(path: Path, *, lazy: bool = False) -> None:
    cmd = ["umount"]
    if lazy:
        cmd.append("-l")
    cmd.append(str(path))
    run(cmd, capture=False)


def unmount_disk(disk_path: str) -> None:
    for part in partitions_for_disk(disk_path):
        for mp in part.mountpoints:
            if mp:
                umount_path(Path(mp))


def partition_label(part_path: str) -> str | None:
    try:
        proc = run(["blkid", "-s", "LABEL", "-o", "value", part_path])
    except subprocess.CalledProcessError:
        return None
    label = proc.stdout.strip()
    return label or None


def root_target_end_mb(disk_path: str, root_part_num: int) -> int:
    disk_end = disk_end_mb(disk_path)
    target = disk_end - DATA_TAIL_MB - TAIL_MARGIN_MB
    root_extent = next((row for row in parted_extents(disk_path) if row[0] == root_part_num), None)
    if root_extent is None:
        raise PrepareError(f"Unable to locate root partition {root_part_num} on {disk_path}")

    for number, start, _end, _fstype in parted_extents(disk_path):
        if number == root_part_num:
            continue
        if start > root_extent[1] and start - 1 < target:
            target = start - 1

    for part in partitions_for_disk(disk_path):
        if partition_label(part.path) == "braillatron-data":
            extent = next((row for row in parted_extents(disk_path) if row[0] == part.number), None)
            if extent:
                target = min(target, extent[1] - 1)
    if target <= root_extent[1]:
        raise PrepareError(
            f"No room to grow root on {disk_path}. Another partition may be blocking expansion."
        )
    return target


def import_dietpi_setup_files(root_mount: Path, setup_part: PartitionInfo, *, dry_run: bool) -> None:
    setup_mount = Path(tempfile.mkdtemp(prefix="braillatron-setup-"))
    try:
        if not dry_run:
            run(["mount", "-o", "ro", setup_part.path, str(setup_mount)])
        imported = 0
        for name in DIETPI_SETUP_FILES:
            src = setup_mount / name
            if name == "extlinux.conf":
                dest = root_mount / "boot" / "extlinux" / name
            else:
                dest = root_mount / "boot" / name
            if not src.exists():
                continue
            if dry_run:
                print(f"Would import {name} from {setup_part.path} into {dest.parent}/")
                imported += 1
                continue
            dest.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(src, dest)
            print(f"Imported {name} from DietPi setup partition.")
            imported += 1
        if imported:
            print(f"Merged {imported} file(s) from {DIETPI_SETUP_LABEL} into /boot.")
    finally:
        if not dry_run:
            run(["umount", str(setup_mount)], capture=False)
        setup_mount.rmdir()


def remove_blocking_partitions(
    disk_path: str,
    root_part: PartitionInfo,
    *,
    dry_run: bool,
) -> None:
    root_extent = next((row for row in parted_extents(disk_path) if row[0] == root_part.number), None)
    if root_extent is None:
        raise PrepareError(f"Unable to locate partition extents for {root_part.path}")

    to_remove: list[PartitionInfo] = []
    for part in partitions_for_disk(disk_path):
        if part.number == root_part.number:
            continue
        if partition_label(part.path) == "braillatron-data":
            continue
        extent = next((row for row in parted_extents(disk_path) if row[0] == part.number), None)
        if extent is None or extent[1] <= root_extent[1]:
            continue
        to_remove.append(part)

    if not to_remove:
        return

    setup_parts = [part for part in to_remove if partition_label(part.path) == DIETPI_SETUP_LABEL]
    if setup_parts:
        if dry_run:
            for part in setup_parts:
                print(f"Would import config files from {part.path} ({DIETPI_SETUP_LABEL}) into /boot.")
        else:
            root_mount, owned_root_mount = mount_partition(root_part)
            try:
                for part in setup_parts:
                    import_dietpi_setup_files(root_mount, part, dry_run=False)
            finally:
                if owned_root_mount:
                    umount_path(root_mount)
                    root_mount.rmdir()

    for part in sorted(to_remove, key=lambda item: item.number, reverse=True):
        label = partition_label(part.path) or "unlabeled"
        print(f"Removing blocking partition {part.number} ({label}) at {part.path}...")
        if not dry_run:
            parted_script(disk_path, "rm", str(part.number))


def expand_root_partition(
    disk_path: str,
    root_part: PartitionInfo,
    *,
    dry_run: bool,
) -> None:
    if dry_run:
        remove_blocking_partitions(disk_path, root_part, dry_run=True)
        target_end = disk_end_mb(disk_path) - DATA_TAIL_MB - TAIL_MARGIN_MB
    else:
        remove_blocking_partitions(disk_path, root_part, dry_run=False)
        unmount_disk(disk_path)
        target_end = root_target_end_mb(disk_path, root_part.number)

    root_extent = next((row for row in parted_extents(disk_path) if row[0] == root_part.number), None)
    if root_extent is None:
        raise PrepareError(f"Unable to locate partition extents for {root_part.path}")

    current_end = root_extent[2]
    if current_end >= target_end - 16:
        print(f"Root partition already ends at {current_end} MB (target ~{target_end} MB).")
        return

    print(
        f"Expanding {root_part.path} from {current_end} MB to {target_end} MB "
        f"while reserving {DATA_TAIL_MB} MB tail for /data..."
    )
    if dry_run:
        return

    run(["e2fsck", "-fy", root_part.path], capture=False)
    parted_script(disk_path, "resizepart", str(root_part.number), f"{target_end}MB")
    run(["partprobe", disk_path], capture=False)
    run(["resize2fs", root_part.path], capture=False)

    print(f"Root expanded to ~{target_end} MB.")


def ensure_skip_file(root_mount: Path, dry_run: bool) -> None:
    target = root_mount / SKIP_FILE
    if target.exists():
        print(f"OK: {SKIP_FILE} already present on {root_mount}")
        return
    print(f"Creating {target}")
    if not dry_run:
        target.touch()


def format_size_gb(size_bytes: int) -> str:
    return f"{size_bytes / 1_000_000_000:.2f} GB"


def choose_disk(candidates: list[DiskInfo], forced: str | None) -> DiskInfo:
    if forced:
        for disk in candidates:
            if disk.path == forced:
                return disk
        raise PrepareError(f"--disk {forced} not found among candidates")
    if not candidates:
        raise PrepareError(
            "No removable ~32 GB SD card found. Insert the card, then re-run with --list."
        )
    if len(candidates) == 1:
        return candidates[0]
    print("Multiple candidate disks:")
    for idx, disk in enumerate(candidates, start=1):
        flags = "removable" if disk.removable else "fixed (USB reader?)"
        model = f" [{disk.model}]" if disk.model else ""
        print(f"  {idx}. {disk.path}  {format_size_gb(disk.size_bytes)}  {flags}{model}")
    raise PrepareError("Pass --disk /dev/... to select the SD card.")


def shrink_root_partition(
    disk_path: str,
    root_part: PartitionInfo,
    *,
    dry_run: bool,
) -> None:
    tail = tail_free_mb(disk_path)
    if tail >= DATA_TAIL_MB:
        print(f"Tail space already OK ({tail} MB free); no shrink needed.")
        return

    target_end = root_target_end_mb(disk_path, root_part.number)

    extents = parted_extents(disk_path)
    root_extent = next((row for row in extents if row[0] == root_part.number), None)
    if root_extent is None:
        raise PrepareError(f"Unable to locate partition extents for {root_part.path}")

    current_end = root_extent[2]
    if current_end <= target_end:
        print(
            f"Partition end ({current_end} MB) already leaves room; "
            f"tail reports {tail} MB (may need partprobe)."
        )
        return

    print(
        f"Shrinking {root_part.path} from {current_end} MB to {target_end} MB "
        f"to reserve {DATA_TAIL_MB} MB tail for /data..."
    )
    if dry_run:
        return

    unmount_disk(disk_path)
    run(["e2fsck", "-fy", root_part.path], capture=False)
    # Shrink filesystem first, then the partition table entry.
    run(["resize2fs", root_part.path, f"{target_end}M"], capture=False)
    parted_script(disk_path, "resizepart", str(root_part.number), f"{target_end}MB")
    run(["partprobe", disk_path], capture=False)
    new_tail = tail_free_mb(disk_path)
    if new_tail < DATA_TAIL_MB:
        raise PrepareError(
            f"Shrink finished but tail space is still only {new_tail} MB (need {DATA_TAIL_MB} MB)."
        )
    print(f"Shrink complete; tail space is now {new_tail} MB.")


def prepare(
    *,
    target_gb: float,
    tolerance_gb: float,
    disk_path: str | None,
    shrink_if_needed: bool,
    dry_run: bool,
    yes: bool,
) -> None:
    require_root()
    require_tools("lsblk", "parted", "mount", "umount", "findmnt")

    candidates = disk_candidates(target_gb, tolerance_gb)
    disk = choose_disk(candidates, disk_path)

    print(f"Using disk {disk.path} ({format_size_gb(disk.size_bytes)})")
    if not yes and not dry_run:
        answer = input("Proceed with this disk? [y/N] ").strip().lower()
        if answer not in {"y", "yes"}:
            print("Aborted.")
            return

    root_part = find_root_partition(disk.path)

    if shrink_if_needed:
        require_tools("e2fsck", "resize2fs", "partprobe")
        shrink_root_partition(disk.path, root_part, dry_run=dry_run)

    require_tools("e2fsck", "resize2fs", "partprobe", "blkid")
    expand_root_partition(disk.path, root_part, dry_run=dry_run)

    tail = tail_free_mb(disk.path)
    print(f"Unallocated tail space: {tail} MB (need >= {DATA_TAIL_MB} MB)")
    if tail < DATA_TAIL_MB and not shrink_if_needed:
        raise PrepareError(
            f"Only {tail} MB free at the end of {disk.path}. "
            "Re-flash DietPi, run this script before first boot, or re-run with --shrink-if-needed."
        )

    print(f"DietPi root partition: {root_part.path}")
    root_mount, owned_mount = mount_partition(root_part)
    try:
        if not is_dietpi_root(root_mount):
            raise PrepareError(
                f"{root_mount} does not look like DietPi (missing {', '.join(DIETPI_MARKERS)})."
            )
        ensure_skip_file(root_mount, dry_run=dry_run)
    finally:
        if owned_mount:
            umount_path(root_mount)
            root_mount.rmdir()

    print()
    print("SD card is ready for the Pi.")
    print("Next steps:")
    print("  1. Safely eject the card and insert it into the Orange Pi.")
    print("  2. Power on, SSH in, and verify tail space:")
    print('     DISK="$(findmnt -n -o SOURCE / | sed \'s/p[0-9]*$//\')"')
    print('     sudo parted -ms "${DISK}" unit MB print free')
    print("  3. Run bootstrap on the Pi:")
    print("     cd ~/braillatron/mobile-braille-embosser")
    print("     sudo bash deploy/bootstrap-dietpi.sh")


def list_candidates(target_gb: float, tolerance_gb: float) -> None:
    candidates = disk_candidates(target_gb, tolerance_gb)
    if not candidates:
        print("No candidate SD cards found.")
        return
    system_disk = system_root_disk()
    if system_disk:
        print(f"System disk (excluded): {system_disk}")
    for disk in candidates:
        tail = "?"
        try:
            tail = f"{tail_free_mb(disk.path)} MB"
        except subprocess.CalledProcessError:
            pass
        flags = "removable" if disk.removable else "fixed"
        model = f" [{disk.model}]" if disk.model else ""
        print(
            f"{disk.path}  {format_size_gb(disk.size_bytes)}  tail={tail}  {flags}{model}"
        )


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Prepare a flashed DietPi SD card for Braillatron (/data tail space)."
    )
    parser.add_argument(
        "--list",
        action="store_true",
        help="List candidate SD cards and exit.",
    )
    parser.add_argument(
        "--disk",
        metavar="PATH",
        help="Use this block device (e.g. /dev/mmcblk0).",
    )
    parser.add_argument(
        "--target-gb",
        type=float,
        default=32.0,
        help="Expected card size in GB (default: 32).",
    )
    parser.add_argument(
        "--tolerance-gb",
        type=float,
        default=4.0,
        help="Size matching tolerance in GB (default: 4).",
    )
    parser.add_argument(
        "--shrink-if-needed",
        action="store_true",
        help="Shrink an over-expanded root partition to free 768 MB at the disk tail.",
    )
    parser.add_argument(
        "--dry-run",
        action="store_true",
        help="Show planned actions without writing changes.",
    )
    parser.add_argument(
        "-y",
        "--yes",
        action="store_true",
        help="Do not prompt before modifying the selected disk.",
    )
    return parser


def main(argv: Sequence[str] | None = None) -> int:
    parser = build_parser()
    args = parser.parse_args(argv)
    try:
        if args.list:
            list_candidates(args.target_gb, args.tolerance_gb)
            return 0
        prepare(
            target_gb=args.target_gb,
            tolerance_gb=args.tolerance_gb,
            disk_path=args.disk,
            shrink_if_needed=args.shrink_if_needed,
            dry_run=args.dry_run,
            yes=args.yes,
        )
        return 0
    except PrepareError as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1
    except subprocess.CalledProcessError as exc:
        cmd = " ".join(exc.cmd) if exc.cmd else "command"
        print(f"error: {cmd} failed with exit code {exc.returncode}", file=sys.stderr)
        return exc.returncode or 1


if __name__ == "__main__":
    raise SystemExit(main())
