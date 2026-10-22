# `acceptance/` —— M9 验收与交付包

> 入口文档。M9 出口判据（`ESP32-buddy_M0-M9开发计划.md`）：**跑 G1–G7 全量 + 故障注入 + 证据归档 + manifest（含哈希）+ 许可登记**。

| 文件 | 用途 |
| --- | --- |
| [G1-G7-验收执行清单.md](./G1-G7-验收执行清单.md) | 七条门槛的**跑法、判法、记录法**；含 host 预检与故障注入表 |
| [许可证登记.md](./许可证登记.md) | 逐项许可义务 + 机器可校验哈希 |
| [manifest.json](./manifest.json) | 交付清单：路径 → sha256 → 字节数（`--check` 可校验） |

## 生成与校验

```bash
python3 tools/make-manifest.py            # 重新生成 manifest
python3 tools/make-manifest.py --check    # 校验仓库是否与 manifest 一致
python3 tools/gate-host-smoke.py          # 无需设备的门槛预检（G6-host + 故障注入）
```

> **关于 `git.commit` 字段**：它是**生成时的 HEAD**，而 `manifest.json` 本身随下一次提交落盘，
> 因此该字段**总比包含它的那次提交早一代**。交付时应以 **`--check`（内容哈希）** 为准；
> 若要精确绑定，可在打 tag 后重新生成一次再提交。

## 当前状态（2026-10-22）

| 项 | 状态 |
| --- | --- |
| manifest | ✅ 149 文件 / ≈1.37 MB，`--check` 通过 |
| 许可登记 | ✅ 完成（含 NOTICE/README 事实修正） |
| G6 host 半段 | ✅ 20/20 + 故障 7/7 |
| G1–G7 设备半段 | ⏳ **待设备**（本机无串口） |
| 门槛口径 `Q19`–`Q21` | ⏳ 待拍板（见执行清单 §6） |

## 已知缺口（不算完成的部分）

1. **H 类证据缺失**：G1–G7 尚无一条按 10/10、20/20 记账的完整记录。
2. **G2 与 G5 预算冲突**：25 min × 10 次 ≈ 4h10m > 2h，需按 `Q19` 选口径。
3. **事件入口是脚手架**：`pet_entry_server | pet_sender` 两进程管道，未做单进程守护/重连。
4. **manifest 不含构建产物哈希**：`flash/MANIFEST.txt` 另行记录 `.bin`；两者尚未合并为单一清单。
5. **未发布 tag/Release**（计划明确：除非另行授权）。
