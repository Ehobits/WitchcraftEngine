#include "TextRender.h"

TextRender::TextRender(ID3D12Device* device, ID3D12GraphicsCommandList* commandList, UINT SwapChainBufferCount)
: m_d3dDevice(device), m_commandList(commandList),
m_SwapChainBufferCount(SwapChainBufferCount)
{
	FT_Error error = FT_Init_FreeType(&_FTlibrary);
	if (error != 0)
		MessageBox(nullptr, L"TextRender初始化失败！", L"", MB_OK);
}

TextRender::~TextRender()
{
}

void TextRender::CreateRootSignature()
{
	CD3DX12_DESCRIPTOR_RANGE1 cbvTable;
	cbvTable.Init(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 1, 0, 0);

	// 为根描述符创建一个根参数并填写
	CD3DX12_ROOT_PARAMETER1  rootParameters[2]; // 需要两个根参数
	rootParameters[0].InitAsDescriptorTable(1, &cbvTable);

	CD3DX12_DESCRIPTOR_RANGE1 texTable0;
	texTable0.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0);

	// 填写描述符表的参数。请记住，按更改频率对参数进行排序是个好主意。
	// 我们的常量缓冲区将在每帧中多次更改，而我们的描述符表在renderText功能中根本不会更改。
	rootParameters[1].InitAsDescriptorTable(1, &texTable0, D3D12_SHADER_VISIBILITY_PIXEL);

	// 创建静态采样器
	D3D12_STATIC_SAMPLER_DESC sampler = {};
	sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
	sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
	sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
	sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;

	CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSignatureDesc;
	rootSignatureDesc.Init_1_1(_countof(rootParameters), rootParameters,
		1, &sampler,
		D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

	ComPtr<ID3DBlob> errorBuff = nullptr;
	ComPtr<ID3DBlob> signature = nullptr;
	HRESULT hr = D3DX12SerializeVersionedRootSignature(&rootSignatureDesc,
		D3D_ROOT_SIGNATURE_VERSION_1, &signature, &errorBuff);
	if (FAILED(hr))
	{
		::OutputDebugStringA((char*)errorBuff->GetBufferPointer());
	}
	ThrowIfFailed(hr);

	ThrowIfFailed(m_d3dDevice->CreateRootSignature(
		0,
		signature->GetBufferPointer(),
		signature->GetBufferSize(),
		IID_PPV_ARGS(RootSignature.GetAddressOf())));
}

void TextRender::CreatePipesAndShaders(ID3DBlob* vertexShader, ID3DBlob* pixelShader, DXGI_FORMAT BackBufferFormat, DXGI_FORMAT DepthStencilFormat, ComPtr<ID3D12PipelineState> *PipelineState)
{
	// 定义顶点输入布局。
	std::vector<D3D12_INPUT_ELEMENT_DESC> InputElementDescs =
	{
		{ "POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1 },
		{ "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 16, D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1 },
		{ "TEXCOORD", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 32, D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1 }
	};
	//
	//文字对象的PSO。
	//
	D3D12_GRAPHICS_PIPELINE_STATE_DESC textPsoDesc = {};
	textPsoDesc.InputLayout = { InputElementDescs.data(), (UINT)InputElementDescs.size() };
	textPsoDesc.pRootSignature = RootSignature.Get();
	textPsoDesc.VS = CD3DX12_SHADER_BYTECODE(vertexShader);
	textPsoDesc.PS = CD3DX12_SHADER_BYTECODE(pixelShader);
	textPsoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	textPsoDesc.RTVFormats[0] = BackBufferFormat;
	textPsoDesc.DSVFormat = DepthStencilFormat;
	DXGI_SAMPLE_DESC sampleDesc = {};
	sampleDesc.Count = 1; // 多重采样计数（没有多重采样，所以我们只输入 1，因为我们仍然需要 1 个样本）
	textPsoDesc.SampleDesc = sampleDesc;
	textPsoDesc.SampleMask = UINT_MAX;
	textPsoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);

	D3D12_BLEND_DESC textBlendStateDesc = {};
	textBlendStateDesc.AlphaToCoverageEnable = FALSE;
	textBlendStateDesc.IndependentBlendEnable = FALSE;
	textBlendStateDesc.RenderTarget[0].BlendEnable = TRUE;

	textBlendStateDesc.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
	textBlendStateDesc.RenderTarget[0].DestBlend = D3D12_BLEND_ONE;
	textBlendStateDesc.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;

	textBlendStateDesc.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_SRC_ALPHA;
	textBlendStateDesc.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ONE;
	textBlendStateDesc.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;

	textBlendStateDesc.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

	textPsoDesc.BlendState = textBlendStateDesc;
	textPsoDesc.NumRenderTargets = 1;
	D3D12_DEPTH_STENCIL_DESC textDepthStencilDesc = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
	textDepthStencilDesc.DepthEnable = false;
	textPsoDesc.DepthStencilState = textDepthStencilDesc;
	ThrowIfFailed(m_d3dDevice->CreateGraphicsPipelineState(&textPsoDesc,
		IID_PPV_ARGS(&(*PipelineState))));
}

UINT16 TextRender::GetUnicodeID(wchar_t c)
{
	// 获取Unicode编码
	std::wstringstream wss;
	wss << std::showbase << static_cast<unsigned>(c);
	std::wstring tmp = wss.str();
	UINT16 unicode_encoding = _wtoi(tmp.c_str());

	return unicode_encoding;
}

FontTextureData TextRender::CreateFontTextureData(FT_Face face, FT_GlyphSlot slot, CharacterNumbering NumberingSet, std::wstring name)
{
	FT_Error error = 0;

	FontTextureData Data;

	// 计算最大符号高度和宽度
	unsigned int symbolCount = 0;
	unsigned int symbolWidth = 0;
	unsigned int symbolHeight = 0;
	for (UINT32 i = NumberingSet.begin; i <= NumberingSet.end && error == 0; i++) // ASCII符号开始的所有符号
	{
		++symbolCount;

		FT_UInt glyph_index = FT_Get_Char_Index(face, (FT_ULong)i);

		error = FT_Load_Glyph(face, glyph_index, FT_LOAD_DEFAULT | FT_LOAD_NO_BITMAP);
		if (error == 0)
			error = FT_Render_Glyph(face->glyph, FT_RENDER_MODE_MONO);

		if (error == 0)
		{
			// 重新计算最大大小
			if (slot->bitmap.width > symbolWidth)
			{
				symbolWidth = slot->bitmap.width;
			}
			if (slot->bitmap.rows > symbolHeight)
			{
				symbolHeight = slot->bitmap.rows;
			}
		}
	}
	Data.symbolWidth = symbolWidth;
	Data.symbolHeight = symbolHeight;

	// 计算字体的纹理大小
	UINT textureWidth = 0;
	UINT textureHeight = 0;
	UINT horSymbols = 0;
	UINT vertSymbols = 0;
	if (error == 0)
	{
		// Calculate texture size
		if (error == 0)
		{
			int symbolSquare = symbolWidth * symbolHeight;
			int textureSquare = symbolSquare * symbolCount;

			textureWidth = (int)ceil(sqrtf((float)textureSquare));
			DWORD idx = 0;
			_BitScanReverse(&idx, textureWidth);
			UINT res = 1 << idx;

			textureWidth = (textureWidth & ~res) == 0 ? res : (res << 1);

			horSymbols = textureWidth / symbolWidth;
			vertSymbols = ((UINT)symbolCount + horSymbols - 1) / horSymbols;

			idx = 0;
			_BitScanReverse(&idx, vertSymbols * symbolHeight);
			res = 1 << idx;

			textureHeight = (vertSymbols * symbolHeight & ~res) == 0 ? res : (res << 1);
		}
	}
	Data.textureWidth = textureWidth;
	Data.textureHeight = textureHeight;

	// 渲染字形和
	UINT64 Symbolcount = 0;
	if (error == 0)
	{
		Data.m_symbolsData.resize(symbolCount);

		Data.pHostBuffer.resize(textureWidth * textureHeight);

		// 循环符号
		for (UINT32 ch = NumberingSet.begin; ch <= NumberingSet.end && error == 0; ch++)
		{
			FT_UInt glyphIndex = FT_Get_Char_Index(face, ch);
			FT_Glyph glyph = nullptr;
			error = FT_Get_Glyph(face->glyph, &glyph);
			if (glyphIndex > 0 && (error == 0))
			{
				error = FT_Load_Glyph(face, glyphIndex, FT_LOAD_DEFAULT | FT_LOAD_NO_BITMAP);
				if (error == 0)
					error = FT_Render_Glyph(face->glyph, FT_RENDER_MODE_MONO);

				if (error == 0)
				{
					UINT32 idx = ch - NumberingSet.begin;

					int symbolX = idx % horSymbols * symbolWidth;
					int symbolY = idx / horSymbols * symbolHeight;

					FT_GlyphSlot slot = face->glyph;

					// 循环符号像素
					for (UINT j = 0; j < slot->bitmap.rows; j++)
					{
						for (UINT i = 0; i < slot->bitmap.width; i++)
						{
							// 目标像素坐标
							int dstX = symbolX + i;
							int dstY = symbolY + j;

							switch (slot->bitmap.pixel_mode)
							{
							case FT_PIXEL_MODE_MONO:
							{
								int byteIdx = i / 8;
								int bitIdx = 7 - (i % 8);
								Data.pHostBuffer[dstY * textureWidth + dstX] = ((slot->bitmap.buffer[j * slot->bitmap.pitch + byteIdx] >> bitIdx) & 0x1) * 0xff;
							}
							break;

							case FT_PIXEL_MODE_GRAY:
								Data.pHostBuffer[dstY * textureWidth + dstX] = slot->bitmap.buffer[j * slot->bitmap.pitch + i];
								break;

							case FT_PIXEL_MODE_GRAY2:
								Data.pHostBuffer[dstY * textureWidth + dstX] = slot->bitmap.buffer[j * slot->bitmap.pitch + i];
								break;

							case FT_PIXEL_MODE_GRAY4:
								Data.pHostBuffer[dstY * textureWidth + dstX] = slot->bitmap.buffer[j * slot->bitmap.pitch + i];
								break;

							case FT_PIXEL_MODE_BGRA:
								break;

							default:
								assert(0); // FT: 未知像素类型
								break;
							}
						}
					}


					std::wstringstream wss;
					wss << std::showbase << ch - 1;
					std::wstring tmp = wss.str();
					Data.m_symbolsData[idx].id = _wtoi(tmp.c_str());
					Data.m_symbolsData[idx].leftTop = DirectX::XMFLOAT2{ (float)symbolX / textureWidth, (float)symbolY / textureHeight };
					Data.m_symbolsData[idx].rightBottom = DirectX::XMFLOAT2{ (float)(symbolX + slot->bitmap.width) / textureWidth, (float)(symbolY + slot->bitmap.rows) / textureHeight };
					Data.m_symbolsData[idx].symbolSize = DirectX::XMFLOAT2{ (float)slot->bitmap.width, (float)slot->bitmap.rows };
					Data.m_symbolsData[idx].basePoint = DirectX::XMFLOAT2{ (float)slot->bitmap_left, (float)slot->bitmap_top };
				}
				Symbolcount++;
			}
			error = 0;
		}
	}

	//// 保存到文件
	//{
	//	FILE* BinFile;
	//	BITMAPFILEHEADER FileHeader;	//定义BMP文件头
	//	BITMAPINFOHEADER BmpHeader;		//定义信息头
	//	int i, extend;
	//	bool Suc = true;
	//	BYTE p[4], * pCur;
	//	BYTE* ex = nullptr;

	//	//存储图像数据，每行字节数为4的倍数
	//	//所以 + 3是怕出现不满足4的倍数这种情况；如果是4的倍数则结果和不 + 3的结果是一样的；如果不是4的倍数则结果进1位
	//	//  /4*4除以四在乘以四是把数据归为4的倍数。
	//	extend = (textureWidth + 3) / 4 * 4 - textureWidth;

	//	// Open File
	//	std::wstring bmpflie = (EngineUtils::GetAppDirPath() + name + L"_" + std::to_wstring(imageCount) + L".bmp").c_str();
	//	if ((_wfopen_s(&BinFile, bmpflie.c_str(), L"w+b")))
	//		MessageBox(nullptr, L"保存图片失败！", L"", MB_OK);

	//	//参数填法见结构链接  BMP文件头
	//	//FileHeader.bfType = ((WORD)('M' << 8) | 'B');
	//	FileHeader.bfType = 0x4d42;//两种方法都可以
	//	//biBitCount=8时，为256色图像，BMP位图中有256个数据结构RGBQUAD，一个调色板占用4字节数据，所以256色图像的调色板长度为256*4为1024字节。
	//	FileHeader.bfOffBits = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER) + 256 * 4L;//2个头结构后加调色板（bfOffBits = sizeof (BITMAPFILEHEADER) + sizeof (BITMAPINFOHEADER) + NumberOfRGBQUAD * sizeof (RGBQUAD) ;）
	//	FileHeader.bfSize = FileHeader.bfOffBits + (textureWidth + extend) * textureHeight;//bfSize=bfoffBits+数据区的大小
	//	FileHeader.bfReserved1 = 0;
	//	FileHeader.bfReserved2 = 0;
	//	if (fwrite((void*)&FileHeader, 1, sizeof(FileHeader), BinFile) != sizeof(FileHeader)) Suc = false;
	//	// Fill the ImgHeader   信息头
	//	BmpHeader.biSize = sizeof(BITMAPINFOHEADER);//sizeof(BITMAPINFOHEADER)=40
	//	BmpHeader.biWidth = textureWidth;
	//	BmpHeader.biHeight = textureHeight;
	//	BmpHeader.biPlanes = 1;
	//	BmpHeader.biBitCount = 8;
	//	BmpHeader.biCompression = 0;
	//	BmpHeader.biSizeImage = 0;
	//	BmpHeader.biXPelsPerMeter = 0;
	//	BmpHeader.biYPelsPerMeter = 0;
	//	BmpHeader.biClrUsed = 0;
	//	BmpHeader.biClrImportant = 0;

	//	if (fwrite((void*)&BmpHeader, 1, sizeof(BmpHeader), BinFile) != sizeof(BmpHeader)) Suc = false;

	//	// 写入调色板
	//	for (i = 0, p[3] = 0; i < 256; i++)
	//	{
	//		//下面两句选择保存的图像颜色刚好互补
	//		//p[0] = p[1] = p[2] = 255 - i; // blue,green,red;
	//		p[0] = p[1] = p[2] = i;
	//		if (fwrite((void*)p, 1, 4, BinFile) != 4) { Suc = false; break; }
	//	}

	//	if (extend)
	//	{
	//		ex = new BYTE[extend]; //填充数组大小为 0~3
	//		memset(ex, 0, extend);
	//	}

	//	BYTE* pImg = Data.pHostBuffer.data();
	//	//write data 图像数据 从下到上保存
	//	for (pCur = pImg + (textureHeight - 1) * textureWidth; pCur >= pImg; pCur -= textureWidth)
	//	{
	//		if (fwrite((void*)pCur, 1, textureWidth, BinFile) != (unsigned int)textureWidth) Suc = false; // 真实的数据
	//		if (extend) // 扩充的数据 这里填充0
	//			if (fwrite((void*)ex, 1, extend, BinFile) != 1) Suc = false;
	//	}

	//	fclose(BinFile);
	//	if (extend)
	//		delete[] ex;

	//}

	imageCount++;

	return Data;
}

bool TextRender::DXCreateFont(std::wstring fontFilename, int fontSize)
{
	FT_Face face;

	std::vector<BYTE> data;
	DWORD filerror = NO_ERROR;
	HANDLE hFile = CreateFile(
		fontFilename.c_str(),
		GENERIC_READ,
		FILE_SHARE_READ,
		nullptr,
		OPEN_EXISTING,
		FILE_ATTRIBUTE_NORMAL,
		nullptr
	);
	filerror = GetLastError();
	if (hFile != INVALID_HANDLE_VALUE)
	{
		// 仅适用于小于 2Gb 的文件。我们不需要更大的
		DWORD size = GetFileSize(hFile, nullptr);
		filerror = GetLastError();
		if (filerror == NO_ERROR)
		{
			data.resize(size);

			DWORD readBytes = 0;
			ReadFile(hFile, data.data(), size, &readBytes, nullptr);
			filerror = GetLastError();
			if (readBytes != size)
			{
				OutputDebugString(L"文件： ");
				OutputDebugString(fontFilename.c_str());
				OutputDebugString(L" 读取的字节数错误。\n");
				// 我们需要以某种方式镜像错误，因为我们只期望拿到给定的字节数
				if (filerror == NO_ERROR)
					filerror = ERROR_READ_FAULT;
			}
		}

		CloseHandle(hFile);
		hFile = INVALID_HANDLE_VALUE;
	}
	else
		return false;

	// 从文件加载图像
	
	mFont.font_size = fontSize;
	mFont.name = L"STXIHEI";
	FT_Error error = FT_New_Memory_Face(_FTlibrary, (const FT_Byte*)data.data(), (FT_Long)data.size(), 0, &face);// 加载字体
	error = FT_Set_Char_Size(face, 0, 16 * 32, mFont.font_size * 10, mFont.font_size * 10);    // 设置字符大小
	if (error != 0)
		return false;
	// 读取字体
	error = FT_Load_Char(face, 97, FT_LOAD_FORCE_AUTOHINT | FT_LOAD_MONOCHROME);
	FT_Int advance = (face->glyph->advance.x >> 6) + (face->glyph->metrics.horiBearingX >> 6);
	error = FT_Render_Glyph(face->glyph, FT_RENDER_MODE_NORMAL);
	if (error != 0)
		return false;
	FT_GlyphSlot slot = face->glyph;

	mFont.CharacterNumberingSet.resize(10);
	mFont.CharacterNumberingSet[0] = { 0x000020, 0x00007f };
	mFont.CharacterNumberingSet[1] = { 0x003300, 0x003400 };
	mFont.CharacterNumberingSet[2] = { 0x004e00, 0x005500 };
	mFont.CharacterNumberingSet[3] = { 0x005501, 0x006600 };
	mFont.CharacterNumberingSet[4] = { 0x006601, 0x007700 };
	mFont.CharacterNumberingSet[5] = { 0x007701, 0x008800 };
	mFont.CharacterNumberingSet[6] = { 0x008801, 0x009900 };
	mFont.CharacterNumberingSet[7] = { 0x009901, 0x009fff };
	mFont.CharacterNumberingSet[8] = { 0x00dd01, 0x00ee00 };
	mFont.CharacterNumberingSet[9] = { 0x00ee01, 0x00ff00 };

	for (UINT i = 0; i < mFont.CharacterNumberingSet.size(); i++)
		mFont.pFontTextureData[i] = CreateFontTextureData(face, slot, mFont.CharacterNumberingSet[i], mFont.name);

	FT_Done_FreeType(_FTlibrary);

	// 确保我们切实有数据
	for (UINT i = 0; i < mFont.pFontTextureData.size(); i++)
		if (mFont.pFontTextureData[i].pHostBuffer.size() <= 0)
			return false;

	// 创建SRV堆
	D3D12_DESCRIPTOR_HEAP_DESC srvHeapDesc = {};
	srvHeapDesc.NumDescriptors = m_SwapChainBufferCount * imageCount; //贴图资源数量（大于实际数量没关系小了不行）
	srvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	srvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
	ThrowIfFailed(m_d3dDevice->CreateDescriptorHeap(&srvHeapDesc, IID_PPV_ARGS(&SrvDescriptorHeap)));

	for (UINT i = 0; i < mFont.pFontTextureData.size(); i++)
	{
		D3D12_RESOURCE_DESC fontTextureDesc;

		fontTextureDesc = CD3DX12_RESOURCE_DESC::Tex2D(
			DXGI_FORMAT_A8_UNORM, mFont.pFontTextureData[i].textureWidth, mFont.pFontTextureData[i].textureHeight, 1, 1, 1, 0,
			D3D12_RESOURCE_FLAG_NONE);

		// 创建字体纹理资源
		D3D12_HEAP_PROPERTIES HeapProperties = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
		ThrowIfFailed(m_d3dDevice->CreateCommittedResource(
			&HeapProperties,
			D3D12_HEAP_FLAG_NONE,
			&fontTextureDesc,
			D3D12_RESOURCE_STATE_COPY_DEST,
			nullptr,
			IID_PPV_ARGS(&mFont.pFontTextureData[i].textureBuffer)));

		ID3D12Resource* fontTextureBufferUploadHeap;
		UINT64 fontTextureUploadBufferSize;
		m_d3dDevice->GetCopyableFootprints(&fontTextureDesc, 0, 1, 0, nullptr, nullptr, nullptr, &fontTextureUploadBufferSize);

		// 创建一个上传堆，将纹理复制到gpu
		HeapProperties = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
		fontTextureDesc = CD3DX12_RESOURCE_DESC::Buffer(fontTextureUploadBufferSize);
		ThrowIfFailed(m_d3dDevice->CreateCommittedResource(
			&HeapProperties,
			D3D12_HEAP_FLAG_NONE,
			&fontTextureDesc,
			D3D12_RESOURCE_STATE_GENERIC_READ,
			nullptr,
			IID_PPV_ARGS(&fontTextureBufferUploadHeap)));

		// 将字体图像存储在上传堆中
		D3D12_SUBRESOURCE_DATA fontTextureData = {};
		fontTextureData.pData = &mFont.pFontTextureData[i].pHostBuffer[0]; // 指向图像数据的指针
		fontTextureData.RowPitch = mFont.pFontTextureData[i].textureWidth; // 所有三角形顶点数据的大小
		fontTextureData.SlicePitch = mFont.pFontTextureData[i].textureWidth * mFont.pFontTextureData[i].textureHeight; // 还有三角形顶点数据的大小

		// 现在将上传缓冲区内容复制到默认堆
		UpdateSubresources(m_commandList, mFont.pFontTextureData[i].textureBuffer, fontTextureBufferUploadHeap,
			0, 0, 1, &fontTextureData);

		// 将纹理默认堆转换为像素着色器资源（我们将在像素着色器中从该堆采样以获得像素的颜色）
		D3D12_RESOURCE_BARRIER Barriers = CD3DX12_RESOURCE_BARRIER::Transition(
			mFont.pFontTextureData[i].textureBuffer, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
		m_commandList->ResourceBarrier(1, &Barriers);

		// 为字体创建一个srv
		D3D12_SHADER_RESOURCE_VIEW_DESC fontsrvDesc = {};
		fontsrvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		fontsrvDesc.Format = fontTextureDesc.Format;
		fontsrvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
		fontsrvDesc.Texture2D.MipLevels = 1;
		fontsrvDesc.Texture2D.MostDetailedMip = 0;
		fontsrvDesc.Texture2D.ResourceMinLODClamp = 0.0f;

		UINT CbvSrvUavDescriptorSize = m_d3dDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
		// 我们需要获取描述符堆中的下一个描述符位置来存储此 srv
		mFont.pFontTextureData[i].GPUsrvHandle = SrvDescriptorHeap->GetGPUDescriptorHandleForHeapStart();
		mFont.pFontTextureData[i].CPUsrvHandle = SrvDescriptorHeap->GetCPUDescriptorHandleForHeapStart();
		mFont.pFontTextureData[i].GPUsrvHandle.Offset(SrvDescriptorHeapIndex, CbvSrvUavDescriptorSize);
		mFont.pFontTextureData[i].CPUsrvHandle.Offset(SrvDescriptorHeapIndex, CbvSrvUavDescriptorSize);

		m_d3dDevice->CreateShaderResourceView(mFont.pFontTextureData[i].textureBuffer, &fontsrvDesc, mFont.pFontTextureData[i].CPUsrvHandle);

		SrvDescriptorHeapIndex++;
	}

	// 完成之后清理pHostBuffer，因为不会再用到。放着占内存
	for (UINT i = 0; i < mFont.pFontTextureData.size(); i++)
		mFont.pFontTextureData[i].pHostBuffer.clear();


	// 创建文本顶点缓冲区提交的资源
	for (int i = 0; i < m_SwapChainBufferCount; ++i)
	{
		// 创建上传堆。我们将用文本数据填充它
		D3D12_HEAP_PROPERTIES HeapProperties = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
		D3D12_RESOURCE_DESC fontTextureDesc = CD3DX12_RESOURCE_DESC::Buffer(maxNumTextCharacters * sizeof(TextVertex));
		ThrowIfFailed(m_d3dDevice->CreateCommittedResource(
			&HeapProperties,
			D3D12_HEAP_FLAG_NONE,
			&fontTextureDesc,
			D3D12_RESOURCE_STATE_GENERIC_READ, // GPU将从该缓冲区读取内容并将其内容复制到默认堆
			nullptr,
			IID_PPV_ARGS(&textVertexBuffer[i])));

		CD3DX12_RANGE readRange(0, 0);	// 我们不打算读取 CPU 上的此资源。
										//（所以结束小于或等于开始）

		// 映射资源堆以获取gpu虚拟地址到堆的开头
		ThrowIfFailed(textVertexBuffer[i]->Map(0, &readRange, reinterpret_cast<void**>(&textVBGPUAddress[i])));
	}

	// 为每一帧设置文本顶点缓冲区视图
	for (int i = 0; i < m_SwapChainBufferCount; ++i)
	{
		textVertexBufferView[i].BufferLocation = textVertexBuffer[i]->GetGPUVirtualAddress();
		textVertexBufferView[i].StrideInBytes = sizeof(TextVertex);
		textVertexBufferView[i].SizeInBytes = maxNumTextCharacters * sizeof(TextVertex);
	}
	return true;
}

void TextRender::DXDrawText(ID3D12GraphicsCommandList* cmdList, std::wstring text, const DirectX::XMFLOAT2 pos, const DirectX::XMFLOAT4& color, UINT CurrBackBufferIndex)
{
	UINT count = (UINT)wcslen(text.c_str());

	// 这样，每个四边形只需要 4 个顶点，而不是使用三角形列表拓扑时需要 6 个顶点
	cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);

	// 设置文本顶点缓冲区
	cmdList->IASetVertexBuffers(0, 1, &textVertexBufferView[CurrBackBufferIndex]);

	cmdList->SetGraphicsRootSignature(RootSignature.Get());

	cmdList->SetDescriptorHeaps(1, SrvDescriptorHeap.GetAddressOf());

	//cmdList->SetGraphicsRootDescriptorTable(1, mFont.pFontTextureData[0].GPUsrvHandle);

	int numCharacters = 0;

	float topLeftScreenX = (pos.x * 1.0f) - 1.0f;
	float topLeftScreenY = ((1.0f - pos.y) * 1.0f);

	float x = topLeftScreenX;
	float y = topLeftScreenY;

	// 将 GPU 虚拟地址强制转换为文本顶点，这样我们就可以直接将顶点存储在那里
	TextVertex* vert = (TextVertex*)textVBGPUAddress[CurrBackBufferIndex];

	wchar_t lastChar = -1;
	UINT NumberingSetIndex = 0;
	bool ChangeCharacterNumberingSet = true;

	SymbolData* sData = nullptr;
	for (int i = 0; i < count; ++i)
	{
		wchar_t c = text[i];

		UINT16 id = GetUnicodeID(c);
		for (UINT j = 0; j < mFont.CharacterNumberingSet.size(); j++)
		{
			if (id < mFont.CharacterNumberingSet[j].end)
			{
				if (NumberingSetIndex != j)
				{
					NumberingSetIndex = j;
					ChangeCharacterNumberingSet = true;
				}
				break;
			}
		}

		if (ChangeCharacterNumberingSet)
		{
			// 我们将为每个字符设置 4 个顶点（用三角形条组成四边形），并且每个实例都是一个字符
			if (numCharacters > 0)
				cmdList->DrawInstanced(4, numCharacters, 0, 0);

			// 绑定文本srv。我们将假设当前绑定并设置了正确的描述符堆和表
			cmdList->SetGraphicsRootDescriptorTable(1, mFont.pFontTextureData[NumberingSetIndex].GPUsrvHandle);
			//numCharacters = 0;
			ChangeCharacterNumberingSet = false;
		}

		if (c == L'\n')
		{
		}
		else
			for (int i = 0; i < mFont.pFontTextureData[NumberingSetIndex].m_symbolsData.size(); i++)
			{
				if (id == mFont.pFontTextureData[NumberingSetIndex].m_symbolsData[i].id)
					sData = &mFont.pFontTextureData[NumberingSetIndex].m_symbolsData[i - 1];
			}

		float Zoom = 1.2f;
		float puDown = 0.0f;
		// 结束符
		if (c == L'\0')
			break;

		// 换行符
		if (c == L'\n')
		{
			x = topLeftScreenX;
			y = Zoom* (sData->rightBottom.y - sData->leftTop.y);
			continue;
		}

		// 如果字符不在字体字符集中
		if (sData == nullptr)
			continue;

		if (1 < NumberingSetIndex && NumberingSetIndex < 8)
		{
			Zoom = 4.8f;
			puDown = ((sData->rightBottom.y - sData->leftTop.y) + (sData->basePoint.y / mFont.pFontTextureData[0].textureHeight)) / 2.0f;
		}

		// 不要使缓冲区溢出。在你的应用程序中，如果这是真的，你可以实现文本顶点缓冲区的大小调整
		if (numCharacters >= maxNumTextCharacters)
			break;

		float kerning = 1.0f;
		kerning = sData->symbolSize.x;
		vert[numCharacters] = TextVertex(
			x + (sData->rightBottom.x - sData->leftTop.x) + (sData->basePoint.x / mFont.pFontTextureData[0].textureWidth),
			y - puDown - (sData->rightBottom.y - sData->leftTop.y) + (sData->basePoint.y / mFont.pFontTextureData[0].textureHeight) - ((mFont.pFontTextureData[0].symbolHeight - sData->basePoint.y) / mFont.pFontTextureData[0].textureHeight),
			Zoom * (sData->rightBottom.x - sData->leftTop.x),
			Zoom * (sData->rightBottom.y - sData->leftTop.y),
			color.x,
			color.y,
			color.z,
			color.w,
			sData->leftTop.x,
			sData->leftTop.y,
			(sData->rightBottom.x - sData->leftTop.x),
			(sData->rightBottom.y - sData->leftTop.y)
		);

		numCharacters++;

		// 前进到下一个字符位置
		x += ((kerning + (sData->rightBottom.x - sData->leftTop.x)) / mFont.pFontTextureData[0].textureWidth) + (sData->rightBottom.x - sData->leftTop.x) / Zoom;

		lastChar = c;
	}

	// 我们将为每个字符设置 4 个顶点（用三角形条组成四边形），并且每个实例都是一个字符
	cmdList->DrawInstanced(4, numCharacters, 0, 0);
}
