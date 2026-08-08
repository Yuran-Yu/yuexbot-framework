# YuexBot SDK API Matrix

This document tracks YuexBot native SDK coverage for OneBot 11, NapCat, and LLBot APIs.

Status meanings:

- `typed`: YuexBot exposes a dedicated C ABI function in `YuexBotApi`.
- `generic`: use `call_onebot_api_as_ex(account_id, action, params_json)`.
- `partial`: events/data are received or visible, but the UI or SDK wrapper is not complete.
- `pending`: not wrapped and not yet verified in YuexBot.

## Core OneBot 11 APIs

| Category | Action | Status | Recommended SDK usage |
| --- | --- | --- | --- |
| Message | `send_private_msg` | typed | `send_private_msg_as` |
| Message | `send_group_msg` | typed | `send_group_msg_as` |
| Message | `send_msg` | typed | `send_msg_as` |
| Message | `send_message` | generic | Compatibility alias through `call_onebot_api*`, routed to `send_group_msg` or `send_private_msg` |
| Message | `delete_msg` | typed | `delete_msg_as` |
| Message | `get_msg` | typed | `get_msg_as` |
| Message | `get_forward_msg` | typed | `get_forward_msg_as` |
| Account | `get_login_info` | typed | `get_login_info_as` |
| Account | `get_status` | typed | `get_status_as` |
| Account | `get_version_info` | typed | `get_version_info_as` |
| Friend | `get_friend_list` | typed | `get_friend_list_as` |
| Friend | `get_stranger_info` | typed | `get_stranger_info_as` |
| Friend | `send_like` | typed | `send_like_as` |
| Group | `get_group_list` | typed | `get_group_list_as` |
| Group | `get_group_info` | typed | `get_group_info_as` |
| Group | `get_group_member_info` | typed | `get_group_member_info_as` |
| Group | `get_group_member_list` | typed | `get_group_member_list_as` |
| Group | `get_group_honor_info` | typed | `get_group_honor_info_as` |
| Group Admin | `set_group_ban` | typed | `set_group_ban_as` |
| Group Admin | `set_group_kick` | typed | `set_group_kick_as` |
| Group Admin | `set_group_admin` | typed | `set_group_admin_as` |
| Group Admin | `set_group_name` | typed | `set_group_name_as` |
| Group Admin | `set_group_whole_ban` | typed | `set_group_whole_ban_as` |
| Group Admin | `set_group_leave` | typed | `set_group_leave_as` |
| Group Admin | `set_group_card` | typed | `set_group_card_as` |
| Group Admin | `set_group_special_title` | typed | `set_group_special_title_as` |
| Request | `set_friend_add_request` | typed | `set_friend_add_request_as` |
| Request | `set_group_add_request` | typed | `set_group_add_request_as` |
| File | `get_group_root_files` | typed | `get_group_root_files_as` |
| File | `get_group_files` | typed | `get_group_files_as` |
| File | `upload_group_file` | typed | `upload_group_file_as` |
| File | `delete_group_file` | typed | `delete_group_file_as` |
| File | `move_group_file` | typed | `move_group_file_as` |
| File | `create_group_folder` | typed | `create_group_folder_as` |
| File | `delete_group_folder` | typed | `delete_group_folder_as` |
| File | `rename_group_folder` | typed | `rename_group_folder_as` |
| Resource | `get_record` | typed | `get_record_as` |
| Resource | `get_image` | typed | `get_image_as` |
| Capability | `can_send_image` | typed | `can_send_image_as` |
| Capability | `can_send_record` | typed | `can_send_record_as` |

## NapCat / LLBot Extensions

These actions vary by implementation and version. YuexBot should call them through the generic bridge until they are promoted to typed SDK wrappers.

| Area | Common actions | Status |
| --- | --- | --- |
| OCR | `ocr_image` | typed |
| RKey / client key | `get_rkey`, `get_clientkey` | typed |
| AI voice | `get_ai_characters`, `send_group_ai_record` | typed |
| Version info | `get_version_info` | typed |
| Stranger info | `get_stranger_info` | typed |
| Group honor | `get_group_honor_info` | typed |
| Media resource | `get_record`, `get_image` | typed |
| Capability | `can_send_image`, `can_send_record` | typed |
| Group request cache | `get_group_ignore_add_request`, `get_group_system_msg` | typed |
| Group mute/status | `get_group_shut_list`, `get_group_at_all_remain` | typed |
| Group profile | `set_group_portrait` | typed |
| Group album | `get_group_album_list`, `get_group_album_media_list` | typed |
| File link/download | `get_group_file_url`, `download_file` | typed |
| Private file | `upload_private_file` | typed |
| Custom face | `fetch_custom_face`, `get_custom_face_url_list` | typed |
| Notices | `notice.notify.*`, `message_sent.*` | partial |
| Rich message | markdown, ark, json cards, forward nodes | partial |

## Event Coverage

| Event | Backend receive | Plugin dispatch | UI specialized operations |
| --- | --- | --- | --- |
| `message.group` | yes | yes | partial |
| `message.private` | yes | yes | partial |
| `message_sent.*` | partial | yes if received | pending |
| `notice.group_increase` | yes | yes | partial |
| `notice.group_decrease` | yes | yes | partial |
| `notice.group_upload` | yes | yes | partial |
| `notice.notify.*` | yes if received | yes | pending |
| `request.friend` | yes | yes | approve/reject |
| `request.group` | yes | yes | approve/reject |
| `meta_event.lifecycle` | yes | yes | log/status |
| `meta_event.heartbeat` | yes | yes | log/status |

## Message Segment Rendering

| Segment | Current UI | Next improvement |
| --- | --- | --- |
| `text` | readable preview | done |
| `reply` | compact preview + raw detail | clickable original message |
| `at` | compact preview + raw detail | member card hover |
| `image` | compact media summary + raw detail | thumbnail preview |
| `record` | compact media summary + raw detail | audio playback |
| `video` | compact media summary + raw detail | video preview |
| `file` | compact media summary + raw detail | download/open panel |
| `json` / card | raw detail | rich card preview |
| `markdown` | raw detail | markdown preview |
| `ark` | raw detail | rich card preview |
| `forward` | raw detail | forward message tree |

## How To Call Generic Extensions

```cpp
const char* result = api->call_onebot_api_as_ex(
    account_id,
    "ocr_image",
    "{\"image\":\"file:///C:/tmp/test.png\"}"
);

if (!result && api->get_last_error) {
    api->log("error", api->get_last_error());
}
```

Prefer `call_onebot_api_as_ex` over `call_onebot_api_ex` in multi-account plugins.

## Typed Extension Helpers

YuexBot also exposes typed wrappers for high-frequency NapCat/LLBot extensions:

```cpp
api->ocr_image_as(account_id, image);
api->get_rkey_as(account_id);
api->get_version_info_as(account_id);
api->get_stranger_info_as(account_id, user_id, 0);
api->get_group_honor_info_as(account_id, group_id, "all");
api->get_record_as(account_id, file, "mp3");
api->get_image_as(account_id, file);
api->can_send_image_as(account_id);
api->can_send_record_as(account_id);
api->get_clientkey_as(account_id);
api->get_group_album_list_as(account_id, group_id);
api->get_group_album_media_list_as(account_id, group_id, album_id);
api->get_group_system_msg_as(account_id);
api->get_group_ignore_add_request_as(account_id);
api->get_group_file_url_as(account_id, group_id, file_id, busid);
api->download_file_as(account_id, url, headers_json);
api->get_ai_characters_as(account_id, group_id, chat_type);
api->send_group_ai_record_as(account_id, group_id, character, text);
```

These helpers still return the native implementation response, because NapCat and LLBot extension
payloads can differ by version.
