#include "shader_manager.h"
#include <filesystem>
// Shader manager implementation - Phase 3
ShaderManager::ShaderManager()  {}
ShaderManager::~ShaderManager() {}
void ShaderManager::scanShaderPacks(const std::string&) {}
bool ShaderManager::applyPreset(const std::string&) { return false; }
bool ShaderManager::applyBuiltin(BuiltinShader s) { m_activeBuiltin = s; return true; }
void ShaderManager::clearShader() { m_activeBuiltin = BuiltinShader::NONE; m_activePreset = nullptr; }
void ShaderManager::addSearchPath(const std::string& p) { m_searchPaths.push_back(p); }
