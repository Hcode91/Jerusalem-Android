#!/usr/bin/env python3
"""Actions-only emulator launch and synthetic image regression, never original playback."""
import os
from pathlib import Path
import re
import struct
import subprocess
import time
import xml.etree.ElementTree as ET

PACKAGE = 'org.jerusalem.engine'
OUTPUT = Path('smoke-output')


def adb(*args, **kwargs):
    return subprocess.run(['adb', *args], check=True, stdout=subprocess.PIPE, timeout=90, **kwargs).stdout


def wait_for(predicate, message, seconds=30):
    deadline = time.monotonic() + seconds
    while time.monotonic() < deadline:
        if predicate():
            return
        time.sleep(1)
    raise RuntimeError(message)


def control_center(xml, label):
    """Prefer stable accessibility labels; Android themes may uppercase visible text."""
    tree = ET.fromstring(xml)
    nodes = list(tree.iter('node'))
    for attribute in ('content-desc', 'text'):
        for node in nodes:
            if node.get(attribute, '').casefold() != label.casefold() or node.get('enabled') == 'false':
                continue
            bounds = list(map(int, re.findall(r'-?\d+', node.get('bounds', ''))))
            if len(bounds) == 4:
                x1, y1, x2, y2 = bounds
                if 0 <= x1 < x2 and 0 <= y1 < y2:
                    return (x1 + x2) // 2, (y1 + y2) // 2
    return None


def current_ui():
    adb('shell', 'uiautomator dump /data/local/tmp/jerusalem-ui.xml')
    return adb('shell', 'cat /data/local/tmp/jerusalem-ui.xml')


def click(text):
    def locate():
        center = control_center(current_ui(), text)
        if center is not None:
            adb('shell', 'input', 'tap', str(center[0]), str(center[1]))
            return True
        return False
    wait_for(locate, f'UI control not found: {text}')


def collect_diagnostics():
    # A failed diagnostic command must not hide the original smoke-test error.
    commands = [
        ('logcat.txt', ('logcat', '-d')),
        ('activity.txt', ('shell', 'dumpsys', 'activity', 'activities')),
        ('screen.png', ('exec-out', 'screencap', '-p')),
        ('ui-dump.txt', ('shell', 'uiautomator dump /data/local/tmp/jerusalem-ui.xml')),
        ('ui.xml', ('shell', 'cat /data/local/tmp/jerusalem-ui.xml')),
    ]
    errors = []
    for name, command in commands:
        try:
            (OUTPUT / name).write_bytes(adb(*command))
        except (OSError, subprocess.SubprocessError) as error:
            errors.append(f'{name}: {error}\n')
    if errors:
        (OUTPUT / 'diagnostic-errors.txt').write_text(''.join(errors))


def main():
    if os.environ.get('GITHUB_ACTIONS') != 'true':
        raise RuntimeError('Android runtime smoke tests run only in GitHub Actions')
    OUTPUT.mkdir(exist_ok=True)
    try:
        adb('install', '-r', 'android/app/build/outputs/apk/debug/app-debug.apk')
        adb('logcat', '-c')
        adb('shell', 'am', 'start', '-W', '-n', PACKAGE + '/.MainActivity')
        wait_for(lambda: b'Ready.' in adb('logcat', '-d', '-s', 'Jerusalem:I', '*:S'), 'Native engine did not become ready')
        # An independently generated 2x2 uncompressed BMP. No game data enters CI.
        bitmap = bytearray(70)
        bitmap[:2] = b'BM'
        struct.pack_into('<I', bitmap, 2, len(bitmap))
        struct.pack_into('<I', bitmap, 10, 54)
        struct.pack_into('<IiiHH', bitmap, 14, 40, 2, 2, 1, 24)
        bitmap[54:] = bytes([255, 0, 0, 0, 255, 0, 0, 0, 0, 0, 255, 255, 255, 255, 0, 0])
        adb('shell', f"run-as {PACKAGE} sh -c 'cat > files/game-data/synthetic.bmp'", input=bytes(bitmap))
        adb('shell', f"run-as {PACKAGE} sh -c 'cat > files/game-data/synthetic-labels.ini'",
            input=b'42\r\nENG #Synthetic label#\r\nFRA #Etiquette#\r\n')
        click('Browse assets')
        click('synthetic.bmp')
        click('Close')
        wait_for(lambda: b'BMP: 2 x 2' in adb('logcat', '-d', '-s', 'Jerusalem:I', '*:S'), 'Native BMP rendering path did not complete')
        adb('shell', 'pidof', PACKAGE)
        (OUTPUT / 'synthetic-image.png').write_bytes(adb('exec-out', 'screencap', '-p'))
        click('Browse assets')
        click('synthetic-labels.ini')
        wait_for(lambda: b'Synthetic label' in current_ui(), 'Decoded localization text was not displayed')
        click('Close')
        wait_for(lambda: b'Localization:' in adb('logcat', '-d', '-s', 'Jerusalem:I', '*:S'),
                 'Native view did not use the localization-aware summary')
        (OUTPUT / 'text-labels.png').write_bytes(adb('exec-out', 'screencap', '-p'))
        (OUTPUT / 'result.txt').write_text('PASS: x86_64 native launch, JNI inspection, synthetic BMP display and readable localization text.\nARM64 hardware and original asset playback remain separate validation gates.\n')
    finally:
        collect_diagnostics()


if __name__ == '__main__':
    main()
