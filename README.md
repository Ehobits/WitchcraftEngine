<p align="center">
  <img src="images/屏幕截图 2026-03-17 154127.png" width="720" />
</p>

<p align="center">
  <b>Witchcraft</b> — 一个 D3D12 驱动的实时渲染引擎。<br/>
  <sub>~53,000 行 C++ &nbsp;·&nbsp; 26 个 HLSL Shader &nbsp;·&nbsp; 9 个编辑器窗口</sub>
</p>

---

## WitchcraftEngine是什么

Witchcraft 是一个从零构建的游戏引擎，目标是亲手拆解现代实时渲染的每一个环节。它不是一个拿来就用的成熟产品，而是一份活的渲染管线系统、ECS系统和简单骨骼系统。

当前阶段聚焦于 **PBR 前向渲染** 的完整实现：从 Assimp 导入、自建文件格式、Flecs ECS 场景管理，到 D3D12 多线程命令录制、阴影/SSAO/体积光效果链，再到 ImGui 编辑器工具链的闭环。

---

## 技术纵深

### 线程模型
```
主线程 (Win32 消息泵)
  │
  ├── resize 标志 ──atomic──→ 渲染线程 (无锁轮询)
  │
  └── InputMessage 队列 ──mcv──→ 输入线程 (条件变量)
```
> 渲染线程每帧自行检查 resize 原子变量，不阻塞等信号；输入线程用生产者-消费者队列阻塞等待，有事件才被唤醒。二者完全对等，不存在主线程→渲染线程的直接通信。

### 渲染管线
| Pass                  | 说明                          |
|-----------------------|-------------------------------|
| Shadow (静态缓存层)   | 增量更新，静态投射体缓存回写    |
| SSAO + 双边模糊       | Compute Shader 驱动           |
| GPU Skinning          | 256 骨骼/绘制，三重缓冲 VB     |
| Volumetric Light      | Ray marching 沿深度重建        |
| OIT (Weighted Blended) | 独立累积 + 揭示纹理           |
| FXAA                  | 后处理抗锯齿                   |

4 个工作线程在 RenderB 中并行录制 Shadow / Normal / Opaque / Translucent 四个阶段的命令列表，渲染项按名称排序后均匀分片，`ExecuteCommandLists` 一次性提交到 GPU 队列。

### ECS 双存储

每个实体内部通过 `ServicesContainer` 持有 14 种组件（C++ 侧），关键变换和类型标签同步到 Flecs World。这层双存储让编辑器能直接操作 C++ 组件，同时 Flecs 提供 ECS 查询能力供渲染线程组装 RenderItem。

### 资产管线

```
FBX/glTF ──→ Assimp ──→ ImportedAssetTypes
                              │
    .wmodel / .wmat / .wskel ←┘ (XML 中间格式)
              │
              └──→ D3DWindow 几何注册列表 ──→ GPU
```

无 UV 模型自动生成纹理坐标（自适应圆柱/平面投影 + 极点拆分），`WRAP` 采样器自然处理接缝。

---

## 功能矩阵

### 已完成 ✔
| 类别  | 功能                                          |
| --- | ------------------------------------------- |
| 渲染  | PBR (粗糙度/金属度/自发光/G 缓冲)                      |
|     | 级联阴影（平行光） + Cube Shadow （点光 ）+ 一般阴影（聚光）     |
|     | SSAO 环境遮蔽                                   |
|     | 体积光 (Ray Marching)                          |
|     | 天空盒                                         |
|     | 透明物体 OIT 合成                                 |
|     | FXAA 快速近似抗锯齿                                |
|     | 多线程命令录制 (4 工作线程并行)                          |
| ECS | Flecs 集成，14 种组件，实体层次结构与重父化                  |
| 编辑器 | ImGui 多窗口 (场景/材质/动画/骨骼/资产/文件)               |
|     | Transform Gizmo (平移/旋转/缩放)                  |
|     | 骨骼编辑器 (关节属性 + 刷权重可视化)                       |
|     | 射线拾取 (三角形级精确检测)                             |
| 资产  | .wmodel / .wmat / .wskel / .wanim / .wscene |
|     | Assimp 多格式导入 + UV 自动生成                      |
| 物理  | JoltPhysics 集成 (碰撞体/刚体)                     |

### 进行中 △
大气渲染 · 动态全局照明 · 屏幕空间全局照明 · 后期处理完善 · 动画系统 · 脚本

---

## 愿景

* 粒子系统
* 色调映射管线
* 集群光照剔除 (Clustered Forward)
* 体素全局照明 (VXGI)
* 体积云/雾
* 卡通渲染着色器
* 程序化地形生成
* 水体渲染
* 音频系统

---

## 构建

| 项目   | 要求                       |
|--------|----------------------------|
| 语言   | C++20                      |
| 平台   | Windows 10/11              |
| GPU    | RTX 4060 或 RX 6000 系列   |
| IDE    | Visual Studio 2022         |

---

## 致谢

本项目站在以下开源项目的肩膀上：

| 库            | 用途               |
|---------------|--------------------|
| [ImGui]       | 编辑器 UI 框架     |
| [Assimp]      | 3D 模型导入        |
| [Flecs]       | ECS 运行时         |
| [DirectXTK12] | D3D12 工具链       |
| [JoltPhysics] | 刚体物理模拟       |
| [pugixml]     | XML 文件解析       |
| [Lua] + [sol2]| 脚本引擎           |
| [Box2D]       | 2D 物理            |
| [zlib]        | 数据压缩           |

[ImGui]: https://github.com/ocornut/imgui
[Assimp]: https://github.com/assimp/assimp
[Flecs]: https://github.com/SanderMertens/flecs
[DirectXTK12]: https://github.com/Microsoft/DirectXTK12
[JoltPhysics]: https://github.com/jrouwe/JoltPhysics
[pugixml]: https://github.com/zeux/pugixml
[Lua]: https://github.com/lua/lua
[sol2]: https://github.com/ThePhD/sol2
[Box2D]: https://github.com/erincatto/box2d
[zlib]: https://github.com/madler/zlib
