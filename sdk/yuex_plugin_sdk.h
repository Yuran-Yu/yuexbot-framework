#pragma once

#include <stdint.h>

#ifdef _WIN32
  #define YUEX_PLUGIN_EXPORT extern "C" __declspec(dllexport)
  #define YUEX_PLUGIN_CALL __stdcall
#else
  #define YUEX_PLUGIN_EXPORT extern "C"
  #define YUEX_PLUGIN_CALL
#endif

#define YUEX_PLUGIN_ABI_VERSION 10

enum YuexPluginEventType {
    YUEX_EVENT_MESSAGE = 1,
    YUEX_EVENT_NOTICE = 2,
    YUEX_EVENT_REQUEST = 3,
    YUEX_EVENT_META = 4
};

struct YuexBotApi {
    uint32_t abi_version;
    const char* (YUEX_PLUGIN_CALL *call_onebot_api)(const char* action, const char* params_json);
    // ABI v4 keeps the original raw OneBot call above and adds normalized,
    // account-aware calls at the end of this struct.
    const char* (YUEX_PLUGIN_CALL *get_active_account)();
    const char* (YUEX_PLUGIN_CALL *get_accounts)();
    const char* (YUEX_PLUGIN_CALL *get_login_info)();
    const char* (YUEX_PLUGIN_CALL *get_status)();
    const char* (YUEX_PLUGIN_CALL *get_friend_list)();
    int (YUEX_PLUGIN_CALL *delete_friend)(int64_t user_id);
    const char* (YUEX_PLUGIN_CALL *get_group_list)();
    const char* (YUEX_PLUGIN_CALL *get_group_member_list)(int64_t group_id);
    const char* (YUEX_PLUGIN_CALL *get_group_info)(int64_t group_id);
    const char* (YUEX_PLUGIN_CALL *get_group_member_info)(int64_t group_id, int64_t user_id);
    int (YUEX_PLUGIN_CALL *send_msg)(int msg_type, int64_t target_id, const char* message);
    int (YUEX_PLUGIN_CALL *send_private_msg)(int64_t user_id, const char* message);
    int (YUEX_PLUGIN_CALL *send_group_msg)(int64_t group_id, const char* message);
    int (YUEX_PLUGIN_CALL *delete_msg)(int32_t message_id);
    const char* (YUEX_PLUGIN_CALL *get_msg)(int32_t message_id);
    const char* (YUEX_PLUGIN_CALL *get_forward_msg)(const char* id);
    int (YUEX_PLUGIN_CALL *set_group_ban)(int64_t group_id, int64_t user_id, int32_t duration);
    int (YUEX_PLUGIN_CALL *set_group_kick)(int64_t group_id, int64_t user_id, int reject_add_request);
    int (YUEX_PLUGIN_CALL *set_group_admin)(int64_t group_id, int64_t user_id, int enable);
    int (YUEX_PLUGIN_CALL *set_group_name)(int64_t group_id, const char* group_name);
    int (YUEX_PLUGIN_CALL *set_group_whole_ban)(int64_t group_id, int enable);
    int (YUEX_PLUGIN_CALL *set_group_leave)(int64_t group_id, int is_dismiss);
    int (YUEX_PLUGIN_CALL *set_group_card)(int64_t group_id, int64_t user_id, const char* card);
    int (YUEX_PLUGIN_CALL *set_group_special_title)(int64_t group_id, int64_t user_id, const char* special_title);
    int (YUEX_PLUGIN_CALL *set_friend_add_request)(const char* flag, int approve, const char* remark);
    int (YUEX_PLUGIN_CALL *set_group_add_request)(const char* flag, const char* sub_type, int approve, const char* reason);
    const char* (YUEX_PLUGIN_CALL *get_group_root_files)(int64_t group_id);
    const char* (YUEX_PLUGIN_CALL *get_group_files)(int64_t group_id, const char* folder_id, int32_t start_index);
    int (YUEX_PLUGIN_CALL *upload_group_file)(int64_t group_id, const char* file, const char* name);
    int (YUEX_PLUGIN_CALL *delete_group_file)(int64_t group_id, const char* file_id, int32_t busid);
    int (YUEX_PLUGIN_CALL *move_group_file)(int64_t group_id, const char* file_id, const char* parent_folder, const char* target_folder);
    int (YUEX_PLUGIN_CALL *create_group_folder)(int64_t group_id, const char* folder_name);
    int (YUEX_PLUGIN_CALL *delete_group_folder)(int64_t group_id, const char* folder_id);
    int (YUEX_PLUGIN_CALL *rename_group_folder)(int64_t group_id, const char* folder_id, const char* new_folder_name);
    int (YUEX_PLUGIN_CALL *set_avatar)(const char* file);
    int (YUEX_PLUGIN_CALL *set_nickname)(const char* nickname);
    int (YUEX_PLUGIN_CALL *set_bio)(const char* bio);
    int (YUEX_PLUGIN_CALL *send_like)(int64_t user_id, int32_t times);
    int (YUEX_PLUGIN_CALL *send_group_notice)(int64_t group_id, const char* content, const char* image);
    const char* (YUEX_PLUGIN_CALL *get_group_notice)(int64_t group_id);
    const char* (YUEX_PLUGIN_CALL *get_custom_face_url_list)();
    const char* (YUEX_PLUGIN_CALL *get_group_essence_msg_list)(int64_t group_id);
    const char* (YUEX_PLUGIN_CALL *get_plugin_config)(const char* plugin_id, const char* key, const char* default_value);
    int (YUEX_PLUGIN_CALL *set_plugin_config)(const char* plugin_id, const char* key, const char* value);
    const char* (YUEX_PLUGIN_CALL *get_data_dir)(const char* plugin_id);
    void (YUEX_PLUGIN_CALL *log)(const char* level, const char* message);

    // ABI v4 additions. Return values are UTF-8 JSON strings owned by YuexBot
    // until the next SDK call.
    const char* (YUEX_PLUGIN_CALL *get_sdk_info)();
    const char* (YUEX_PLUGIN_CALL *get_framework_version)();
    const char* (YUEX_PLUGIN_CALL *get_account_status)(const char* account_id_or_qq);
    const char* (YUEX_PLUGIN_CALL *call_onebot_api_as)(const char* account_id_or_qq, const char* action, const char* params_json);
    const char* (YUEX_PLUGIN_CALL *call_onebot_api_ex)(const char* action, const char* params_json);
    const char* (YUEX_PLUGIN_CALL *call_onebot_api_as_ex)(const char* account_id_or_qq, const char* action, const char* params_json);
    const char* (YUEX_PLUGIN_CALL *get_friend_list_as)(const char* account_id_or_qq);
    const char* (YUEX_PLUGIN_CALL *get_group_list_as)(const char* account_id_or_qq);
    const char* (YUEX_PLUGIN_CALL *get_avatar_url)(int64_t user_id, int32_t size);

    // ABI v5 additions. These helpers route through the account-aware OneBot
    // bridge. YuexBot first tries the target account's own runtime transport,
    // then falls back to the active global connection when applicable.
    const char* (YUEX_PLUGIN_CALL *get_login_info_as)(const char* account_id_or_qq);
    const char* (YUEX_PLUGIN_CALL *get_status_as)(const char* account_id_or_qq);
    const char* (YUEX_PLUGIN_CALL *get_group_member_list_as)(const char* account_id_or_qq, int64_t group_id);
    int (YUEX_PLUGIN_CALL *send_msg_as)(const char* account_id_or_qq, int msg_type, int64_t target_id, const char* message);
    int (YUEX_PLUGIN_CALL *send_private_msg_as)(const char* account_id_or_qq, int64_t user_id, const char* message);
    int (YUEX_PLUGIN_CALL *send_group_msg_as)(const char* account_id_or_qq, int64_t group_id, const char* message);

    // ABI v6 additions. Account-aware common message and group/request APIs.
    int (YUEX_PLUGIN_CALL *delete_msg_as)(const char* account_id_or_qq, int32_t message_id);
    const char* (YUEX_PLUGIN_CALL *get_msg_as)(const char* account_id_or_qq, int32_t message_id);
    int (YUEX_PLUGIN_CALL *set_group_ban_as)(const char* account_id_or_qq, int64_t group_id, int64_t user_id, int32_t duration);
    int (YUEX_PLUGIN_CALL *set_group_kick_as)(const char* account_id_or_qq, int64_t group_id, int64_t user_id, int reject_add_request);
    int (YUEX_PLUGIN_CALL *set_group_admin_as)(const char* account_id_or_qq, int64_t group_id, int64_t user_id, int enable);
    int (YUEX_PLUGIN_CALL *set_group_name_as)(const char* account_id_or_qq, int64_t group_id, const char* group_name);
    int (YUEX_PLUGIN_CALL *set_group_whole_ban_as)(const char* account_id_or_qq, int64_t group_id, int enable);
    int (YUEX_PLUGIN_CALL *set_group_card_as)(const char* account_id_or_qq, int64_t group_id, int64_t user_id, const char* card);
    int (YUEX_PLUGIN_CALL *set_friend_add_request_as)(const char* account_id_or_qq, const char* flag, int approve, const char* remark);
    int (YUEX_PLUGIN_CALL *set_group_add_request_as)(const char* account_id_or_qq, const char* flag, const char* sub_type, int approve, const char* reason);

    // ABI v7 note: yuex_plugin_on_event keeps the same function signature, but
    // event_json now includes YuexBot-normalized fields in addition to raw
    // OneBot fields: account_id, account_name, sender_name, group_name,
    // self_id, message_id, target_id, operator_id, and normalized type fields.

    // ABI v8 additions. More account-aware wrappers for OneBot 11 common APIs.
    const char* (YUEX_PLUGIN_CALL *get_forward_msg_as)(const char* account_id_or_qq, const char* id);
    const char* (YUEX_PLUGIN_CALL *get_group_info_as)(const char* account_id_or_qq, int64_t group_id);
    const char* (YUEX_PLUGIN_CALL *get_group_member_info_as)(const char* account_id_or_qq, int64_t group_id, int64_t user_id);
    int (YUEX_PLUGIN_CALL *set_group_leave_as)(const char* account_id_or_qq, int64_t group_id, int is_dismiss);
    int (YUEX_PLUGIN_CALL *set_group_special_title_as)(const char* account_id_or_qq, int64_t group_id, int64_t user_id, const char* special_title);
    const char* (YUEX_PLUGIN_CALL *get_group_root_files_as)(const char* account_id_or_qq, int64_t group_id);
    const char* (YUEX_PLUGIN_CALL *get_group_files_as)(const char* account_id_or_qq, int64_t group_id, const char* folder_id, int32_t start_index);
    int (YUEX_PLUGIN_CALL *upload_group_file_as)(const char* account_id_or_qq, int64_t group_id, const char* file, const char* name);
    int (YUEX_PLUGIN_CALL *delete_group_file_as)(const char* account_id_or_qq, int64_t group_id, const char* file_id, int32_t busid);
    int (YUEX_PLUGIN_CALL *move_group_file_as)(const char* account_id_or_qq, int64_t group_id, const char* file_id, const char* parent_folder, const char* target_folder);
    int (YUEX_PLUGIN_CALL *create_group_folder_as)(const char* account_id_or_qq, int64_t group_id, const char* folder_name);
    int (YUEX_PLUGIN_CALL *delete_group_folder_as)(const char* account_id_or_qq, int64_t group_id, const char* folder_id);
    int (YUEX_PLUGIN_CALL *rename_group_folder_as)(const char* account_id_or_qq, int64_t group_id, const char* folder_id, const char* new_folder_name);
    int (YUEX_PLUGIN_CALL *send_like_as)(const char* account_id_or_qq, int64_t user_id, int32_t times);
    int (YUEX_PLUGIN_CALL *send_group_notice_as)(const char* account_id_or_qq, int64_t group_id, const char* content, const char* image);
    const char* (YUEX_PLUGIN_CALL *get_group_notice_as)(const char* account_id_or_qq, int64_t group_id);
    const char* (YUEX_PLUGIN_CALL *get_custom_face_url_list_as)(const char* account_id_or_qq);
    const char* (YUEX_PLUGIN_CALL *get_group_essence_msg_list_as)(const char* account_id_or_qq, int64_t group_id);

    // ABI v9 additions. Developer-experience helpers for normalized errors
    // and plugin permissions stored in main/data/plugins/<plugin_id>.json.
    // Event-filter functions are kept for ABI compatibility; YuexBot now
    // dispatches all messages and events to enabled plugins by default.
    const char* (YUEX_PLUGIN_CALL *get_last_result)();
    const char* (YUEX_PLUGIN_CALL *get_last_error)();
    const char* (YUEX_PLUGIN_CALL *get_plugin_permissions)(const char* plugin_id);
    int (YUEX_PLUGIN_CALL *has_plugin_permission)(const char* plugin_id, const char* permission);
    int (YUEX_PLUGIN_CALL *set_event_filter)(const char* plugin_id, uint32_t event_mask);
    uint32_t (YUEX_PLUGIN_CALL *get_event_filter)(const char* plugin_id);

    // ABI v9 extension tail. These helpers wrap common NapCat/LLBot extension
    // actions while keeping the generic call_onebot_api_as_ex escape hatch.
    const char* (YUEX_PLUGIN_CALL *ocr_image_as)(const char* account_id_or_qq, const char* image);
    const char* (YUEX_PLUGIN_CALL *get_rkey_as)(const char* account_id_or_qq);
    const char* (YUEX_PLUGIN_CALL *get_clientkey_as)(const char* account_id_or_qq);
    const char* (YUEX_PLUGIN_CALL *get_group_album_list_as)(const char* account_id_or_qq, int64_t group_id);
    const char* (YUEX_PLUGIN_CALL *get_group_album_media_list_as)(const char* account_id_or_qq, int64_t group_id, const char* album_id);
    const char* (YUEX_PLUGIN_CALL *get_group_system_msg_as)(const char* account_id_or_qq);
    const char* (YUEX_PLUGIN_CALL *get_group_ignore_add_request_as)(const char* account_id_or_qq);
    const char* (YUEX_PLUGIN_CALL *get_group_file_url_as)(const char* account_id_or_qq, int64_t group_id, const char* file_id, int32_t busid);
    const char* (YUEX_PLUGIN_CALL *download_file_as)(const char* account_id_or_qq, const char* url, const char* headers_json);
    const char* (YUEX_PLUGIN_CALL *get_ai_characters_as)(const char* account_id_or_qq, int64_t group_id, const char* chat_type);
    int (YUEX_PLUGIN_CALL *send_group_ai_record_as)(const char* account_id_or_qq, int64_t group_id, const char* character, const char* text);

    // ABI v10 additions. Typed wrappers for frequently used OneBot 11
    // APIs that were previously available only through call_onebot_api_as_ex.
    const char* (YUEX_PLUGIN_CALL *get_version_info_as)(const char* account_id_or_qq);
    const char* (YUEX_PLUGIN_CALL *get_stranger_info_as)(const char* account_id_or_qq, int64_t user_id, int no_cache);
    const char* (YUEX_PLUGIN_CALL *get_group_honor_info_as)(const char* account_id_or_qq, int64_t group_id, const char* type);
    const char* (YUEX_PLUGIN_CALL *get_record_as)(const char* account_id_or_qq, const char* file, const char* out_format);
    const char* (YUEX_PLUGIN_CALL *get_image_as)(const char* account_id_or_qq, const char* file);
    const char* (YUEX_PLUGIN_CALL *can_send_image_as)(const char* account_id_or_qq);
    const char* (YUEX_PLUGIN_CALL *can_send_record_as)(const char* account_id_or_qq);
    const char* (YUEX_PLUGIN_CALL *get_cookies_as)(const char* account_id_or_qq, const char* domain);
    const char* (YUEX_PLUGIN_CALL *get_csrf_token_as)(const char* account_id_or_qq);
    const char* (YUEX_PLUGIN_CALL *get_credentials_as)(const char* account_id_or_qq, const char* domain);
    const char* (YUEX_PLUGIN_CALL *get_group_shut_list_as)(const char* account_id_or_qq, int64_t group_id);
    const char* (YUEX_PLUGIN_CALL *get_group_at_all_remain_as)(const char* account_id_or_qq, int64_t group_id);
    int (YUEX_PLUGIN_CALL *set_group_portrait_as)(const char* account_id_or_qq, int64_t group_id, const char* file, int cache);
    int (YUEX_PLUGIN_CALL *upload_private_file_as)(const char* account_id_or_qq, int64_t user_id, const char* file, const char* name);
    const char* (YUEX_PLUGIN_CALL *fetch_custom_face_as)(const char* account_id_or_qq, int32_t count);
};

struct YuexPluginInfo {
    uint32_t abi_version;
    const char* id;
    const char* name;
    const char* version;
    const char* author;
    const char* description;
};

typedef int  (YUEX_PLUGIN_CALL *YuexPluginGetInfoFn)(YuexPluginInfo* info);
typedef int  (YUEX_PLUGIN_CALL *YuexPluginInitFn)(const YuexBotApi* api);
typedef void (YUEX_PLUGIN_CALL *YuexPluginShutdownFn)();
typedef int  (YUEX_PLUGIN_CALL *YuexPluginOnEventFn)(int event_type, const char* event_json);

// XiaoLiZiVM_CppSDK-style compatibility aliases. Keep plugin code close to
// YuexBot while allowing later ABI renaming when the upstream header is present.
typedef YuexBotApi XiaoLiZiBotApi;
typedef YuexPluginInfo XiaoLiZiPluginInfo;
typedef YuexPluginGetInfoFn XiaoLiZiPluginGetInfoFn;
typedef YuexPluginInitFn XiaoLiZiPluginInitFn;
typedef YuexPluginShutdownFn XiaoLiZiPluginShutdownFn;
typedef YuexPluginOnEventFn XiaoLiZiPluginOnEventFn;

#define XiaoLiZiPluginExport YUEX_PLUGIN_EXPORT
#define XiaoLiZiPluginCall YUEX_PLUGIN_CALL
