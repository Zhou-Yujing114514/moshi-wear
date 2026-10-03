/*
 * proto_const.h — protobuf 信封（WearPacket / Account 消息）字段编号
 *
 * ⚠️⚠️⚠️ 待核实（WIP / UNVERIFIED）⚠️⚠️⚠️
 * 协议笔记 §B6 明确指出：protobuf WearPacket / Account / AuthAppVerify /
 * AuthDeviceVerify / AppConfirm / CompanionDevice 的「完整字段编号」在
 * `pb::xiaomi::protocol` 生成代码里，原始仓库未展开，本笔记未给出权威编号。
 *
 * 因此本文件把所有字段编号做成【可配置宏】，并给出一组"最可能"的占位默认值。
 * 真机首次握手失败时，应优先用抓包（nRF Connect / Wireshark BTLE）核对真实编号，
 * 然后修改此处宏即可，无需改动协议栈逻辑。
 *
 * 绝不编造：以下编号若与真机不符，表现为握手第 2/3 步解析失败或签名校验不过，
 * 日志 HS_STEP / PROTO 会打印原始字节用于核对。
 */
#pragma once

#include <stdint.h>

/* ---- WearPacket（Pb 通道 L2 payload 的外层信封）----
 * 推测结构：一个 length-delimited 的 bytes 字段承载内层消息（Account 或 QAIC JSON）。
 * 待核实：真实 Xiaomi WearPacket 可能含更多字段（cmd/seq/channel 等）。 */
#define WP_FIELD_INNER          1   /* 待核实：内层消息 bytes */

/* ---- Account 认证消息（Pb 通道，密钥建立前明文 opcode=1）----
 * 推测：外层 Account 消息按消息类型分字段承载各子消息。 */
/* 各子消息在 Account 信封中的字段号（待核实） */
#define ACC_FIELD_APP_VERIFY    1   /* 待核实：AuthAppVerify */
#define ACC_FIELD_DEV_VERIFY    2   /* 待核实：AuthDeviceVerify */
#define ACC_FIELD_APP_CONFIRM   3   /* 待核实：AuthAppConfirm */
#define ACC_FIELD_DEV_CONFIRM   4   /* 待核实：AuthDeviceConfirm */
/* QAIC/互联 JSON 应用数据在 Account/WearPacket 中的字段号（待核实） */
#define ACC_FIELD_QAIC_PAYLOAD  5   /* 待核实：JSON 文本 bytes */

/* ---- AuthAppVerify{ app_random(16B) } ---- */
#define AV_FIELD_APP_RANDOM     1   /* 待核实：app_random bytes */

/* ---- AuthDeviceVerify{ device_random(16B), device_sign(32B) } ---- */
#define DV_FIELD_DEV_RANDOM     1   /* 待核实 */
#define DV_FIELD_DEV_SIGN       2   /* 待核实 */

/* ---- AuthAppConfirm{ app_sign(32B), encrypt_companion_device(密文‖tag) } ---- */
#define AC_FIELD_APP_SIGN       1   /* 待核实 */
#define AC_FIELD_COMPANION      2   /* 待核实：CCM 密文‖4B tag */

/* ---- AuthDeviceConfirm{ confirm_result(bool) } ---- */
#define DC_FIELD_RESULT         1   /* 待核实：bool varint */

/* ---- CompanionDevice（被 CCM 加密的内层明文 protobuf）---- */
#define CD_FIELD_DEVICE_TYPE    1   /* 待核实：varint */
#define CD_FIELD_DEVICE_NAME    2   /* 待核实：string */
#define CD_FIELD_APP_CAPABILITY 3   /* 待核实：varint (0xFFFFFFFF) */

/* QAIC 历史包名占位（笔记 §10 = "com.fetch"） */
#define QAIC_DEFAULT_PKG        "com.fetch"
