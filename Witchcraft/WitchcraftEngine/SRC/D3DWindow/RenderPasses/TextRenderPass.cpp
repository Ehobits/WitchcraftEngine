#include "TextRenderPass.h"
#include "../D3DHelpers.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>


TextRenderPass::TextRenderPass(ID3D12Device* device, ID3D12GraphicsCommandList* commandList, UINT SwapChainBufferCount)
	: m_d3dDevice(device), m_commandList(commandList), m_SwapChainBufferCount(SwapChainBufferCount)
{
	const FT_Error error = FT_Init_FreeType(&_FTlibrary);
	m_fontLibraryInitialized = (error == 0);
	if (!m_fontLibraryInitialized)
		MessageBox(nullptr, L"TextRender 初始化失败！", L"", MB_OK);
}

TextRenderPass::~TextRenderPass()
{
	ReleaseTextBuffers();
	ReleaseFontResources();

	if (m_fontLibraryInitialized)
	{
		FT_Done_FreeType(_FTlibrary);
		_FTlibrary = nullptr;
		m_fontLibraryInitialized = false;
	}
}

void TextRenderPass::CreateRootSignature()
{
	CD3DX12_DESCRIPTOR_RANGE1 texTable;
	texTable.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0);

	CD3DX12_ROOT_PARAMETER1 rootParameters[1];
	rootParameters[0].InitAsDescriptorTable(1, &texTable, D3D12_SHADER_VISIBILITY_PIXEL);

	D3D12_STATIC_SAMPLER_DESC sampler = {};
	sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
	sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
	sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
	sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
	sampler.ShaderRegister = 0;
	sampler.RegisterSpace = 0;
	sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
	sampler.MinLOD = 0.0f;
	sampler.MaxLOD = D3D12_FLOAT32_MAX;

	CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSignatureDesc;
	rootSignatureDesc.Init_1_1(_countof(rootParameters), rootParameters,
		1, &sampler,
		D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

	ComPtr<ID3DBlob> errorBuff = nullptr;
	ComPtr<ID3DBlob> signature = nullptr;
	HRESULT hr = D3DX12SerializeVersionedRootSignature(&rootSignatureDesc,
		D3D_ROOT_SIGNATURE_VERSION_1, &signature, &errorBuff);
	if (FAILED(hr) && errorBuff)
		::OutputDebugStringA((char*)errorBuff->GetBufferPointer());
	ThrowIfFailed(hr);

	ThrowIfFailed(m_d3dDevice->CreateRootSignature(
		0,
		signature->GetBufferPointer(),
		signature->GetBufferSize(),
		IID_PPV_ARGS(RootSignature.GetAddressOf())));
}

void TextRenderPass::CreatePipesAndShaders(DXGI_FORMAT BackBufferFormat, DXGI_FORMAT DepthStencilFormat, ComPtr<ID3D12PipelineState>* PipelineState)
{
	ComPtr<ID3DBlob> vertexShader = CompileShader(L"DATA/Shaders/Text", nullptr, "VS", "vs_5_1");
	ComPtr<ID3DBlob> pixelShader = CompileShader(L"DATA/Shaders/Text", nullptr, "PS", "ps_5_1");

	std::vector<D3D12_INPUT_ELEMENT_DESC> inputElementDescs =
	{
		{ "POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 0,  D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1 },
		{ "COLOR",    0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 16, D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1 },
		{ "TEXCOORD", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 32, D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1 }
	};

	D3D12_GRAPHICS_PIPELINE_STATE_DESC textPsoDesc = {};
	textPsoDesc.InputLayout = { inputElementDescs.data(), static_cast<UINT>(inputElementDescs.size()) };
	textPsoDesc.pRootSignature = RootSignature.Get();
	textPsoDesc.VS = CD3DX12_SHADER_BYTECODE(vertexShader.Get());
	textPsoDesc.PS = CD3DX12_SHADER_BYTECODE(pixelShader.Get());
	textPsoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	textPsoDesc.RTVFormats[0] = BackBufferFormat;
	textPsoDesc.DSVFormat = DepthStencilFormat;
	textPsoDesc.SampleDesc.Count = 1;
	textPsoDesc.SampleDesc.Quality = 0;
	textPsoDesc.SampleMask = UINT_MAX;
	textPsoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
	textPsoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;

	D3D12_BLEND_DESC blendDesc = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
	blendDesc.AlphaToCoverageEnable = FALSE;
	blendDesc.IndependentBlendEnable = FALSE;
	blendDesc.RenderTarget[0].BlendEnable = TRUE;
	blendDesc.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
	blendDesc.RenderTarget[0].DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
	blendDesc.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
	blendDesc.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
	blendDesc.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
	blendDesc.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
	blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
	textPsoDesc.BlendState = blendDesc;
	textPsoDesc.NumRenderTargets = 1;

	D3D12_DEPTH_STENCIL_DESC depthStencilDesc = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
	depthStencilDesc.DepthEnable = FALSE;
	depthStencilDesc.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
	textPsoDesc.DepthStencilState = depthStencilDesc;

	ThrowIfFailed(m_d3dDevice->CreateGraphicsPipelineState(&textPsoDesc, IID_PPV_ARGS(&(*PipelineState))));
	SetD3DObjectName(PipelineState->Get(), L"管线_文字_TextRender");
}

UINT32 TextRenderPass::GetUnicodeID(wchar_t c) const
{
	return static_cast<UINT32>(c);
}

UINT32 TextRenderPass::NextPowerOfTwo(UINT32 value)
{
	UINT32 result = 1u;
	while (result < value)
		result <<= 1u;
	return result;
}

float TextRenderPass::GetScreenWidth() const
{
	return std::max(m_screenWidth, 1.0f);
}

float TextRenderPass::GetScreenHeight() const
{
	return std::max(m_screenHeight, 1.0f);
}

float TextRenderPass::PixelToNdcX(float pixels) const
{
	return (pixels * 2.0f / GetScreenWidth()) - 1.0f;
}

float TextRenderPass::PixelToNdcY(float pixels) const
{
	return 1.0f - (pixels * 2.0f / GetScreenHeight());
}

float TextRenderPass::PixelToNdcWidth(float pixels) const
{
	return (pixels * 2.0f) / GetScreenWidth();
}

float TextRenderPass::PixelToNdcHeight(float pixels) const
{
	return (pixels * 2.0f) / GetScreenHeight();
}

void TextRenderPass::ReleaseTextBuffers()
{
	for (auto& [index, resource] : textVertexUploadBuffer)
	{
		if (resource)
			resource->Unmap(0, nullptr);
	}

	textVBGPUAddress.clear();
	textVertexBufferView.clear();
	textVertexBufferState.clear();
	textVertexDrawBuffer.clear();
	textVertexUploadBuffer.clear();
	m_textSubmissionCache.clear();
}

void TextRenderPass::ReleaseFontResources()
{
	if (!m_useSharedSrvDescriptorHeap)
		SrvDescriptorHeap.Reset();

	mFont = Font{};
	m_layoutCache.clear();
	m_fontBinary.clear();
	m_loadedPageCount = 0u;
	m_stats = TextRenderStats{};
	++m_layoutGeneration;
	m_textSubmissionCache.clear();
}

const GlyphLookupEntry* TextRenderPass::FindGlyphEntry(UINT32 codepoint) const
{
	const auto it = mFont.glyphLookup.find(codepoint);
	if (it == mFont.glyphLookup.end())
		return nullptr;

	return &it->second;
}

FontTextureData* TextRenderPass::FindFontTextureData(UINT32 codepoint)
{
	const GlyphLookupEntry* glyphEntry = FindGlyphEntry(codepoint);
	if (glyphEntry == nullptr)
		return nullptr;

	auto it = mFont.pFontTextureData.find(glyphEntry->pageIndex);
	if (it == mFont.pFontTextureData.end())
		return nullptr;

	return &it->second;
}

const SymbolData* TextRenderPass::FindGlyph(UINT32 codepoint, FontTextureData** outTextureData)
{
	if (outTextureData != nullptr)
		*outTextureData = nullptr;

	const GlyphLookupEntry* glyphEntry = FindGlyphEntry(codepoint);
	if (glyphEntry == nullptr)
		return nullptr;

	FontTextureData* textureData = FindFontTextureData(codepoint);
	if (textureData == nullptr)
		return nullptr;

	if (outTextureData != nullptr)
		*outTextureData = textureData;

	return &glyphEntry->symbol;
}

CharacterNumbering TextRenderPass::ResolvePageRange(UINT32 codepoint) const
{
	// 这里的目标不是把所有字符一次性预烘焙进图集，
	// 而是根据码点把字符映射到一个“适合当前场景”的分页区间：
	// 1. 高频 ASCII / 标点 / 全角字符走固定常驻页，减少运行时动态加载；
	// 2. CJK 主区与扩展区按 256 个码点一页切分，平衡页数量与单页大小；
	// 3. 其余字符走通用 256 码点兜底页，保证偶发字符也有加载入口。

	// ASCII 调试文字最常见，直接归到固定页。
	if (codepoint <= 0x007Fu)
		return { 0x000020u, 0x00007Fu };
	// 常用标点补充区，主要覆盖中文环境下容易遇到的 CJK 标点。
	if (0x002000u <= codepoint && codepoint <= 0x00206Fu)
		return { 0x002000u, 0x00206Fu };
	// CJK Symbols and Punctuation，单独常驻，避免中文标点频繁触发动态建页。
	if (0x003000u <= codepoint && codepoint <= 0x00303Fu)
		return { 0x003000u, 0x00303Fu };
	// 全角数字 / 全角字母 / 全角标点单独常驻，解决“：”这类字符的高频显示问题。
	if (0x00FF00u <= codepoint && codepoint <= 0x00FFEFu)
		return { 0x00FF00u, 0x00FFEFu };
	// CJK 扩展 A 走 256 码点分页，避免一次性铺满整个扩展区。
	if (0x003400u <= codepoint && codepoint <= 0x004DBFu)
	{
		const UINT32 begin = 0x003400u + (((codepoint - 0x003400u) / kDynamicPageSize) * kDynamicPageSize);
		return { begin, std::min<UINT32>(begin + kDynamicPageSize - 1u, 0x004DBFu) };
	}
	// CJK 主区同样按 256 码点分页；中文正文大多会命中这里。
	if (0x004E00u <= codepoint && codepoint <= 0x009FFFu)
	{
		const UINT32 begin = 0x004E00u + (((codepoint - 0x004E00u) / kDynamicPageSize) * kDynamicPageSize);
		return { begin, std::min<UINT32>(begin + kDynamicPageSize - 1u, 0x009FFFu) };
	}

	// 其他 BMP 字符统一落到通用兜底页。
	// 这样即便不是预设重点字符区，也能按需加载，而不是直接显示失败。
	const UINT32 begin = codepoint & ~static_cast<UINT32>(kDynamicPageSize - 1u);
	return { begin, std::min<UINT32>(begin + kDynamicPageSize - 1u, 0x0000FFFFu) };
}

FT_Face TextRenderPass::CreateFontFaceFromMemory() const
{
	if (!m_fontLibraryInitialized || m_fontBinary.empty())
		return nullptr;

	FT_Face face = nullptr;
	if (FT_New_Memory_Face(_FTlibrary, m_fontBinary.data(), static_cast<FT_Long>(m_fontBinary.size()), 0, &face) != 0)
		return nullptr;

	if (FT_Set_Pixel_Sizes(face, 0, mFont.font_size) != 0)
	{
		FT_Done_Face(face);
		return nullptr;
	}

	return face;
}

FontTextureData TextRenderPass::CreateFontTextureData(FT_Face face, CharacterNumbering numberingSet)
{
	FontTextureData data;
	data.NumberingSet = numberingSet;

	if (face == nullptr)
		return data;

	UINT32 glyphCount = 0u;
	UINT32 maxGlyphWidth = 0u;
	UINT32 maxGlyphHeight = 0u;

	for (UINT32 codepoint = numberingSet.begin; codepoint <= numberingSet.end; ++codepoint)
	{
		const FT_UInt glyphIndex = FT_Get_Char_Index(face, codepoint);
		if (glyphIndex == 0)
			continue;

		if (FT_Load_Glyph(face, glyphIndex, FT_LOAD_DEFAULT) != 0)
			continue;
		if (FT_Render_Glyph(face->glyph, FT_RENDER_MODE_NORMAL) != 0)
			continue;

		++glyphCount;
		maxGlyphWidth = std::max<UINT32>(maxGlyphWidth, face->glyph->bitmap.width);
		maxGlyphHeight = std::max<UINT32>(maxGlyphHeight, face->glyph->bitmap.rows);
	}

	if (glyphCount == 0u || maxGlyphWidth == 0u || maxGlyphHeight == 0u)
		return data;

	const UINT32 cellWidth = maxGlyphWidth + (kAtlasPadding * 2u);
	const UINT32 cellHeight = maxGlyphHeight + (kAtlasPadding * 2u);
	const UINT32 columnCount = std::max<UINT32>(1u, static_cast<UINT32>(std::ceil(std::sqrt(static_cast<float>(glyphCount)))));
	const UINT32 rowCount = (glyphCount + columnCount - 1u) / columnCount;

	data.textureWidth = NextPowerOfTwo(columnCount * cellWidth);
	data.textureHeight = NextPowerOfTwo(rowCount * cellHeight);
	data.pHostBuffer.assign(data.textureWidth * data.textureHeight, 0u);
	data.m_symbolsData.reserve(glyphCount);

	UINT32 glyphSlotIndex = 0u;
	for (UINT32 codepoint = numberingSet.begin; codepoint <= numberingSet.end; ++codepoint)
	{
		const FT_UInt glyphIndex = FT_Get_Char_Index(face, codepoint);
		if (glyphIndex == 0)
			continue;

		if (FT_Load_Glyph(face, glyphIndex, FT_LOAD_DEFAULT) != 0)
			continue;
		if (FT_Render_Glyph(face->glyph, FT_RENDER_MODE_NORMAL) != 0)
			continue;

		FT_GlyphSlot slot = face->glyph;
		const UINT32 atlasColumn = glyphSlotIndex % columnCount;
		const UINT32 atlasRow = glyphSlotIndex / columnCount;
		const UINT32 atlasX = atlasColumn * cellWidth + kAtlasPadding;
		const UINT32 atlasY = atlasRow * cellHeight + kAtlasPadding;

		for (UINT row = 0; row < slot->bitmap.rows; ++row)
		{
			for (UINT col = 0; col < slot->bitmap.width; ++col)
			{
				const UINT32 dstX = atlasX + col;
				const UINT32 dstY = atlasY + row;
				data.pHostBuffer[dstY * data.textureWidth + dstX] = slot->bitmap.buffer[row * slot->bitmap.pitch + col];
			}
		}

		SymbolData symbol;
		symbol.id = codepoint;
		symbol.uvMin = DirectX::XMFLOAT2(static_cast<float>(atlasX) / static_cast<float>(data.textureWidth), static_cast<float>(atlasY) / static_cast<float>(data.textureHeight));
		symbol.uvMax = DirectX::XMFLOAT2(static_cast<float>(atlasX + slot->bitmap.width) / static_cast<float>(data.textureWidth), static_cast<float>(atlasY + slot->bitmap.rows) / static_cast<float>(data.textureHeight));
		symbol.symbolSize = DirectX::XMFLOAT2(static_cast<float>(slot->bitmap.width), static_cast<float>(slot->bitmap.rows));
		symbol.bearing = DirectX::XMFLOAT2(static_cast<float>(slot->bitmap_left), static_cast<float>(slot->bitmap_top));
		symbol.advanceX = static_cast<float>(slot->advance.x >> 6);
		data.m_symbolsData.emplace(codepoint, symbol);

		++glyphSlotIndex;
	}

	return data;
}

bool TextRenderPass::UploadFontTexturePage(ID3D12GraphicsCommandList* cmdList, UINT pageIndex, FontTextureData& pageData)
{
	if (cmdList == nullptr || !SrvDescriptorHeap || pageIndex >= m_srvDescriptorCapacity)
		return false;

	// 每个图集页都会上传成一张独立的 R8 atlas 纹理，并占用一个 SRV 槽位。
	const D3D12_RESOURCE_DESC textureDesc = CD3DX12_RESOURCE_DESC::Tex2D(
		DXGI_FORMAT_R8_UNORM,
		pageData.textureWidth,
		pageData.textureHeight,
		1,
		1,
		1,
		0,
		D3D12_RESOURCE_FLAG_NONE);

	const D3D12_HEAP_PROPERTIES defaultHeap = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
	ThrowIfFailed(m_d3dDevice->CreateCommittedResource(
		&defaultHeap,
		D3D12_HEAP_FLAG_NONE,
		&textureDesc,
		D3D12_RESOURCE_STATE_COPY_DEST,
		nullptr,
		IID_PPV_ARGS(pageData.textureBuffer.GetAddressOf())));

	UINT64 uploadBufferSize = 0u;
	m_d3dDevice->GetCopyableFootprints(&textureDesc, 0, 1, 0, nullptr, nullptr, nullptr, &uploadBufferSize);

	const D3D12_HEAP_PROPERTIES uploadHeap = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
	const D3D12_RESOURCE_DESC uploadDesc = CD3DX12_RESOURCE_DESC::Buffer(uploadBufferSize);
	ThrowIfFailed(m_d3dDevice->CreateCommittedResource(
		&uploadHeap,
		D3D12_HEAP_FLAG_NONE,
		&uploadDesc,
		D3D12_RESOURCE_STATE_GENERIC_READ,
		nullptr,
		IID_PPV_ARGS(pageData.textureUploadBuffer.GetAddressOf())));

	D3D12_SUBRESOURCE_DATA subresourceData = {};
	subresourceData.pData = pageData.pHostBuffer.data();
	subresourceData.RowPitch = static_cast<LONG_PTR>(pageData.textureWidth);
	subresourceData.SlicePitch = static_cast<LONG_PTR>(pageData.textureWidth * pageData.textureHeight);
	UpdateSubresources(cmdList, pageData.textureBuffer.Get(), pageData.textureUploadBuffer.Get(), 0, 0, 1, &subresourceData);

	const D3D12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(
		pageData.textureBuffer.Get(),
		D3D12_RESOURCE_STATE_COPY_DEST,
		D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
	cmdList->ResourceBarrier(1, &barrier);

	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.Format = DXGI_FORMAT_R8_UNORM;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Texture2D.MipLevels = 1;
	srvDesc.Texture2D.MostDetailedMip = 0;
	srvDesc.Texture2D.ResourceMinLODClamp = 0.0f;

	pageData.CPUsrvHandle = SrvDescriptorHeap->GetCPUDescriptorHandleForHeapStart();
	pageData.GPUsrvHandle = SrvDescriptorHeap->GetGPUDescriptorHandleForHeapStart();
	pageData.CPUsrvHandle.Offset(static_cast<INT>(m_srvDescriptorBaseIndex + pageIndex), m_srvDescriptorSize);
	pageData.GPUsrvHandle.Offset(static_cast<INT>(m_srvDescriptorBaseIndex + pageIndex), m_srvDescriptorSize);
	m_d3dDevice->CreateShaderResourceView(pageData.textureBuffer.Get(), &srvDesc, pageData.CPUsrvHandle);

	pageData.pHostBuffer.clear();
	return true;
}

bool TextRenderPass::EnsurePageLoaded(CharacterNumbering numberingSet, ID3D12GraphicsCommandList* cmdList)
{
	const UINT64 rangeKey = MakeRangeKey(numberingSet);
	if (mFont.pageRangeLookup.find(rangeKey) != mFont.pageRangeLookup.end())
		return true;

	// 共享 heap 预留区耗尽时，当前实现直接拒绝继续加载新页。
	if (m_loadedPageCount >= m_srvDescriptorCapacity)
		return false;

	FT_Face face = CreateFontFaceFromMemory();
	if (face == nullptr)
		return false;

	FontTextureData pageData = CreateFontTextureData(face, numberingSet);
	FT_Done_Face(face);
	if (pageData.m_symbolsData.empty())
		return false;

	const UINT pageIndex = m_loadedPageCount;
	if (!UploadFontTexturePage(cmdList, pageIndex, pageData))
		return false;

	for (const auto& [codepoint, symbol] : pageData.m_symbolsData)
		mFont.glyphLookup[codepoint] = GlyphLookupEntry{ pageIndex, symbol };

	mFont.pageRangeLookup[rangeKey] = pageIndex;
	mFont.pFontTextureData[pageIndex] = std::move(pageData);
	++m_loadedPageCount;
	++m_stats.pageLoads;
	m_stats.loadedPageCount = m_loadedPageCount;
	return true;
}

bool TextRenderPass::EnsureGlyphLoaded(UINT32 codepoint, ID3D12GraphicsCommandList* cmdList)
{
	if (FindGlyphEntry(codepoint) != nullptr)
		return true;

	// 缺页时按码点范围懒加载对应图集页，而不是启动时一次性铺满大字符区间。
	const CharacterNumbering range = ResolvePageRange(codepoint);
	if (!EnsurePageLoaded(range, cmdList))
		return false;

	return FindGlyphEntry(codepoint) != nullptr;
}

TextLayoutCacheEntry* TextRenderPass::GetOrBuildLayout(const std::wstring& text, ID3D12GraphicsCommandList* cmdList, bool* outCacheHit)
{
	if (outCacheHit != nullptr)
		*outCacheHit = false;

	TextLayoutCacheEntry& layoutEntry = m_layoutCache[text];
	if (layoutEntry.valid && layoutEntry.generation == m_layoutGeneration)
	{
		if (outCacheHit != nullptr)
			*outCacheHit = true;
		return &layoutEntry;
	}

	if (!BuildLayoutCache(text, cmdList, layoutEntry))
		return nullptr;

	return &layoutEntry;
}

bool TextRenderPass::BuildLayoutCache(const std::wstring& text, ID3D12GraphicsCommandList* cmdList, TextLayoutCacheEntry& layoutEntry)
{
	layoutEntry = TextLayoutCacheEntry{};
	layoutEntry.text = text;
	layoutEntry.generation = m_layoutGeneration;

	if (text.empty())
	{
		layoutEntry.valid = true;
		return true;
	}

	EnsureGlyphLoaded(static_cast<UINT32>(L'?'), cmdList);
	const GlyphLookupEntry* fallbackGlyph = FindGlyphEntry(static_cast<UINT32>(L'?'));
	const float lineAdvancePixels = std::max(mFont.lineHeight, static_cast<float>(mFont.font_size));

	UINT totalGlyphCount = 0u;
	UINT currentBatchStart = 0u;
	UINT currentBatchCount = 0u;
	UINT currentBatchPageIndex = static_cast<UINT>(-1);
	float cursorPixelX = 0.0f;
	float lineOffsetY = 0.0f;
	float layoutWidth = 0.0f;
	float layoutHeight = 0.0f;

	const auto closeCurrentBatch = [&]()
	{
		if (currentBatchPageIndex == static_cast<UINT>(-1) || currentBatchCount == 0u)
			return;

		layoutEntry.batches.push_back(TextDrawBatch{ currentBatchPageIndex, currentBatchStart, currentBatchCount });
		currentBatchStart = totalGlyphCount;
		currentBatchCount = 0u;
		currentBatchPageIndex = static_cast<UINT>(-1);
	};

	for (const wchar_t character : text)
	{
		if (character == L'\0')
			break;

		if (character == L'\n')
		{
			closeCurrentBatch();
			cursorPixelX = 0.0f;
			lineOffsetY += lineAdvancePixels;
			layoutHeight = std::max(layoutHeight, lineOffsetY);
			continue;
		}

		if (totalGlyphCount >= static_cast<UINT>(maxNumTextCharacters))
			break;

		UINT32 codepoint = GetUnicodeID(character);
		if (!EnsureGlyphLoaded(codepoint, cmdList))
			codepoint = static_cast<UINT32>(L'?');

		const GlyphLookupEntry* glyphEntry = FindGlyphEntry(codepoint);
		if (glyphEntry == nullptr)
			glyphEntry = fallbackGlyph;
		if (glyphEntry == nullptr)
			continue;

		if (currentBatchPageIndex != glyphEntry->pageIndex)
		{
			closeCurrentBatch();
			currentBatchPageIndex = glyphEntry->pageIndex;
			currentBatchStart = totalGlyphCount;
		}

		// layout cache 只保存与文本内容相关的像素布局，不绑定具体颜色与屏幕位置。
		const SymbolData& symbol = glyphEntry->symbol;
		GlyphPlacement placement;
		placement.codepoint = codepoint;
		placement.pageIndex = glyphEntry->pageIndex;
		placement.left = cursorPixelX + symbol.bearing.x;
		placement.top = lineOffsetY + mFont.ascent - symbol.bearing.y;
		placement.width = symbol.symbolSize.x;
		placement.height = symbol.symbolSize.y;
		placement.texRect = DirectX::XMFLOAT4(symbol.uvMin.x, symbol.uvMin.y, symbol.uvMax.x, symbol.uvMax.y);
		layoutEntry.placements.push_back(placement);

		layoutWidth = std::max(layoutWidth, placement.left + placement.width);
		layoutHeight = std::max(layoutHeight, placement.top + placement.height);
		cursorPixelX += symbol.advanceX;
		++totalGlyphCount;
		++currentBatchCount;
	}

	closeCurrentBatch();
	layoutEntry.valid = true;
	layoutEntry.glyphCount = static_cast<UINT>(layoutEntry.placements.size());
	layoutEntry.width = layoutWidth;
	layoutEntry.height = layoutHeight;
	return true;
}

void TextRenderPass::FillVerticesFromLayout(const TextLayoutCacheEntry& layoutEntry, const DirectX::XMFLOAT2& pos, const DirectX::XMFLOAT4& color, TextVertex* vertices) const
{
	// 提交阶段再把 layout cache 转成最终实例数据，这样位置/颜色变化不需要重建布局。
	const float startPixelX = std::clamp(pos.x * 0.5f, 0.0f, 1.0f) * GetScreenWidth();
	const float startPixelY = std::clamp(pos.y * 0.5f, 0.0f, 1.0f) * GetScreenHeight();

	for (UINT glyphIndex = 0u; glyphIndex < layoutEntry.glyphCount; ++glyphIndex)
	{
		const GlyphPlacement& placement = layoutEntry.placements[glyphIndex];
		vertices[glyphIndex] = TextVertex(
			PixelToNdcX(startPixelX + placement.left),
			PixelToNdcY(startPixelY + placement.top),
			PixelToNdcWidth(placement.width),
			PixelToNdcHeight(placement.height),
			color.x,
			color.y,
			color.z,
			color.w,
			placement.texRect.x,
			placement.texRect.y,
			placement.texRect.z,
			placement.texRect.w);
	}
}

void TextRenderPass::UpdateStats(UINT glyphCount, UINT batchCount, UINT64 copyBytes, bool submissionCacheHit, bool layoutCacheHit)
{
	++m_stats.drawCalls;
	m_stats.lastSubmissionCacheHit = submissionCacheHit;
	m_stats.lastLayoutCacheHit = layoutCacheHit;
	m_stats.lastGlyphCount = glyphCount;
	m_stats.lastBatchCount = batchCount;
	m_stats.lastCopyBytes = copyBytes;
	m_stats.loadedPageCount = m_loadedPageCount;
	m_stats.glyphsSubmitted += glyphCount;
	m_stats.batchesSubmitted += batchCount;
	m_stats.copyBytes += copyBytes;
	if (submissionCacheHit)
		++m_stats.submissionCacheHits;
	else
		++m_stats.submissionCacheMisses;
	if (layoutCacheHit)
		++m_stats.layoutCacheHits;
	else
		++m_stats.layoutCacheMisses;
}

UINT64 TextRenderPass::MakeRangeKey(const CharacterNumbering& range)
{
	return (static_cast<UINT64>(range.begin) << 32) | static_cast<UINT64>(range.end);
}

bool TextRenderPass::DXCreateFont(std::wstring fontFilename, int fontSize)
{
	if (!m_fontLibraryInitialized || fontSize <= 0)
		return false;

	// 重建字体时同时清空旧图集页、布局缓存和提交缓存，避免调用失效数据。
	ReleaseTextBuffers();
	ReleaseFontResources();

	std::ifstream file(std::filesystem::path(fontFilename), std::ios::binary | std::ios::ate);
	if (!file.is_open())
		return false;

	const std::streamsize fileSize = file.tellg();
	if (fileSize <= 0)
		return false;

	file.seekg(0, std::ios::beg);
	m_fontBinary.resize(static_cast<size_t>(fileSize));
	if (!file.read(reinterpret_cast<char*>(m_fontBinary.data()), fileSize))
		return false;

	mFont.font_size = static_cast<UINT>(fontSize);
	FT_Face face = CreateFontFaceFromMemory();
	if (face == nullptr)
		return false;

	mFont.name = std::filesystem::path(fontFilename).filename().wstring();
	mFont.lineHeight = static_cast<float>(std::max<long>(face->size->metrics.height >> 6, fontSize));
	mFont.ascent = static_cast<float>(std::max<long>(face->size->metrics.ascender >> 6, fontSize));
	mFont.descent = static_cast<float>(std::abs(face->size->metrics.descender >> 6));
	FT_Done_Face(face);

	if (m_useSharedSrvDescriptorHeap)
	{
		SrvDescriptorHeap = m_sharedSrvDescriptorHeap;
		if (m_srvDescriptorCapacity == 0u)
			m_srvDescriptorCapacity = kDefaultTextDescriptorReservation;
	}
	else
	{
		m_srvDescriptorCapacity = kDefaultTextDescriptorReservation;
		m_srvDescriptorSize = m_d3dDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
		m_srvDescriptorBaseIndex = 0u;

		D3D12_DESCRIPTOR_HEAP_DESC srvHeapDesc = {};
		srvHeapDesc.NumDescriptors = m_srvDescriptorCapacity;
		srvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
		srvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
		ThrowIfFailed(m_d3dDevice->CreateDescriptorHeap(&srvHeapDesc, IID_PPV_ARGS(&SrvDescriptorHeap)));
	}

	const CharacterNumbering preloadRanges[] =
	{
		{ 0x000020u, 0x00007Fu },
		{ 0x003000u, 0x00303Fu },
		{ 0x00FF00u, 0x00FFEFu }
	};

	// 启动时只预热最常用的 ASCII / CJK 标点 / 全角字符页，其余字符按需加载。
	for (const CharacterNumbering& range : preloadRanges)
		EnsurePageLoaded(range, m_commandList);

	for (UINT backBufferIndex = 0; backBufferIndex < m_SwapChainBufferCount; ++backBufferIndex)
	{
		// 每个 back buffer 都维护一对 upload/default 顶点缓冲，
		// CPU 写 upload，GPU 最终从 default buffer 读取。
		ComPtr<ID3D12Resource> uploadVertexBuffer;
		ComPtr<ID3D12Resource> drawVertexBuffer;
		const D3D12_HEAP_PROPERTIES uploadHeap = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
		const D3D12_RESOURCE_DESC bufferDesc = CD3DX12_RESOURCE_DESC::Buffer(static_cast<UINT64>(maxNumTextCharacters) * sizeof(TextVertex));
		ThrowIfFailed(m_d3dDevice->CreateCommittedResource(
			&uploadHeap,
			D3D12_HEAP_FLAG_NONE,
			&bufferDesc,
			D3D12_RESOURCE_STATE_GENERIC_READ,
			nullptr,
			IID_PPV_ARGS(uploadVertexBuffer.GetAddressOf())));

		UINT8* mappedData = nullptr;
		const CD3DX12_RANGE readRange(0, 0);
		ThrowIfFailed(uploadVertexBuffer->Map(0, &readRange, reinterpret_cast<void**>(&mappedData)));

		const D3D12_HEAP_PROPERTIES defaultHeap = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
		ThrowIfFailed(m_d3dDevice->CreateCommittedResource(
			&defaultHeap,
			D3D12_HEAP_FLAG_NONE,
			&bufferDesc,
			D3D12_RESOURCE_STATE_COPY_DEST,
			nullptr,
			IID_PPV_ARGS(drawVertexBuffer.GetAddressOf())));

		textVBGPUAddress[backBufferIndex] = mappedData;
		textVertexBufferView[backBufferIndex].BufferLocation = drawVertexBuffer->GetGPUVirtualAddress();
		textVertexBufferView[backBufferIndex].StrideInBytes = sizeof(TextVertex);
		textVertexBufferView[backBufferIndex].SizeInBytes = static_cast<UINT>(maxNumTextCharacters * sizeof(TextVertex));
		textVertexBufferState[backBufferIndex] = D3D12_RESOURCE_STATE_COPY_DEST;
		textVertexUploadBuffer[backBufferIndex] = std::move(uploadVertexBuffer);
		textVertexDrawBuffer[backBufferIndex] = std::move(drawVertexBuffer);
	}

	return !mFont.pFontTextureData.empty();
}

void TextRenderPass::SetScreenSize(float width, float height)
{
	m_screenWidth = std::max(width, 1.0f);
	m_screenHeight = std::max(height, 1.0f);
	m_textSubmissionCache.clear();
}

void TextRenderPass::SetSharedSrvDescriptorHeap(ID3D12DescriptorHeap* descriptorHeap, UINT descriptorSize, UINT descriptorBaseIndex, UINT descriptorCapacity)
{
	m_sharedSrvDescriptorHeap = descriptorHeap;
	m_useSharedSrvDescriptorHeap = (descriptorHeap != nullptr);
	m_srvDescriptorSize = descriptorSize;
	m_srvDescriptorBaseIndex = descriptorBaseIndex;
	m_srvDescriptorCapacity = descriptorCapacity > 0u ? descriptorCapacity : kDefaultTextDescriptorReservation;
	if (m_useSharedSrvDescriptorHeap)
		SrvDescriptorHeap = m_sharedSrvDescriptorHeap;
	else
		SrvDescriptorHeap.Reset();
}

UINT TextRenderPass::GetSrvDescriptorCount() const
{
	return m_srvDescriptorCapacity;
}

const TextRenderStats& TextRenderPass::GetStats() const
{
	return m_stats;
}

void TextRenderPass::DXDrawText(ID3D12GraphicsCommandList* cmdList, std::wstring text, const DirectX::XMFLOAT2 pos, const DirectX::XMFLOAT4& color, UINT CurrBackBufferIndex)
{
	if (cmdList == nullptr || text.empty())
		return;

	const auto vbAddress = textVBGPUAddress.find(CurrBackBufferIndex);
	const auto vbView = textVertexBufferView.find(CurrBackBufferIndex);
	const auto vbUpload = textVertexUploadBuffer.find(CurrBackBufferIndex);
	const auto vbDraw = textVertexDrawBuffer.find(CurrBackBufferIndex);
	const auto vbState = textVertexBufferState.find(CurrBackBufferIndex);
	if (vbAddress == textVBGPUAddress.end() ||
		vbView == textVertexBufferView.end() ||
		vbUpload == textVertexUploadBuffer.end() ||
		vbDraw == textVertexDrawBuffer.end() ||
		vbState == textVertexBufferState.end() ||
		!SrvDescriptorHeap ||
		!RootSignature)
		return;

	bool layoutCacheHit = false;
	TextLayoutCacheEntry* layoutEntry = GetOrBuildLayout(text, cmdList, &layoutCacheHit);
	if (layoutEntry == nullptr || layoutEntry->glyphCount == 0u)
	{
		UpdateStats(0u, 0u, 0u, false, layoutCacheHit);
		return;
	}

	cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
	cmdList->IASetVertexBuffers(0, 1, &vbView->second);
	cmdList->SetGraphicsRootSignature(RootSignature.Get());
	if (!m_useSharedSrvDescriptorHeap)
		cmdList->SetDescriptorHeaps(1, SrvDescriptorHeap.GetAddressOf());

	const auto drawBatches = [&](const std::vector<TextDrawBatch>& batches)
	{
		for (const TextDrawBatch& batch : batches)
		{
			if (batch.instanceCount == 0u)
				continue;

			auto textureIt = mFont.pFontTextureData.find(batch.pageIndex);
			if (textureIt == mFont.pFontTextureData.end())
				continue;

			cmdList->SetGraphicsRootDescriptorTable(0, textureIt->second.GPUsrvHandle);
			cmdList->DrawInstanced(4, batch.instanceCount, 0, batch.startInstance);
		}
	};

	CachedTextSubmission& cachedSubmission = m_textSubmissionCache[CurrBackBufferIndex];
	// submission cache 命中后，说明 default buffer 中已有当前文本的实例数据，
	// 本次可以直接 draw，跳过实例重写与 upload->default copy。
	const bool submissionCacheHit =
		cachedSubmission.valid &&
		cachedSubmission.layoutGeneration == layoutEntry->generation &&
		cachedSubmission.text == text &&
		cachedSubmission.pos.x == pos.x &&
		cachedSubmission.pos.y == pos.y &&
		cachedSubmission.color.x == color.x &&
		cachedSubmission.color.y == color.y &&
		cachedSubmission.color.z == color.z &&
		cachedSubmission.color.w == color.w;

	if (submissionCacheHit)
	{
		drawBatches(cachedSubmission.batches);
		UpdateStats(layoutEntry->glyphCount, static_cast<UINT>(layoutEntry->batches.size()), 0u, true, layoutCacheHit);
		return;
	}

	TextVertex* vertices = reinterpret_cast<TextVertex*>(vbAddress->second);
	FillVerticesFromLayout(*layoutEntry, pos, color, vertices);

	if (vbState->second != D3D12_RESOURCE_STATE_COPY_DEST)
	{
		const D3D12_RESOURCE_BARRIER toCopyDest = CD3DX12_RESOURCE_BARRIER::Transition(
			vbDraw->second.Get(),
			vbState->second,
			D3D12_RESOURCE_STATE_COPY_DEST);
		cmdList->ResourceBarrier(1, &toCopyDest);
		vbState->second = D3D12_RESOURCE_STATE_COPY_DEST;
	}

	const UINT64 copySize = static_cast<UINT64>(layoutEntry->glyphCount) * sizeof(TextVertex);
	// cache miss 时把本次实例数据从 upload buffer 拷到 default buffer，
	// 后续 draw 与 cache hit 都直接复用 default buffer 内容。
	cmdList->CopyBufferRegion(vbDraw->second.Get(), 0, vbUpload->second.Get(), 0, copySize);

	const D3D12_RESOURCE_BARRIER toVertexBuffer = CD3DX12_RESOURCE_BARRIER::Transition(
		vbDraw->second.Get(),
		D3D12_RESOURCE_STATE_COPY_DEST,
		D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);
	cmdList->ResourceBarrier(1, &toVertexBuffer);
	vbState->second = D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;

	cachedSubmission.valid = true;
	cachedSubmission.layoutGeneration = layoutEntry->generation;
	cachedSubmission.text = text;
	cachedSubmission.pos = pos;
	cachedSubmission.color = color;
	cachedSubmission.glyphCount = layoutEntry->glyphCount;
	cachedSubmission.batches = layoutEntry->batches;

	drawBatches(layoutEntry->batches);
	UpdateStats(layoutEntry->glyphCount, static_cast<UINT>(layoutEntry->batches.size()), copySize, false, layoutCacheHit);
}
