#include "error_code.h"

typedef struct {
    PtcErrorCode code;
    const char *reason;
    const char *message_zh;
} PtcErrorInfo;

static const PtcErrorInfo PTC_ERROR_TABLE[] = {
    {PTC_ERR_OK, "ok", "成功"},
    {PTC_ERR_UNSUPPORTED_VERSION, "unsupported_version", "不支持的协议版本"},
    {PTC_ERR_BAD_REQUEST, "bad_request", "请求格式无效"},
    {PTC_ERR_UNKNOWN_REQUEST_TYPE, "unknown_request_type", "未知请求类型"},
    {PTC_ERR_REQUEST_EXPIRED, "request_expired", "请求已过期"},
    {PTC_ERR_BAD_CODE, "bad_code", "授权码格式无效"},
    {PTC_ERR_BAD_TOKEN_VERSION, "bad_token_version", "授权码版本不支持"},
    {PTC_ERR_UNSUPPORTED_TOKEN_ACTION, "unsupported_action", "授权码动作不支持"},
    {PTC_ERR_BAD_SIGNATURE, "bad_signature", "授权码签名不匹配"},
    {PTC_ERR_BAD_CLOCK, "bad_clock", "系统时间不可用"},
    {PTC_ERR_WRONG_DATE, "wrong_date", "授权码不是今天有效"},
    {PTC_ERR_USED_TOKEN, "used_token", "授权码已经使用过"},
    {PTC_ERR_MINUTES_EXCEED_LIMIT, "minutes_exceed_limit", "授权分钟数超过上限"},
    {PTC_ERR_CODE_COOLDOWN, "code_cooldown", "短码错误次数过多，请稍后再试"},
    {PTC_ERR_DISABLED, "disabled", "后台当前已禁用"},
    {PTC_ERR_UNLIMITED_NOT_ALLOWED, "unlimited_not_allowed", "当前无限制状态不允许改为有限制"},
#ifdef PLAYWISE_DEVICE_LAB
    {PTC_ERR_RAW_BLOCK_NOT_VERIFIED, "raw_block_not_verified", "禁玩能力尚未验证"},
    {PTC_ERR_SUSPEND_NOT_VERIFIED, "suspend_not_verified", "暂停能力尚未验证"},
    {PTC_ERR_PCTL_WRITE_NOT_VERIFIED, "pctl_write_not_verified", "家长控制写入能力尚未验证"},
    {PTC_ERR_PCTL_EFFECT_NOT_VERIFIED, "pctl_effect_not_verified", "家长控制运行时生效能力尚未验证"},
#endif
    {PTC_ERR_PCTL_EFFECT_NOT_OBSERVED, "pctl_effect_not_observed", "家长控制运行时未观察到生效"},
    {PTC_ERR_PCTL_RESTORE_FAILED, "pctl_restore_failed", "家长控制原始设置恢复失败，已进入禁用保护"},
    {PTC_ERR_SETUP_PENDING, "setup_pending", "首次设置尚未完成"},
    {PTC_ERR_RECOVERY_UNAVAILABLE, "recovery_unavailable", "没有可用的安装前恢复快照"},
    {PTC_ERR_RECOVERY_FAILED, "recovery_failed", "恢复安装前家长控制状态失败"},
    {PTC_ERR_COMPATIBILITY_CONFIRMATION_REQUIRED, "compatibility_confirmation_required", "当前环境尚未认证，需要家长确认"},
    {PTC_ERR_PROTECTION_MODE, "protection_mode", "安全前置检查失败，已进入保护模式"},
    {PTC_ERR_PCTL_INIT_FAILED, "pctl_init_failed", "家长控制服务初始化失败"},
    {PTC_ERR_PCTL_READ_FAILED, "pctl_read_failed", "读取家长控制状态失败"},
    {PTC_ERR_PCTL_WRITE_FAILED, "pctl_write_failed", "写入家长控制设置失败"},
    {PTC_ERR_PCTL_BACKUP_FAILED, "pctl_backup_failed", "备份家长控制设置失败"},
    {PTC_ERR_PCTL_LAYOUT_MISMATCH, "pctl_layout_mismatch", "家长控制设置布局不匹配"},
    {PTC_ERR_STORAGE_READ_FAILED, "storage_read_failed", "读取存储文件失败"},
    {PTC_ERR_STORAGE_WRITE_FAILED, "storage_write_failed", "写入存储文件失败"},
    {PTC_ERR_CONFIG_INVALID, "config_invalid", "配置文件无效"},
    {PTC_ERR_RULES_INVALID, "rules_invalid", "规则文件无效"},
    {PTC_ERR_RELEASE_MANIFEST_INVALID, "release_manifest_invalid", "发布清单损坏或组件不一致"},
};

static const PtcErrorInfo *ptc_error_info(PtcErrorCode code)
{
    unsigned int i;
    for (i = 0; i < sizeof(PTC_ERROR_TABLE) / sizeof(PTC_ERROR_TABLE[0]); ++i) {
        if (PTC_ERROR_TABLE[i].code == code) {
            return &PTC_ERROR_TABLE[i];
        }
    }
    return &PTC_ERROR_TABLE[2];
}

const char *ptc_error_reason(PtcErrorCode code)
{
    return ptc_error_info(code)->reason;
}

const char *ptc_error_message_zh(PtcErrorCode code)
{
    return ptc_error_info(code)->message_zh;
}
