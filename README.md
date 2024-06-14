# Witchcraft

使用 DirectX12 后端用 C++ 编写的玩具引擎。适用于游戏应用程序。
`Witchcraft`画面当前只实现了PBR部分。
![image](https://github.com/Ehobits/WitchcraftEngine/blob/fd519433b77f06a00e8b80c9f562cadbc259b4d8/images/%E5%B1%8F%E5%B9%95%E6%88%AA%E5%9B%BE%202024-06-14%20121614.png)

这是一款开源的学习用引擎，后续会不断添加各种功能，由于个人时间有限，这个过程将是漫长的。

## 功能

* 网格加载 ✔
* 物体变换 ✔
* 灵活的相机 ✔
* 天空盒 ✔
* 大气渲染 △
* 具有 G粗糙度/金属度、自发光的 PBR ✔
* 动态全局照明 △
* 屏幕空间全局照明 △
* 环境光遮蔽 AO △
* 阴影效果 △
* 基本场景管理框架 ✔
* Imgui 编辑器管理器 ✔
* 后期处理 △
	* 快速近似抗锯齿 FXAA △


## 特征 `todo`

* 粒子系统
* 自然色调映射
* 基于集群的光线剔除
* 动态全局照明
* 体素全局照明
* 体积照明
* 卡通渲染着色器
* 程序内容生成地形网格
* 水渲染
* 完全脚本化的交互和行为
* 集成的第三方物理系统
* 音频系统

## 主要平台
`WitchcraftEngine` 目前可在有限的操作系统和硬件上运行。

推荐硬件：
* Nvidia GTX 1060 series 或
* AMD Radeon RX 6000 series

操作系统和语言：
* Windows
* C++ 17

编译该项目还需要以下第三方库：
* mGui https://github.com/ocornut/imgui
* assimp https://github.com/assimp/assimp
* Flecs [![image](images/flecs.png)](https://github.com/SanderMertens/flecs)
* DirectXTK12 https://github.com/Microsoft/DirectXTK12
* Box2D https://github.com/erincatto/box2d
* JoltPhysics https://github.com/jrouwe/JoltPhysics
* Lua [![image](images/lua.png)](https://github.com/lua/lua)
* sol2 https://github.com/ThePhD/sol2
* zlib
	
