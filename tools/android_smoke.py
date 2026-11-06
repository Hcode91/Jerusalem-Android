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


def click(text):
    def locate():
        adb('shell', 'uiautomator dump /data/local/tmp/jerusalem-ui.xml')
        tree = ET.fromstring(adb('shell', 'cat /data/local/tmp/jerusalem-ui.xml'))
        for node in tree.iter('node'):
            if node.get('text') == text:
                bounds = list(map(int, re.findall(r'\d+', node.get('bounds', ''))))
                if len(bounds) == 4:
                    adb('shell', 'input', 'tap', str((bounds[0] + bounds[2]) // 2), str((bounds[1] + bounds[3]) // 2))
                    return True
        return False
    wait_for(locate, f'UI control not found: {text}')


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
        click('Browse assets')
        click('synthetic.bmp')
        click('Close')
        wait_for(lambda: b'BMP: 2 x 2' in adb('logcat', '-d', '-s', 'Jerusalem:I', '*:S'), 'Native BMP rendering path did not complete')
        adb('shell', 'pidof', PACKAGE)
        (OUTPUT / 'synthetic-image.png').write_bytes(adb('exec-out', 'screencap', '-p'))
        (OUTPUT / 'result.txt').write_text('PASS: x86_64 native launch, JNI inspection and synthetic BMP display.\nARM64 hardware and original asset playback remain separate validation gates.\n')
    finally:
        (OUTPUT / 'logcat.txt').write_bytes(adb('logcat', '-d'))
        (OUTPUT / 'activity.txt').write_bytes(adb('shell', 'dumpsys', 'activity', 'activities'))


if __name__ == '__main__':
    main()
