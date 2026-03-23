
# 更新日志

## 2.7.4
- [feat] NavMesh Generator 添加了一个坐标配置管理器
    1.支持选点添加或更新到配置
    2.支持导入导出json、python
    3.支持下载集合或单个地图配置
    4.支持浏览器本地存储（30s自动存储
- [fix] 修复navigation补帧触发导致脱离生命周期出现的bug
- [fix] c# sdk event中补全一些参数和注释 [Issue #125](https://github.com/KBEngineLab/KBEngine-Nex/issues/125)

## 2.7.3

- [fix] entity添加isOnNavigate，使用navigateToDetour时，设置isOnGround为true的同时也能下发y轴
- [fix] 修复navigate不绕过障碍物的bug
- [fix] 修复navigate持续触发时卡顿的bug
- [fix] 修复bots不触发心跳导致被卸载的bug
- [fix] 【KBEX】 启动多个pycharm时，进程管理有问题  [Issue #111](https://github.com/KBEngineLab/KBEngine-Nex/issues/111)
- [feat] bots接入logger进程，用于单元测试 [#119](https://github.com/KBEngineLab/KBEngine-Nex/issues/119)
- [perf] vcpkg 依赖固定版本及安装脚本优化 [#115](https://github.com/KBEngineLab/KBEngine-Nex/issues/115)
- [perf] 【KBEX】优化调用链不清晰时的补全逻辑
- [feat] 【KBEX】添加Bots快捷启动入口

## 2.7.2

- [fix] 修复EntityComponent里的方法回调，需要owner实现该方法后，EntityComponent的回调才会被调用的BUG [Issue #107](https://github.com/KBEngineLab/KBEngine-Nex/issues/107)
- [fix] mysql9中md5内置方法被移除 [Issue #108](https://github.com/KBEngineLab/KBEngine-Nex/issues/108)
- [update] mongodbc 升级到2.2.2 [Issue #113](https://github.com/KBEngineLab/KBEngine-Nex/issues/113)

## 2.7.1

- [update] kbex 调试下，entity.xxx 不支持输出的问题 [Issue #102](https://github.com/KBEngineLab/KBEngine-Nex/issues/102)
- [fix] 同时启动多个不同类型数据库，watcher冲突的bug [Issue #103](https://github.com/KBEngineLab/KBEngine-Nex/issues/103)
- [update] mongodb authSource应该验证对应的数据库，而不是admin [Issue #105](https://github.com/KBEngineLab/KBEngine-Nex/issues/105)
- [update] kbex 增加entity快捷操作功能 [Issue #86](https://github.com/KBEngineLab/KBEngine-Nex/issues/86)

## 2.7.0

由于本次更新中，navmesh属于底层破坏性更新，所以直接调整为一个大版本更新

- [feat] 新增navigateToDetour方法，用于使用Detour导航，原navigate方法不变（points导航） [Issue #96](https://github.com/KBEngineLab/KBEngine-Nex/issues/96)
  - Detour导航可以在服务端贴合navmesh高度，在多层建筑中非常有用
- [feat] recastnavigation升级，并改为由vcpkg导入 [Issue #74](https://github.com/KBEngineLab/KBEngine-Nex/issues/74)
  - 重要：navmesh升级后，为了保持多客户端兼容和未来插件升级兼容，由之前的左手坐标系转换为官方支持的-z右手坐标系（Recast Navigation / Three.js），KBE层是+Z的右手坐标
  - 客户端侧所有的坐标同步都要做对应手系的翻转
  - xyz分别为roll、pitch、yaw
  - unity:
    - 位置：x = -x ,y = y ,z = z
    - 朝向：yaw = -z
  - Cocos Creator:
    - 位置：-x = x ，y = z ，z = y 
    - 朝向：yaw + 180
  - Godot
    - 位置：-x = x ，y = z ，z = y 
    - 朝向：yaw + 180
  - UE
    - 位置：x = x * 100，y = z * 100 ，z = y * 100
    - 朝向：yaw + 90
- [feat] navmesh 周边工具，一个web端的navmesh生成工具（https://navmesh.kbelab.com/） [Issue #58](https://github.com/KBEngineLab/KBEngine-Nex/issues/58)
- [feat] mongodb接入 [Issue #59](https://github.com/KBEngineLab/KBEngine-Nex/issues/59)
- [feat] 原生c++ sdk [Issue #60](https://github.com/KBEngineLab/KBEngine-Nex/issues/60)
- [feat] 原生cxx ue5 demo+原生cxx demo [Issue #67](https://github.com/KBEngineLab/KBEngine-Nex/issues/67)
- [feat] 文档完善 docker使用教程，云服务器部署教程，kbex docker教程 [Issue #64](https://github.com/KBEngineLab/KBEngine-Nex/issues/64)
- [feat] WebConsole 全新重构 [Issue #44](https://github.com/KBEngineLab/KBEngine-Nex/issues/44)
- [feat] csharp sdk ，websocket 端口和域名映射支持 [Issue #50](https://github.com/KBEngineLab/KBEngine-Nex/issues/50)
- [feat] ts sdk ，websocket 端口和域名映射支持 [Issue #51](https://github.com/KBEngineLab/KBEngine-Nex/issues/51)
- [feat] webconsole 新增用户时配置权限 [Issue #62](https://github.com/KBEngineLab/KBEngine-Nex/issues/62)
- [feat] kbex 添加日志直连功能，用于外部启动引擎时连接日志 [Issue #61](https://github.com/KBEngineLab/KBEngine-Nex/issues/61)
- [feat] kbex 插件更优的docker支持 [Issue #55](https://github.com/KBEngineLab/KBEngine-Nex/issues/55)
- [feat] dockerfile 以及基础镜像 [Issue #56](https://github.com/KBEngineLab/KBEngine-Nex/issues/56)
- [fix] webconsole 创建用户时，设置用户扩展数据报错 [Issue #53](https://github.com/KBEngineLab/KBEngine-Nex/issues/53)
- [fix] webconsole py控制台无法多行输入的bug [Issue #52](https://github.com/KBEngineLab/KBEngine-Nex/issues/52)
- [fix] kbex 调试模式异常输出的bug [Issue #63](https://github.com/KBEngineLab/KBEngine-Nex/issues/63)
- [fix] 修复ts sdk里event Fire没有立即触发导致的延迟
- [delete] 删除底层redis持久化实现 [Issue #71](https://github.com/KBEngineLab/KBEngine-Nex/issues/71)
- [update] 基础demo 全面升级，适配服务端navmesh （unity、cocos、godot、ue5）
- [update] assets移除一些历史spaces配置，所有基础demo统一在一个space配置（kbengine_all_demo）下实现，加速服务端启动

## v2.6.3

- [feat] 系统回调支持asyncio [Issue #1](https://github.com/KBEngineLab/KBEngine-Nex/issues/1)
- [feat] 新增 ts sdk  [Issue #6](https://github.com/KBEngineLab/KBEngine-Nex/issues/6)
- [feat] 添加原生C# SDK，支持unity和GODOT [Issue #15](https://github.com/KBEngineLab/KBEngine-Nex/issues/15) [Issue #6](https://github.com/KBEngineLab/KBEngine-Nex/issues/6)
- [feat] 原生C# SDK支持unity websocket [Issue #29](https://github.com/KBEngineLab/KBEngine-Nex/issues/29)
- [feat] vcpkg 支持，为未来支持arm处理器编译做准备 [Issue #34](https://github.com/KBEngineLab/KBEngine-Nex/issues/34)
- [feat] makefile迁移到cmake [Issue #13](https://github.com/KBEngineLab/KBEngine-Nex/issues/13)
- [feat] 升级三方依赖（ "fmt",log4cxx","zlib","hiredis","expat","apr","apr-util", "curl"） [Issue #32](https://github.com/KBEngineLab/KBEngine-Nex/issues/32)
- [feat] ubuntu-24.x /25.x支持 [Issue #33](https://github.com/KBEngineLab/KBEngine-Nex/issues/33)
- [feat] Linux/windows 一键编译脚本支持 [Issue #25](https://github.com/KBEngineLab/KBEngine-Nex/issues/25)
- [feat] 数据库创建table时，给字段添加上注释 [Issue #23](https://github.com/KBEngineLab/KBEngine-Nex/issues/23)
- [feat] linux arm支持 [Issue #38](https://github.com/KBEngineLab/KBEngine-Nex/issues/38)
- [feat] linux下cmake编译，Hybrid默认启用ASan，Release、Evaluation默认启用FORTIFY_SOURCE，可根据实际情况开关
- [feat] base、cell、interfaces asyncioRepeatOffset配置支持
- [fix] 添加mysqlclient缺失的2个dll
- [fix] 修复sync_item_to_db时， utf8mb4 中每次启动都重复同步一次UNICODE字段的BUG


## v2.6.2

- [feat] 添加dockerfile
- [feat] python 3.7.x升级 -> 3.13.5 [Issue #2](https://github.com/KBEngineLab/KBEngine-Nex/issues/2)
- [feat] 引擎支持python venv虚拟环境
- [feat] ssl / rsa / caching_sha2_password 支持 [Issue #4](https://github.com/KBEngineLab/KBEngine-Nex/issues/4)
- [feat] linux 并行构建支持 [Issue #8](https://github.com/KBEngineLab/KBEngine-Nex/issues/8)
- [feat] UE5 SDK支持 [Issue #6](https://github.com/KBEngineLab/KBEngine-Nex/issues/6)
- [feat] 升级mysqlclient到8.x
- [fix] 调整server_assets文件结构
