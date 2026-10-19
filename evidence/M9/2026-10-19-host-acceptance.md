# M9 host 验收记录（自动生成）

> 日期：2026-10-19T18:00:13+08:00；commit：`78b11e1`；类别：**T/C（host）**
> 本记录**不包含** G1–G7 的真机（H）结论；H 必须另按 runbook 执行。

## 环境

```
Linux C133LX090781853 6.18.54.1-microsoft-standard-WSL2 #1 SMP PREEMPT_DYNAMIC Tue Oct  6 19:13:07 UTC 2026 x86_64 GNU/Linux
cc (Ubuntu 15.2.0-16ubuntu1) 15.2.0
Python 3.14.4
```

## 结果

== host C tests ==
== test_pet_render ==
[RUN] parse_rejects_bad_magic
[RUN] parse_rejects_short
[RUN] parse_accepts_valid_pack
[RUN] parse_pack_is_12_frames_of_expected_size
[RUN] draw_paints_opaque_pixel_as_2x_block
[RUN] draw_skips_transparent_pixel
[RUN] draw_clips_at_right_edge
[RUN] draw_clips_at_bottom_edge
[RUN] draw_clips_on_negative_origin
[RUN] gray_renders_grayscale
[RUN] mirror_flips_asymmetric_pixel
[RUN] working_and_input_draw_marks_above_head
[RUN] states_render_differently
[RUN] skit_patrol_moves_head_horizontally
[RUN] skit_kick_ball_bottom_right
[RUN] skit_book_below_head
[RUN] skit_sparkles_above_head
[RUN] skit_peekaboo_duck_closed_eyes
[RUN] skit_scared_jolt_settles
[RUN] skit_sleep_talk_mouth_toggles
[RUN] skit_invalid_id_renders_normal_scene
[RUN] ring05_progress_zero_is_track
[RUN] ring05_quarter_boundary_is_color_start_not_reached
[RUN] ring05_full_covers_cardinals
[RUN] ring05_hole_inside_untouched
[RUN] pomo_view_null_is_bg_only
[RUN] pomo_view_focus_track_ring_and_center_text
[RUN] pomo_view_break_full_ring_green
[RUN] menu_view_null_is_bg_only
[RUN] menu_view_inverts_selected_row_and_title_white
[RUN] menu_view_selected_follows_field
94 checks, 0 failed
== test_pet_state ==
99 checks, 0 failed
== test_pet_card ==
[RUN] text_draw_paints_glyph
[RUN] text_draw_respects_max_chars
[RUN] elapsed_formats
[RUN] card_zone_is_empty_without_card
[RUN] card_zone_has_content_with_card
[RUN] pet_zone_has_content_either_way
[RUN] card_long_title_does_not_overflow
[RUN] two_cards_render_two_compact_rows
[RUN] three_cards_render_three_rows
[RUN] single_card_still_uses_big_card
38 checks, 0 failed
== test_pet_voice ==
[RUN] mapping_is_one_to_one
[RUN] should_play_rules
[RUN] mute_is_toggleable_and_default_off
[RUN] assets_are_valid_pcm
28 checks, 0 failed
== test_pet_link ==
[RUN] task_start_carries_card
[RUN] all_event_names_map
[RUN] card_state_follows_event
[RUN] tolerates_whitespace_unknown_keys_and_newline
[RUN] truncates_overlong_title
[RUN] rejects_malformed_json
[RUN] rejects_bad_version
[RUN] rejects_missing_event
[RUN] rejects_unknown_event
[RUN] rejects_wrong_types
[RUN] rejects_overlong_line
[RUN] tasks_two_cards_parse_with_status_mapping
[RUN] tasks_three_cards_missing_and_unknown_status_default_working
[RUN] tasks_failed_maps_and_four_items_capped_at_three
[RUN] tasks_non_list_ignored_and_single_card_still_works
[RUN] tasks_non_object_items_skipped
[RUN] tasks_item_with_type_error_skipped
[RUN] tasks_and_top_level_card_coexist
[RUN] unknown_key_array_value_no_longer_json_error
[RUN] nested_but_bounded_unknown_value_still_ok
[RUN] malformed_tasks_array_still_json_error
98 checks, 0 failed
== test_pet_pomo ==
[RUN] pomo01_init_defaults
[RUN] pomo02_short_starts_focus
[RUN] pomo03_pause_resume
[RUN] pomo04_focus_completes
[RUN] pomo05_boundary_exact
[RUN] pomo06_done_is_sticky
[RUN] pomo07_focus_then_break_then_idle
[RUN] pomo08_abandon_focus
[RUN] pomo09_alarm_take_once
[RUN] pomo10_daily_reset
[RUN] pomo11_long_in_idle_noop
[RUN] pomo12_abandon_from_paused
[RUN] pomo13_encode_layout_is_le_with_magic
[RUN] pomo13_roundtrip_restores_stats_only
[RUN] pomo13_decode_rejects_and_leaves_struct
[RUN] pomo13_encode_short_cap_returns_zero
96 checks, 0 failed
== test_pet_linebuf ==
[RUN] lf01_simple_line
[RUN] lf02_crlf_stripped
[RUN] lf03_empty_line
[RUN] lf04_two_lines_reset_between
[RUN] lf05_exact_max_ok
[RUN] lf06_overlong_dropped_then_recovers
[RUN] lf07_no_newline_no_line
25 checks, 0 failed
== test_pet_button ==
[RUN] b01_init_idle_none
[RUN] b02_press_edge_none
[RUN] b03_short_on_release
[RUN] b04_long_fires_once
[RUN] b05_boundary_599_600
[RUN] b06_time_backwards_no_long
34 checks, 0 failed
== test_pet_keymap ==
[RUN] k01_duty_key1
[RUN] k02_duty_key2
[RUN] k03_pomo_key1
[RUN] k04_pomo_key2
[RUN] k05_none_event_none_action
12 checks, 0 failed
== test_pet_menu ==
[RUN] m01_init_selects_zero
[RUN] m02_key1_short_toggles_selection
[RUN] m03_key2_short_on_mute_toggles_mute_cmd
[RUN] m04_key2_short_on_status_closes
[RUN] m05_key2_long_closes
[RUN] m06_key1_long_none
[RUN] m07_ev_none_none
15 checks, 0 failed
== test_pet_leisure ==
[RUN] lei01_init_defaults
[RUN] lei02_phase_boundaries
[RUN] lei03_bored_skit_schedule
[RUN] lei04_sleepy_skit_schedule
[RUN] lei05_wake_resets_timer
[RUN] lei05_same_seed_same_intervals
50 checks, 0 failed
== test_pet_layout ==
10 checks, 0 failed
PASS: host C tests
== host Python tests ==
test_completed_with_error_is_error (test_opencode_source.DeriveState.test_completed_with_error_is_error) ... ok
test_completed_without_error_is_idle (test_opencode_source.DeriveState.test_completed_without_error_is_idle) ... ok
test_incomplete_time_is_busy (test_opencode_source.DeriveState.test_incomplete_time_is_busy) ... ok
test_no_msg_is_idle (test_opencode_source.DeriveState.test_no_msg_is_idle) ... ok
test_interval_elapsed_is_due (test_opencode_source.HeartbeatDue.test_interval_elapsed_is_due) ... ok
test_never_sent_is_due (test_opencode_source.HeartbeatDue.test_never_sent_is_due) ... ok
test_within_interval_not_due (test_opencode_source.HeartbeatDue.test_within_interval_not_due) ... ok
test_no_assistant_returns_none (test_opencode_source.LatestAssistant.test_no_assistant_returns_none) ... ok
test_user_and_malformed_rows_skipped (test_opencode_source.LatestAssistant.test_user_and_malformed_rows_skipped) ... ok
test_newest_top_level_wins_child_ignored (test_opencode_source.LatestSession.test_newest_top_level_wins_child_ignored) ... ok
test_no_session_returns_none (test_opencode_source.LatestSession.test_no_session_returns_none) ... ok
test_once_polls_sends_line_returns_zero (test_opencode_source.MainOnce.test_once_polls_sends_line_returns_zero) ... sent task.start
ok
test_busy_done_error_idle_edge_sequence (test_opencode_source.PollTransitions.test_busy_done_error_idle_edge_sequence) ... ok
test_no_session_polls_none (test_opencode_source.PollTransitions.test_no_session_polls_none) ... ok
test_defaults (test_pet_daemon.MainCli.test_defaults) ... ok
test_factory_wiring_port_baud_and_dispatch (test_pet_daemon.MainCli.test_factory_wiring_port_baud_and_dispatch) ... ok
test_back_window_line_calls_focus_once (test_pet_daemon.RunDispatch.test_back_window_line_calls_focus_once) ... ok
test_no_command_line_focus_not_called (test_pet_daemon.RunDispatch.test_no_command_line_focus_not_called) ... ok
test_opened_serial_closed_injected_not (test_pet_daemon.RunDispatch.test_opened_serial_closed_injected_not) ... ok
test_timeout_lines_do_not_count_or_dispatch (test_pet_daemon.RunDispatch.test_timeout_lines_do_not_count_or_dispatch) ... ok
test_blank_line_returns_none (test_pet_devio.ParseDeviceLine.test_blank_line_returns_none) ... ok
test_empty_cmd_returns_none (test_pet_devio.ParseDeviceLine.test_empty_cmd_returns_none) ... ok
test_extra_keys_allowed (test_pet_devio.ParseDeviceLine.test_extra_keys_allowed) ... ok
test_invalid_json_starting_with_brace_returns_none (test_pet_devio.ParseDeviceLine.test_invalid_json_starting_with_brace_returns_none) ... ok
test_json_array_returns_none (test_pet_devio.ParseDeviceLine.test_json_array_returns_none) ... ok
test_log_line_returns_none (test_pet_devio.ParseDeviceLine.test_log_line_returns_none) ... ok
test_missing_cmd_returns_none (test_pet_devio.ParseDeviceLine.test_missing_cmd_returns_none) ... ok
test_non_string_cmd_returns_none (test_pet_devio.ParseDeviceLine.test_non_string_cmd_returns_none) ... ok
test_valid_back_window_command (test_pet_devio.ParseDeviceLine.test_valid_back_window_command) ... ok
test_version_2_returns_none (test_pet_devio.ParseDeviceLine.test_version_2_returns_none) ... ok
test_version_true_returns_none (test_pet_devio.ParseDeviceLine.test_version_true_returns_none) ... ok
test_whitespace_is_stripped (test_pet_devio.ParseDeviceLine.test_whitespace_is_stripped) ... ok
test_body_too_long_413 (test_pet_ingest.IngestErrors.test_body_too_long_413) ... ok
test_content_type_415 (test_pet_ingest.IngestErrors.test_content_type_415) ... ok
test_control_char_project_400 (test_pet_ingest.IngestErrors.test_control_char_project_400) ... ok
test_control_char_title_400 (test_pet_ingest.IngestErrors.test_control_char_title_400) ... ok
test_elapsed_at_device_limit_ok (test_pet_ingest.IngestErrors.test_elapsed_at_device_limit_ok) ... ok
test_elapsed_bool_400 (test_pet_ingest.IngestErrors.test_elapsed_bool_400) ... ok
test_elapsed_negative_400 (test_pet_ingest.IngestErrors.test_elapsed_negative_400) ... ok
test_elapsed_over_device_limit_400 (test_pet_ingest.IngestErrors.test_elapsed_over_device_limit_400) ... ok
test_event_missing_400 (test_pet_ingest.IngestErrors.test_event_missing_400) ... ok
test_event_non_str_400 (test_pet_ingest.IngestErrors.test_event_non_str_400) ... ok
test_method_get_405 (test_pet_ingest.IngestErrors.test_method_get_405) ... ok
test_non_json_body_400 (test_pet_ingest.IngestErrors.test_non_json_body_400) ... ok
test_path_not_found_404 (test_pet_ingest.IngestErrors.test_path_not_found_404) ... ok
test_tasks0_control_char_400 (test_pet_ingest.IngestErrors.test_tasks0_control_char_400) ... ok
test_tasks0_not_dict_400 (test_pet_ingest.IngestErrors.test_tasks0_not_dict_400) ... ok
test_tasks_not_list_400 (test_pet_ingest.IngestErrors.test_tasks_not_list_400) ... ok
test_title_int_type_400 (test_pet_ingest.IngestErrors.test_title_int_type_400) ... ok
test_top_level_array_400 (test_pet_ingest.IngestErrors.test_top_level_array_400) ... ok
test_unknown_event_400 (test_pet_ingest.IngestErrors.test_unknown_event_400) ... ok
test_version_2_400 (test_pet_ingest.IngestErrors.test_version_2_400) ... ok
test_version_missing_400 (test_pet_ingest.IngestErrors.test_version_missing_400) ... ok
test_heartbeat_minimal_line (test_pet_ingest.IngestHappyPath.test_heartbeat_minimal_line) ... ok
test_rich_form_tasks0_wins (test_pet_ingest.IngestHappyPath.test_rich_form_tasks0_wins) ... ok
test_simple_form_ok (test_pet_ingest.IngestHappyPath.test_simple_form_ok) ... ok
test_stats_ignored (test_pet_ingest.IngestHappyPath.test_stats_ignored) ... ok
test_title_truncated_to_63 (test_pet_ingest.IngestHappyPath.test_title_truncated_to_63) ... ok
test_loopback_integration (test_pet_ingest.LoopbackServer.test_loopback_integration) ... ok
test_server_binds_loopback (test_pet_ingest.LoopbackServer.test_server_binds_loopback) ... ok
test_correct_token_200 (test_pet_ingest.TokenAuth.test_correct_token_200) ... ok
test_missing_token_401 (test_pet_ingest.TokenAuth.test_missing_token_401) ... ok
test_wrong_token_401 (test_pet_ingest.TokenAuth.test_wrong_token_401) ... ok
test_junk_lines_are_ignored_not_crashing (test_pet_robustness.DeviceLineFuzz.test_junk_lines_are_ignored_not_crashing) ... ok
test_valid_control_line (test_pet_robustness.DeviceLineFuzz.test_valid_control_line) ... ok
test_deeply_nested_unknown_key_does_not_crash (test_pet_robustness.IngestFuzz.test_deeply_nested_unknown_key_does_not_crash) ... ok
test_huge_elapsed_rejected (test_pet_robustness.IngestFuzz.test_huge_elapsed_rejected) ... ok
test_invalid_utf8_never_reaches_device (test_pet_robustness.IngestFuzz.test_invalid_utf8_never_reaches_device) ... ok
test_nan_title_rejected_as_type (test_pet_robustness.IngestFuzz.test_nan_title_rejected_as_type) ... ok
test_nul_and_control_chars_rejected (test_pet_robustness.IngestFuzz.test_nul_and_control_chars_rejected) ... ok
test_raw_control_byte_is_invalid_json (test_pet_robustness.IngestFuzz.test_raw_control_byte_is_invalid_json) ... ok
test_tasks_item_wrong_type_rejected (test_pet_robustness.IngestFuzz.test_tasks_item_wrong_type_rejected) ... ok
test_malformed_rows_are_skipped (test_pet_robustness.OpenCodeFaults.test_malformed_rows_are_skipped) ... ok
test_missing_db_raises_operational_error (test_pet_robustness.OpenCodeFaults.test_missing_db_raises_operational_error) ... ok
test_control_char_dict_is_framed_but_entry_would_reject (test_pet_robustness.TransportFuzz.test_control_char_dict_is_framed_but_entry_would_reject) ... ok
test_invalid_type_raises (test_pet_robustness.TransportFuzz.test_invalid_type_raises) ... ok
test_full_queue_drops_oldest (test_pet_service.QueueSinkUnit.test_full_queue_drops_oldest) ... ok
test_device_back_window_calls_focus (test_pet_service.ServiceIntegration.test_device_back_window_calls_focus) ... [pet_service] back_window -> focus ok
ok
test_heartbeat_when_idle (test_pet_service.ServiceIntegration.test_heartbeat_when_idle) ... ok
test_http_event_reaches_serial (test_pet_service.ServiceIntegration.test_http_event_reaches_serial) ... ok
test_source_error_does_not_kill_service (test_pet_service.ServiceIntegration.test_source_error_does_not_kill_service) ... [pet_service] opencode poll failed: injected poll failure
ok
test_source_event_reaches_serial (test_pet_service.ServiceIntegration.test_source_event_reaches_serial) ... ok
test_token_required (test_pet_service.ServiceIntegration.test_token_required) ... ok
test_write_error_reopens_and_delivers (test_pet_service.ServiceIntegration.test_write_error_reopens_and_delivers) ... [pet_service] serial write failed: injected write failure
[pet_service] serial reopened
ok
test_compact_json_plus_newline (test_pet_transport.EnsureLineDict.test_compact_json_plus_newline) ... ok
test_non_ascii_raw_not_escaped (test_pet_transport.EnsureLineDict.test_non_ascii_raw_not_escaped) ... ok
test_crlf_normalized_to_single_lf (test_pet_transport.EnsureLineStr.test_crlf_normalized_to_single_lf) ... ok
test_multiple_trailing_newlines_collapse_to_one (test_pet_transport.EnsureLineStr.test_multiple_trailing_newlines_collapse_to_one) ... ok
test_plain_gets_one_newline (test_pet_transport.EnsureLineStr.test_plain_gets_one_newline) ... ok
test_str_encoded_utf8 (test_pet_transport.EnsureLineStr.test_str_encoded_utf8) ... ok
test_trailing_newline_not_duplicated (test_pet_transport.EnsureLineStr.test_trailing_newline_not_duplicated) ... ok
test_int_raises_typeerror (test_pet_transport.EnsureLineType.test_int_raises_typeerror) ... ok
test_list_raises_typeerror (test_pet_transport.EnsureLineType.test_list_raises_typeerror) ... ok
test_main_sends_valid_json_arg (test_pet_transport.PetSenderCli.test_main_sends_valid_json_arg) ... ok
test_close_closes_serial (test_pet_transport.SerialSinkWrites.test_close_closes_serial) ... ok
test_init_does_not_import_serial (test_pet_transport.SerialSinkWrites.test_init_does_not_import_serial) ... ok
test_two_sends_append_two_lines (test_pet_transport.SerialSinkWrites.test_two_sends_append_two_lines) ... ok
test_writes_exactly_ensure_line_bytes (test_pet_transport.SerialSinkWrites.test_writes_exactly_ensure_line_bytes) ... ok

----------------------------------------------------------------------
Ran 98 tests in 6.539s

OK
PASS: host Python tests
== simulator scenes ==
wrote sim/01-idle.bmp (state=0 frame=0)
wrote sim/01b-idle-blink.bmp (state=0 frame=1)
wrote sim/01c-idle-nap.bmp (state=0 frame=1)
wrote sim/02-working.bmp (state=1 frame=4)
wrote sim/03-input.bmp (state=2 frame=5)
wrote sim/04-done.bmp (state=3 frame=2)
wrote sim/05-failed.bmp (state=4 frame=3)
wrote sim/06-offline.bmp (state=5 frame=1)
wrote sim/link-0.bmp (kind=1 card=1 title=BUILD FIRMWARE)
wrote sim/link-1.bmp (kind=2 card=1 title=REVIEW PR)
wrote sim/link-2.bmp (kind=3 card=1 title=RUN TESTS)
wrote sim/link-3.bmp (kind=4 card=1 title=FLASH DEVICE)
wrote sim/cards-2.bmp (card_count=2)
wrote sim/cards-3.bmp (card_count=3)
done.
PASS: simulator scenes

## 结论

**host T/C：PASS**。G1–G7 的 H 类仍为 `NOT_RUN`，需真机执行。
