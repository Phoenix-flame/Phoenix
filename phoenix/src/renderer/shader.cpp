#include "Phoenix/renderer/shader.h"


namespace Phoenix{

    static GLenum ShaderTypeFromString(const std::string& type){
		if (type == "vertex")
			return GL_VERTEX_SHADER;
		if (type == "fragment" || type == "pixel")
			return GL_FRAGMENT_SHADER;

		return 0;
	}

	Shader::Shader(const std::string& filepath){
		// Extract name from filepath (first, so compile errors can name the shader)
		auto lastSlash = filepath.find_last_of("/\\");
		lastSlash = lastSlash == std::string::npos ? 0 : lastSlash + 1;
		auto lastDot = filepath.rfind('.');
		auto count = lastDot == std::string::npos ? filepath.size() - lastSlash : lastDot - lastSlash;
		_name = filepath.substr(lastSlash, count);

		std::string source = ReadFile(filepath);
		auto shaderSources = PreProcess(source);
		Compile(shaderSources);
	}

	Shader::Shader(const std::string& name, const std::string& vertexSrc, const std::string& fragmentSrc)
		: _name(name){

		std::unordered_map<GLenum, std::string> sources;
		sources[GL_VERTEX_SHADER] = vertexSrc;
		sources[GL_FRAGMENT_SHADER] = fragmentSrc;
		Compile(sources);
	}

	Shader::~Shader(){
		glDeleteProgram(_rendererID);
	}

	std::string Shader::ReadFile(const std::string& filepath){
		std::string result;
		std::ifstream in(filepath, std::ios::in | std::ios::binary); // ifstream closes itself due to RAII
		if (in){
			in.seekg(0, std::ios::end);
			std::streamoff size = in.tellg();
			if (size != -1){
				result.resize(size);
				in.seekg(0, std::ios::beg);
				in.read(&result[0], size);
			}
			else{
				PHX_CORE_ERROR("Could not read from file '{0}'", filepath);
			}
		}
		else{
			PHX_CORE_ERROR("Could not open file '{0}'", filepath);
		}

		return result;
	}

	std::unordered_map<GLenum, std::string> Shader::PreProcess(const std::string& source){
		std::unordered_map<GLenum, std::string> shaderSources;
		const char* typeToken = "#type";
		size_t typeTokenLength = strlen(typeToken);
		size_t pos = source.find(typeToken, 0); //Start of shader type declaration line
		while (pos != std::string::npos)
		{
			size_t eol = source.find_first_of("\r\n", pos); //End of shader type declaration line

			size_t begin = pos + typeTokenLength + 1; //Start of shader type name (after "#type " keyword)
			std::string type = source.substr(begin, eol - begin);

			size_t nextLinePos = source.find_first_not_of("\r\n", eol); //Start of shader code after shader type declaration line
			pos = source.find(typeToken, nextLinePos); //Start of next shader type declaration line

			shaderSources[ShaderTypeFromString(type)] = (pos == std::string::npos) ? source.substr(nextLinePos) : source.substr(nextLinePos, pos - nextLinePos);
		}
		return shaderSources;
	}

	void Shader::Compile(const std::unordered_map<GLenum, std::string>& shaderSources)
	{
		// On any failure the shader keeps _rendererID = 0 (binding it draws nothing)
		// instead of a deleted/half-built program.
		GLuint program = glCreateProgram();
		std::vector<GLuint> shaderIDs;
		bool ok = true;
		for (auto& kv : shaderSources){
			GLenum type = kv.first;
			if (type == 0){
				PHX_CORE_ERROR("Shader '{0}': unknown #type section", _name);
				ok = false;
				break;
			}
			const std::string& source = kv.second;

			GLuint shader = glCreateShader(type);

			const GLchar* sourceCStr = source.c_str();
			glShaderSource(shader, 1, &sourceCStr, 0);

			glCompileShader(shader);

			GLint isCompiled = 0;
			glGetShaderiv(shader, GL_COMPILE_STATUS, &isCompiled);
			if (isCompiled == GL_FALSE){
				GLint maxLength = 0;
				glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &maxLength);

				std::vector<GLchar> infoLog(maxLength + 1);
				glGetShaderInfoLog(shader, maxLength, &maxLength, infoLog.data());

				glDeleteShader(shader);

				PHX_CORE_ERROR("Shader '{0}' failed to compile:\n{1}", _name, infoLog.data());
				ok = false;
				break;
			}

			glAttachShader(program, shader);
			shaderIDs.push_back(shader);
		}

		if (ok){
			glLinkProgram(program);

			// Note the different functions here: glGetProgram* instead of glGetShader*.
			GLint isLinked = 0;
			glGetProgramiv(program, GL_LINK_STATUS, &isLinked);
			if (isLinked == GL_FALSE){
				GLint maxLength = 0;
				glGetProgramiv(program, GL_INFO_LOG_LENGTH, &maxLength);

				std::vector<GLchar> infoLog(maxLength + 1);
				glGetProgramInfoLog(program, maxLength, &maxLength, infoLog.data());

				PHX_CORE_ERROR("Shader '{0}' failed to link:\n{1}", _name, infoLog.data());
				ok = false;
			}
		}

		for (auto id : shaderIDs){
			glDetachShader(program, id);
			glDeleteShader(id);
		}

		if (!ok){
			glDeleteProgram(program);
			_rendererID = 0;
			return;
		}
		_rendererID = program;
	}

	void Shader::Bind() const{
		glUseProgram(_rendererID);
	}

	void Shader::Unbind() const{
		glUseProgram(0);
	}

	void Shader::SetInt(const std::string& name, int value){
		UploadUniformInt(name, value);
	}

	void Shader::SetIntArray(const std::string& name, int* values, uint32_t count)
	{
		UploadUniformIntArray(name, values, count);
	}

	void Shader::SetFloat(const std::string& name, float value){
		UploadUniformFloat(name, value);
	}

	void Shader::SetFloat2(const std::string& name, const glm::vec2& value){
		UploadUniformFloat2(name, value);
	}

	void Shader::SetFloat3(const std::string& name, const glm::vec3& value){
		UploadUniformFloat3(name, value);
	}

	void Shader::SetFloat4(const std::string& name, const glm::vec4& value){
		UploadUniformFloat4(name, value);
	}

	void Shader::SetMat4(const std::string& name, const glm::mat4& value){
		UploadUniformMat4(name, value);
	}

	void Shader::SetFloat4Array(const std::string& name, const glm::vec4* values, uint32_t count){
		GLint location = GetUniformLocation(name);
		glUniform4fv(location, (GLsizei)count, glm::value_ptr(values[0]));
	}

	void Shader::SetMat4Array(const std::string& name, const glm::mat4* values, uint32_t count){
		GLint location = GetUniformLocation(name);
		glUniformMatrix4fv(location, (GLsizei)count, GL_FALSE, glm::value_ptr(values[0]));
	}

	GLint Shader::GetUniformLocation(const std::string& name){
		auto it = _uniformLocationCache.find(name);
		if (it != _uniformLocationCache.end())
			return it->second;
		GLint location = glGetUniformLocation(_rendererID, name.c_str());
		_uniformLocationCache.emplace(name, location);
		return location;
	}

	void Shader::UploadUniformInt(const std::string& name, int value){
		GLint location = GetUniformLocation(name);
		glUniform1i(location, value);
	}

	void Shader::UploadUniformIntArray(const std::string& name, int* values, uint32_t count){
		GLint location = GetUniformLocation(name);
		glUniform1iv(location, count, values);
	}

	void Shader::UploadUniformFloat(const std::string& name, float value){
		GLint location = GetUniformLocation(name);
		glUniform1f(location, value);
	}

	void Shader::UploadUniformFloat2(const std::string& name, const glm::vec2& value){
		GLint location = GetUniformLocation(name);
		glUniform2f(location, value.x, value.y);
	}

	void Shader::UploadUniformFloat3(const std::string& name, const glm::vec3& value){
		GLint location = GetUniformLocation(name);
		glUniform3f(location, value.x, value.y, value.z);
	}

	void Shader::UploadUniformFloat4(const std::string& name, const glm::vec4& value){
		GLint location = GetUniformLocation(name);
		glUniform4f(location, value.x, value.y, value.z, value.w);
	}

	void Shader::UploadUniformMat3(const std::string& name, const glm::mat3& matrix){
		GLint location = GetUniformLocation(name);
		glUniformMatrix3fv(location, 1, GL_FALSE, glm::value_ptr(matrix));
	}

	void Shader::UploadUniformMat4(const std::string& name, const glm::mat4& matrix){
		GLint location = GetUniformLocation(name);
		glUniformMatrix4fv(location, 1, GL_FALSE, glm::value_ptr(matrix));
	}





    Ref<Shader> Shader::Create(const std::string& filepath){
        return CreateRef<Shader>(filepath);
	}

	Ref<Shader> Shader::Create(const std::string& name, const std::string& vertexSrc, const std::string& fragmentSrc){
		return CreateRef<Shader>(name, vertexSrc, fragmentSrc);
	}

	void ShaderLibrary::Add(const std::string& name, const Ref<Shader>& shader){
		PHX_CORE_ASSERT(!Exists(name), "Shader already exists!");
		m_Shaders[name] = shader;
		PHX_CORE_TRACE("{0} shader added to library.", name);
	}

	void ShaderLibrary::Add(const Ref<Shader>& shader){
		auto& name = shader->GetName();
		Add(name, shader);
	}

	Ref<Shader> ShaderLibrary::Load(const std::string& filepath){
		auto shader = Shader::Create(filepath);
		Add(shader);
		return shader;
	}

	Ref<Shader> ShaderLibrary::Load(const std::string& name, const std::string& filepath)
	{
		auto shader = Shader::Create(filepath);
		Add(name, shader);
		return shader;
	}

	Ref<Shader> ShaderLibrary::Get(const std::string& name)
	{
		PHX_CORE_ASSERT(Exists(name), "Shader not found!");
		return m_Shaders[name];
	}

	bool ShaderLibrary::Exists(const std::string& name) const
	{
		return m_Shaders.find(name) != m_Shaders.end();
	}
}