import importlib.util
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch
import xml.etree.ElementTree as ET
import io
import struct
import sys
import wave

spec = importlib.util.spec_from_file_location('android_smoke', Path(__file__).parents[1] / 'tools/android_smoke.py')
smoke = importlib.util.module_from_spec(spec)
spec.loader.exec_module(smoke)


def ui(*nodes):
    root = ET.Element('hierarchy')
    for node in nodes:
        ET.SubElement(root, 'node', node)
    return ET.tostring(root)


class SmokeHarnessTests(unittest.TestCase):
    def test_puzzle_tap_includes_captured_android_status_bar_offset(self):
        # Failed Actions capture: 320x640 screen, SDL surface below 24px status bar.
        xml = ui({'content-desc': 'Game viewport', 'bounds': '[0,24][320,640]'},
                 {'content-desc': 'Game controls', 'bounds': '[0,549][320,640]'})
        logs = b'A01 puzzle viewport: left=0.000 top=174.500 width=320.000 height=240.000 output=320x616 bottom=91 image=640x480'
        self.assertEqual(smoke.screen_puzzle_point(xml, logs, 140, 315), (70, 356))
        self.assertEqual(smoke.screen_puzzle_point(xml, logs, 305, 155), (152, 276))

    def test_puzzle_tap_rejects_menu_inset_until_game_controls_match(self):
        xml = ui({'content-desc': 'Game viewport', 'bounds': '[0,24][320,640]'},
                 {'content-desc': 'Game controls', 'bounds': '[0,549][320,640]'})
        stale = b'A01 puzzle viewport: left=0.000 top=142.000 width=320.000 height=240.000 output=320x616 bottom=156 image=640x480'
        fresh = b'A01 puzzle viewport: left=0.000 top=174.500 width=320.000 height=240.000 output=320x616 bottom=91 image=640x480'
        self.assertIsNone(smoke.screen_puzzle_point(xml, stale, 140, 315))
        with patch.object(smoke, 'current_ui', return_value=xml), patch.object(smoke, 'game_logs', side_effect=[stale, stale+b'\n'+fresh]), patch.object(smoke, 'adb') as adb, patch.object(smoke.time, 'sleep'):
            smoke.puzzle_tap(140, 315)
            adb.assert_called_once_with('shell', 'input', 'tap', '70', '356')

    def test_puzzle_tap_supports_fullscreen_and_scaled_render_outputs(self):
        fullscreen = ui({'content-desc': 'Game viewport', 'bounds': '[0,0][1280,720]'},
                        {'content-desc': 'Game controls', 'bounds': '[0,640][1280,720]'})
        logs = b'A01 puzzle viewport: left=256.000 top=64.000 width=768.000 height=576.000 output=1280x720 bottom=80 image=640x480'
        self.assertEqual(smoke.screen_puzzle_point(fullscreen, logs, 140, 315), (424, 442))
        scaled = ui({'content-desc': 'Game viewport', 'bounds': '[10,30][650,510]'},
                    {'content-desc': 'Game controls', 'bounds': '[10,430][650,510]'})
        logs = b'A01 puzzle viewport: left=256.000 top=128.000 width=768.000 height=576.000 output=1280x960 bottom=160 image=640x480'
        self.assertEqual(smoke.screen_puzzle_point(scaled, logs, 140, 315), (222, 283))

    def test_puzzle_tap_never_uses_movie_geometry_or_hidden_controls(self):
        xml = ui({'content-desc': 'Game viewport', 'bounds': '[0,24][320,640]'},
                 {'content-desc': 'Game controls', 'bounds': '[0,549][320,640]'})
        movie = b'A01 puzzle viewport: left=0.000 top=292.500 width=320.000 height=20.000 output=320x616 bottom=91 image=32x2'
        self.assertIsNone(smoke.screen_puzzle_point(xml, movie, 140, 315))
        self.assertIsNone(smoke.screen_puzzle_point(ui(), movie, 140, 315))
        self.assertIsNone(smoke.screen_puzzle_point(xml, b'', 140, 315))
        with self.assertRaises(ValueError): smoke.screen_puzzle_point(xml, movie, 640, 315)

    def test_current_ui_retries_idle_and_reads_only_a_fresh_dump(self):
        xml = ui({'text': 'New Game', 'enabled': 'true', 'bounds': '[0,0][100,48]'})
        replies = [b'', b'ERROR: could not get idle state.', b'', b'UI hierchary dumped to file', xml]
        with patch.object(smoke, 'adb', side_effect=replies) as adb, patch.object(smoke.time, 'sleep'):
            self.assertEqual(smoke.current_ui(), xml)
            self.assertEqual(adb.call_args_list[0].args, ('shell', 'rm', '-f', '/data/local/tmp/jerusalem-ui.xml'))
            self.assertEqual(adb.call_args_list[2].args, adb.call_args_list[0].args)
            self.assertEqual(adb.call_count, 5)

    def test_current_ui_reports_persistent_idle_failure_without_reading_old_xml(self):
        replies = [b'', b'ERROR: could not get idle state.']*3
        with patch.object(smoke, 'adb', side_effect=replies) as adb, patch.object(smoke.time, 'sleep'):
            with self.assertRaisesRegex(RuntimeError, 'unavailable after 3 attempts.*could not get idle state'):
                smoke.current_ui()
            self.assertEqual(adb.call_count, 6)
            self.assertFalse(any('cat ' in str(call.args) for call in adb.call_args_list))

    def test_current_ui_does_not_retry_unrelated_dump_errors(self):
        with patch.object(smoke, 'adb', side_effect=[b'', b'ERROR: not connected']) as adb:
            with self.assertRaisesRegex(RuntimeError, 'not connected'):
                smoke.current_ui()
            self.assertEqual(adb.call_count, 2)

    def test_game_logs_exclude_previous_phase_success_and_errors(self):
        logs = b'Old Ready.\nOld Cinema error\nI Jerusalem: CURRENT_PHASE\nNew checkpoint saved\n'
        with patch.object(smoke, '_log_marker', b'CURRENT_PHASE'), patch.object(smoke, 'adb', return_value=logs):
            self.assertEqual(smoke.game_logs(), b'\nNew checkpoint saved\n')

    def test_game_logs_missing_marker_never_reuses_old_success(self):
        with patch.object(smoke, '_log_marker', b'CURRENT_PHASE'), patch.object(smoke, 'adb', return_value=b'Old Ready.\n'):
            self.assertEqual(smoke.game_logs(), b'')

    def test_game_logs_require_an_explicit_phase(self):
        with patch.object(smoke, '_log_marker', None):
            with self.assertRaisesRegex(RuntimeError, 'phase has not started'):
                smoke.game_logs()

    def test_log_phase_is_acknowledged_without_clearing_logcat(self):
        marker = b'JERUSALEM_SMOKE_PHASE_fixed'
        with patch.object(smoke, '_log_marker', None), patch.object(smoke.uuid, 'uuid4') as identifier, patch.object(smoke, 'adb', side_effect=[b'', b'I Jerusalem: '+marker+b'\n']) as adb:
            identifier.return_value.hex = 'fixed'
            smoke.begin_log_phase()
            self.assertEqual(smoke._log_marker, marker)
            self.assertEqual(adb.call_args_list[0].args, ('shell', 'log', '-p', 'i', '-t', 'Jerusalem', marker.decode()))
            self.assertEqual(adb.call_args_list[1].args, ('logcat', '-d', '-s', 'Jerusalem:I', '*:S'))

    def test_checkpoint_evidence_distinguishes_save_load_and_other_logs(self):
        logs = (b'Checkpoint state: action=save node=2 yaw=90.500000 pitch=-12.250000 fov=64.000000 inventory=1\n'
                b'Other status\nCheckpoint state: action=load node=2 yaw=90.500000 pitch=-12.250000 fov=64.000000 inventory=1\n')
        self.assertEqual(smoke.checkpoint_states(logs), [('save', 2, 90.5, -12.25, 64., 1), ('load', 2, 90.5, -12.25, 64., 1)])
        self.assertEqual(smoke.checkpoint_states(b'No checkpoint state'), [])

    def test_preview_frame_evidence(self):
        logs = b'AQC preview frame: channel=1 frame=0\nAQC preview frame: channel=1 frame=1\nOther diagnostic\n'
        self.assertEqual(smoke.preview_states(logs), [(1, 0), (1, 1)])

    def test_offscreen_preview_button_is_scrolled_into_view(self):
        before = ui({'class': 'android.widget.HorizontalScrollView', 'bounds': '[0,100][300,150]'})
        after = ui({'text': 'PREVIEW ANIMATION', 'enabled': 'true', 'bounds': '[20,100][220,150]'})
        with patch.object(smoke, 'current_ui', side_effect=[before, after]), patch.object(smoke, 'adb') as adb:
            smoke.click_scrolling('Preview animation')
            self.assertEqual(adb.call_args_list[0].args[:3], ('shell', 'input', 'swipe'))
            self.assertEqual(adb.call_args_list[1].args, ('shell', 'input', 'tap', '120', '125'))

    def test_scrolling_reverses_to_reach_an_earlier_control(self):
        edge = ui({'class': 'android.widget.HorizontalScrollView', 'bounds': '[0,100][300,150]'})
        found = ui({'text': 'A01 NAVIGATION', 'enabled': 'true', 'bounds': '[20,100][220,150]'})
        with patch.object(smoke, 'current_ui', side_effect=[edge, edge, edge, found]), patch.object(smoke, 'adb') as adb:
            smoke.click_scrolling('A01 navigation')
            self.assertEqual(adb.call_args_list[0].args, ('shell', 'input', 'swipe', '280', '125', '20', '125', '350'))
            self.assertEqual(adb.call_args_list[1].args, ('shell', 'input', 'swipe', '20', '125', '280', '125', '350'))
            self.assertEqual(adb.call_args_list[2].args, ('shell', 'input', 'tap', '120', '125'))

    def test_menu_scrolls_to_developer_tools(self):
        before = ui({'class': 'android.widget.ScrollView', 'scrollable': 'true', 'bounds': '[120,0][320,220]'})
        after = ui({'text': 'Developer Tools', 'enabled': 'true', 'bounds': '[130,150][310,210]'})
        with patch.object(smoke, 'current_ui', side_effect=[before, after]), patch.object(smoke, 'adb') as adb:
            smoke.click_menu('Developer Tools')
            self.assertEqual(adb.call_args_list[0].args, ('shell', 'input', 'swipe', '220', '200', '220', '20', '300'))
            self.assertEqual(adb.call_args_list[1].args, ('shell', 'input', 'tap', '220', '180'))

    def test_menu_reverses_to_reach_new_game(self):
        edge = ui({'class': 'android.widget.ScrollView', 'scrollable': 'true', 'bounds': '[120,0][320,220]'})
        found = ui({'text': 'New Game', 'enabled': 'true', 'bounds': '[130,20][310,80]'})
        with patch.object(smoke, 'current_ui', side_effect=[edge, edge, edge, found]), patch.object(smoke, 'adb') as adb:
            smoke.click_menu('New Game')
            self.assertEqual(adb.call_args_list[1].args, ('shell', 'input', 'swipe', '220', '20', '220', '200', '300'))
            self.assertEqual(adb.call_args_list[2].args, ('shell', 'input', 'tap', '220', '50'))

    def test_artwork_menu_can_open_large_button_actions(self):
        before = ui({'class': 'android.widget.Button', 'text': 'Menu actions', 'enabled': 'true', 'bounds': '[0,200][120,250]'})
        after = ui({'class': 'android.widget.Button', 'content-desc': 'Settings', 'enabled': 'true', 'bounds': '[10,80][180,130]'})
        with patch.object(smoke, 'current_ui', side_effect=[before, after]), patch.object(smoke, 'adb') as adb:
            smoke.click_menu('Settings')
            self.assertEqual(adb.call_args_list[0].args, ('shell', 'input', 'tap', '60', '225'))
            self.assertEqual(adb.call_args_list[1].args, ('shell', 'input', 'tap', '95', '105'))

    def test_action_sheet_title_is_not_treated_as_another_open_button(self):
        before = ui({'class': 'android.widget.TextView', 'text': 'Menu actions', 'bounds': '[0,0][200,40]'},
                    {'class': 'android.widget.ScrollView', 'scrollable': 'true', 'bounds': '[0,40][200,240]'})
        after = ui({'class': 'android.widget.Button', 'content-desc': 'Settings', 'enabled': 'true', 'bounds': '[10,80][180,130]'})
        with patch.object(smoke, 'current_ui', side_effect=[before, after]), patch.object(smoke, 'adb') as adb:
            smoke.click_menu('Settings')
            self.assertEqual(adb.call_args_list[0].args[:3], ('shell', 'input', 'swipe'))
            self.assertEqual(adb.call_args_list[1].args, ('shell', 'input', 'tap', '95', '105'))

    def test_menu_alignment_rejects_shifted_or_duplicated_accessible_controls(self):
        entries = [('New Game', 28, 433), ('Continue', 64, 469), ('Load Game', 100, 449), ('Save Game', 136, 437),
                   ('Settings', 242, 405), ('Visit', 278, 377), ('Encyclopedia', 314, 449), ('Intro', 350, 377),
                   ('Credits', 386, 377), ('Quit', 422, 373)]
        nodes = [{'content-desc': 'Original game menu', 'bounds': '[0,0][640,480]'}]
        nodes += [{'content-desc': label, 'bounds': f'[269,{top}][{right},{top+36}]'} for label, top, right in entries]
        smoke.verify_menu_geometry(ui(*nodes))
        nodes[1]['bounds'] = '[279,28][443,64]'
        with self.assertRaisesRegex(RuntimeError, 'not aligned.*New Game'):
            smoke.verify_menu_geometry(ui(*nodes))
        nodes[1]['bounds'] = '[269,28][433,64]'
        with self.assertRaisesRegex(RuntimeError, 'duplicated.*Quit'):
            smoke.verify_menu_geometry(ui(*nodes, nodes[-1]))

    def test_offscreen_play_audio_uses_the_scrollable_toolbar(self):
        before = ui({'class': 'android.widget.HorizontalScrollView', 'bounds': '[0,100][300,150]'})
        after = ui({'content-desc': 'Play audio', 'enabled': 'true', 'bounds': '[80,100][240,150]'})
        with patch.object(smoke, 'current_ui', side_effect=[before, after]), patch.object(smoke, 'adb') as adb:
            smoke.click_scrolling('Play audio')
            self.assertEqual(adb.call_args_list[0].args[:3], ('shell', 'input', 'swipe'))
            self.assertEqual(adb.call_args_list[1].args, ('shell', 'input', 'tap', '160', '125'))

    def test_movie_below_browser_fold_is_scrolled_into_view(self):
        before = ui({'class': 'android.widget.HorizontalScrollView', 'bounds': '[0,572][320,620]'},
                    {'class': 'android.widget.ListView', 'scrollable': 'true', 'bounds': '[16,94][304,568]'},
                    {'text': 'synthetic-labels.ini', 'bounds': '[16,526][304,568]'})
        after = ui({'text': 'synthetic-movie.CI2', 'enabled': 'true', 'bounds': '[16,142][304,190]'})
        with patch.object(smoke, 'current_ui', side_effect=[before, after]), patch.object(smoke, 'adb') as adb:
            smoke.click_list_item('synthetic-movie.CI2')
            self.assertEqual(adb.call_args_list[0].args, ('shell', 'input', 'swipe', '160', '548', '160', '114', '350'))
            self.assertEqual(adb.call_args_list[1].args, ('shell', 'input', 'tap', '160', '166'))

    def test_asset_list_can_reverse_to_find_an_earlier_row(self):
        edge = ui({'class': 'android.widget.ListView', 'scrollable': 'true', 'bounds': '[16,94][304,568]'})
        found = ui({'text': 'Earlier.CI2', 'bounds': '[16,94][304,142]'})
        with patch.object(smoke, 'current_ui', side_effect=[edge, edge, edge, found]), patch.object(smoke, 'adb') as adb:
            smoke.click_list_item('Earlier.CI2')
            self.assertEqual(adb.call_args_list[1].args, ('shell', 'input', 'swipe', '160', '114', '160', '548', '350'))
            self.assertEqual(adb.call_args_list[2].args, ('shell', 'input', 'tap', '160', '118'))

    def test_missing_asset_still_fails_after_bounded_list_search(self):
        edge = ui({'class': 'android.widget.ListView', 'scrollable': 'true', 'bounds': '[16,94][304,568]'})
        with patch.object(smoke, 'current_ui', return_value=edge) as snapshot, patch.object(smoke, 'adb') as adb:
            with self.assertRaisesRegex(RuntimeError, 'Asset list item not found: missing.CI2'):
                smoke.click_list_item('missing.CI2')
            self.assertEqual(snapshot.call_count, 4)
            self.assertEqual(adb.call_count, 2)

    def test_camera_state_evidence_ignores_other_diagnostics(self):
        logs = (b'Ready.\nPanorama frame: yaw=225.000 pitch=0.000 fov=85.000 size=480x640\n'
                b'Other status\nPanorama frame: yaw=196.667 pitch=-1.500 fov=68.000 size=480x640\n')
        self.assertEqual(smoke.camera_states(logs), [(225., 0., 85.), (196.667, -1.5, 68.)])
        self.assertEqual(smoke.camera_states(b'No camera frame rendered'), [])

    def test_synthetic_aqc_contains_known_colour_bars(self):
        sys.path.insert(0, str(Path(__file__).parents[1] / 'tools'))
        from aqc_decode import decode
        info, rgb = decode(smoke.synthetic_aqc())
        self.assertEqual((info['width'], info['height']), (8, 4))
        self.assertEqual(rgb, bytes([248, 0, 0, 0, 252, 0, 0, 0, 248, 248, 252, 248]) * 8)

    def test_preloaded_cinema_fixture_matches_original_audio_chunk_layout(self):
        def audio_chunks(data):
            position = 192+data[27]*16+struct.unpack_from('<I', data, 28)[0]*8
            chunks = []
            while position < len(data):
                tag = data[position]; position += 1
                size = struct.unpack_from('<I', data, position)[0]
                if tag == ord('Z'):
                    position += 4; chunks.append(data[position:position+size])
                elif tag == ord('T'):
                    position += 8
                else:
                    self.assertIn(tag, b'SU'); position += 47
                position += size
            self.assertEqual(position, len(data))
            return chunks
        streamed = audio_chunks(smoke.synthetic_cinema(3))
        preloaded = audio_chunks(smoke.synthetic_cinema(3, preload_audio=True))
        self.assertEqual([len(chunk) for chunk in streamed], [3528]*3)
        self.assertEqual([len(chunk) for chunk in preloaded], [10584, 0, 0])
        self.assertEqual(b''.join(preloaded), b''.join(streamed))

    def test_compressed_audio_signals_are_nonempty_valid_waves(self):
        sys.path.insert(0, str(Path(__file__).parents[1] / 'tools'))
        from wac_decode import decode
        for name, data in smoke.compressed_audio_fixtures().items():
            with self.subTest(file=name), wave.open(io.BytesIO(decode(data))) as reader:
                self.assertEqual(reader.getframerate(), 22050)
                self.assertGreaterEqual(reader.getnframes(), 22050)
                self.assertTrue(any(reader.readframes(reader.getnframes())))

    def test_android_all_caps_button_regression(self):
        xml = ui({'text': 'BROWSE ASSETS', 'enabled': 'true', 'bounds': '[12,40][152,88]'})
        self.assertEqual(smoke.control_center(xml, 'Browse assets'), (82, 64))

    def test_accessibility_label_survives_theme_or_visible_text_change(self):
        xml = ui({'text': 'Different displayed text', 'content-desc': 'Browse assets', 'bounds': '[10,20][110,60]'})
        self.assertEqual(smoke.control_center(xml, 'Browse assets'), (60, 40))

    def test_disabled_and_clipped_controls_are_not_tapped(self):
        xml = ui({'text': 'Close', 'enabled': 'false', 'bounds': '[0,0][100,100]'},
                 {'text': 'CLOSE', 'bounds': '[300,20][100,60]'},
                 {'text': 'CLOSE', 'bounds': '[-50,20][-10,60]'})
        self.assertIsNone(smoke.control_center(xml, 'Close'))

    def test_diagnostics_survive_unavailable_device(self):
        with tempfile.TemporaryDirectory() as directory, patch.object(smoke, 'OUTPUT', Path(directory)), \
             patch.object(smoke, 'adb', side_effect=OSError('device disconnected')):
            smoke.collect_diagnostics()
            self.assertIn('device disconnected', (Path(directory) / 'diagnostic-errors.txt').read_text())


if __name__ == '__main__':
    unittest.main()
