#include <Phoenix/renderer/Texture.h>
#include <Phoenix/core/assert.h>
#define STB_IMAGE_IMPLEMENTATION
#include <Phoenix/Utils/stb_image.h>

#include <unordered_map>
#include <string>


namespace Phoenix{
    Ref<Texture2D> Texture2D::Create(uint32_t width, uint32_t height){
        return CreateRef<Texture2D>(width, height);
	}

	// Cache loaded textures by path so a model whose many sub-meshes share one
	// material (e.g. the backpack: 79 meshes, one 4K diffuse map) decodes/uploads
	// each unique image only once.
	Ref<Texture2D> Texture2D::Create(const std::string& path){
		static std::unordered_map<std::string, Ref<Texture2D>> s_Cache;
		auto it = s_Cache.find(path);
		if (it != s_Cache.end())
			return it->second;

		// A failed load is cached as null too (callers treat null as "no texture"), so a
		// bad path is reported once instead of being re-read from disk every draw.
		Ref<Texture2D> texture = CreateRef<Texture2D>(path);
		if (!texture->IsLoaded())
			texture = nullptr;
		s_Cache[path] = texture;
		return texture;
	}


    Texture2D::Texture2D(uint32_t width, uint32_t height)
		: m_Width(width), m_Height(height){
		m_InternalFormat = GL_RGBA8;
		m_DataFormat = GL_RGBA;

		glCreateTextures(GL_TEXTURE_2D, 1, &m_RendererID);
		glTextureStorage2D(m_RendererID, 1, m_InternalFormat, m_Width, m_Height);

		glTextureParameteri(m_RendererID, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTextureParameteri(m_RendererID, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

		glTextureParameteri(m_RendererID, GL_TEXTURE_WRAP_S, GL_REPEAT);
		glTextureParameteri(m_RendererID, GL_TEXTURE_WRAP_T, GL_REPEAT);
	}

	Texture2D::Texture2D(const std::string& path)
		: m_Path(path){
		int width = 0, height = 0, channels = 0;
		stbi_set_flip_vertically_on_load(1);
		stbi_uc* data = stbi_load(path.c_str(), &width, &height, &channels, 0);
		if (data && channels != 3 && channels != 4){
			// Grey / grey+alpha images: expand to RGBA rather than uploading with no format.
			stbi_image_free(data);
			data = stbi_load(path.c_str(), &width, &height, &channels, 4);
			channels = 4;
		}
		if (!data){
			PHX_CORE_ERROR("Failed to load texture '{0}': {1}", path, stbi_failure_reason());
			return;
		}
		m_Width = width;
		m_Height = height;

		GLenum internalFormat = (channels == 4) ? GL_RGBA8 : GL_RGB8;
		GLenum dataFormat = (channels == 4) ? GL_RGBA : GL_RGB;
		m_InternalFormat = internalFormat;
		m_DataFormat = dataFormat;

		glCreateTextures(GL_TEXTURE_2D, 1, &m_RendererID);
		glTextureStorage2D(m_RendererID, 1, internalFormat, m_Width, m_Height);

		glTextureParameteri(m_RendererID, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTextureParameteri(m_RendererID, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

		glTextureParameteri(m_RendererID, GL_TEXTURE_WRAP_S, GL_REPEAT);
		glTextureParameteri(m_RendererID, GL_TEXTURE_WRAP_T, GL_REPEAT);

		// stbi rows are tightly packed; GL's default 4-byte row alignment would skew RGB
		// images whose width * 3 isn't a multiple of 4 (and read past the buffer).
		glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
		glTextureSubImage2D(m_RendererID, 0, 0, 0, m_Width, m_Height, dataFormat, GL_UNSIGNED_BYTE, data);
		glPixelStorei(GL_UNPACK_ALIGNMENT, 4);

		stbi_image_free(data);
	}

	Texture2D::~Texture2D(){
		glDeleteTextures(1, &m_RendererID);
	}

	void Texture2D::SetData(void* data, uint32_t size){
		uint32_t bpp = m_DataFormat == GL_RGBA ? 4 : 3;
		PHX_CORE_ASSERT(size == m_Width * m_Height * bpp, "Data must be entire texture!");
		glTextureSubImage2D(m_RendererID, 0, 0, 0, m_Width, m_Height, m_DataFormat, GL_UNSIGNED_BYTE, data);
	}

	void Texture2D::Bind(uint32_t slot) const{
		glBindTextureUnit(slot, m_RendererID);
	}
}