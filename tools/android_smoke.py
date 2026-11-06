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


def camera_states(logs):
    pattern = rb'Panorama frame: yaw=([-\d.]+) pitch=([-\d.]+) fov=([-\d.]+)'
    return [tuple(map(float, match)) for match in re.findall(pattern, logs)]


def preview_states(logs):
    return [tuple(map(int, match)) for match in re.findall(rb'AQC preview frame: channel=(\d+) frame=(\d+)', logs)]


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


def click_scrolling(text):
    # Search both ways: opening Inventory can leave earlier controls offscreen.
    for direction in (1, -1):
        previous = None
        for _ in range(6):
            xml = current_ui()
            center = control_center(xml, text)
            if center is not None:
                adb('shell', 'input', 'tap', str(center[0]), str(center[1]))
                return
            if xml == previous:
                break
            previous = xml
            for node in ET.fromstring(xml).iter('node'):
                if node.get('class') != 'android.widget.HorizontalScrollView':
                    continue
                values = list(map(int, re.findall(r'\d+', node.get('bounds', ''))))
                if len(values) == 4 and values[2]-values[0]>40 and values[3]>values[1]:
                    x1, y1, x2, y2 = values
                    start, end = (x2-20, x1+20) if direction == 1 else (x1+20, x2-20)
                    adb('shell', 'input', 'swipe', str(start), str((y1+y2)//2), str(end), str((y1+y2)//2), '350')
                    break
            else:
                raise RuntimeError(f'No horizontal control row while locating {text}')
    raise RuntimeError(f'Scrollable UI control not found: {text}')


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


def compressed_audio_fixtures():
    """Generate independent triangle/square signals in the documented dialects."""
    def pack(bits):
        bits += '0' * (-len(bits) % 8)
        return int(bits, 2).to_bytes(len(bits) // 8, 'big')

    def header(channels, pcm):
        return (b'RIFF' + struct.pack('<I', 36 + pcm) + b'WAVEfmt ' +
                struct.pack('<IHHIIHH', 16, 1, channels, 22050, 44100 * channels, channels * 2, 16) +
                b'data' + struct.pack('<I', pcm))

    payload = pack('000' + (('0' + f'{16:010b}' + '1' * 7) + ('0' + f'{528:010b}' + '1' * 7)) * 16)
    mono_size = 88 * 512
    mono = struct.pack('<II', 88, 44 + mono_size) + header(1, mono_size)
    mono += (struct.pack('<H', len(payload)) + payload) * 88
    tokens, remaining, high = [], 44100, True
    while remaining:
        run = min(100, remaining)
        tokens.append('0' + f'{0x100 if high else 0xf00:012b}' + '11' * (run - 1))
        remaining -= run
        high = not high
    payload = pack(''.join(tokens))
    stereo_size = 44100 * 2
    stereo = (struct.pack('<II', 1, 44 + stereo_size) + header(2, stereo_size) +
              struct.pack('<II', len(payload), stereo_size) + payload)
    return {'synthetic-mono.wac': mono, 'synthetic-stereo.was': stereo}


def synthetic_aqc():
    """A small original-free colour-bar image in the supported AQC static dialect."""
    def pack(bits):
        bits += '0' * (-len(bits) % 8)
        return int(bits, 2).to_bytes(len(bits) // 8, 'big')
    header = struct.pack('<III4f6I', 8, 4, 2, 0., 360., -75., 75., 0, 8, 0, 4, 64, 16)
    indices = pack('0' + '0' * 13 + '11' * 7)
    palette = pack(''.join('0' + f'{n:016b}' for n in (0xf800, 0x07e0, 0x001f, 0xffff)) + '11' * 32396)
    return struct.pack('<I', len(indices)) + header + indices + struct.pack('<I', len(palette)) + palette


def synthetic_animated_aqc():
    base = synthetic_aqc()
    def frame(left, top, index):
        header = struct.pack('<III', 8, 4, 2) + base[16:32] + struct.pack('<6I', left, left+4, top, top+2, 16, 8)
        bits = '0' + f'{index:013b}' + '11'
        payload = int(bits, 2).to_bytes(2, 'big')
        return struct.pack('<I', len(payload)) + header + payload
    static = struct.pack('<IfI', 1, 0., 0) + frame(0, 0, 0)
    animated = struct.pack('<IfI', 2, 12., 1) + frame(0, 1, 1) + frame(4, 2, 0)
    return base + static + animated


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
        for name, data in compressed_audio_fixtures().items():
            adb('shell', f"run-as {PACKAGE} sh -c 'cat > files/game-data/{name}'", input=data)
        adb('shell', f"run-as {PACKAGE} sh -c 'cat > files/game-data/synthetic-node.aqc'", input=synthetic_animated_aqc())
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
        for name in compressed_audio_fixtures():
            click('Browse assets')
            click(name)
            wait_for(lambda: ('Playing audio: ' + name).encode() in adb('logcat', '-d', '-s', 'Jerusalem:I', '*:S'),
                     f'{name} did not decode and start playing')
            wait_for(lambda: ('Audio buffer drained: ' + name).encode() in adb('logcat', '-d', '-s', 'Jerusalem:I', '*:S'),
                     f'{name} decoded output did not drain')
        click('Browse assets')
        click('synthetic-node.aqc')
        click('Close')
        wait_for(lambda: b'AQC static node image: 8 x 4' in adb('logcat', '-d', '-s', 'Jerusalem:I', '*:S'),
                 'Native AQC decoding/display path did not complete')
        wait_for(lambda: b'Panorama frame:' in adb('logcat', '-d', '-s', 'Jerusalem:I', '*:S'),
                 'AQC camera frame was not rendered')
        (OUTPUT / 'aqc-panorama.png').write_bytes(adb('exec-out', 'screencap', '-p'))
        before = camera_states(adb('logcat', '-d', '-s', 'Jerusalem:I', '*:S'))[-1][0]
        # The emulator's default screen is 1080x1920. Use its reported dimensions instead.
        size = adb('shell', 'wm', 'size').decode()
        dimensions = re.findall(r'(\d+)x(\d+)', size)
        if not dimensions:
            raise RuntimeError('Cannot determine emulator touch dimensions')
        width, height = map(int, dimensions[-1])
        adb('shell', 'input', 'swipe', str(width // 3), str(height // 2), str(2 * width // 3), str(height // 2), '350')
        wait_for(lambda: any(abs((yaw-before+180)%360-180)>0.1 for yaw, _, _ in
                             camera_states(adb('logcat', '-d', '-s', 'Jerusalem:I', '*:S'))),
                 'Dragging did not change the panorama camera yaw')
        click_scrolling('Preview animation')
        wait_for(lambda: len(set(preview_states(adb('logcat', '-d', '-s', 'Jerusalem:I', '*:S'))))>=2,
                 'Original-format preview did not advance between synthetic frames')
        (OUTPUT / 'aqc-animation-preview.png').write_bytes(adb('exec-out', 'screencap', '-p'))
        click_scrolling('Preview animation')
        wait_for(lambda: b'Animation preview off.' in adb('logcat', '-d', '-s', 'Jerusalem:I', '*:S'),
                 'Animation preview did not switch off')
        # Add traversal fixtures after browser checks so earlier file lists remain stable.
        # These records and images are synthetic; no original executable is used in CI.
        for name in ('SYNTH_A', 'SYNTH_B'):
            adb('shell', f"run-as {PACKAGE} sh -c 'cat > files/game-data/{name}.aqc'", input=synthetic_animated_aqc())
        model = ('JERUSALEM_A01_NAV_V3\nnode 1 SYNTH_A 2\nnode 2 SYNTH_B 2\n'
                 'exit 1 0 2 0 1 0 -750 3600 750 53 0 0 0 85\n'
                 'exit 2 0 1 0 0 0 -750 3600 750 53 0 0 0 85\n'
                 'exit 2 1 99 1 1 0 -750 3600 750 53 0 0 0 85\n'
                 'ambient 1 7 80 SYNTH_LOOP\nambient 2 7 55 SYNTH_LOOP\n'
                 'ambient 1 8 50 SYNTH_SECOND\n'
                 'item 10 2 0 -750 3600 750 60 0 50 1 "Synthetic pickup"\n'
                 'item-channel 10 1 0\nitem-channel 10 2 0\n')
        # Short, distinct generated loops exercise repeated refill and concurrent voices.
        for name, frequency in (('SYNTH_LOOP', 330), ('SYNTH_SECOND', 550)):
            sound = io.BytesIO()
            with wave.open(sound, 'wb') as writer:
                writer.setnchannels(1); writer.setsampwidth(2); writer.setframerate(8000)
                writer.writeframes(b''.join(struct.pack('<h', int(8192*math.sin(2*math.pi*frequency*i/8000))) for i in range(1600)))
            adb('shell', f"run-as {PACKAGE} sh -c 'cat > files/game-data/{name}.wav'", input=sound.getvalue())
        adb('shell', f"run-as {PACKAGE} sh -c 'cat > files/game-data/a01-navigation.tsv'", input=model.encode())
        click_scrolling('A01 navigation')
        wait_for(lambda: b'A01 traversal: SYNTH_A.' in adb('logcat', '-d', '-s', 'Jerusalem:I', '*:S'),
                 'Traversal mode did not load the initial synthetic scene')
        wait_for(lambda: b'Scene audio active: SYNTH_LOOP.wav volume=80' in adb('logcat', '-d', '-s', 'Jerusalem:I', '*:S') and
                 b'Scene audio active: SYNTH_SECOND.wav volume=50' in adb('logcat', '-d', '-s', 'Jerusalem:I', '*:S'),
                 'Both synthetic ambient voices did not start')
        wait_for(lambda: len(re.findall(rb'Scene audio loop: SYNTH_LOOP.wav wraps=', adb('logcat', '-d', '-s', 'Jerusalem:I', '*:S')))>=3,
                 'Ambient stream did not continue refilling beyond initial queue')
        adb('shell', 'input', 'keyevent', 'KEYCODE_HOME')
        wait_for(lambda: b'Scene audio paused' in adb('logcat', '-d', '-s', 'Jerusalem:I', '*:S'),
                 'Backgrounding did not pause scene audio')
        adb('shell', 'am', 'start', '-W', '-n', PACKAGE + '/.MainActivity')
        wait_for(lambda: b'Scene audio resumed' in adb('logcat', '-d', '-s', 'Jerusalem:I', '*:S'),
                 'Returning to the app did not resume scene audio')
        adb('shell', 'input', 'tap', str(width // 2), str(height // 2))
        wait_for(lambda: b'A01 traversal: SYNTH_B.' in adb('logcat', '-d', '-s', 'Jerusalem:I', '*:S'),
                 'Tapping an enabled exit did not change scene')
        wait_for(lambda: b'Scene audio active: SYNTH_LOOP.wav volume=55' in adb('logcat', '-d', '-s', 'Jerusalem:I', '*:S'),
                 'Scene transition did not update the retained ambient voice volume')
        logs = adb('logcat', '-d', '-s', 'Jerusalem:I', '*:S')
        if logs.count(b'Scene audio opened: SYNTH_LOOP.wav') != 1 or b'Scene audio closed: SYNTH_SECOND.wav' not in logs:
            raise RuntimeError('Scene transition restarted a retained track or kept an unused voice')

        adb('shell', 'input', 'tap', str(width // 2), str(height // 2))
        wait_for(lambda: b'Collected: Synthetic pickup [10]' in adb('logcat', '-d', '-s', 'Jerusalem:I', '*:S'),
                 'Item hotspot did not take priority over the overlapping puzzle action')
        wait_for(lambda: b'Pickup state: item=10 counter=50 value=1 inventory=1' in adb('logcat', '-d', '-s', 'Jerusalem:I', '*:S'),
                 'Pickup did not update the native variable and inventory')
        click_scrolling('Inventory')
        wait_for(lambda: b'Collected items: 1' in current_ui() and b'Synthetic pickup' in current_ui(),
                 'Inventory dialog did not display the collected original-format item')
        click('Close')
        adb('shell', 'input', 'tap', str(width // 2), str(height // 2))
        wait_for(lambda: b'Puzzle interactions are not implemented; no route has been bypassed.' in adb('logcat', '-d', '-s', 'Jerusalem:I', '*:S'),
                 'Disabled return route or puzzle boundary was not respected')
        (OUTPUT / 'a01-traversal-synthetic.png').write_bytes(adb('exec-out', 'screencap', '-p'))
        click_scrolling('A01 navigation')
        wait_for(lambda: b'A01 traversal off.' in adb('logcat', '-d', '-s', 'Jerusalem:I', '*:S'),
                 'Traversal preview did not switch off')
        wait_for(lambda: b'Scene audio closed: SYNTH_LOOP.wav' in adb('logcat', '-d', '-s', 'Jerusalem:I', '*:S'),
                 'Leaving traversal did not close ambient playback')
        (OUTPUT / 'result.txt').write_text('PASS: x86_64 native launch, JNI inspection, synthetic BMP/AQC display, AQC camera rendering and drag redraw, channel preview frame advancement/stop, synthetic scene traversal, item hit priority/pickup/variable update and inventory display, concurrent looping ambient refill and scene volume changes, disabled-route/puzzle boundaries, readable localization text, stereo PCM output-buffer consumption and replay, native synthetic WAC/WAS decoding and output-buffer consumption.\nThis does not verify audible sound through a physical speaker. ARM64 hardware and original asset playback remain separate validation gates.\n')
    finally:
        collect_diagnostics()


if __name__ == '__main__':
    main()
