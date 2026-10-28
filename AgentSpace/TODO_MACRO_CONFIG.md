# 宏化 / 配置化 剩余工作清单

- 更新时间：2026-10-28
- 范围：只覆盖"宏化 + 运行期配置化"这条线，以及它带出来的清理项
- 细节看：`CONSTANT_MACRO_REPORT.md`（宏化）、`CONFIG_INJECTION_REPORT.md`（配置化）

## 状态

| 线 | 状态 |
|---|---|
| 宏化 A / B / D 组 | ✅ 完成 |
| 死宏 / 死代码 | ✅ 0 个零引用宏 |
| C / E 组宏化 | ⛔ 不做 |
| 配置骨架 | ✅ 结构体 29 个配置字段（+ 内部 set_mask）+ 默认值 + 校验 + dump + init |
| 文件注入 | ✅ 解析器完成（`key = value`、整行 `#` 注释、CRLF、未知键告警、重复键告警、非法值拒启动） |
| 环境变量注入 | ✅ 29 个键全覆盖（`CWEB_<KEY>`），env 覆盖文件；`CWEB_PARALLEL` 作为 `worker.parallel` 的兼容别名 |
| 变量注入到调用点 | ✅ 29 个字段全部落地（含 `worker.parallel`，`ThreadPool.c` 里不再有 getenv） |
| 示例配置 | ✅ 仓库根 `cweb.conf.example` |
| 单元测试 | ✅ `test_config` 33 项（默认值/文件/env/非法值/边界/缺文件） |
| 池配额（原 L-04） | ⏸ 挂起：仍只统计，以后换真限额（`SIZE_CLASS_FREE_LIST_DESIGN.md`） |

## 一、配置生效顺序（已实现）

```
内置默认值  →  WebStater 入参(IP/PORT)  →  配置文件  →  环境变量
```

- 文件/`CWEB_CONF`/`/etc/cweb.conf`：显式给路径但文件不存在 → **启动失败**；默认路径不存在 → 忽略
- **文件或 env 显式写过的键，会盖掉 `WebStater` 入参**（`CWebConfigIsSet()` 记录的 set_mask 实现），所以 `main.c` 里的 `5343` 只是兜底
- env 名规则：`CWEB_` + 键名大写、`.` → `_`（`listen.port` → `CWEB_LISTEN_PORT`）
- 顺序由 `CWebConfigInit()` 保证：`SetDefaults → LoadFile → ApplyEnv → 归一化 → Validate`

## 二、剩余待做

| # | 事项 | 说明 |
|---|---|---|
| R-01 | 411 用例补进单测 | POST/PUT/PATCH 无 `Content-Length` → 411（目前只在真机脚本里验过） |
| R-02 | 两份长报告打勾 | `CONSTANT_MACRO_REPORT.md` / `CONFIG_INJECTION_REPORT.md` 里把已完成条目标掉 |
| R-03 | 部署文档 | systemd `EnvironmentFile=`、Docker `--env-file` 示例；"容量类改完必须重启" |
| R-04 | 池配额专项（挂起） | 换尺寸类空闲链后，`pool.*` 才变成硬上限 |

## 三、明确不做

| 项 | 原因 |
|---|---|
| 命令行参数（`-f/-p/-w`、`--check-config`） | 本轮只做文件 + env |
| SIGHUP 热重载 / 管理接口 / 配置中心 | 同上 |
| C 组、E 组宏化（`buf[32]`/`path[64]`/`re[1024]`/`events[1024]`/`RSP_HEADER_BLOCK`） | 纯可读性，收益低 |
| 队列布局宏加 `CWEB_` 前缀 | 波及 4 个文件、零行为收益 |

## 四、验收命令

```
make                                         # 0 error
bash AgentSpace/test/run_tests.sh            # 13 个用例 315 项全过
CWEB_CONF=./cweb.conf.example ./main         # 启动日志逐行打印生效配置
CWEB_CONF=./cweb.conf.example CWEB_LISTEN_PORT=8080 ./main   # env 覆盖文件
```
