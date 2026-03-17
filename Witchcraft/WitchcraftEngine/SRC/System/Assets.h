#pragma once

#include <xstring>
#include <vector>

#include "D3DWindow/D3DWindow.h"

struct FILEs
{
	enum File_Type : UINT
	{
		TXTFILE = 0,
		PNGFILE,
		DDSFILE,
		MATFILE,
		WAVFILE,
		OBJFILE,
		GLTFFILE,
		GLBFILE,
		WMODELFILE,
		TTFFILE,
		SKYFILE,
		LUAFILE,
		Count,
	};

	//// FILE/DIR ////
	std::wstring file_name;
	std::wstring file_name_only;
	UINT file_type;
	std::wstring file_path;
	bool is_dir = false;
	bool is_selected = false;
	int file_size = 0;
	//// IMG INFO ////
	Texture texture;

	FILEs(std::wstring file_path, std::wstring file_name, std::wstring file_name_only, UINT file_type, bool is_dir, int file_size)
	{
		this->file_path = file_path;
		this->file_name = file_name;
		this->file_name_only = file_name_only;
		this->file_type = file_type;
		this->is_dir = is_dir;
		this->file_size = file_size;
	}

	// 根据扩展名推导资源窗口中的文件类型。
	static File_Type extensionToFileType(std::wstring extension)
	{
		File_Type type = Count;
		if (wcscmp(extension.c_str(), L".txt") == 0 || wcscmp(extension.c_str(), L".TXT") == 0)
			type = File_Type::TXTFILE;
		else if (wcscmp(extension.c_str(), L".png") == 0 || wcscmp(extension.c_str(), L".PNG") == 0)
			type = File_Type::PNGFILE;
		else if (wcscmp(extension.c_str(), L".dds") == 0 || wcscmp(extension.c_str(), L".DDS") == 0)
			type = File_Type::DDSFILE;
		else if (wcscmp(extension.c_str(), L".mat") == 0 || wcscmp(extension.c_str(), L".MAT") == 0 ||
			wcscmp(extension.c_str(), L".wmat") == 0 || wcscmp(extension.c_str(), L".WMAT") == 0)
			type = File_Type::MATFILE;
		else if (wcscmp(extension.c_str(), L".wav") == 0 || wcscmp(extension.c_str(), L".WAV") == 0 ||
			wcscmp(extension.c_str(), L".wave") == 0 || wcscmp(extension.c_str(), L".WAVE") == 0)
			type = File_Type::WAVFILE;
		else if (wcscmp(extension.c_str(), L".obj") == 0 || wcscmp(extension.c_str(), L".OBJ") == 0)
			type = File_Type::OBJFILE;
		else if (wcscmp(extension.c_str(), L".gltf") == 0 || wcscmp(extension.c_str(), L".GLTF") == 0)
			type = File_Type::GLTFFILE;
		else if (wcscmp(extension.c_str(), L".glb") == 0 || wcscmp(extension.c_str(), L".GLB") == 0)
			type = File_Type::GLBFILE;
		else if (wcscmp(extension.c_str(), L".wmodel") == 0 || wcscmp(extension.c_str(), L".WMODEL") == 0)
			type = File_Type::WMODELFILE;
		else if (wcscmp(extension.c_str(), L".ttf") == 0 || wcscmp(extension.c_str(), L".TTF") == 0)
			type = File_Type::TTFFILE;
		else if (wcscmp(extension.c_str(), L".sky") == 0 || wcscmp(extension.c_str(), L".SKY") == 0)
			type = File_Type::SKYFILE;
		else if (wcscmp(extension.c_str(), L".lua") == 0 || wcscmp(extension.c_str(), L".LUA") == 0)
			type = File_Type::LUAFILE;

		return type;
	}

	// 将文件类型反查为默认扩展名。
	static std::wstring fileTypeToExtension(File_Type type)
	{
		std::wstring extension = L"";
		if (type == File_Type::TXTFILE)
			extension = L".txt";
		else if (type == File_Type::PNGFILE)
			extension = L".png";
		else if (type == File_Type::DDSFILE)
			extension = L".dds";
		else if (type == File_Type::MATFILE)
			extension = L".wmat";
		else if (type == File_Type::WAVFILE)
			extension = L".wav";
		else if (type == File_Type::OBJFILE)
			extension = L".obj";
		else if (type == File_Type::GLTFFILE)
			extension = L".gltf";
		else if (type == File_Type::GLBFILE)
			extension = L".glb";
		else if (type == File_Type::WMODELFILE)
			extension = L".wmodel";
		else if (type == File_Type::TTFFILE)
			extension = L".ttf";
		else if (type == File_Type::SKYFILE)
			extension = L".sky";
		else if (type == File_Type::LUAFILE)
			extension = L".lua";

		return extension;
	}
};

struct AssetDragPayload
{
	// 资源窗口拖放时传递给其他窗口的轻量数据。
	UINT file_type = FILEs::File_Type::Count;
	bool is_dir = false;
	wchar_t full_path[1024] = {};
	wchar_t file_name_only[260] = {};
};

struct dir_list
{
	std::wstring dir_name;
	std::vector<dir_list> dir_child;
	dir_list()
	{
		/* code */
	}
};
