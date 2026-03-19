#include "../include/audio_replacer.h"
AudioReplacer::AudioReplacer()  {}
AudioReplacer::~AudioReplacer() {}
bool AudioReplacer::loadPackForGame(const std::string&) { return false; }
void AudioReplacer::unloadPack() {}
bool AudioReplacer::processAudioFrame(int16_t*, int) { return false; }
void AudioReplacer::addSearchPath(const std::string& p) { m_searchPaths.push_back(p); }
