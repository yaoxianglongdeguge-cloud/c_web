# 宏化 / 配置化 剩余工作清单

- 日期：2026-10-28
- 范围：只覆盖"宏化 + 运行期配置化"这条线，以及它带出来的清理项；其它历史报告（`CODE_AUDIT_FINDINGS.md`、`TODO_ISSUES_AND_DEADLOCK_PLAN.md` 等）里的问题不重复
- 细节看：`CONSTANT_MACRO_REPORT.md`（宏化）、`CONFIG_INJECTION_REPORT.md`（配置化）

## 状态

| 线 | 状态 |
|---|---|
| 宏化 A / B / D 组 | ✅ 完成（协议常量、结构布局、KB 取整宏） |
| 死宏 / 死代码 | ✅ 0 个零引用宏；`WORKER_THREAD_NUM_*`、`sleepInit/sleepAdd` 已删 |
| `GlobalResource.h` | ✅ 已清空，宏都还给了使用它的 `.c` |
| 结构可配置化前置 | ✅ `Temp_Pack` 已改「指针 + blocknum」 |
| C / E 组宏化 | ⛔ 不做 |
| 运行期配置化 | ⬜ **0%**（无 `Config/` 模块，只有 `CWEB_GROUP_NUM`、`CWEB_PARALLEL` 两个 env） |
| 池配额（原 L-04） | ⏸ 挂起：先按"仅统计"用，以后换真限额（见 `SIZE_CLASS_FREE_LIST_DESIGN.md`） |

---

## 一、P0：配置骨架（必须）

| # | 事项 | 要点 | 验收 |
|---|---|---|---|
| S-01 | 新建 `Config/CWebConfig.{c,h}` | 结构体 + 默认值（默认 = 当前宏值） | 不配文件时行为与现在完全一致 |
| S-02 | 加载顺序 | 默认 → 文件（`CWEB_CONF` / `-f`）→ 环境变量 | 三层优先级可验证 |
| S-03 | 解析器 | `key = value`、`#` 注释；`strtol`+`endptr`；未知键 warn；字符串超长拒启动 | 单测覆盖 |
| S-04 | 启动校验 | `timer.capacity ≥ 组数×连接数`、`req_total ≥ header+body`、`worker_per_group ≥ 2` … 失败 `exit(1)` | 越界值拒启动 |
| S-05 | 启动打印 | 逐行 `+ key = value` | 日志与文件一致 |
| S-06 | API / 热路径 | `WebStater` 不变 + `WebStaterWithConfig`；每请求路径不读 `g_cfg`（启动时拷进线程结构体） | 无 `g_cfg` 进入热路径 |

## 二、P0：配置项接入（17 个，现状都是硬编码/局部宏）

| # | 键名 | 现值 | 位置 |
|---|---|---|---|
| 1–2 | `listen.addr` / `listen.port` | `127.0.0.1` / `5343` | `AgentSpace/main.c:9` |
| 3 | `listen.backlog` | `100` | `StaterMain/StarterMain.c:79` |
| 4 | `web.root` | `"WebSite"` | `AgentSpace/WebApp/WebApp.c:26` |
| 5 | `group.num` | `CWEB_GROUP_NUM` 默认 1 | `StarterMain.c:93` |
| 6 | `worker.per_group` | `PER_RECEIVER_WORKER 4` | `StarterMain.c:5` |
| 7 | `timer.capacity` | `5000` | `Threads/ReceiverThread.c:28` |
| 8 | `fd_table.init_level` | `4` | `ReceiverThread.c:29` |
| 9 | `receiver.idle_timeout_s` | `60` | `StarterMain.c:127` |
| 10–13 | `pool.recv_http_kb` / `recv_store_kb` / `worker_txt_kb` / `worker_http_kb` | 1024 / 2048 / 2048 / 256 | `ReceiverThread.c:19,21`、`WorkerThread.c:103,107` |
| 14 | `request.max_total_bytes` | `HTTP_MAX_REQUEST_BYTES`（1088KB） | `Include/CWEBCommon.h:12` |
| 15 | `request.max_url_bytes` | `MAX_URL_LEN 8192` | `RequestAnalysiser.c:3` |
| 16 | `request.max_header_bytes` | `MAX_HEADER_LEN 16384` | `RequestAnalysiser.c:4` |
| 17 | `request.max_body_bytes` | `HTTP_MAX_BODY_BYTES`（1MB） | `Include/CWEBCommon.h:11` |

## 三、P1：配置项接入（14 个）

| # | 键名 | 现值 | 位置 |
|---|---|---|---|
| 18 | `task_queue.capacity` | `TASK_QUEUE_NUM 100` | `ReceiverThread.c:4` |
| 19 | `temp_pack.capacity` | `CWEB_TEMP_PACK_CAP 1024` | `Queue/TempPackQueue.h:4`（**结构已就绪，只差传值**） |
| 20 | `receiver.epoll_events` | `events[1024]` | `ReceiverThread.c:76,81` |
| 21 | `receiver.send_batch` | `100` | `ReceiverThread.c:96` |
| 22 | `send.pipeline_window` | `100` | `StarterMain.c:128` |
| 23 | `worker.stall_ms` | `ONE_TASK_TIME 4` | `ReceiverThread.c:5`（用在 `:89`） |
| 24 | `worker.sem_wait_ms` | `100` | `WorkerThread.c:133` |
| 25 | `worker.idle_sleep_us` | `100000` | `WorkerThread.c:126` |
| 26 | `request.temp_init_kb` | `2` | `RequestComeMain.c:28` |
| 27 | `request.temp_pool_max_bytes` | `1MB` | `RequestTempPool.c:4` |
| 28 | `response.header_max_bytes` | `RSP_HEADER_BLOCK 4KB` | `WorkerThread.c:5` |
| 29 | `request.max_header_fields` | `CWEB_ANALYSISER_SPILL_MAX 128` | `AnalysiserHash.h:21` |
| 30 | `epoll.tick_ms` | `CWEB_EPOLL_TICK_MS 2` | `ReceiverThread.c:9` |
| 31 | `error.buf_bytes` | `re[1024]` | `FdConnectOrClose.c:47` |

## 四、清理 / 收尾（与配置化无强依赖，但建议一起收）

| # | 事项 | 说明 |
|---|---|---|
| C-01 | `Include/CWEBCommon.h` 还没提交 | 当前唯一的 untracked 文件 |
| C-02 | `ToOut/foo.h` 已过期 | 10-16 的发布副本，和 `AgentSpace/foo.h` 已不一致（少了 `cweb_listen_port`）。要么同步，要么删 |
| C-03 | `AgentSpace/test/src/test_queues.c` 编不过 | 仍是旧 `WorkerQueue` API（`WorkerQueueInit(&q,3)`、`blocknum`），60 行 error，属既有问题 |
| C-04 | `AgentSpace/test/build/*` 是**已提交的旧二进制** | 源码改了但产物没重编，建议要么重编提交、要么 `git rm --cached` 后加 `.gitignore` |
| C-05 | 测试补齐 | Config 解析单测；411 三条（POST/PUT/PATCH 无 CL）；`TempPack` cap=1/2/3（目前只在 `/tmp` 里跑过） |
| C-06 | 文档同步 | 两份报告里把已完成的 A/B/D、§9-1/9-3/9-4 打勾 |
| C-07 | `cweb.conf.example` | 配合 S-01 一起提供，含"容量类改完要重启"的说明 |

## 五、建议顺序

1. **第 0 批**：C-01 ~ C-04（提交、清理，不动逻辑）
2. **第 1 批**：S-01 ~ S-05（Config 骨架，默认值 = 现状，纯新增）
3. **第 2 批**：P0 第 1–9 项（监听/站点/线程/计时器/超时，均不在热路径）
4. **第 3 批**：P0 第 10–17 项（池配额 + HTTP 限额，注意拷进线程结构体）
5. **第 4 批**：P1 全部（`temp_pack.capacity` 可提前到第 2 批，因为结构已就绪）
6. **第 5 批**：C-05 ~ C-07（测试 + 文档 + 示例配置）

每批验收：`make` → `bash AgentSpace/test/run_tests.sh` → `bash AgentSpace/test/website_test.sh` → `make qps` 对比。

## 六、明确不做的

| 项 | 原因 |
|---|---|
| C 组宏化（`buf[32]`、`path[64]/buf[512]`、`re[1024]`） | 纯可读性，收益低 |
| E 组宏化（`events[1024]`、`RSP_HEADER_BLOCK`） | 同上；`RSP_HEADER_BLOCK` 已是宏 |
| 队列布局宏加 `CWEB_` 前缀 | 波及 4 个文件、零行为收益 |
| L-04 池配额变真限额 | 挂起，等尺寸类池方案（见 `SIZE_CLASS_FREE_LIST_DESIGN.md`） |
