/* openblack - A reimplementation of Lionhead's Black & White.
 *
 * openblack is the legal property of its developers, whose names
 * can be found in the AUTHORS.md file distributed with this source
 * distribution.
 *
 * openblack is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 3
 * of the License, or (at your option) any later version.
 *
 * openblack is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with openblack. If not, see <http://www.gnu.org/licenses/>.
 */

#include "LandIsland.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "Common/FileSystem.h"
#include "Common/IStream.h"
#include "Common/stb_image_write.h"
#include "Game.h"

#include <spdlog/spdlog.h>

#include <stdexcept>

using namespace openblack;
using namespace openblack::graphics;

const float LandIsland::HeightUnit = 0.67f;
const float LandIsland::CellSize = 10.0f;

LandIsland::LandIsland():  _blockIndexLookup {0}
{
	auto file = Game::instance()->GetFileSystem().Open("Data/Textures/smallbumpa.raw", FileMode::Read);
	auto* smallbumpa = new uint8_t[file->Size()];
	file->Read(smallbumpa, file->Size());

	_textureSmallBump = std::make_unique<Texture2D>("LandIslandSmallBump");
	_textureSmallBump->Create(256, 256, 1, Format::R8, Wrapping::Repeat, smallbumpa, file->Size());
	delete[] smallbumpa;
}

LandIsland::~LandIsland() = default;

/*
Loads from the original Black & White .lnd file format and
converts the data into our own
*/
void LandIsland::LoadFromFile(IStream& stream)
{
	uint32_t blockCount, countryCount, blockSize, matSize, countrySize;

	stream.Read<uint32_t>(&blockCount);
	stream.Read(_blockIndexLookup.data(), 1024);
	stream.Read<uint32_t>(&_materialCount);
	stream.Read<uint32_t>(&countryCount);

	// todo: lets assert these against sizeof(LandBlock) etc..
	stream.Read<uint32_t>(&blockSize);
	stream.Read<uint32_t>(&matSize);
	stream.Read<uint32_t>(&countrySize);
	stream.Read<uint32_t>(&_lowresCount);

	// skip over low resolution textures
	// for future reference these are formatted GL_COMPRESSED_RGBA_S3TC_DXT3_EXT
	for (unsigned int i = 0; i < _lowresCount; i++)
	{
		stream.Seek(16, SeekMode::Current);
		uint32_t textureSize = stream.ReadValue<uint32_t>();
		stream.Seek(textureSize - 4, SeekMode::Current);
	}

	blockCount--; // take away a block from the count, because it's not in the
	              // file?

	spdlog::debug("[LandIsland] loading {} blocks", blockCount);
	_landBlocks.reserve(blockCount);
	for (uint32_t i = 0; i < blockCount; i++)
	{
		auto* blockData = new uint8_t[blockSize];
		stream.Read(blockData, blockSize);

		_landBlocks.push_back(LandBlock());
		_landBlocks[i].Load(blockData, blockSize);

		delete[] blockData;
	}

	spdlog::debug("[LandIsland] loading {} countries", countryCount);
	_countries.reserve(countryCount);
	for (uint32_t i = 0; i < countryCount; i++)
	{
		Country country;
		stream.Read(&country);
		_countries.push_back(country);
	}

	spdlog::debug("[LandIsland] loading {} textures", _materialCount);
	std::vector<uint16_t> rgba5TextureData;
	rgba5TextureData.resize(256 * 256 * _materialCount);
	stream.Read(rgba5TextureData.data(), rgba5TextureData.size() * sizeof(rgba5TextureData[0]));
	_materialArray = std::make_unique<Texture2D>("LandIslandMaterialArray");
	_materialArray->Create(256, 256, _materialCount, Format::RGB5A1, Wrapping::ClampEdge, rgba5TextureData.data(),
	                       rgba5TextureData.size() * sizeof(rgba5TextureData[0]));

	// read noise map into Texture2D
	stream.Read(_noiseMap.data(), _noiseMap.size() * sizeof(_noiseMap[0]));
	_textureNoiseMap = std::make_unique<Texture2D>("LandIslandNoiseMap");
	_textureNoiseMap->Create(256, 256, 1, Format::R8, Wrapping::ClampEdge, _noiseMap.data(),
	                         _noiseMap.size() * sizeof(_noiseMap[0]));

	// read bump map into Texture2D
	std::array<uint8_t, 256 * 256> bumpMapTextureData;
	stream.Read(bumpMapTextureData.data(), bumpMapTextureData.size() * sizeof(bumpMapTextureData[0]));
	_textureBumpMap = std::make_unique<Texture2D>("LandIslandBumpMap");
	_textureBumpMap->Create(256, 256, 1, Format::R8, Wrapping::ClampEdge, bumpMapTextureData.data(),
	                        bumpMapTextureData.size() * sizeof(bumpMapTextureData[0]));

	// build the meshes (we could move this elsewhere)
	for (auto& block : _landBlocks) block.BuildMesh(*this);
	bgfx::frame();
}

void openblack::LandIsland::Update(float timeOfDay, float bumpMapStrength, float smallBumpMapStrength)
{
	_timeOfDay = timeOfDay;
	_bumpMapStrength = bumpMapStrength;
	_smallBumpMapStrength = smallBumpMapStrength;
}

/*const uint8_t LandIsland::GetAltitudeAt(glm::ivec2 vec) const
{
    return uint8_t();
}
*/

float LandIsland::GetHeightAt(glm::vec2 vec) const
{
	return GetCell(vec * 0.1f).Altitude() * LandIsland::HeightUnit;
}

uint8_t LandIsland::GetNoise(int x, int y)
{
	return _noiseMap[(y & 0xFF) + 256 * (x & 0xFF)];
}

const LandBlock* LandIsland::GetBlock(const glm::u8vec2& coordinates) const
{
	// our blocks can only be between [0-31, 0-31]
	if (coordinates.x > 32 || coordinates.y > 32)
		return nullptr;

	const uint8_t blockIndex = _blockIndexLookup[coordinates.x * 32 + coordinates.y];
	if (blockIndex == 0)
		return nullptr;

	return &_landBlocks[blockIndex - 1];
}

const LandCell EmptyCell;

const LandCell& LandIsland::GetCell(const glm::u16vec2& coordinates) const
{
	if (coordinates.x > 511 || coordinates.y > 511)
	{
		return EmptyCell;
	}

	const uint16_t lookupIndex = ((coordinates.x & ~0xFU) << 1U) | (coordinates.y >> 4U);
	const uint16_t cellIndex = (coordinates.x & 0xFU) * 0x11u + (coordinates.y & 0xFU);

	const uint8_t blockIndex = _blockIndexLookup[lookupIndex];

	if (blockIndex == 0)
	{
		return EmptyCell;
	}

	return _landBlocks[blockIndex - 1].GetCells()[cellIndex];
}

void LandIsland::Draw(graphics::RenderPass viewId, const ShaderProgram& program, bool cullBack) const
{
	for (auto& block : _landBlocks)
	{
		program.SetTextureSampler("s_materials", 0, *_materialArray);
		program.SetTextureSampler("s_bump", 1, *_textureBumpMap);
		program.SetTextureSampler("s_smallBump", 2, *_textureSmallBump);
		program.SetUniformValue("u_timeOfDay", &_timeOfDay);
		program.SetUniformValue("u_bumpmapStrength", &_bumpMapStrength);
		program.SetUniformValue("u_smallBumpmapStrength", &_smallBumpMapStrength);
		block.Draw(viewId, program, cullBack);
	}
}

void LandIsland::DumpTextures()
{
	_materialArray->DumpTexture();
}

void LandIsland::DumpMaps()
{
	const int cellsize = 16;

	// 32x32 block grid with 16x16 cells
	// 512 x 512 pixels
	// lets go with 3 channels for a laugh
	auto* data = new uint8_t[32 * 32 * cellsize * cellsize];

	memset(data, 0x00, 32 * 32 * cellsize * cellsize);

	for (unsigned int b = 0; b < _landBlocks.size(); b++)
	{
		LandBlock& block = _landBlocks[b];

		int mapx = block.GetBlockPosition().x;
		int mapz = block.GetBlockPosition().y;
		int lineStride = 32 * cellsize;

		for (int x = 0; x < cellsize; x++)
		{
			for (int y = 0; y < cellsize; y++)
			{
				LandCell cell = block.GetCells()[y * 17 + x];

				int cellX = (mapx * cellsize) + x;
				int cellY = (mapz * cellsize) + y;

				data[(cellY * lineStride) + cellX] = cell.Altitude();
			}
		}
	}

	FILE* fptr = fopen("dump.raw", "wb");
	fwrite(data, 32 * 32 * cellsize * cellsize, 1, fptr);
	fclose(fptr);

	delete[] data;
}
