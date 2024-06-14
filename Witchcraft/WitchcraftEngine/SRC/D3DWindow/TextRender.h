#pragma once

#include "Engine/EngineUtils.h"
#include "Texture.h"

#include <map>

#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_MODULE_H            // <freetype/ftmodapi.h>
#include FT_GLYPH_H             // <freetype/ftglyph.h>
#include FT_SYNTHESIS_H         // <freetype/ftsynth.h>

struct TextVertex
{
	TextVertex(float x, float y, float w, float h, float r, float g, float b, float a, float u, float v, float tw, float th)
		: pos(x, y, w, h), color(r, g, b, a), TexC(u, v, tw, th) {}
	XMFLOAT4 pos;
	XMFLOAT4 color;
	XMFLOAT4 TexC;
};

struct CharacterNumbering
{
	UINT32 begin = 0u;
	UINT32 end = 0u;
};

struct SymbolData
{
	UINT16 id = 0;

	DirectX::XMFLOAT2 leftTop;       //< 纹理坐标中的左上角
	DirectX::XMFLOAT2 rightBottom;   //< 纹理坐标中的右下角
	DirectX::XMFLOAT2 symbolSize;    //< 符号大小（以像素为单位）
	DirectX::XMFLOAT2 basePoint;     //< 该符号位图的基点（左上角位置），以像素为单位
};

struct FontTextureData
{
	UINT textureWidth = 0; // 贴图宽度
	UINT textureHeight = 0; // 贴图高度
	UINT symbolWidth = 0;
	UINT symbolHeight = 0;
	std::vector<BYTE> pHostBuffer;
	ID3D12Resource* textureBuffer = nullptr; // 贴图资源
	CD3DX12_GPU_DESCRIPTOR_HANDLE GPUsrvHandle;
	CD3DX12_CPU_DESCRIPTOR_HANDLE CPUsrvHandle;

	std::vector<SymbolData> m_symbolsData;
};

struct Font
{
	Font() {}

	std::wstring name = L""; // 字体名称
	UINT font_size = 72;
	std::map<UINT, FontTextureData> pFontTextureData;
	int numCharacters = 0; // 字体中的字符数

	std::vector<CharacterNumbering> CharacterNumberingSet;
};

class TextRender
{
public:
	TextRender(ID3D12Device* device, ID3D12GraphicsCommandList* commandList, UINT SwapChainBufferCount);
	~TextRender();

	void CreateRootSignature();
	void CreatePipesAndShaders(ID3DBlob* vertexShader, ID3DBlob* pixelShader, DXGI_FORMAT BackBufferFormat, DXGI_FORMAT DepthStencilFormat, ComPtr<ID3D12PipelineState> *PipelineState);

	UINT16 GetUnicodeID(wchar_t c);

	FontTextureData CreateFontTextureData(FT_Face face, FT_GlyphSlot slot, CharacterNumbering NumberingSet, std::wstring name);
	bool DXCreateFont(std::wstring fontFilename, int fontSize);

	void DXDrawText(ID3D12GraphicsCommandList* cmdList, std::wstring text, const DirectX::XMFLOAT2 pos, const DirectX::XMFLOAT4& color, UINT CurrBackBufferIndex);

private:
	ComPtr<ID3D12RootSignature> RootSignature = nullptr;
	std::map<UINT, ID3D12Resource*> textVertexBuffer;
	// 一帧中可以渲染的最大字符数。这仅用于确保每帧为文本顶点缓冲区分配足够的内存
	int maxNumTextCharacters = 2048;
	std::map<UINT, D3D12_VERTEX_BUFFER_VIEW> textVertexBufferView; // 文本顶点缓冲区的视图
	std::map<UINT, UINT8*> textVBGPUAddress; // 这是指向每个文本常量缓冲区的指针
	ComPtr<ID3D12DescriptorHeap> SrvDescriptorHeap = nullptr;
	UINT SrvDescriptorHeapIndex = 0;

	Font mFont;
	UINT imageCount = 0;
	FT_Library _FTlibrary;

	ID3D12Device* m_d3dDevice = nullptr;
	ID3D12GraphicsCommandList* m_commandList = nullptr;
	UINT m_SwapChainBufferCount = 0;
};