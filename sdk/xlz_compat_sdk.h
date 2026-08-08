#pragma once
// XiaoLiZiVM_CppSDK x64-compatible plugin SDK for YuexBot
// Plugins compiled with this header can be loaded by YuexBot framework.
// The ABI is compatible with XiaoLiZi-style plugin architecture:
//   - apprun(pluginkey, apidata) entry point
//   - RecviceGroupMesg / RecvicePrivateMsg / RecviceEventCallBack callbacks
//   - Chinese-named API functions resolved via apidata addresses

#include <cstdint>
#include <cstring>
#include <string>

#if defined(_WIN32)
  #define XLZ_CALL __stdcall
  #if defined(XLZSDK_EXPORTS)
    #define XLZ_API extern "C" __declspec(dllexport)
  #else
    #define XLZ_API extern "C" __declspec(dllimport)
  #endif
#else
  #error "XiaoLiZi compatibility SDK is Windows-only"
#endif

namespace xlz {

// ============ API function pointer types ============
// x64: all pointers are 8 bytes, addresses stored as hex strings in apidata JSON
typedef const char* (XLZ_CALL *Fn_OutputLog)(const char* pluginKey, const char* msg, uint32_t fgColor, uint32_t bgColor);
typedef const char* (XLZ_CALL *Fn_SendFriendMessage)(const char* pluginKey, uint64_t thisQQ, uint64_t friendQQ, const char* msg, int64_t* outRandom, uint32_t* outReq);
typedef const char* (XLZ_CALL *Fn_SendGroupMessage)(const char* pluginKey, uint64_t thisQQ, uint64_t groupQQ, const char* msg, uint32_t anonymous);
typedef const char* (XLZ_CALL *Fn_GetFrameworkQQ)(const char* pluginKey);
typedef uint32_t    (XLZ_CALL *Fn_GetGroupList)(const char* pluginKey, uint64_t thisQQ, void* outBlocks);
typedef uint32_t    (XLZ_CALL *Fn_GetGroupMemberList)(const char* pluginKey, uint64_t thisQQ, uint64_t groupQQ, void* outBlocks);
typedef const char* (XLZ_CALL *Fn_SendGroupTemporaryMessage)(const char* pluginKey, uint64_t thisQQ, uint64_t groupId, uint64_t otherQQ, const char* content, int64_t* outRandom, int32_t* outReq);
typedef const char* (XLZ_CALL *Fn_SendGroupJsonMessage)(const char* pluginKey, uint64_t thisQQ, uint64_t groupQQ, const char* jsonMsg, uint32_t anonymous);
typedef uint32_t    (XLZ_CALL *Fn_MuteGroupMember)(const char* pluginKey, uint64_t thisQQ, uint64_t groupQQ, uint64_t memberQQ, uint32_t duration);
typedef uint32_t    (XLZ_CALL *Fn_RemoveGroupMember)(const char* pluginKey, uint64_t thisQQ, uint64_t groupQQ, uint64_t memberQQ, uint32_t reject);
typedef uint32_t    (XLZ_CALL *Fn_RecallGroupMessage)(const char* pluginKey, uint64_t thisQQ, uint64_t groupQQ, int64_t msgSeq);
typedef uint32_t    (XLZ_CALL *Fn_MuteAll)(const char* pluginKey, uint64_t thisQQ, uint64_t groupQQ, uint32_t enable);
typedef uint32_t    (XLZ_CALL *Fn_QQLike)(const char* pluginKey, uint64_t thisQQ, uint64_t targetQQ);
typedef const char* (XLZ_CALL *Fn_GetGroupCard)(const char* pluginKey, uint64_t thisQQ, uint64_t groupQQ, uint64_t memberQQ);
typedef uint32_t    (XLZ_CALL *Fn_SetGroupCard)(const char* pluginKey, uint64_t thisQQ, uint64_t groupQQ, uint64_t memberQQ, const char* card);
typedef const char* (XLZ_CALL *Fn_GetNicknameForce)(const char* pluginKey, uint64_t thisQQ, uint64_t targetQQ);
typedef const char* (XLZ_CALL *Fn_GetGroupMemberInfo)(const char* pluginKey, uint64_t thisQQ, uint64_t groupQQ, uint64_t otherQQ, void* outData);
typedef const char* (XLZ_CALL *Fn_GetPluginDataDirectory)(const char* pluginKey);
typedef const char* (XLZ_CALL *Fn_GetPluginSelfVersion)(const char* pluginKey);
typedef uint32_t    (XLZ_CALL *Fn_GetFrameworkMainWindowHandle)(const char* pluginKey);
typedef const char* (XLZ_CALL *Fn_GetQQAvatar)(const char* pluginKey, uint64_t otherQQ, uint32_t hd);
typedef const char* (XLZ_CALL *Fn_GetPluginFileName)(const char* pluginKey);
typedef const char* (XLZ_CALL *Fn_GetFrameworkVersion)(const char* pluginKey);
typedef const char* (XLZ_CALL *Fn_GetCurrentOneBotClientType)(const char* pluginKey, uint64_t thisQQ);
typedef const char* (XLZ_CALL *Fn_CallOneBotInterface)(const char* pluginKey, uint64_t thisQQ, const char* sendData, uint32_t noWait);
typedef uint32_t    (XLZ_CALL *Fn_HandleFriendVerificationEvent)(const char* pluginKey, uint64_t thisQQ, uint64_t sourceQQ, uint32_t eventType, const char* seq, uint32_t accept);
typedef uint32_t    (XLZ_CALL *Fn_HandleGroupVerificationEvent)(const char* pluginKey, uint64_t thisQQ, uint64_t groupQQ, uint64_t sourceQQ, uint32_t eventType, const char* seq, uint32_t accept);
typedef const char* (XLZ_CALL *Fn_GetAdministratorList)(const char* pluginKey, uint64_t thisQQ, uint64_t groupQQ);
typedef void        (XLZ_CALL *Fn_ReloadItSelf)(const char* pluginKey, const char* dllPath);
typedef const char* (XLZ_CALL *Fn_UploadFriendImage)(const char* pluginKey, uint64_t thisQQ, uint64_t friendQQ, uint32_t flash, const void* pic, uint32_t size, int32_t w, int32_t h, uint32_t cartoon, const char* preview);
typedef const char* (XLZ_CALL *Fn_UploadGroupImage)(const char* pluginKey, uint64_t thisQQ, uint64_t groupQQ, uint32_t flash, const void* pic, uint32_t size, int32_t w, int32_t h, uint32_t cartoon, const char* preview);

// ============ Event structures (packed, x64 compatible) ============
#pragma pack(push, 1)

struct PrivateMessageEvent {
    int64_t  SenderQQ;
    int64_t  ThisQQ;
    uint32_t MessageReq;
    int64_t  MessageSeq;
    uint32_t MessageReceiveTime;
    int64_t  MessageGroupQQ;
    uint32_t MessageSendTime;
    int64_t  MessageRandom;
    uint32_t MessageClip;
    uint32_t MessageClipCount;
    int64_t  MessageClipID;
    const char* MessageContent;
    uint32_t BubbleID;
    uint32_t MessageType;
    uint32_t MessageSubType;
    uint32_t MessageSubTemporaryType;
    uint32_t RedEnvelopeType;
    void*    SessionToken;
    int64_t  SourceEventQQ;
    const char* SourceEventQQName;
    const char* FileID;
    const char* FileMD5;
    const char* FileName;
    int64_t  MsgGroupId;
};

struct GroupMessageEvent {
    int64_t  SenderQQ;
    int64_t  ThisQQ;
    int32_t  MessageReq;
    int32_t  MessageReceiveTime;
    int64_t  MessageGroupQQ;
    const char* SourceGroupName;
    const char* SenderNickname;
    int32_t  MessageSendTime;
    int64_t  MessageRandom;
    int32_t  MessageClip;
    int32_t  MessageClipCount;
    int64_t  MessageClipID;
    uint32_t MessageType;
    const char* SenderTitle;
    const char* MessageContent;
    const char* ReplyMessageContent;
    int32_t  BubbleID;
    int32_t  GroupChatLevel;
    int32_t  PendantID;
    const char* AnonymousNickname;
    void*    AnonymousFlag;
    const char* ReservedParameters;
    int64_t  AnonymousId;
    int32_t  FontId;
};

struct EventTypeBase {
    int64_t  ThisQQ;
    int64_t  SourceGroupQQ;
    int64_t  OperateQQ;
    int64_t  TriggerQQ;
    int64_t  MessageSeq;
    uint32_t MessageTime;
    const char* SourceGroupName;
    const char* OperateQQName;
    const char* TriggerQQName;
    const char* MessageContent;
    uint32_t EventType;
    uint32_t EventSubType;
};

#pragma pack(pop)

// ============ AppRun context ============
struct AppRunContext {
    const char* pluginKey;
    Fn_OutputLog                    OutputLog;
    Fn_SendFriendMessage            SendFriendMessage;
    Fn_SendGroupMessage             SendGroupMessage;
    Fn_GetFrameworkQQ               GetFrameworkQQ;
    Fn_GetGroupList                 GetGroupList;
    Fn_GetGroupMemberList           GetGroupMemberList;
    Fn_SendGroupTemporaryMessage    SendGroupTemporaryMessage;
    Fn_SendGroupJsonMessage         SendGroupJsonMessage;
    Fn_MuteGroupMember              MuteGroupMember;
    Fn_RemoveGroupMember            RemoveGroupMember;
    Fn_RecallGroupMessage           RecallGroupMessage;
    Fn_MuteAll                      MuteAll;
    Fn_QQLike                       QQLike;
    Fn_GetGroupCard                 GetGroupCard;
    Fn_SetGroupCard                 SetGroupCard;
    Fn_GetNicknameForce             GetNicknameForce;
    Fn_GetGroupMemberInfo           GetGroupMemberInfo;
    Fn_GetPluginDataDirectory       GetPluginDataDirectory;
    Fn_GetPluginSelfVersion         GetPluginSelfVersion;
    Fn_GetFrameworkMainWindowHandle GetFrameworkMainWindowHandle;
    Fn_GetQQAvatar                  GetQQAvatar;
    Fn_GetPluginFileName            GetPluginFileName;
    Fn_GetFrameworkVersion          GetFrameworkVersion;
    Fn_GetCurrentOneBotClientType   GetCurrentOneBotClientType;
    Fn_CallOneBotInterface          CallOneBotInterface;
    Fn_HandleFriendVerificationEvent HandleFriendVerificationEvent;
    Fn_HandleGroupVerificationEvent HandleGroupVerificationEvent;
    Fn_GetAdministratorList         GetAdministratorList;
    Fn_ReloadItSelf                 ReloadItSelf;
    Fn_UploadFriendImage            UploadFriendImage;
    Fn_UploadGroupImage             UploadGroupImage;
};

// Global context set during apprun
inline AppRunContext& GetContext() {
    static AppRunContext ctx = {};
    return ctx;
}

// ============ API name constants (UTF-8 Chinese) ============
static constexpr const char kApi_OutputLog[]              = "\xE8\xBE\x93\xE5\x87\xBA\xE6\x97\xA5\xE5\xBF\x97";
static constexpr const char kApi_SendFriendMessage[]      = "\xE5\x8F\x91\xE9\x80\x81\xE5\xA5\xBD\xE5\x8F\x8B\xE6\xB6\x88\xE6\x81\xAF";
static constexpr const char kApi_SendGroupMessage[]       = "\xE5\x8F\x91\xE9\x80\x81\xE7\xBE\xA4\xE6\xB6\x88\xE6\x81\xAF";
static constexpr const char kApi_GetFrameworkQQ[]         = "\xE5\x8F\x96\xE6\xA1\x86\xE6\x9E\xB6\x51\x51";
static constexpr const char kApi_GetGroupList[]           = "\xE5\x8F\x96\xE7\xBE\xA4\xE5\x88\x97\xE8\xA1\xA8";
static constexpr const char kApi_GetGroupMemberList[]     = "\xE5\x8F\x96\xE7\xBE\xA4\xE6\x88\x90\xE5\x91\x98\xE5\x88\x97\xE8\xA1\xA8";
static constexpr const char kApi_SendGroupTemporaryMsg[]  = "\xE5\x8F\x91\xE9\x80\x81\xE7\xBE\xA4\xE4\xB8\xB4\xE6\x97\xB6\xE6\xB6\x88\xE6\x81\xAF";
static constexpr const char kApi_SendGroupJsonMessage[]   = "\xE5\x8F\x91\xE9\x80\x81\xE7\xBE\xA4\x6A\x73\x6F\x6E\xE6\xB6\x88\xE6\x81\xAF";
static constexpr const char kApi_MuteGroupMember[]        = "\xE7\xA6\x81\xE8\xA8\x80\xE7\xBE\xA4\xE6\x88\x90\xE5\x91\x98";
static constexpr const char kApi_RemoveGroupMember[]      = "\xE5\x88\xA0\xE9\x99\xA4\xE7\xBE\xA4\xE6\x88\x90\xE5\x91\x98";
static constexpr const char kApi_RecallGroupMessage[]     = "\xE6\x92\xA4\xE5\x9B\x9E\xE6\xB6\x88\xE6\x81\xAF\x5F\xE7\xBE\xA4\xE8\x81\x8A";
static constexpr const char kApi_MuteAll[]               = "\xE5\x85\xA8\xE5\x91\x98\xE7\xA6\x81\xE8\xA8\x80";
static constexpr const char kApi_QQLike[]                = "\x51\x51\xE7\x82\xB9\xE8\xB5\x9E";
static constexpr const char kApi_GetGroupCard[]          = "\xE5\x8F\x96\xE7\xBE\xA4\xE5\x90\x8D\xE7\x89\x87";
static constexpr const char kApi_SetGroupCard[]          = "\xE8\xAE\xBE\xE7\xBD\xAE\xE7\xBE\xA4\xE5\x90\x8D\xE7\x89\x87";
static constexpr const char kApi_GetNicknameForce[]      = "\xE5\xBC\xBA\xE5\x88\xB6\xE5\x8F\x96\xE6\x98\xB5\xE7\xA7\xB0";
static constexpr const char kApi_GetGroupMemberInfo[]    = "\xE5\x8F\x96\xE7\xBE\xA4\xE6\x88\x90\xE5\x91\x98\xE4\xBF\xA1\xE6\x81\xAF";
static constexpr const char kApi_GetPluginDataDir[]      = "\xE5\x8F\x96\xE6\x8F\x92\xE4\xBB\xB6\xE6\x95\xB0\xE6\x8D\xAE\xE7\x9B\xAE\xE5\xBD\x95";
static constexpr const char kApi_GetPluginSelfVersion[]  = "\xE5\x8F\x96\xE6\x8F\x92\xE4\xBB\xB6\xE8\x87\xAA\xE8\xBA\xAB\xE7\x89\x88\xE6\x9C\xAC\xE5\x8F\xB7";
static constexpr const char kApi_GetFrameworkMainWnd[]   = "\xE5\x8F\x96\xE6\xA1\x86\xE6\x9E\xB6\xE4\xB8\xBB\xE7\xAA\x97\xE5\x8F\xA3\xE5\x8F\xA5\xE6\x9F\x84";
static constexpr const char kApi_GetQQAvatar[]           = "\xE5\x8F\x96\x51\x51\xE5\xA4\xB4\xE5\x83\x8F";
static constexpr const char kApi_GetPluginFileName[]     = "\xE5\x8F\x96\xE6\x8F\x92\xE4\xBB\xB6\xE6\x96\x87\xE4\xBB\xB6\xE5\x90\x8D";
static constexpr const char kApi_GetFrameworkVersion[]   = "\xE5\x8F\x96\xE6\xA1\x86\xE6\x9E\xB6\xE7\x89\x88\xE6\x9C\xAC";
static constexpr const char kApi_GetCurrentOBType[]      = "\xE5\x8F\x96\xE5\xBD\x93\xE5\x89\x8DOneBot\xE5\xAE\xA2\xE6\x88\xB7\xE7\xAB\xAF\xE7\xB1\xBB\xE5\x9E\x8B";
static constexpr const char kApi_CallOneBotInterface[]   = "\xE8\xB0\x83\xE7\x94\xA8\xE6\x8C\x87\xE5\xAE\x9A\x4F\x6E\x65\x42\x6F\x74\xE6\x8E\xA5\xE5\x8F\xA3";
static constexpr const char kApi_HandleFriendVerify[]    = "\xE5\xA4\x84\xE7\x90\x86\xE5\xA5\xBD\xE5\x8F\x8B\xE9\xAA\x8C\xE8\xAF\x81\xE4\xBA\x8B\xE4\xBB\xB6";
static constexpr const char kApi_HandleGroupVerify[]     = "\xE5\xA4\x84\xE7\x90\x86\xE7\xBE\xA4\xE9\xAA\x8C\xE8\xAF\x81\xE4\xBA\x8B\xE4\xBB\xB6";
static constexpr const char kApi_GetAdminList[]          = "\xE5\x8F\x96\xE7\xAE\xA1\xE7\x90\x86\xE5\xB1\x82\xE5\x88\x97\xE8\xA1\xA8";
static constexpr const char kApi_ReloadItSelf[]          = "\xE9\x87\x8D\xE8\xBD\xBD\xE8\x87\xAA\xE8\xBA\xAB";
static constexpr const char kApi_UploadFriendImage[]     = "\xE4\xB8\x8A\xE4\xBC\xA0\xE5\xA5\xBD\xE5\x8F\x8B\xE5\x9B\xBE\xE7\x89\x87";
static constexpr const char kApi_UploadGroupImage[]      = "\xE4\xB8\x8A\xE4\xBC\xA0\xE7\xBE\xA4\xE5\x9B\xBE\xE7\x89\x87";
// Plugin -> host metadata registration callbacks (called during AppStart by XLZ e-language SDK)
static constexpr const char kApi_SetAppName[]            = "\xE7\xBD\xAE\xE5\xBA\x94\xE7\x94\xA8\xE5\x90\x8D";
static constexpr const char kApi_SetAppAuthor[]          = "\xE7\xBD\xAE\xE5\xBA\x94\xE7\x94\xA8\xE4\xBD\x9C\xE8\x80\x85";
static constexpr const char kApi_SetAppVersion[]         = "\xE7\xBD\xAE\xE5\xBA\x94\xE7\x94\xA8\xE7\x89\x88\xE6\x9C\xAC";
static constexpr const char kApi_SetAppDescription[]     = "\xE7\xBD\xAE\xE5\xBA\x94\xE7\x94\xA8\xE8\xAF\xB4\xE6\x98\x8E";

// ============ Convenience wrapper functions ============
inline void OutputLog(const char* msg, uint32_t fg = 0, uint32_t bg = 16777215) {
    auto& c = GetContext();
    if (c.OutputLog) c.OutputLog(c.pluginKey, msg, fg, bg);
}

inline const char* SendPrivateMessage(int64_t thisQQ, int64_t friendQQ, const char* msg, int64_t* outRandom = nullptr, uint32_t* outReq = nullptr) {
    auto& c = GetContext();
    if (c.SendFriendMessage) return c.SendFriendMessage(c.pluginKey, thisQQ, friendQQ, msg, outRandom, outReq);
    return "";
}

inline const char* SendGroupMessage(int64_t thisQQ, int64_t groupQQ, const char* msg, bool anon = false) {
    auto& c = GetContext();
    if (c.SendGroupMessage) return c.SendGroupMessage(c.pluginKey, thisQQ, groupQQ, msg, anon ? 1u : 0u);
    return "";
}

inline const char* GetFrameworkQQ() {
    auto& c = GetContext();
    if (c.GetFrameworkQQ) return c.GetFrameworkQQ(c.pluginKey);
    return "{}";
}

inline uint32_t GetGroupList(uint64_t thisQQ, void* outBlocks) {
    auto& c = GetContext();
    if (c.GetGroupList) return c.GetGroupList(c.pluginKey, thisQQ, outBlocks);
    return 0;
}

inline uint32_t GetGroupMemberList(uint64_t thisQQ, uint64_t groupQQ, void* outBlocks) {
    auto& c = GetContext();
    if (c.GetGroupMemberList) return c.GetGroupMemberList(c.pluginKey, thisQQ, groupQQ, outBlocks);
    return 0;
}

inline const char* GetGroupMemberInfo(uint64_t thisQQ, uint64_t groupQQ, uint64_t otherQQ, void* outData = nullptr) {
    auto& c = GetContext();
    if (c.GetGroupMemberInfo) return c.GetGroupMemberInfo(c.pluginKey, thisQQ, groupQQ, otherQQ, outData);
    return "{}";
}

inline const char* GetPluginDataDirectory() {
    auto& c = GetContext();
    if (c.GetPluginDataDirectory) return c.GetPluginDataDirectory(c.pluginKey);
    return "";
}

inline const char* GetPluginSelfVersion() {
    auto& c = GetContext();
    if (c.GetPluginSelfVersion) return c.GetPluginSelfVersion(c.pluginKey);
    return "";
}

inline uint32_t GetFrameworkMainWindowHandle() {
    auto& c = GetContext();
    if (c.GetFrameworkMainWindowHandle) return c.GetFrameworkMainWindowHandle(c.pluginKey);
    return 0;
}

inline const char* GetQQAvatar(uint64_t qq, bool hd = false) {
    auto& c = GetContext();
    if (c.GetQQAvatar) return c.GetQQAvatar(c.pluginKey, qq, hd ? 1u : 0u);
    return "";
}

inline const char* GetPluginFileName() {
    auto& c = GetContext();
    if (c.GetPluginFileName) return c.GetPluginFileName(c.pluginKey);
    return "";
}

inline const char* GetFrameworkVersion() {
    auto& c = GetContext();
    if (c.GetFrameworkVersion) return c.GetFrameworkVersion(c.pluginKey);
    return "";
}

inline const char* GetCurrentOneBotClientType(uint64_t thisQQ) {
    auto& c = GetContext();
    if (c.GetCurrentOneBotClientType) return c.GetCurrentOneBotClientType(c.pluginKey, thisQQ);
    return "";
}

inline const char* CallOneBotInterface(uint64_t thisQQ, const char* data, bool noWait = false) {
    auto& c = GetContext();
    if (c.CallOneBotInterface) return c.CallOneBotInterface(c.pluginKey, thisQQ, data, noWait ? 1u : 0u);
    return "{}";
}

inline uint32_t MuteGroupMember(uint64_t thisQQ, uint64_t groupQQ, uint64_t memberQQ, uint32_t duration) {
    auto& c = GetContext();
    if (c.MuteGroupMember) return c.MuteGroupMember(c.pluginKey, thisQQ, groupQQ, memberQQ, duration);
    return 0;
}

inline uint32_t RemoveGroupMember(uint64_t thisQQ, uint64_t groupQQ, uint64_t memberQQ, bool reject = false) {
    auto& c = GetContext();
    if (c.RemoveGroupMember) return c.RemoveGroupMember(c.pluginKey, thisQQ, groupQQ, memberQQ, reject ? 1u : 0u);
    return 0;
}

inline uint32_t RecallGroupMessage(uint64_t thisQQ, uint64_t groupQQ, int64_t msgSeq) {
    auto& c = GetContext();
    if (c.RecallGroupMessage) return c.RecallGroupMessage(c.pluginKey, thisQQ, groupQQ, msgSeq);
    return 0;
}

inline uint32_t MuteAll(uint64_t thisQQ, uint64_t groupQQ, bool enable) {
    auto& c = GetContext();
    if (c.MuteAll) return c.MuteAll(c.pluginKey, thisQQ, groupQQ, enable ? 1u : 0u);
    return 0;
}

inline uint32_t QQLike(uint64_t thisQQ, uint64_t targetQQ) {
    auto& c = GetContext();
    if (c.QQLike) return c.QQLike(c.pluginKey, thisQQ, targetQQ);
    return 0;
}

inline const char* GetGroupCard(uint64_t thisQQ, uint64_t groupQQ, uint64_t memberQQ) {
    auto& c = GetContext();
    if (c.GetGroupCard) return c.GetGroupCard(c.pluginKey, thisQQ, groupQQ, memberQQ);
    return "";
}

inline uint32_t SetGroupCard(uint64_t thisQQ, uint64_t groupQQ, uint64_t memberQQ, const char* card) {
    auto& c = GetContext();
    if (c.SetGroupCard) return c.SetGroupCard(c.pluginKey, thisQQ, groupQQ, memberQQ, card);
    return 0;
}

inline const char* GetNicknameForce(uint64_t thisQQ, uint64_t targetQQ) {
    auto& c = GetContext();
    if (c.GetNicknameForce) return c.GetNicknameForce(c.pluginKey, thisQQ, targetQQ);
    return "";
}

inline uint32_t HandleFriendVerificationEvent(uint64_t thisQQ, uint64_t sourceQQ, uint32_t eventType, const char* seq, bool accept) {
    auto& c = GetContext();
    if (c.HandleFriendVerificationEvent) return c.HandleFriendVerificationEvent(c.pluginKey, thisQQ, sourceQQ, eventType, seq, accept ? 1u : 0u);
    return 0;
}

inline uint32_t HandleGroupVerificationEvent(uint64_t thisQQ, uint64_t groupQQ, uint64_t sourceQQ, uint32_t eventType, const char* seq, bool accept) {
    auto& c = GetContext();
    if (c.HandleGroupVerificationEvent) return c.HandleGroupVerificationEvent(c.pluginKey, thisQQ, groupQQ, sourceQQ, eventType, seq, accept ? 1u : 0u);
    return 0;
}

inline const char* GetAdministratorList(uint64_t thisQQ, uint64_t groupQQ) {
    auto& c = GetContext();
    if (c.GetAdministratorList) return c.GetAdministratorList(c.pluginKey, thisQQ, groupQQ);
    return "[]";
}

inline void ReloadItSelf(const char* dllPath = nullptr) {
    auto& c = GetContext();
    if (c.ReloadItSelf) c.ReloadItSelf(c.pluginKey, dllPath ? dllPath : "empty");
}

} // namespace xlz

// ============ Plugin template (usage: include this, define your callbacks) ============
// Example plugin code:
//
//   #include "XiaoLiZiVM_CppSDK.h"
//
//   static xlz::AppRunContext g_ctx;
//
//   static int OnGroupMessage(const xlz::GroupMessageEvent* ev) {
//       xlz::OutputLog("Got group message!");
//       xlz::SendGroupMessage(ev->ThisQQ, ev->MessageGroupQQ, "Hello!");
//       return 0;
//   }
//
//   XLZ_API const char* XLZ_CALL apprun(const char* a, const char* b) {
//       // Parse apidata and set up ctx (framework does this automatically)
//       return ""{\"name\":\"MyPlugin\",\"version\":\"1.0\"}"";
//   }
//   XLZ_API int XLZ_CALL RecviceGroupMesg(void* data) { return OnGroupMessage((const xlz::GroupMessageEvent*)data); }
//   XLZ_API int XLZ_CALL RecvicePrivateMsg(void* data) { return 0; }
//   XLZ_API int XLZ_CALL RecviceEventCallBack(void* data) { return 0; }
