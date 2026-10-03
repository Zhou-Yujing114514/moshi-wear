# sign/ 目录说明

本目录用于 Vela 快应用的签名材料，供 `aiot release` 打包生产 rpk。

## 文件
- `private.pem`：私钥，权限 **600**，**绝不提交/推送公开仓库**（已被顶层 .gitignore 屏蔽）。
- `certificate.pem`：自签证书，可入仓库，供设备侧校验 rpk。

## 证书重建记录
- **2026-10-03（v2，当前有效）**：因旧私钥曾被误推公开仓库，旧证书全部作废，已用 openssl
  重新生成全新自签证书对（覆盖 `private.pem` / `certificate.pem`，保留文件名以兼容 `aiot release`）。
  - 新主题：`CN=mosshi-quickapp-v2, O=mosshi, C=CN`，有效期 2026-10-03 ~ 2036-09-30。
  - 新 SHA256 指纹：`20:FA:C7:48:D9:B3:67:AB:C3:6A:96:1E:20:A1:B7:8E:ED:A3:34:5B:40:A5:F2:2A:F7:A3:97:6C:AD:FE:EE:E9`
- **旧证书（已作废，勿用）**：主题 `CN=moshi-test`，SHA256 指纹
  `37:83:C6:5C:C0:DB:0A:8E:F8:CB:C1:08:33:07:1F:2D:10:17:56:AB:E2:7B:17:DB:B3:44:2D:29:A2:CD:88:48`，
  对应旧 rpk 已全部重签作废。

## 生成命令（如需再次重建）
```
openssl req -newkey rsa:2048 -nodes -keyout private.pem \
  -x509 -days 3650 -out certificate.pem -subj "/CN=<你的CN>/O=<你的O>/C=<你的国家>"
chmod 600 private.pem
```

## 注意
- **私钥（private.pem）绝不公开、绝不推仓库**；仅 certificate.pem 可入库。
- `.gitignore` 已屏蔽 `quickapp-moshi-novel/sign/private.pem`（可用
  `git check-ignore -v sign/private.pem` 复核）。
