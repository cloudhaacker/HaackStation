#include "texture_replacer.h"
// Texture replacement implementation - Phase 3
TextureReplacer::TextureReplacer()  {}
TextureReplacer::~TextureReplacer() {}
bool TextureReplacer::loadPackForGame(const std::string&) { return false; }
void TextureReplacer::unloadPack() {}
const ReplacementTexture* TextureReplacer::getReplacementTexture(uint64_t) { return nullptr; }
void TextureReplacer::notifyTextureUsed(uint64_t) {}
void TextureReplacer::addSearchPath(const std::string& p) { m_searchPaths.push_back(p); }
