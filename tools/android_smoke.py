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
import uuid

PACKAGE = 'org.jerusalem.engine'
OUTPUT = Path('smoke-output')


def adb(*args, **kwargs):
    return subprocess.run(['adb', *args], check=True, stdout=subprocess.PIPE, timeout=90, **kwargs).stdout


_log_marker = None


def _log_buffer():
    return adb('logcat', '-d', '-s', 'Jerusalem:I', '*:S')


def begin_log_phase():
    """Separate assertions without depending on destructive logcat -c support."""
    global _log_marker
    marker = 'JERUSALEM_SMOKE_PHASE_'+uuid.uuid4().hex
    adb('shell', 'log', '-p', 'i', '-t', 'Jerusalem', marker)
    _log_marker = marker.encode('ascii')
    wait_for(lambda: _log_marker in _log_buffer(), 'Emulator did not record the smoke log phase marker', seconds=10)


def game_logs():
    """Never let older success/error messages satisfy a later phase's checks."""
    if _log_marker is None:
        raise RuntimeError('Smoke log phase has not started')
    logs = _log_buffer()
    _, found, tail = logs.rpartition(_log_marker)
    return tail if found else b''


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


def checkpoint_states(logs):
    pattern = (rb'Checkpoint state: action=(save|load) node=(\d+) yaw=([-\d.]+) '
               rb'pitch=([-\d.]+) fov=([-\d.]+) inventory=(\d+)')
    return [(action.decode(), int(node), float(yaw), float(pitch), float(fov), int(count))
            for action, node, yaw, pitch, fov, count in re.findall(pattern, logs)]


def current_ui():
    path = '/data/local/tmp/jerusalem-ui.xml'
    # A failed dump must not accidentally reuse a hierarchy from an earlier screen.
    for attempt in range(3):
        adb('shell', 'rm', '-f', path)
        output = adb('shell', f'uiautomator dump {path}', stderr=subprocess.STDOUT)
        if b'could not get idle state' in output:
            if attempt == 2:
                raise RuntimeError('Android UI hierarchy unavailable after 3 attempts: '+output.decode('utf-8', errors='replace').strip())
            print('Android UI was not idle; retrying a fresh hierarchy dump', flush=True)
            time.sleep(0.25)
            continue
        if b'ERROR:' in output:
            raise RuntimeError('Android UI hierarchy unavailable: '+output.decode('utf-8', errors='replace').strip())
        try:
            xml = adb('shell', f'cat {path}')
        except subprocess.CalledProcessError as error:
            raise RuntimeError('Android UI dump produced no hierarchy: '+output.decode('utf-8', errors='replace').strip()) from error
        ET.fromstring(xml)
        return xml


def click(text):
    def locate():
        center = control_center(current_ui(), text)
        if center is not None:
            adb('shell', 'input', 'tap', str(center[0]), str(center[1]))
            return True
        return False
    wait_for(locate, f'UI control not found: {text}')


def click_list_item(text):
    """Find a browser row using swipes confined to its vertical list viewport."""
    for direction in (1, -1):
        previous = None
        for _ in range(16):
            xml = current_ui()
            center = control_center(xml, text)
            if center is not None:
                adb('shell', 'input', 'tap', str(center[0]), str(center[1]))
                return
            if xml == previous:
                break
            previous = xml
            for node in ET.fromstring(xml).iter('node'):
                if node.get('class') != 'android.widget.ListView' or node.get('scrollable') != 'true':
                    continue
                bounds = list(map(int, re.findall(r'-?\d+', node.get('bounds', ''))))
                if len(bounds) == 4:
                    x1, y1, x2, y2 = bounds
                    if 0 <= x1 < x2 and 0 <= y1 and y2-y1 > 40:
                        start, end = (y2-20, y1+20) if direction == 1 else (y1+20, y2-20)
                        adb('shell', 'input', 'swipe', str((x1+x2)//2), str(start),
                            str((x1+x2)//2), str(end), '350')
                        break
            else:
                raise RuntimeError(f'No scrollable asset list while locating {text}')
    raise RuntimeError(f'Asset list item not found: {text}')


def click_scrolling(text):
    # Search both ways: opening Inventory can leave earlier controls offscreen.
    for direction in (1, -1):
        previous = None
        for _ in range(12):
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


def click_menu(text):
    """Menu actions remain reachable at phone font sizes and in landscape."""
    for direction in (1, -1):
        previous = None
        for _ in range(12):
            xml = current_ui();center = control_center(xml, text)
            if center is not None:
                adb('shell', 'input', 'tap', str(center[0]), str(center[1]));return
            if xml == previous: break
            previous = xml
            for node in ET.fromstring(xml).iter('node'):
                if node.get('class') != 'android.widget.ScrollView' or node.get('scrollable') != 'true': continue
                bounds = list(map(int, re.findall(r'-?\d+', node.get('bounds', ''))))
                if len(bounds) == 4 and bounds[2] > bounds[0] and bounds[3]-bounds[1] > 40:
                    x1, y1, x2, y2 = bounds
                    start, end = (y2-20, y1+20) if direction == 1 else (y1+20, y2-20)
                    adb('shell', 'input', 'swipe', str((x1+x2)//2), str(start), str((x1+x2)//2), str(end), '300');break
            else: raise RuntimeError(f'No menu scroll area while locating {text}')
    raise RuntimeError(f'Menu action not found: {text}')


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


def synthetic_story_aqc():
    base = synthetic_aqc()
    result = bytearray(base)
    for index, left, top in ((0, 0, 0), (1, 4, 0), (0, 0, 2), (1, 4, 2)):
        header = struct.pack('<III', 8, 4, 2) + base[16:32] + struct.pack('<6I', left, left+4, top, top+2, 16, 8)
        payload = int('0'+f'{index:013b}'+'11', 2).to_bytes(2, 'big')
        result += struct.pack('<IfII', 1, 0., 0, len(payload)) + header + payload
    return bytes(result)


def synthetic_cinema(frames=3, preload_audio=False):
    """Independent progressive CNM with T/U/S frames and stereo raw PCM."""
    header = bytearray(192)
    header[:8] = b'CNM UNR\0'
    struct.pack_into('<I', header, 12, 2500)
    struct.pack_into('<ii', header, 17, 32, 2)
    header[25] = 32; header[27] = 1
    struct.pack_into('<I', header, 28, frames)
    struct.pack_into('<I', header, 36, frames*4)
    sound = bytes([1, 16])+struct.pack('<H', 22050)+bytes(12)  # Encoded channels-minus-one.
    result = header+sound+bytes(frames*8)
    def bits(values):
        stream = []
        for value, count in values:
            stream.extend((value >> i)&1 for i in range(count))
        packed = bytearray((len(stream)+7)//8)
        for i, bit in enumerate(stream): packed[i//8] |= bit << (i%8)
        return bytes(packed)
    for i in range(frames):
        pcm = b''.join(struct.pack('<hh', int(4096*math.sin(2*math.pi*330*j/22050)),
                                  int(4096*math.sin(2*math.pi*550*j/22050))) for j in range(882))
        if preload_audio:
            pcm = pcm*frames if i == 0 else b''
        result += b'Z'+struct.pack('<I', len(pcm))+pcm
        fresh = i in (0, frames-1)
        if fresh:
            book = bytes([5 if i == 0 else 90, 7, 9, 0])*4+bytes(2)
            result += b'T'+struct.pack('<IHH', len(book), 1, 4)+book
        indices = bits([(0, 1)]+[(v, n) for _ in range(8) for v, n in ((0, 3), (int(fresh), 1))]+[(1, 1)])
        frame_header = struct.pack('<IIHHII', len(indices), len(indices), 1, 4, 32, 2)+bytes(27)
        result += (b'U' if fresh else b'S')+frame_header+indices
    return bytes(result)


def synthetic_stage_fixture(bitmap, pcm_wave):
    """Original-free fixtures exercise story callbacks; never represent game playback."""
    ids = (10999, 10000, 10001, 10002, 11000, 11002, 11004, 11005, 11008,
           11011, 11012, 11013, 11014, 11015, 11016, 11017)
    stage = 'JERUSALEM_A01_STAGE_V1\n'
    for scene in (10045, 10016, 10017, 10018, 10039):
        stage += f'background {scene} SYNTH.BMP 0 16\n'
    for voice in ids:
        stage += f'dialogue {voice} A01 SYNTH_VOICE_{voice}\n'
    model = 'JERUSALEM_A01_NAV_V3\n'
    files = {'a01-stage.tsv': stage.encode()}
    for node in range(1, 7):
        model += f'node {1010100+node} STORY_NODE_{node} 4\n'
        files[f'STORY_NODE_{node}.aqc'] = synthetic_story_aqc()
        if node < 6:
            model += f'exit {1010100+node} 0 {1010101+node} 0 1 0 -750 3600 750 53 0 0 0 85\n'
    model += ('exit 1010106 0 1010105 0 0 0 -750 3600 750 53 0 0 0 85\n'
              'exit 1010106 1 10039 1 1 0 -750 3600 750 53 0 0 0 85\n')
    files['a01-navigation.tsv'] = model.encode()
    # AT3 directory wraps an independently generated BMP, including both archive fallbacks.
    name = b'\\bgr\\SYNTH.BMP'
    entry = name+bytes(243-len(name))+struct.pack('<III', 32+255, len(bitmap), len(bitmap))
    archive = b'ATIII'+bytes(3)+struct.pack('<III', 255, 1, 255)+bytes(12)+entry+bitmap
    files['data/A01.AT3'] = archive
    entries = bytearray()
    for entry_name in (b'\\bgr\\SYNTH.BMP', b'\\image\\MENU.BMP'):
        entries += entry_name+bytes(243-len(entry_name))+struct.pack('<III', 32+510, len(bitmap), len(bitmap))
    files['data/SY.AT3'] = b'ATIII'+bytes(3)+struct.pack('<III', 510, 2, 255)+bytes(12)+entries+bitmap
    for voice in ids:
        # The original opening is WAS on disc despite the executable naming WAC.
        extension = 'WAS' if voice == 10999 else 'WAC'
        audio_bytes = pcm_wave
        if voice == 10999:
            with wave.open(io.BytesIO(pcm_wave), 'rb') as source:
                params, samples = source.getparams(), source.readframes(source.getnframes())
            opening = io.BytesIO()
            with wave.open(opening, 'wb') as target:
                target.setparams(params);target.writeframes(samples*4)
            audio_bytes = opening.getvalue()
        files[f'data/a01/sound/eng/SYNTH_VOICE_{voice}.{extension}'] = audio_bytes
        files[f'data/a01/dia/eng/SYNTH_VOICE_{voice}.DIA'] = b'0 Synthetic story speech\r\n400 END\r\n'
    for movie in ('001', '002', '003', '004', '006', '007A', '007B', '007C', '009'):
        files[f'data/a01/pla/eng/A01S01_AN{movie}.CI2'] = synthetic_cinema(3, preload_audio=True)
    return files


def main():
    if os.environ.get('GITHUB_ACTIONS') != 'true':
        raise RuntimeError('Android runtime smoke tests run only in GitHub Actions')
    OUTPUT.mkdir(exist_ok=True)
    try:
        adb('install', '-r', 'android/app/build/outputs/apk/debug/app-debug.apk')
        begin_log_phase()
        adb('shell', 'am', 'start', '-W', '-n', PACKAGE + '/.MainActivity')
        wait_for(lambda: b'Ready.' in game_logs(), 'Native engine did not become ready')
        wait_for(lambda: b'New Game' in current_ui(), 'Main menu did not appear on a fresh launch')
        (OUTPUT / 'main-menu.png').write_bytes(adb('exec-out', 'screencap', '-p'))
        click_menu('Developer Tools')
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
        click_scrolling('Browse assets')
        click('synthetic.bmp')
        click('Close')
        wait_for(lambda: b'BMP: 2 x 2' in game_logs(), 'Native BMP rendering path did not complete')
        adb('shell', 'pidof', PACKAGE)
        (OUTPUT / 'synthetic-image.png').write_bytes(adb('exec-out', 'screencap', '-p'))
        click_scrolling('Browse assets')
        click('synthetic-labels.ini')
        wait_for(lambda: b'Synthetic label' in current_ui(), 'Decoded localization text was not displayed')
        click('Close')
        wait_for(lambda: b'Localization:' in game_logs(),
                 'Native view did not use the localization-aware summary')
        (OUTPUT / 'text-labels.png').write_bytes(adb('exec-out', 'screencap', '-p'))
        click_scrolling('Browse assets')
        click('synthetic-audio.wav')
        # Selecting a WAV must leave the main window focused, without a report modal.
        wait_for(lambda: b'Playing audio: synthetic-audio.wav' in game_logs(),
                 'Selecting a PCM WAV did not start the native audio stream')
        wait_for(lambda: b'Audio buffer drained: synthetic-audio.wav' in game_logs(),
                 'The native audio output device did not consume the PCM stream')
        click_scrolling('Play audio')
        wait_for(lambda: game_logs().count(b'Playing audio: synthetic-audio.wav') >= 2,
                 'Play audio did not replay the selected sound')
        logs = game_logs()
        if b'driver=dummy' in logs or b'Audio output: driver=' not in logs:
            raise RuntimeError('Audio smoke check did not use an Android output backend')
        (OUTPUT / 'audio-playback.png').write_bytes(adb('exec-out', 'screencap', '-p'))
        for name in compressed_audio_fixtures():
            click_scrolling('Browse assets')
            click(name)
            wait_for(lambda: ('Playing audio: ' + name).encode() in game_logs(),
                     f'{name} did not decode and start playing')
            wait_for(lambda: ('Audio buffer drained: ' + name).encode() in game_logs(),
                     f'{name} decoded output did not drain')
        click_scrolling('Browse assets')
        click('synthetic-node.aqc')
        click('Close')
        wait_for(lambda: b'AQC static node image: 8 x 4' in game_logs(),
                 'Native AQC decoding/display path did not complete')
        wait_for(lambda: b'Panorama frame:' in game_logs(),
                 'AQC camera frame was not rendered')
        (OUTPUT / 'aqc-panorama.png').write_bytes(adb('exec-out', 'screencap', '-p'))
        before = camera_states(game_logs())[-1][0]
        # The emulator's default screen is 1080x1920. Use its reported dimensions instead.
        size = adb('shell', 'wm', 'size').decode()
        dimensions = re.findall(r'(\d+)x(\d+)', size)
        if not dimensions:
            raise RuntimeError('Cannot determine emulator touch dimensions')
        width, height = map(int, dimensions[-1])
        adb('shell', 'input', 'swipe', str(width // 3), str(height // 2), str(2 * width // 3), str(height // 2), '350')
        wait_for(lambda: any(abs((yaw-before+180)%360-180)>0.1 for yaw, _, _ in
                             camera_states(game_logs())),
                 'Dragging did not change the panorama camera yaw')
        click_scrolling('Preview animation')
        wait_for(lambda: len(set(preview_states(game_logs())))>=2,
                 'Original-format preview did not advance between synthetic frames')
        (OUTPUT / 'aqc-animation-preview.png').write_bytes(adb('exec-out', 'screencap', '-p'))
        click_scrolling('Preview animation')
        wait_for(lambda: b'Animation preview off.' in game_logs(),
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
        wait_for(lambda: b'A01 traversal: SYNTH_A.' in game_logs(),
                 'Traversal mode did not load the initial synthetic scene')
        wait_for(lambda: b'Scene audio active: SYNTH_LOOP.wav volume=80' in game_logs() and
                 b'Scene audio active: SYNTH_SECOND.wav volume=50' in game_logs(),
                 'Both synthetic ambient voices did not start')
        wait_for(lambda: len(re.findall(rb'Scene audio loop: SYNTH_LOOP.wav wraps=', game_logs()))>=3,
                 'Ambient stream did not continue refilling beyond initial queue')
        adb('shell', 'input', 'keyevent', 'KEYCODE_HOME')
        wait_for(lambda: b'Scene audio paused' in game_logs(),
                 'Backgrounding did not pause scene audio')
        adb('shell', 'am', 'start', '-W', '-n', PACKAGE + '/.MainActivity')
        wait_for(lambda: b'Scene audio resumed' in game_logs(),
                 'Returning to the app did not resume scene audio')
        adb('shell', 'input', 'tap', str(width // 2), str(height // 2))
        wait_for(lambda: b'A01 traversal: SYNTH_B.' in game_logs(),
                 'Tapping an enabled exit did not change scene')
        wait_for(lambda: b'Scene audio active: SYNTH_LOOP.wav volume=55' in game_logs(),
                 'Scene transition did not update the retained ambient voice volume')
        logs = game_logs()
        if logs.count(b'Scene audio opened: SYNTH_LOOP.wav') != 1 or b'Scene audio closed: SYNTH_SECOND.wav' not in logs:
            raise RuntimeError('Scene transition restarted a retained track or kept an unused voice')

        adb('shell', 'input', 'tap', str(width // 2), str(height // 2))
        wait_for(lambda: b'Collected: Synthetic pickup [10]' in game_logs(),
                 'Item hotspot did not take priority over the overlapping puzzle action')
        wait_for(lambda: b'Pickup state: item=10 counter=50 value=1 inventory=1' in game_logs(),
                 'Pickup did not update the native variable and inventory')
        click_scrolling('Inventory')
        wait_for(lambda: b'Collected items: 1' in current_ui() and b'Synthetic pickup' in current_ui(),
                 'Inventory dialog did not display the collected original-format item')
        click('Close')
        adb('shell', 'input', 'tap', str(width // 2), str(height // 2))
        wait_for(lambda: b'Puzzle interactions are not implemented; no route has been bypassed.' in game_logs(),
                 'Disabled return route or puzzle boundary was not respected')
        click_scrolling('Save checkpoint')
        wait_for(lambda: any(row[0] == 'save' for row in checkpoint_states(game_logs())),
                 'Native checkpoint was not saved')
        saved = [row for row in checkpoint_states(game_logs()) if row[0] == 'save'][-1]
        checkpoint = adb('exec-out', 'run-as', PACKAGE, 'cat', 'files/native-saves/a01-preview.jrs')
        if checkpoint[:8] != b'JRNSAVE\0' or not 68 <= len(checkpoint) <= 132:
            raise RuntimeError('Checkpoint is missing or not the bounded native metadata format')
        # Settings must survive restart, not merely fall back to their defaults.
        click_scrolling('Menu');click_menu('Settings');click('Show subtitles');click('Apply')
        # A real process restart proves persistence beyond in-memory state.
        (OUTPUT / 'before-checkpoint-restart-logcat.txt').write_bytes(adb('logcat', '-d'))
        adb('shell', 'am', 'force-stop', PACKAGE)
        begin_log_phase()
        adb('shell', 'am', 'start', '-W', '-n', PACKAGE + '/.MainActivity')
        wait_for(lambda: b'Ready.' in game_logs(),
                 'Native app did not restart for checkpoint restoration')
        click_menu('Settings')
        caption_nodes = [node for node in ET.fromstring(current_ui()).iter('node') if node.get('text') == 'Show subtitles']
        if not caption_nodes or caption_nodes[0].get('checked') != 'false':
            raise RuntimeError('Subtitle setting did not persist across process restart')
        click('Show subtitles');click('Apply');click_menu('Developer Tools')
        click_scrolling('Load checkpoint')
        wait_for(lambda: any(row[0] == 'load' for row in checkpoint_states(game_logs())),
                 'Native checkpoint did not load after process restart')
        loaded = [row for row in checkpoint_states(game_logs()) if row[0] == 'load'][-1]
        if loaded[1:] != saved[1:]:
            raise RuntimeError(f'Checkpoint scene/camera/inventory changed: {saved} vs {loaded}')
        click_scrolling('Inventory')
        wait_for(lambda: b'Collected items: 1' in current_ui() and b'Synthetic pickup' in current_ui(),
                 'Restored inventory did not contain the saved pickup')
        click('Close')
        adb('shell', 'input', 'tap', str(width // 2), str(height // 2))
        wait_for(lambda: b'Puzzle interactions are not implemented; no route has been bypassed.' in game_logs(),
                 'Restored picked item still intercepted the puzzle hotspot')
        # Invalid load must preserve the currently restored session.
        adb('shell', f"run-as {PACKAGE} sh -c 'cat > files/native-saves/a01-preview.jrs'", input=b'invalid checkpoint')
        click_scrolling('Load checkpoint')
        wait_for(lambda: b'Checkpoint load failed:' in game_logs(),
                 'Corrupt checkpoint did not produce a clear load error')
        click_scrolling('Inventory')
        wait_for(lambda: b'Collected items: 1' in current_ui() and b'Synthetic pickup' in current_ui(),
                 'Invalid checkpoint replaced the active inventory')
        click('Close')
        adb('shell', f"run-as {PACKAGE} sh -c 'cat > files/native-saves/a01-preview.jrs'", input=checkpoint)
        # A separate V4 synthetic session exercises accessible ingredient selection and persistence.
        click_scrolling('A01 navigation')
        wait_for(lambda: b'A01 traversal off.' in game_logs(),
                 'Previous navigation session did not stop before V4 setup')
        upgraded = model.replace('NAV_V3', 'NAV_V4') + (
            'item 11 2 0 -750 3600 750 60 0 50 1 "Synthetic stone"\n'
            'item-channel 11 2 0\ncombine 10 11 20 "Synthetic sling"\n')
        adb('shell', f"run-as {PACKAGE} sh -c 'cat > files/game-data/a01-navigation.tsv'", input=upgraded.encode())
        begin_log_phase()
        click_scrolling('A01 navigation')
        # Navigation deliberately reopens the last viewed asset. The checkpoint above
        # restored SYNTH_B; toggling off resets interactions but retains that selection.
        wait_for(lambda: b'A01 traversal: SYNTH_B.' in game_logs(), 'V4 scene did not reopen the selected node')
        for expected in (b'Collected: Synthetic pickup [10]', b'Collected: Synthetic stone [11]'):
            adb('shell', 'input', 'tap', str(width // 2), str(height // 2))
            wait_for(lambda: expected in game_logs(), 'V4 traversal/pickup failed')
        click_scrolling('Inventory')
        click('Combine items')
        click('Synthetic stone')
        click('Synthetic pickup')
        wait_for(lambda: b'Combination state: first=11 second=10 result=20 inventory=1' in game_logs(),
                 'Reverse-order combination did not consume ingredients and add result')
        click_scrolling('Inventory')
        wait_for(lambda: b'Collected items: 1' in current_ui() and b'Synthetic sling [20]' in current_ui(), 'Combination result missing from inventory')
        click('Close')
        click_scrolling('Save checkpoint')
        wait_for(lambda: b'Checkpoint state: action=save' in game_logs(), 'Combination checkpoint not saved')
        combined = adb('exec-out', 'run-as', PACKAGE, 'cat', 'files/native-saves/a01-preview.jrs')
        if len(combined) != 80 or struct.unpack_from('<I', combined, 12)[0] != 2 or struct.unpack_from('<III', combined, 64) != (10, 11, 0x80000014):
            raise RuntimeError('Combination checkpoint lacks the expected revision/action sequence')
        adb('shell', 'am', 'force-stop', PACKAGE)
        begin_log_phase()
        adb('shell', 'am', 'start', '-W', '-n', PACKAGE + '/.MainActivity')
        wait_for(lambda: b'Ready.' in game_logs(), 'Combination restart failed')
        click_menu('Developer Tools')
        click_scrolling('Load checkpoint')
        wait_for(lambda: b'Checkpoint state: action=load' in game_logs(), 'Combination checkpoint did not reload')
        click_scrolling('Inventory')
        wait_for(lambda: b'Collected items: 1' in current_ui() and b'Synthetic sling [20]' in current_ui(), 'Reloaded result missing or ingredients returned')
        click('Close')
        adb('shell', 'input', 'tap', str(width // 2), str(height // 2))
        wait_for(lambda: b'Puzzle interactions are not implemented; no route has been bypassed.' in game_logs(),
                 'Consumed ingredients became collectible again after loading')
        (OUTPUT / 'a01-traversal-synthetic.png').write_bytes(adb('exec-out', 'screencap', '-p'))
        click_scrolling('A01 navigation')
        wait_for(lambda: b'A01 traversal off.' in game_logs(),
                 'Traversal preview did not switch off')
        wait_for(lambda: b'Scene audio closed: SYNTH_LOOP.wav' in game_logs(),
                 'Leaving traversal did not close ambient playback')
        # Palm + consuming character offer + revision-3 persistence, with original-free data.
        for name in ('STORY_A', 'STORY_B'):
            adb('shell', f"run-as {PACKAGE} sh -c 'cat > files/game-data/{name}.aqc'", input=synthetic_story_aqc())
        story_model = ('JERUSALEM_A01_NAV_V5\nnode 1 STORY_A 4\nnode 2 STORY_B 4\n'
                       'exit 1 0 2 0 1 0 -750 3600 750 53 0 0 0 85\n'
                       'item 10 1 0 -750 3600 750 60 0 50 1 "Story harness"\nitem-channel 10 1 0\n'
                       'item 11 1 0 -750 3600 750 60 0 50 1 "Story matting"\nitem-channel 11 1 1\n'
                       'item 12 2 0 -750 3600 750 60 0 51 0 "Story palm"\nitem-channel 12 2 0\npickup-set 12 1\n'
                       'presentation 40 0 2 2\npresentation 40 1 2 3\npickup-switch 12 52 1 40 1 0\n'
                       'target 30 2 0 -750 3600 750 62 0 0 "Successor"\n'
                       'target 31 2 0 -750 3600 750 62 0 1 "Offer"\n'
                       'use 1 31 11 52 1 30 51 40 1 0 0 1\n')
        adb('shell', f"run-as {PACKAGE} sh -c 'cat > files/game-data/a01-navigation.tsv'", input=story_model.encode())
        begin_log_phase()
        click_scrolling('A01 navigation')
        wait_for(lambda: b'A01 traversal: STORY_A.' in game_logs(), 'V5 story scene did not start')
        for expected in (b'Collected: Story harness [10]', b'Collected: Story matting [11]', b'A01 traversal: STORY_B.', b'Collected: Story palm [12]'):
            adb('shell', 'input', 'tap', str(width // 2), str(height // 2))
            wait_for(lambda: expected in game_logs(), 'V5 pickup/traversal sequence failed')
        click_scrolling('Inventory'); click('Use item'); click('Story matting')
        wait_for(lambda: b'Using Story matting.' in game_logs(), 'Item-use selection did not reach native engine')
        adb('shell', 'input', 'tap', str(width // 2), str(height // 2))
        wait_for(lambda: b'Item use state: rule=1 target=31 item=11 consumed=1 inventory=2' in game_logs(), 'Character offer did not consume matting')
        click_scrolling('Save checkpoint')
        wait_for(lambda: b'Checkpoint state: action=save' in game_logs(), 'Character checkpoint was not saved')
        story_checkpoint = adb('exec-out', 'run-as', PACKAGE, 'cat', 'files/native-saves/a01-preview.jrs')
        if len(story_checkpoint) != 84 or struct.unpack_from('<I', story_checkpoint, 12)[0] != 3 or struct.unpack_from('<4I', story_checkpoint, 64) != (10, 11, 12, 0x40000001):
            raise RuntimeError('Story checkpoint lost assignment/use action order')
        adb('shell', 'am', 'force-stop', PACKAGE); begin_log_phase()
        adb('shell', 'am', 'start', '-W', '-n', PACKAGE + '/.MainActivity')
        wait_for(lambda: b'Ready.' in game_logs(), 'Story restart failed')
        click_menu('Developer Tools')
        click_scrolling('Load checkpoint')
        wait_for(lambda: b'Checkpoint state: action=load node=2' in game_logs(), 'Story checkpoint did not restore')
        click_scrolling('Inventory')
        wait_for(lambda: b'Collected items: 2' in current_ui() and b'Story palm' in current_ui() and b'Story matting' not in current_ui(), 'Story inventory did not restore consumption/palm')
        click('Close'); click_scrolling('A01 navigation')
        wait_for(lambda: b'A01 traversal off.' in game_logs(), 'Story session did not stop')
        # Same native item-use path starts generated speech and genuinely parsed DIA cues.
        voice_model = ('JERUSALEM_A01_NAV_V5\nnode 1 STORY_A 4\nnode 2 STORY_B 4\n'
                       'item 10 2 0 -750 3600 750 60 0 50 1 "Voice harness"\nitem-channel 10 2 0\n'
                       'target 32 2 0 -750 3600 750 62 0 1 "Feedback"\n'
                       'use 2 32 10 0 0 0 0 0 0 0 60 0\ndialogue 60 A01 SYNTH_SPEECH\n')
        adb('shell', f'run-as {PACKAGE} mkdir -p files/game-data/data/a01/sound/eng files/game-data/data/a01/dia/eng')
        sound = io.BytesIO()
        with wave.open(sound, 'wb') as writer:
            writer.setnchannels(1); writer.setsampwidth(2); writer.setframerate(22050)
            writer.writeframes(b''.join(struct.pack('<h', int(4096*math.sin(2*math.pi*330*j/22050))) for j in range(44100)))
        adb('shell', f"run-as {PACKAGE} sh -c 'cat > files/game-data/data/a01/sound/eng/SYNTH_SPEECH.WAC'", input=sound.getvalue())
        adb('shell', f"run-as {PACKAGE} sh -c 'cat > files/game-data/data/a01/dia/eng/SYNTH_SPEECH.DIA'", input=b'0 Synthetic first\r\n400 Synthetic second\r\n1500 END\r\n')
        adb('shell', f"run-as {PACKAGE} sh -c 'cat > files/game-data/a01-navigation.tsv'", input=voice_model.encode())
        begin_log_phase(); click_scrolling('A01 navigation')
        wait_for(lambda: b'A01 traversal: STORY_B.' in game_logs(), 'Voice scene did not retain selected node')
        adb('shell', 'input', 'tap', str(width // 2), str(height // 2))
        wait_for(lambda: b'Collected: Voice harness [10]' in game_logs(), 'Voice ingredient missing')
        click_scrolling('Inventory'); click('Use item'); click('Voice harness')
        wait_for(lambda: b'Using Voice harness.' in game_logs(), 'Voice selection failed')
        adb('shell', 'input', 'tap', str(width // 2), str(height // 2))
        wait_for(lambda: b'Dialogue started: id=60 stem=SYNTH_SPEECH' in game_logs(), 'Dialogue did not resolve/play audio')
        wait_for(lambda: b'text=Synthetic second' in game_logs(), 'DIA timeline did not advance')
        click_scrolling('A01 navigation')
        # Movie playback has no report dialog and uses header timing plus stereo PCM.
        adb('shell', f"run-as {PACKAGE} sh -c 'cat > files/game-data/synthetic-movie.CI2'", input=synthetic_cinema(100))
        begin_log_phase(); click_scrolling('Browse assets'); click_list_item('synthetic-movie.CI2')
        wait_for(lambda: b'Cinema frame: index=1' in game_logs(), 'Native cinema preview did not advance')
        wait_for(lambda: b'Cinema preview decoded 100 frames.' in game_logs(), 'Cinema did not reach EOF at its declared frame count')
        wait_for(lambda: b'Audio buffer drained: synthetic-movie.CI2' in game_logs(), 'Movie PCM did not reach SDL output')
        # Match AN004/AN007C: one preloaded soundtrack followed by valid zero-byte Z chunks.
        adb('shell', f"run-as {PACKAGE} sh -c 'cat > files/game-data/synthetic-preloaded.CI2'", input=synthetic_cinema(30, preload_audio=True))
        begin_log_phase(); click_scrolling('Browse assets'); click_list_item('synthetic-preloaded.CI2')
        wait_for(lambda: b'Cinema preview decoded 30 frames.' in game_logs(), 'Preloaded-soundtrack movie stopped on an empty PCM chunk')
        wait_for(lambda: b'Audio buffer drained: synthetic-preloaded.CI2' in game_logs(), 'Preloaded soundtrack did not reach SDL output')
        if b'Cinema preview stopped:' in game_logs():
            raise RuntimeError('Cinema emitted an error while playing the preloaded soundtrack fixture')
        # New story mode: actual movie/sound EOF callbacks, puzzle screens and first encounter.
        for name, content in synthetic_stage_fixture(bytes(bitmap), audio.getvalue()).items():
            parent = str(Path(name).parent)
            adb('shell', f'run-as {PACKAGE} mkdir -p files/game-data/{parent}')
            adb('shell', f"run-as {PACKAGE} sh -c 'cat > files/game-data/{name}'", input=content)
        begin_log_phase()
        click_scrolling('Menu')
        click_menu('New Game')
        wait_for(lambda: b'A01 story cue: kind=0 id=1010101' in game_logs(),
                 'Opening movie / black puzzle / speech / second movie did not enter node 1')
        for node in range(2, 7):
            adb('shell', 'input', 'tap', str(width // 2), str(height // 2))
            wait_for(lambda: f'A01 story cue: kind=0 id={1010100+node}'.encode() in game_logs(),
                     f'Story navigation did not reach node {node}')
        wait_for(lambda: b'A01 story cue: kind=3 id=10002' in game_logs(),
                 'First encounter did not chain all three dialogue callbacks')
        wait_for(lambda: game_logs().count(b'A01 story cue: kind=0 id=1010106') >= 2,
                 'First encounter did not return to node 6 after speech finished')
        adb('shell', 'input', 'tap', str(width // 2), str(height // 2))
        wait_for(lambda: b'A01 story cue: kind=1 id=10039' in game_logs(),
                 'Native puzzle exit did not invoke the warning scene')
        wait_for(lambda: b'A01 story cue: kind=3 id=11005' in game_logs(),
                 'Puzzle speech did not complete into the second speaker')
        wait_for(lambda: game_logs().count(b'A01 story cue: kind=0 id=1010105') >= 2,
                 'Puzzle speech did not return to the original arrival node')
        if b'A01 story stopped:' in game_logs():
            raise RuntimeError('Story emitted a decoder, layout or playback error')
        (OUTPUT / 'synthetic-story-callbacks.png').write_bytes(adb('exec-out', 'screencap', '-p'))
        click_scrolling('Menu')
        click_menu('Save Game')
        wait_for(lambda: b'Game saved.' in current_ui(), 'Menu Save Game did not finish')
        story_save = adb('exec-out', 'run-as', PACKAGE, 'cat', 'files/native-saves/a01-story.jrs')
        if not story_save.startswith(b'JRNSTORY'):
            raise RuntimeError('Story slot contains the wrong save format')
        (OUTPUT / 'story-menu-save.png').write_bytes(adb('exec-out', 'screencap', '-p'))
        adb('shell', 'am', 'force-stop', PACKAGE);begin_log_phase()
        adb('shell', 'am', 'start', '-W', '-n', PACKAGE + '/.MainActivity')
        wait_for(lambda: b'Ready.' in game_logs(), 'Menu restart failed')
        wait_for(lambda: b'Menu artwork: size=2x2' in game_logs(), 'Imported menu artwork did not reach the Android menu')
        click_menu('Load Game');click('Manual save')
        wait_for(lambda: b'Story session: action=load scene=1010105' in game_logs(),
                 'Manual story save did not restore after a process restart')
        wait_for(lambda: b'Inventory' in current_ui(), 'Load Game did not dismiss the menu')
        click_scrolling('Menu');click_menu('Continue')
        wait_for(lambda: b'Inventory' in current_ui(), 'Continue did not return to the game')
        click_scrolling('Menu')
        # A bad manual save must leave the active game resumable.
        adb('shell', f"run-as {PACKAGE} sh -c 'cat > files/native-saves/a01-story.jrs'", input=b'bad story save')
        click_menu('Load Game');click('Manual save')
        wait_for(lambda: b'not a native story save' in current_ui(), 'Corrupted story save was not rejected')
        click_menu('Continue')
        wait_for(lambda: b'Inventory' in current_ui(), 'Failed load damaged the active session')

        click_scrolling('Menu');begin_log_phase();click_menu('New Game')
        wait_for(lambda: b'Audio buffer drained: SYNTH_VOICE_10999' not in game_logs() and b'Dialogue started: id=10999' in game_logs(),
                 'Opening speech did not become active for save testing')
        click_scrolling('Menu');click_menu('Save Game')
        wait_for(lambda: b'Game saved.' in current_ui(), 'Speech save did not finish')
        click_menu('Load Game');click('Manual save')
        wait_for(lambda: re.search(rb'Story session: action=load scene=10045 media_ms=[1-9][0-9]*', game_logs()),
                 'Speech save did not restore its playback offset')
        wait_for(lambda: b'A01 story cue: kind=0 id=1010101' in game_logs(),
                 'Restored speech/movie sequence did not return to node 1')
        if game_logs().count(b'A01 story cue: kind=2 id=2 ') != 1:
            raise RuntimeError('Restored speech fired its completion callback more than once')
        (OUTPUT / 'result.txt').write_text('PASS: x86_64 native launch, JNI inspection, synthetic BMP/AQC display, AQC camera rendering and drag redraw, channel preview frame advancement/stop, synthetic scene traversal, item hit priority/pickup/variable update and inventory display, native checkpoint restoration after process restart and corrupt-load state preservation, reverse-order inventory combination through native dialogs and result restoration, assignment pickup/character offer and revision-3 persistence, DIA subtitle advancement, native CNM movie frames and stereo PCM with streamed/preloaded soundtrack layouts, main menu and original-data setup entry, separate developer tools, manual story save/load across process restart, failed-load preservation, mid-speech resume with exactly one completion callback, story opening / encounter / puzzle callbacks using synthetic assets, concurrent looping ambient refill and scene volume changes, disabled-route/puzzle boundaries, readable localization text, stereo PCM output-buffer consumption and replay, native synthetic WAC/WAS decoding and output-buffer consumption.\nThis does not verify audible sound through a physical speaker. ARM64 hardware and original asset playback remain separate validation gates.\n')
    finally:
        collect_diagnostics()


if __name__ == '__main__':
    main()
