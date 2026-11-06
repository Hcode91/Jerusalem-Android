#!/usr/bin/env python3
"""Actions-only emulator launch and synthetic image regression, never original playback."""
import os
import io
import math
from pathlib import Path
import re
import struct
import subprocess
import time
import xml.etree.ElementTree as ET
import wave

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
        # Match the reported original crowd sound's stereo/22050 Hz/S16 geometry.
        # This two-second fixture is independently synthesized, never game audio.
        audio = io.BytesIO()
        frames = bytearray()
        for index in range(44100):
            frames.extend(struct.pack('<hh', int(8192 * math.sin(2 * math.pi * 440 * index / 22050)),
                                      int(8192 * math.sin(2 * math.pi * 660 * index / 22050))))
        with wave.open(audio, 'wb') as writer:
            writer.setnchannels(2); writer.setsampwidth(2); writer.setframerate(22050)
            writer.writeframes(frames)
        adb('shell', f"run-as {PACKAGE} sh -c 'cat > files/game-data/synthetic-audio.wav'", input=audio.getvalue())
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
        click('Browse assets')
        click('synthetic-audio.wav')
        # Selecting a WAV must leave the main window focused, without a report modal.
        wait_for(lambda: b'Playing audio: synthetic-audio.wav' in adb('logcat', '-d', '-s', 'Jerusalem:I', '*:S'),
                 'Selecting a PCM WAV did not start the native audio stream')
        wait_for(lambda: b'Audio buffer drained: synthetic-audio.wav' in adb('logcat', '-d', '-s', 'Jerusalem:I', '*:S'),
                 'The native audio output device did not consume the PCM stream')
        click('Play audio')
        wait_for(lambda: adb('logcat', '-d', '-s', 'Jerusalem:I', '*:S').count(b'Playing audio: synthetic-audio.wav') >= 2,
                 'Play audio did not replay the selected sound')
        logs = adb('logcat', '-d', '-s', 'Jerusalem:I', '*:S')
        if b'driver=dummy' in logs or b'Audio output: driver=' not in logs:
            raise RuntimeError('Audio smoke check did not use an Android output backend')
        (OUTPUT / 'audio-playback.png').write_bytes(adb('exec-out', 'screencap', '-p'))
        (OUTPUT / 'result.txt').write_text('PASS: x86_64 native launch, JNI inspection, synthetic BMP display, readable localization text, stereo PCM output-buffer consumption and replay.\nThis does not verify audible sound through a physical speaker. ARM64 hardware and original asset playback remain separate validation gates.\n')
    finally:
        collect_diagnostics()


if __name__ == '__main__':
    main()
