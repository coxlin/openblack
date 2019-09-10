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

#include "Mesh.h"

#include <Game.h>
#include <stdexcept>
#include <stdint.h>

using namespace openblack::graphics;

Mesh::Mesh(VertexBuffer* vertexBuffer, const VertexDecl& decl, GLuint type):
    _vertexBuffer(std::move(vertexBuffer)),
    _vertexDecl(decl),
    _type(type)
{
	glGenVertexArrays(1, &_vao);
	glBindVertexArray(_vao);

	_vertexBuffer->Bind();
	bindVertexDecl();

	glBindVertexArray(0);
}

Mesh::Mesh(VertexBuffer* vertexBuffer, IndexBuffer* indexBuffer, const VertexDecl& decl, GLuint type):
    _vertexBuffer(std::move(vertexBuffer)),
    _indexBuffer(std::move(indexBuffer)),
    _vertexDecl(decl),
    _type(type)
{
	glGenVertexArrays(1, &_vao);
	glBindVertexArray(_vao);

	_vertexBuffer->Bind();
	bindVertexDecl();
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, _indexBuffer->GetIBO());

	glBindVertexArray(0);
}

Mesh::~Mesh()
{
	if (_vao != 0)
		glDeleteVertexArrays(1, &_vao);
}

std::shared_ptr<VertexBuffer> Mesh::GetVertexBuffer()
{
	return _vertexBuffer;
}

std::shared_ptr<IndexBuffer> Mesh::GetIndexBuffer()
{
	return _indexBuffer;
}

GLuint Mesh::GetType() const noexcept
{
	return _type;
}

void Mesh::Draw()
{
	if (_indexBuffer != nullptr && _indexBuffer->GetCount() > 0)
	{
		Draw(_indexBuffer->GetCount(), 0);
	}
	else
	{
		Draw(_vertexBuffer->GetVertexCount(), 0);
	}
}

void Mesh::Draw(uint32_t count, uint32_t startIndex)
{
	glBindVertexArray(_vao);
	if (_indexBuffer != nullptr && _indexBuffer->GetCount() > 0)
	{
		auto indexBufferOffset = startIndex * _indexBuffer->GetStride();
		glDrawElements(_type, count, _indexBuffer->GetType(), reinterpret_cast<void*>(indexBufferOffset));
	}
	else
	{
		glDrawArrays(_type, 0, count);
	}
}

void Mesh::bindVertexDecl()
{
	for (auto& d : _vertexDecl)
	{
		if (d._asInt)
		{
			glVertexAttribIPointer(d._index,
			                       d._size,
			                       d._type,
			                       d._stride,
			                       reinterpret_cast<const void*>(d._offset));
		}
		else
		{
			glVertexAttribPointer(d._index,
			                      d._size,
			                      d._type,
			                      d._normalized ? GL_TRUE : GL_FALSE,
			                      d._stride,
			                      reinterpret_cast<const void*>(d._offset));
		}

		glEnableVertexAttribArray(d._index);
	}
}
