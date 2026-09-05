#pragma once

#include "Engine/EngineUtils.h"
#include "Texture.h"

#include <map>
#include <xstring>
#include <unordered_map>
#include <vector>

#include <ft2build.h>
#include FT_FREETYPE_H

struct TextVertex
{
	TextVertex() = default;
	TextVertex(float x, float y, float w, float h, float r, float g, float b, float a, float u0, float v0, float u1, float v1)
		: pos(x, y, w, h), color(r, g, b, a), TexC(u0, v0, u1, v1) {}

	// x/y 为左上角裁剪空间坐标，z/w 为裁剪空间宽高。
	XMFLOAT4 pos;
	// 每个字形单独携带的颜色，用于调试文字。
	XMFLOAT4 color;
	// 字体图集中的 uvMin.xy 与 uvMax.zw。
	XMFLOAT4 TexC;
};

struct CharacterNumbering
{
	UINT32 begin = 0u;
	UINT32 end = 0u;

	bool Contains(UINT32 codepoint) const
	{
		return begin <= codepoint && codepoint <= end;
	}
};

struct SymbolData
{
	UINT32 id = 0u;
	// 当前字形在图集页内的 UV 范围。
	DirectX::XMFLOAT2 uvMin = { 0.0f, 0.0f };
	DirectX::XMFLOAT2 uvMax = { 0.0f, 0.0f };
	// 字形位图尺寸，单位为像素。
	DirectX::XMFLOAT2 symbolSize = { 0.0f, 0.0f };
	// 当前字形的 FreeType 排版数据。
	DirectX::XMFLOAT2 bearing = { 0.0f, 0.0f };
	float advanceX = 0.0f;
};

struct FontTextureData
{
	UINT textureWidth = 0u;
	UINT textureHeight = 0u;
	CharacterNumbering NumberingSet;
	// CPU 侧临时图集像素数据，上传后清空。
	std::vector<BYTE> pHostBuffer;
	ComPtr<ID3D12Resource> textureBuffer = nullptr;
	ComPtr<ID3D12Resource> textureUploadBuffer = nullptr;
	CD3DX12_GPU_DESCRIPTOR_HANDLE GPUsrvHandle;
	CD3DX12_CPU_DESCRIPTOR_HANDLE CPUsrvHandle;
	std::unordered_map<UINT32, SymbolData> m_symbolsData;
};

struct GlyphLookupEntry
{
	UINT pageIndex = static_cast<UINT>(-1);
	SymbolData symbol;
};

struct GlyphPlacement
{
	UINT32 codepoint = 0u;
	UINT pageIndex = static_cast<UINT>(-1);
	float left = 0.0f;
	float top = 0.0f;
	float width = 0.0f;
	float height = 0.0f;
	DirectX::XMFLOAT4 texRect = { 0.0f, 0.0f, 0.0f, 0.0f };
};

struct TextDrawBatch
{
	UINT pageIndex = 0u;
	UINT startInstance = 0u;
	UINT instanceCount = 0u;
};

struct TextLayoutCacheEntry
{
	bool valid = false;
	UINT64 generation = 0u;
	std::wstring text;
	UINT glyphCount = 0u;
	float width = 0.0f;
	float height = 0.0f;
	std::vector<GlyphPlacement> placements;
	std::vector<TextDrawBatch> batches;
};

struct CachedTextSubmission
{
	bool valid = false;
	UINT64 layoutGeneration = 0u;
	std::wstring text;
	DirectX::XMFLOAT2 pos = { 0.0f, 0.0f };
	DirectX::XMFLOAT4 color = { 1.0f, 1.0f, 1.0f, 1.0f };
	UINT glyphCount = 0u;
	std::vector<TextDrawBatch> batches;
};

struct TextRenderStats
{
	UINT64 drawCalls = 0u;
	UINT64 submissionCacheHits = 0u;
	UINT64 submissionCacheMisses = 0u;
	UINT64 layoutCacheHits = 0u;
	UINT64 layoutCacheMisses = 0u;
	UINT64 pageLoads = 0u;
	UINT64 copyBytes = 0u;
	UINT64 glyphsSubmitted = 0u;
	UINT64 batchesSubmitted = 0u;
	bool lastSubmissionCacheHit = false;
	bool lastLayoutCacheHit = false;
	UINT lastGlyphCount = 0u;
	UINT lastBatchCount = 0u;
	UINT64 lastCopyBytes = 0u;
	UINT loadedPageCount = 0u;
};

struct Font
{
	std::wstring name = L"";
	UINT font_size = 72u;
	float lineHeight = 0.0f;
	float ascent = 0.0f;
	float descent = 0.0f;
	std::map<UINT, FontTextureData> pFontTextureData;
	std::unordered_map<UINT32, GlyphLookupEntry> glyphLookup;
	std::unordered_map<UINT64, UINT> pageRangeLookup;
};

class TextRender
{
public:
	TextRender(ID3D12Device* device, ID3D12GraphicsCommandList* commandList, UINT SwapChainBufferCount);
	~TextRender();

	void CreateRootSignature();
	void CreatePipesAndShaders(ID3DBlob* vertexShader, ID3DBlob* pixelShader, DXGI_FORMAT BackBufferFormat, DXGI_FORMAT DepthStencilFormat, ComPtr<ID3D12PipelineState>* PipelineState);

	UINT32 GetUnicodeID(wchar_t c) const;

	FontTextureData CreateFontTextureData(FT_Face face, CharacterNumbering numberingSet);
	bool DXCreateFont(std::wstring fontFilename, int fontSize);
	void SetScreenSize(float width, float height);
	void SetSharedSrvDescriptorHeap(ID3D12DescriptorHeap* descriptorHeap, UINT descriptorSize, UINT descriptorBaseIndex, UINT descriptorCapacity);
	UINT GetSrvDescriptorCount() const;
	const TextRenderStats& GetStats() const;

	// 保持 D3DWindow 当前使用的对外调用形式不变。
	void DXDrawText(ID3D12GraphicsCommandList* cmdList, std::wstring text, const DirectX::XMFLOAT2 pos, const DirectX::XMFLOAT4& color, UINT CurrBackBufferIndex);

private:
	static UINT32 NextPowerOfTwo(UINT32 value);
	float GetScreenWidth() const;
	float GetScreenHeight() const;
	float PixelToNdcX(float pixels) const;
	float PixelToNdcY(float pixels) const;
	float PixelToNdcWidth(float pixels) const;
	float PixelToNdcHeight(float pixels) const;
	void ReleaseTextBuffers();
	void ReleaseFontResources();
	const GlyphLookupEntry* FindGlyphEntry(UINT32 codepoint) const;
	FontTextureData* FindFontTextureData(UINT32 codepoint);
	const SymbolData* FindGlyph(UINT32 codepoint, FontTextureData** outTextureData);
	CharacterNumbering ResolvePageRange(UINT32 codepoint) const;
	FT_Face CreateFontFaceFromMemory() const;
	bool UploadFontTexturePage(ID3D12GraphicsCommandList* cmdList, UINT pageIndex, FontTextureData& pageData);
	bool EnsurePageLoaded(CharacterNumbering numberingSet, ID3D12GraphicsCommandList* cmdList);
	bool EnsureGlyphLoaded(UINT32 codepoint, ID3D12GraphicsCommandList* cmdList);
	TextLayoutCacheEntry* GetOrBuildLayout(const std::wstring& text, ID3D12GraphicsCommandList* cmdList, bool* outCacheHit);
	bool BuildLayoutCache(const std::wstring& text, ID3D12GraphicsCommandList* cmdList, TextLayoutCacheEntry& layoutEntry);
	void FillVerticesFromLayout(const TextLayoutCacheEntry& layoutEntry, const DirectX::XMFLOAT2& pos, const DirectX::XMFLOAT4& color, TextVertex* vertices) const;
	void UpdateStats(UINT glyphCount, UINT batchCount, UINT64 copyBytes, bool submissionCacheHit, bool layoutCacheHit);

private:
	ComPtr<ID3D12RootSignature> RootSignature = nullptr;
	std::map<UINT, ComPtr<ID3D12Resource>> textVertexUploadBuffer;
	std::map<UINT, ComPtr<ID3D12Resource>> textVertexDrawBuffer;
	int maxNumTextCharacters = 2048;
	std::map<UINT, D3D12_VERTEX_BUFFER_VIEW> textVertexBufferView;
	std::map<UINT, D3D12_RESOURCE_STATES> textVertexBufferState;
	std::map<UINT, UINT8*> textVBGPUAddress;
	ComPtr<ID3D12DescriptorHeap> SrvDescriptorHeap = nullptr;
	ComPtr<ID3D12DescriptorHeap> m_sharedSrvDescriptorHeap = nullptr;

	Font mFont;
	std::unordered_map<std::wstring, TextLayoutCacheEntry> m_layoutCache;
	std::vector<FT_Byte> m_fontBinary;
	FT_Library _FTlibrary = nullptr;
	bool m_fontLibraryInitialized = false;
	std::map<UINT, CachedTextSubmission> m_textSubmissionCache;
	TextRenderStats m_stats;
	UINT64 m_layoutGeneration = 1u;

	ID3D12Device* m_d3dDevice = nullptr;
	ID3D12GraphicsCommandList* m_commandList = nullptr;
	UINT m_SwapChainBufferCount = 0u;
	UINT m_srvDescriptorSize = 0u;
	UINT m_srvDescriptorBaseIndex = 0u;
	UINT m_srvDescriptorCapacity = 0u;
	UINT m_loadedPageCount = 0u;
	bool m_useSharedSrvDescriptorHeap = false;
	float m_screenWidth = 1.0f;
	float m_screenHeight = 1.0f;

	UINT kAtlasPadding = 1u;
	// 共享描述符堆中默认给文字系统预留的 SRV 数量。
	UINT kDefaultTextDescriptorReservation = 64u;
	// 动态页化时，CJK 主区与兜底区间按 256 个码点一页切分。
	UINT32 kDynamicPageSize = 0x100u;

	UINT64 MakeRangeKey(const CharacterNumbering& range);

};