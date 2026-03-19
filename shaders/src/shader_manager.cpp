#include "../include/shader_manager.h"
ShaderManager::ShaderManager()  {}
ShaderManager::~ShaderManager() {}
void ShaderManager::scanShaderPacks(const std::string&) {}
bool ShaderManager::applyPreset(const std::string&) { return false; }
bool ShaderManager::applyBuiltin(BuiltinShader s) { m_activeBuiltin = s; return true; }
void ShaderManager::clearShader() { m_activeBuiltin = BuiltinShader::NONE; m_activePreset = nullptr; }
void ShaderManager::addSearchPath(const std::string& p) { m_searchPaths.push_back(p); }
