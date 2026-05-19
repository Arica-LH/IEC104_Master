# IEC104_Master

Linux 下的 IEC 60870-5-104 主站采集程序，使用 C 语言实现，无第三方运行时依赖。

## 功能范围

- 主站主动连接一个或多个 IEC104 网关 TCP `2404` 端口。
- 支持 `STARTDT`、`STOPDT`、`TESTFR`、I/S/U 帧、接收序号确认和断线重连。
- 支持总召 `C_IC_NA_1`，按配置周期重新总召，适合长时间连续运行。
- 支持遥信解析：`M_SP_NA_1`、`M_DP_NA_1`、`M_SP_TB_1`、`M_DP_TB_1`。
- 支持遥测解析：`M_ME_NA_1`、`M_ME_NB_1`、`M_ME_NC_1` 及对应带时标类型。
- 支持电能累计量解析：`M_IT_NA_1`、`M_IT_TB_1`。
- 不实现遥控、遥调功能，不发送控制类命令。
- 电表电量突发值会输出 `WARN ENERGY_SPIKE` 日志，便于快速识别异常。
- 支持前台运行、传统 daemon 模式和 systemd 守护保护。

## 编译

```sh
make
```

生成可执行文件：

```sh
./iec104-master
```

## 本地前台运行

如果当前用户没有 `/run` 和 `/var/log` 写权限，建议复制配置并改成用户可写路径：

```sh
cp config/iec104_master.conf /tmp/iec104_master.conf
```

修改 `/tmp/iec104_master.conf`：

```conf
gateway.1.name = gateway1
gateway.1.host = 192.168.1.10
gateway.1.port = 2404
gateway.1.common_address = 1
gateway.1.enabled = true
pid_file = /tmp/iec104-master.pid
log_file = /tmp/iec104-master.log
debug_log_file = /tmp/iec104-master/debug/iec104-master-debug.log
debug_level = detail
```

启动：

```sh
./iec104-master -c /tmp/iec104_master.conf -f
```

## 配置说明

默认配置文件在 [config/iec104_master.conf](config/iec104_master.conf)。

- `gateway.N.name`：网关名称，用于日志标识，最多支持 `32` 个网关。
- `gateway.N.host`：网关 IP 或域名，也兼容写作 `gateway.N.slave_host`。
- `gateway.N.port`：网关 IEC104 端口，默认 `2404`，也兼容写作 `gateway.N.slave_port`。
- `gateway.N.common_address`：公共地址，通常由现场规约表确定，也兼容写作 `gateway.N.ca`。
- `gateway.N.enabled`：是否启用该网关，默认启用。
- `debug_level`：打印等级，`off` 只输出告警和错误，`info` 输出大致运行信息，`detail` 输出详细调试信息。
- `log_file`：普通日志文件，记录 `INFO/WARN/ERROR`。
- `debug_log_file`：详细调试日志文件，记录 `DEBUG`，建议放在独立 `debug` 日志目录；程序会自动创建父目录。
- `general_interrogation_interval_sec`：总召周期，设置为 `0` 表示只在连接成功后总召一次。
- `test_frame_interval_sec`：链路保活 TESTFR 周期。
- `reconnect_initial_sec` / `reconnect_max_sec`：断线后的指数退避重连时间。
- `energy_spike_abs_threshold`：电能累计量绝对跳变阈值。
- `energy_spike_rate_threshold`：电能累计量相对倍率跳变阈值。
- `log_all_yc`：是否记录未变化遥测。
- `log_unchanged_yx`：是否记录未变化遥信。

多网关示例：

```conf
gateway.1.name = main-gateway
gateway.1.host = 192.168.1.10
gateway.1.port = 2404
gateway.1.common_address = 1

gateway.2.name = backup-gateway
gateway.2.host = 192.168.1.11
gateway.2.port = 2404
gateway.2.common_address = 1
gateway.2.enabled = true
```

旧配置项 `slave_host`、`slave_port`、`common_address` 仍兼容，会作为单网关配置处理；不要和 `gateway.N.*` 混用。

命令行参数 `-v` 会临时覆盖配置文件，把 `debug_level` 设置为 `detail`，便于现场排查。

突发电量日志示例：

```text
2026-05-18 15:00:00.123 [WARN] [gateway1] ENERGY_SPIKE ca=1 ioa=1001 previous=1200.000 current=8500.000 delta=7300.000 flags=0x00 threshold_abs=1000.000 threshold_rate=5.000
```

## systemd 部署

安装程序和服务文件：

```sh
sudo make install
```

创建运行用户：

```sh
sudo useradd --system --no-create-home --shell /usr/sbin/nologin iec104
```

编辑配置：

```sh
sudo vi /etc/iec104-master/iec104_master.conf
```

启动并设置开机自启：

```sh
sudo systemctl daemon-reload
sudo systemctl enable --now iec104-master
```

查看状态和日志：

```sh
systemctl status iec104-master
journalctl -u iec104-master -f
tail -f /var/log/iec104-master/iec104-master.log
tail -f /var/log/iec104-master/debug/iec104-master-debug.log
```

服务文件 [systemd/iec104-master.service](systemd/iec104-master.service) 使用：

- `Restart=always`：进程异常退出后自动拉起。
- `RestartSec=5`：5 秒后重启。
- `RuntimeDirectory=iec104-master`：创建 PID 目录。
- `LogsDirectory=iec104-master`：创建基础日志目录，程序会自动创建独立 debug 子目录。
- `NoNewPrivileges`、`ProtectSystem`、`ProtectHome`：限制服务权限。

## 代码结构

- [src/main.c](src/main.c)：程序入口、参数解析、信号处理。
- [src/iec104/iec104.c](src/iec104/iec104.c)：IEC104 多网关线程调度、会话循环、总召、保活和重连。
- [src/iec104/iec104_io.c](src/iec104/iec104_io.c)：TCP 连接、socket 超时、IEC104 APDU 读写。
- [src/iec104/iec104_frame.c](src/iec104/iec104_frame.c)：IEC104 I/S/U 帧发送和帧状态处理。
- [src/iec104/iec104_asdu.c](src/iec104/iec104_asdu.c)：ASDU 解析，遥信、遥测、电能累计量分发。
- [src/iec104/iec104_util.c](src/iec104/iec104_util.c)：IEC104 字节序读取和时间辅助函数。
- [include/iec104](include/iec104)：IEC104 模块头文件。
- [src/point_store/point_store.c](src/point_store/point_store.c)：遥信、遥测、电能累计量状态缓存和突发值识别。
- [include/point_store](include/point_store)：点表缓存模块头文件。
- [src/config/config.c](src/config/config.c)：配置文件加载和配置项分发。
- [src/config/config_parser.c](src/config/config_parser.c)：配置值解析工具。
- [src/config/config_gateway.c](src/config/config_gateway.c)：多网关配置解析和校验。
- [include/config](include/config)：配置模块头文件。
- [src/logging/logging.c](src/logging/logging.c)：日志输出。
- [include/logging](include/logging)：日志模块头文件。
- [src/process/process.c](src/process/process.c)：daemon 化和 PID 文件锁。
- [include/process](include/process)：进程模块头文件。
