# evidence/B13 —— host 编译器缺失的根因与修复

> 日期：2026-10-16；类别：**D**（环境/配置）；关联缺口 `B13`、里程碑 `M1`
> 结论范围：本文记录**环境诊断**，不是产品测试结果。

## 1. 现象

- WSL(Ubuntu 26.04) 中 `cc`/`gcc`/`clang`/`make` 全部缺失。
- `sudo apt-get install -y build-essential` 失败：`E: Unable to fetch some archives`。
- 错误正文：`Cannot initiate the connection ... (IPv6) - connect (101: Network is unreachable)` 与 IPv4 `connection timed out`。

## 2. 诊断（本次实测）

| 检查 | 命令 | 观察 |
| --- | --- | --- |
| 代理变量 | `env \| grep -i proxy` | `http_proxy=http://<公司代理主机>:<端口>`（存在）**发布前已脱敏** |
| 走代理可达性 | `curl -s -o /dev/null -w '%{http_code} (%{remote_ip})' <url>` | `api.github.com` / `mirrors.tuna.tsinghua.edu.cn` / `archive.ubuntu.com` 均 **200**，`remote_ip=53.136.237.140`（代理） |
| apt 配置 | `grep -ri proxy /etc/apt/...` | **无** |
| apt 历史 | `/var/log/apt/history.log` | 0 字节（无成功安装记录） |

**根因**：`sudo` 默认 `env_reset`，未把 `http_proxy` 传给 apt；apt 不读普通用户的 `http_proxy` 环境变量，只认 `Acquire::*::Proxy` 配置 → apt 直连 → 失败。

## 3. 修复（待执行）

```bash
PROXY=http://<公司代理主机>:<端口>    # 发布前已脱敏：请替换为你所在网络的实际代理
printf 'Acquire::http::Proxy "%s";\nAcquire::https::Proxy "%s";\nAcquire::ForceIPv4 "true";\n' "$PROXY" "$PROXY" | sudo tee /etc/apt/apt.conf.d/95proxy
sudo apt-get update && sudo apt-get install -y build-essential
```

**验证**：`cc --version` 有输出即成功。

## 4. 残余风险 / 后续

- **ESP-IDF 组件下载同样需要代理**：M1 实机构建时用 `http_proxy=$PROXY https_proxy=$PROXY idf.py ...`（或给 IDF 的 component manager 配代理）。
- 代理是**企业内网**地址，换网络（如回家）时 `/etc/apt/apt.conf.d/95proxy` 会失效——届时需移除或改写该文件。**本文件不承诺长期可用。**
- 本文档**不再是阻塞**：一旦 `cc --version` 通过，`B13` 转入"已验证"，可作为 T 类证据的环境前提。
