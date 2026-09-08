<p align="center">
  <img src="images/屏幕截图 2026-09-05 194715.PNG" width="720" />
</p>

<p align="center">
  <b>Witchcraft</b> - 一个 D3D12 驱动的实时渲染引擎与编辑器原型。<br/>
  <sub>DirectX 12 · ECS · Physics · PBR · IBL/RTT · Lua · Editor</sub>
</p>

---

## WitchcraftEngine 是什么

WitchcraftEngine 是一个从零构建的 Windows / DirectX 12 3D 引擎项目。它当前不是面向通用发行的成熟引擎，而是一套仍在快速成形的实时渲染、ECS 场景、项目管理、编辑器工具、动画、物理和脚本运行时系统。

当前开发重点是把编辑器工作流、D3D12 渲染地基、场景/项目保存读取、Lua 脚本和资源管线整理成稳定闭环。

---

## 技术纵深

### 渲染主线

Witchcraft 的渲染逻辑以 `D3DWindow` 为主调度点：设备、交换链、窗口尺寸变化、帧更新和主渲染循环仍由它统一负责；阴影、AO、RTT、IBL、透明 OIT、体积光、后处理和调试叠加等功能则逐步拆成独立 pass，通过上下文结构和绑定契约接入主流程。

这种结构的目标不是把主循环拆散，而是让每个 pass 的资源、描述符、常量布局和状态切换更清楚。主流程负责“什么时候渲染”，pass 负责“这一段怎么渲染”。

### 光照与反射

项目当前走 PBR 前向渲染路线。直接光、阴影、AO、环境光、IBL 和 RTT 反射共同参与材质最终表现。IBL 使用天空球/经纬图投影，不是传统 cubemap 六面体方案；RTT 则用于虚拟相机、镜面和材质反射等需要实时画面反馈的场景。

材质侧保留金属度、粗糙度、镜面反射开关和反射来源等控制项。场景侧的环境光、天空和 IBL 参数需要参与保存读取，保证编辑器调好的视觉状态可以稳定复现。

### ECS 与编辑态/运行态

实体和组件由项目自有 ECS 数据与 Flecs 查询能力共同支撑。编辑器侧直接操作实体层级和组件数据，运行时侧通过同步、快照和命令缓冲控制结构变化，避免脚本或运行逻辑在遍历过程中直接破坏世界状态。

编辑态和运行态的区分是当前架构重点之一：编辑器可以自由调整对象，进入运行后则由运行时系统接管脚本、动画、物理和输入等逻辑。

### 资源与脚本链路

资源管线以 Assimp、pugixml、DirectXTK12 和 libpng 等库为基础，项目自定义 `.wscene`、`.wmodel`、`.wmat`、`.wanim` 等中间格式用于保存编辑结果和运行时数据。PNG 读取正在向 libpng + D3D12 上传路线推进，DDS 仍由 DirectXTK12 负责。

脚本系统以 Lua + sol2 为核心，脚本编辑器负责编辑、语法检查和错误定位，运行时系统负责加载脚本实例并调用 `OnStart`、`OnUpdate`、`OnAnimationEvent` 等回调。

---

## 当前能力

### 编辑器

- 基于 ImGui docking 的多窗口编辑器界面。
- 包含层级窗口、属性检查器、资源浏览、材质编辑、动画编辑、脚本编辑、控制台、项目窗口和项目设置窗口等工具窗口。
- 支持实体层级、组件属性、灯光、相机、天空、材质和场景数据的编辑。
- 控制台支持运行态日志输出，并已实现“执行游戏时清除”工作流。
- 项目窗口支持 `.wproject` 的打开、保存、重命名、场景管理、入口场景切换和最近项目记录。
- 场景编辑保持单场景语义，切换到其他场景时会先关闭当前场景。

### 脚本编辑器

- 脚本编辑窗口已接入 ImGuiColorTextEdit。
- 支持 Lua 代码高亮、代码块折叠、代码块侧边线框、独立代码字体。
- 支持多标签页打开/编辑脚本，每个标签页可关闭。
- 文件菜单提供打开脚本、保存脚本、另存为、文件重命名等入口。
- 支持 Lua 语法检查，错误列表按代码、说明、文件、行号展示；双击错误可定位到对应行。
- 输出页签可承接脚本打印内容，便于调试。

### 渲染

- 渲染后端以 DirectX 12 为核心。
- `D3DWindow` 仍保留 D3D 设备、窗口尺寸变化、帧更新和主渲染循环职责。
- 各渲染 pass、资源绑定、默认描述符和上下文契约正在逐步模块化，主流程通过统一接口调用各 pass。
- 已具备 PBR 材质、天空球、IBL、RTT 反射、阴影、AO、透明 OIT、体积光、FXAA、颜色调整、后处理、文字与调试叠加等链路。
- IBL 使用天空球/经纬图投影路线，不是六面体 cubemap 路线；材质中的金属度、镜面反射开关、环境光强度和场景保存读取已围绕该链路做过整理。
- 体积光作为独立 pass 参与主场景链路，不依赖 AO 开关。
- `D3DWindow` 仍然负责设备、交换链、主循环与 pass 编排，具体 pass 正在持续模块化拆分。

### ECS 与场景

- ECS 相关代码集中在 `Witchcraft/WitchcraftEngine/SRC/ECS` 与 `Witchcraft/WitchcraftEngine/SRC/System`。
- 支持实体层级、重父化、组件编辑和运行态同步。
- 已存在 Transform、Mesh、Camera、Light、Script、Animation、Physics、Skeleton、SkinnedMesh、Billboard、BoneAttachment 等组件方向。
- 场景文件使用项目自定义 `.wscene` 格式，保存读取覆盖实体、组件、灯光、环境光、天空和部分渲染设置。
- 层级拖拽逻辑已围绕普通实体与骨骼层级做过约束，避免把不应挂载到骨骼节点下的对象错误放入骨骼结构。
- 项目文件使用 `.wproject` 管理场景列表、入口场景、当前编辑场景和编辑器打开状态。

### 脚本运行时

- 运行时脚本以 Lua + sol2 为主。
- 脚本挂载在实体上，通过 `OnStart`、`OnUpdate`、`OnAnimationEvent` 等回调进入运行时逻辑。
- 已暴露 Console、Time、Input、Entity、Transform、Camera、Light、Mesh、Physics、Animator 等方向的脚本接口。
- 运行态结构性操作采用命令缓冲思路，避免脚本执行时直接破坏 ECS 遍历状态。

### 动画与物理

- 动画相关代码位于 `Witchcraft/WitchcraftEngine/SRC/Animation` 及相关编辑器、组件系统中。
- 支持动画资源、播放控制、动画事件和脚本回调方向。
- 物理方向接入 Jolt Physics，并已有碰撞体、刚体和脚本绑定的基础链路。

### 资源与纹理

- 模型导入方向使用 Assimp。
- XML/场景文件解析使用 pugixml。
- DDS 纹理读取依赖 DirectXTK12。
- PNG 读取已开始转向 libpng，并由 `Texture.h` / `Texture.cpp` 承担读取与 D3D12 上传链路的演进。
- 后续目标是以 libpng + DirectXTK12 + D3D12 原生上传组合。
- PNG 运行时读取不会天然得到 BC 显存压缩；如需 BC 压缩，通常需要额外离线或中间转换流程产出 DDS/BC 资源。
- 项目根目录会参与资源路径解析，场景与项目文件尽量保留相对路径。

---

## 仓库结构

| 路径 | 说明 |
| --- | --- |
| `README.md` | 当前项目入口说明。 |
| `LICENSE` | 项目许可证。 |
| `images/` | README 与项目展示图片。 |
| `Witchcraft/` | Visual Studio 解决方案与主工程所在目录。 |
| `Witchcraft/Witchcraft.sln` | 解决方案入口。 |
| `Witchcraft/WitchcraftEngine/` | 主引擎工程目录。 |
| `Witchcraft/WitchcraftEngine/SRC/` | 引擎、编辑器、渲染、ECS、系统、脚本、动画等源码。 |
| `Witchcraft/WitchcraftEngine/DATA/` | 场景、Shader 和运行时数据目录。 |
| `Witchcraft/WitchcraftEngine/ASSETS/` | 项目资源目录。 |
| `Witchcraft/3rdpartd/` | 第三方库工程与源码。 |

---

## 主要源码区域

| 路径 | 说明 |
| --- | --- |
| `Witchcraft/WitchcraftEngine/SRC/D3DWindow/` | D3D12 渲染主链路、材质、纹理、光照、pass 和渲染桥接。 |
| `Witchcraft/WitchcraftEngine/SRC/D3DWindow/RenderPasses/` | 已拆分或正在拆分的渲染 pass。 |
| `Witchcraft/WitchcraftEngine/SRC/ECS/` | ECS 世界、实体、组件与层级数据。 |
| `Witchcraft/WitchcraftEngine/SRC/System/` | 场景、脚本、项目、灯光、运行时等系统。 |
| `Witchcraft/WitchcraftEngine/SRC/Editor/` | 编辑器框架与窗口实现。 |
| `Witchcraft/WitchcraftEngine/SRC/Animation/` | 动画数据与播放相关逻辑。 |
| `Witchcraft/WitchcraftEngine/SRC/GameRunTime/` | 游戏运行态相关逻辑。 |
| `Witchcraft/WitchcraftEngine/SRC/ModelAnalysis/` | 模型分析与导入辅助逻辑。 |

---

## 架构约定

- `D3DWindow` 是渲染设备和渲染循环主流程的所有者。
- `OnReSize()`、`Update()`、`RenderB()`、`RenderE()` 等主流程函数暂不作为拆分目标。
- 每个渲染 pass 可以独立管理自己的状态、资源和绘制接口，并通过明确的上下文结构与 `D3DWindow` 对接。
- 描述符、常量布局、默认资源和资源状态需要有文档化契约，避免 pass 之间产生隐式依赖。
- 编辑态与运行态需要保持隔离；运行时实体结构变化通过受控入口提交。
- 场景保存读取应覆盖用户实际可调参数，尤其是材质、灯光、环境、IBL/RTT 等渲染参数。

---

## 构建环境

| 项目 | 要求 |
| --- | --- |
| 语言 | C++20 |
| 平台 | Windows 10/11 |
| 图形 API | DirectX 12 |
| IDE | Visual Studio 2022 或更新版本 |
| 入口 | `Witchcraft/Witchcraft.sln` |

解决方案中当前可见的第三方工程包括：

- DirectXTK12
- lua
- flecs
- pugixml
- Jolt Physics
- assimp
- imgui docking
- ImGuiColorTextEdit
- freetype
- zlib
- libpng

这些第三方工程不包含在仓库中，需要单独手动配置。

---

## 当前开发主线

- 稳定 D3D 渲染地基：pass 接口、资源状态、描述符绑定、常量布局、默认资源和回归场景。
- 完善 IBL、RTT、环境光、阴影、AO、体积光和后处理之间的组合关系。
- 完善脚本编辑器：多标签、文件菜单、Lua 检查、代码高亮、错误定位和后续代码分析能力。
- 扩展脚本运行时绑定：补齐实体、组件、动画、物理、输入、场景操作等开放接口。

---

## 愿景

- 粒子系统
- 体素全局照明 (VXGI)
- 体积云/雾
- 卡通渲染着色器
- 程序化地形生成
- 水体渲染
- 音频系统

---

## 致谢

本项目站在以下开源项目的肩膀上：

| 库 | 用途 |
| --- | --- |
| [ImGui] | 编辑器 UI 框架 |
| [ImGuiColorTextEdit] | 脚本编辑器代码高亮与文本编辑 |
| [Assimp] | 3D 模型导入 |
| [Flecs] | ECS 运行时 |
| [DirectXTK12] | D3D12 工具链与 DDS 纹理读取 |
| [JoltPhysics] | 刚体物理模拟 |
| [pugixml] | XML 文件解析 |
| [Lua] + [sol2] | 脚本运行时与 C++ 绑定 |
| [freetype] | 字体渲染相关能力 |
| [libpng] | PNG 读取 |
| [zlib] | 数据压缩/解压缩 |

[ImGui]: https://github.com/ocornut/imgui
[ImGuiColorTextEdit]: https://github.com/goossens/ImGuiColorTextEdit
[Assimp]: https://github.com/assimp/assimp
[Flecs]: https://github.com/SanderMertens/flecs
[DirectXTK12]: https://github.com/microsoft/DirectXTK12
[JoltPhysics]: https://github.com/jrouwe/JoltPhysics
[pugixml]: https://github.com/zeux/pugixml
[Lua]: https://github.com/lua/lua
[sol2]: https://github.com/ThePhD/sol2
[freetype]: https://github.com/freetype/freetype
[libpng]: https://github.com/pnggroup/libpng
[zlib]: https://github.com/madler/zlib
