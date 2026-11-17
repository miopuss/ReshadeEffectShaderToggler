///////////////////////////////////////////////////////////////////////
//
// Part of ShaderToggler, a shader toggler add on for ReShade 5+ which allows you
// to define groups of shaders to toggle them on/off with one key press
//
// (c) Frans 'Otis_Inf' Bouma.
//
// All rights reserved.
// https://github.com/FransBouma/ShaderToggler
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met :
//
//  * Redistributions of source code must retain the above copyright notice, this
//	  list of conditions and the following disclaimer.
//
//  * Redistributions in binary form must reproduce the above copyright notice,
//    this list of conditions and the following disclaimer in the documentation
//    and / or other materials provided with the distribution.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
// DISCLAIMED.IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
// FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
// DAMAGES(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
// SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
// CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
// OR TORT(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
/////////////////////////////////////////////////////////////////////////

#include "ToggleGroup.h"
#include "stdafx.h"
#include <algorithm>
#include <charconv>
#include <sstream>
#include <vector>

using namespace std;

namespace ShaderToggler {
ToggleGroup::ToggleGroup(string name, int id) {
    _name = name.size() > 0 ? name : "Default";
    _id = id;
    _keybind = 0;
    _isActive = false;
    _isEditing = false;
    _allowAllTechniques = true;
    _isProvidingTextureBinding = false;
    _textureBindingName = "";
    _hasTechniqueExceptions = false;
    _extractConstants = false;
    _extractResourceViews = false;
    _matchSwapchainResolution = true;
    _copyTextureBinding = false;
    _clearBindings = false;
    _requeueAfterRTMatchingFailure = false;
    _previewClearAlpha = true;
    _tonemapHDRtoSDRtoHDR = false;
    _preserveAlpha = false;
    _renderToResourceViews = false;
    _autoRenderSRV = false;
    _hideMarkedShaders = false;
    _cbCycle = CYCLE_NONE;
    _srvCycle = CYCLE_NONE;
    _rtCycle = CYCLE_NONE;

    _group_buffers[static_cast<uint32_t>(GroupResourceType::RESOURCE_ALPHA)] = {
        {}, {}, {}, {}, {}, {}, {}, [&]() { return _preserveAlpha && !_autoRenderSRV; }, [&]() { return false; }, GroupResourceState::RESOURCE_INVALID, true
    };
    _group_buffers[static_cast<uint32_t>(GroupResourceType::RESOURCE_BINDING)] = { {},
                                                                                   {},
                                                                                   {},
                                                                                   {},
                                                                                   {},
                                                                                   {},
                                                                                   {},
                                                                                   [&]() { return _copyTextureBinding && _isProvidingTextureBinding; },
                                                                                   [&]() { return _clearBindings; },
                                                                                   GroupResourceState::RESOURCE_INVALID,
                                                                                   true };
    _group_buffers[static_cast<uint32_t>(GroupResourceType::RESOURCE_CONSTANTS_COPY)] = {
        {}, {}, {}, {}, {}, {}, {}, [&]() { return _extractConstants; }, [&]() { return false; }, GroupResourceState::RESOURCE_INVALID, true
    };
    _group_buffers[static_cast<uint32_t>(GroupResourceType::RESOURCE_NATIVE_STAGING)] = {
        {},
        {},
        {},
        {},
        {},
        {},
        {},
        [this]() {
            const GroupResource& staging = _group_buffers[static_cast<uint32_t>(GroupResourceType::RESOURCE_NATIVE_STAGING)];
            return _autoRenderSRV && staging.target_description.texture.width > 0 && staging.target_description.texture.height > 0;
        },
        [this]() { return false; },
        GroupResourceState::RESOURCE_INVALID,
        true
    };
}

ToggleGroup::ToggleGroup()
  : ToggleGroup("Default", 0) {}

ToggleGroup::ToggleGroup(const ToggleGroup& other)
  : ToggleGroup() {
    _id = other._id;
    _name = other._name;
    _keybind = other._keybind;
    _vertexShaderHashes = other._vertexShaderHashes;
    _pixelShaderHashes = other._pixelShaderHashes;
    _computeShaderHashes = other._computeShaderHashes;
    _invocationLocation = other._invocationLocation;
    _rtIndex = other._rtIndex;
    _cbSlotIndex = other._cbSlotIndex;
    _cbDescIndex = other._cbDescIndex;
    _cbShaderStage = other._cbShaderStage;
    _bindingInvocationLocation = other._bindingInvocationLocation;
    _bindingRTIndex = other._bindingRTIndex;
    _bindingSrvSlotIndex = other._bindingSrvSlotIndex;
    _bindingSrvDescIndex = other._bindingSrvDescIndex;
    _bindingSrvShaderStage = other._bindingSrvShaderStage;
    _isActive = other._isActive;
    _isEditing = other._isEditing;
    _allowAllTechniques = other._allowAllTechniques;
    _isProvidingTextureBinding = other._isProvidingTextureBinding;
    _copyTextureBinding = other._copyTextureBinding;
    _extractConstants = other._extractConstants;
    _extractResourceViews = other._extractResourceViews;
    _clearBindings = other._clearBindings;
    _previewClearAlpha = other._previewClearAlpha;
    _hasTechniqueExceptions = other._hasTechniqueExceptions;
    _tonemapHDRtoSDRtoHDR = other._tonemapHDRtoSDRtoHDR;
    _preserveAlpha = other._preserveAlpha;
    _flipBuffer = other._flipBuffer;
    _flipBufferBinding = other._flipBufferBinding;
    _matchSwapchainResolution = other._matchSwapchainResolution;
    _bindingMatchSwapchainResolution = other._bindingMatchSwapchainResolution;
    _requeueAfterRTMatchingFailure = other._requeueAfterRTMatchingFailure;
    _cbModePush = other._cbModePush;
    _textureBindingName = other._textureBindingName;
    _preferredTechniques = other._preferredTechniques;
    _preferredTechniqueData = other._preferredTechniqueData;
    _varOffsetMapping = other._varOffsetMapping;
    _cbCycle = other._cbCycle;
    _srvCycle = other._srvCycle;
    _rtCycle = other._rtCycle;
    _renderToResourceViews = other._renderToResourceViews;
    _autoRenderSRV = other._autoRenderSRV;
    _hideMarkedShaders = other._hideMarkedShaders;
    _shaderDrawSettings = other._shaderDrawSettings;
    RebuildShaderDrawSnapshot();
    _renderSrvDescIndex = other._renderSrvDescIndex;
    _renderSrvShaderStage = other._renderSrvShaderStage;
    _renderSrvSlotIndex = other._renderSrvSlotIndex;
    _configDirtyFlag = other._configDirtyFlag;
}


ToggleGroup ToggleGroup::cloneForNewId(int newId) const {
    ToggleGroup clone(*this);
    clone._id = newId;
    clone._name = _name + " Copy";
    clone._keybind = 0;
    clone._isActive = false;
    clone._isEditing = false;
    clone._cbCycle = CYCLE_NONE;
    clone._srvCycle = CYCLE_NONE;
    clone._rtCycle = CYCLE_NONE;
    return clone;
}

std::string ToggleGroup::configurationSignature() const {
    std::ostringstream ss;

    auto appendUIntSet = [&ss](const std::unordered_set<uint32_t>& values) {
        std::vector<uint32_t> sorted(values.begin(), values.end());
        std::sort(sorted.begin(), sorted.end());
        for (const uint32_t value : sorted)
            ss << value << ',';
        ss << ';';
    };

    auto appendStringSet = [&ss](const std::unordered_set<std::string>& values) {
        std::vector<std::string> sorted(values.begin(), values.end());
        std::sort(sorted.begin(), sorted.end());
        for (const auto& value : sorted)
            ss << value.size() << ':' << value << ',';
        ss << ';';
    };

    ss << _name.size() << ':' << _name << ';' << _keybind << ';' << _isActive << ';';
    appendUIntSet(_vertexShaderHashes);
    appendUIntSet(_pixelShaderHashes);
    appendUIntSet(_computeShaderHashes);

    ss << _invocationLocation << ';' << _rtIndex << ';'
       << _cbSlotIndex << ';' << _cbDescIndex << ';' << _cbShaderStage << ';'
       << _bindingInvocationLocation << ';' << _bindingRTIndex << ';'
       << _bindingSrvSlotIndex << ';' << _renderSrvSlotIndex << ';'
       << _bindingSrvDescIndex << ';' << _renderSrvDescIndex << ';'
       << _bindingSrvShaderStage << ';' << _renderSrvShaderStage << ';'
       << _allowAllTechniques << ';' << _isProvidingTextureBinding << ';'
       << _copyTextureBinding << ';' << _renderToResourceViews << ';'
       << _autoRenderSRV << ';' << _hideMarkedShaders << ';' << _extractConstants << ';'
       << _extractResourceViews << ';' << _clearBindings << ';'
       << _previewClearAlpha << ';' << _hasTechniqueExceptions << ';'
       << _tonemapHDRtoSDRtoHDR << ';' << _preserveAlpha << ';'
       << _flipBuffer << ';' << _flipBufferBinding << ';'
       << _matchSwapchainResolution << ';' << _bindingMatchSwapchainResolution << ';'
       << _requeueAfterRTMatchingFailure << ';' << _cbModePush << ';'
       << _textureBindingName.size() << ':' << _textureBindingName << ';';

    std::vector<uint64_t> drawSettingKeys;
    drawSettingKeys.reserve(_shaderDrawSettings.size());
    for (const auto& [key, _] : _shaderDrawSettings)
        drawSettingKeys.push_back(key);
    std::sort(drawSettingKeys.begin(), drawSettingKeys.end());
    for (const uint64_t key : drawSettingKeys) {
        const ShaderDrawSettings& settings = _shaderDrawSettings.at(key);
        ss << key << ':'
           << settings.suppressDrawCall << ',' << settings.matchIndexCount << ','
           << settings.matchVertexCount << ',' << settings.vertexCountMin << ','
           << settings.vertexCountMax << ',' << settings.matchInstanceCount << ','
           << settings.matchVertexBuffer << ',' << settings.instanceCountMin << ',' << settings.instanceCountMax << ',' << settings.spatialFilterEnabled;
        for (const auto& region : settings.spatialRegions)
            ss << ',' << region.minX << ',' << region.minY << ',' << region.maxX << ',' << region.maxY;
        for (const auto& range : settings.indexCountRanges)
            ss << '|' << range.min << ',' << range.max;
        for (const auto identifier : settings.vertexBufferFilters)
            ss << "|VB" << identifier;
        ss << ';';
    }

    appendStringSet(_preferredTechniques);

    std::vector<std::string> variableNames;
    variableNames.reserve(_varOffsetMapping.size());
    for (const auto& [name, _] : _varOffsetMapping)
        variableNames.push_back(name);
    std::sort(variableNames.begin(), variableNames.end());
    for (const auto& name : variableNames) {
        const auto& [offset, usePrevious] = _varOffsetMapping.at(name);
        ss << name.size() << ':' << name << ':' << offset << ':' << usePrevious << ',';
    }

    return ss.str();
}

void ToggleGroup::AssignPreferredTechniqueData(std::unordered_map<std::string, EffectData>& allTechniques) {
    _preferredTechniqueData.clear();

    for (auto& techName : _preferredTechniques) {
        const auto& techData = allTechniques.find(techName);
        if (techData != allTechniques.end()) {
            _preferredTechniqueData.emplace(&techData->second);
        }
    }
}

const std::unordered_set<EffectData*>& ToggleGroup::GetPreferredTechniqueData() {
    return _preferredTechniqueData;
}

int ToggleGroup::getNewGroupId() {
    static atomic_int s_groupId = 0;

    ++s_groupId;
    return s_groupId;
}

GroupResource& ToggleGroup::GetGroupResource(GroupResourceType type) {
    return _group_buffers[static_cast<uint32_t>(type)];
}

void ToggleGroup::storeCollectedHashes(const unordered_set<uint32_t> pixelShaderHashes,
                                       const unordered_set<uint32_t> vertexShaderHashes,
                                       const unordered_set<uint32_t> computeShaderHashes) {
    if (_pixelShaderHashes == pixelShaderHashes &&
        _vertexShaderHashes == vertexShaderHashes &&
        _computeShaderHashes == computeShaderHashes)
        return;

    _pixelShaderHashes = pixelShaderHashes;
    _vertexShaderHashes = vertexShaderHashes;
    _computeShaderHashes = computeShaderHashes;
    for (auto it = _shaderDrawSettings.begin(); it != _shaderDrawSettings.end();) {
        const uint32_t stage = static_cast<uint32_t>(it->first >> 32);
        const uint32_t hash = static_cast<uint32_t>(it->first & 0xFFFFFFFFu);
        const bool present = stage == 0 ? _pixelShaderHashes.contains(hash) :
                             stage == 1 ? _vertexShaderHashes.contains(hash) :
                             stage == 2 ? _computeShaderHashes.contains(hash) : false;
        it = present ? std::next(it) : _shaderDrawSettings.erase(it);
    }
    RebuildShaderDrawSnapshot();
    markConfigDirty();
}

bool ToggleGroup::isBlockedVertexShader(uint32_t shaderHash) const {
    return _isActive && (_vertexShaderHashes.contains(shaderHash));
}

bool ToggleGroup::isBlockedPixelShader(uint32_t shaderHash) const {
    return _isActive && (_pixelShaderHashes.contains(shaderHash));
}

bool ToggleGroup::isBlockedComputeShader(uint32_t shaderHash) const {
    return _isActive && (_computeShaderHashes.contains(shaderHash));
}

void ToggleGroup::clearHashes() {
    _pixelShaderHashes.clear();
    _vertexShaderHashes.clear();
    _computeShaderHashes.clear();
    _shaderDrawSettings.clear();
    RebuildShaderDrawSnapshot();
}

void ToggleGroup::setName(string newName) {
    if (newName.empty() || _name == newName)
        return;
    _name = std::move(newName);
    markConfigDirty();
}

bool ToggleGroup::SetVarMapping(uintptr_t offset, string& variable, bool prev) {
    const auto [it, inserted] = _varOffsetMapping.insert_or_assign(variable, make_tuple(offset, prev));
    markConfigDirty();
    return true;
}

bool ToggleGroup::RemoveVarMapping(string& variable) {
    if (_varOffsetMapping.erase(variable) > 0)
        markConfigDirty();
    return true;
}

void ToggleGroup::saveState(CDataFile& iniFile, int groupCounter) const {
    const string sectionRoot = "Group" + std::to_string(groupCounter);
    const string vertexHashesCategory = sectionRoot + "_VertexShaders";
    const string pixelHashesCategory = sectionRoot + "_PixelShaders";
    const string computeHashesCategory = sectionRoot + "_ComputeShaders";
    const string constantsCategory = sectionRoot + "_Constants";

    int counter = 0;
    for (const auto hash : _vertexShaderHashes) {
        iniFile.SetUInt("ShaderHash" + std::to_string(counter), hash, "", vertexHashesCategory);
        counter++;
    }
    iniFile.SetUInt("AmountHashes", counter, "", vertexHashesCategory);

    counter = 0;
    for (const auto hash : _pixelShaderHashes) {
        iniFile.SetUInt("ShaderHash" + std::to_string(counter), hash, "", pixelHashesCategory);
        counter++;
    }
    iniFile.SetUInt("AmountHashes", counter, "", pixelHashesCategory);

    counter = 0;
    for (const auto hash : _computeShaderHashes) {
        iniFile.SetUInt("ShaderHash" + std::to_string(counter), hash, "", computeHashesCategory);
        counter++;
    }
    iniFile.SetUInt("AmountHashes", counter, "", computeHashesCategory);

    counter = 0;
    for (const auto& [varName, varData] : _varOffsetMapping) {
        const auto& [varOffset, varUsePref] = varData;
        iniFile.SetUInt("Offset" + std::to_string(counter), static_cast<uint32_t>(varOffset), "", constantsCategory);
        iniFile.SetValue("Variable" + std::to_string(counter), varName, "", constantsCategory);
        iniFile.SetBool("UsePreviousValue" + std::to_string(counter), varUsePref, "", constantsCategory);
        counter++;
    }
    iniFile.SetUInt("AmountConstants", counter, "", constantsCategory);

    iniFile.SetValue("Name", _name, "", sectionRoot);
    iniFile.SetUInt("ToggleKey", _keybind, "", sectionRoot);
    iniFile.SetBool("Active", _isActive, "", sectionRoot);

    stringstream ss("");
    bool firstElement = true;
    for (const auto& el : _preferredTechniques) {
        if (!firstElement) {
            ss << ",";
        }

        ss << el;

        firstElement = false;
    }
    iniFile.SetBool("RenderToSRVs", _renderToResourceViews, "", sectionRoot);
    iniFile.SetBool("AutoRenderSRV", _autoRenderSRV, "", sectionRoot);
    iniFile.SetBool("HideMarkedShaders", _hideMarkedShaders, "", sectionRoot);
    iniFile.SetUInt("RenderSRVPipelineSlot", _renderSrvSlotIndex, "", sectionRoot);
    iniFile.SetUInt("RenderSRVDescriptorIndex", _renderSrvDescIndex, "", sectionRoot);
    iniFile.SetUInt("RenderSRVShaderStage", _renderSrvShaderStage, "", sectionRoot);
    iniFile.SetUInt("RenderTargetIndex", _rtIndex, "", sectionRoot);
    iniFile.SetUInt("InvocationLocation", _invocationLocation, "", sectionRoot);
    iniFile.SetUInt("MatchSwapchainResolutionOnly", _matchSwapchainResolution, "", sectionRoot);
    iniFile.SetBool("RequeueAfterRTMatchingFailure", _requeueAfterRTMatchingFailure, "", sectionRoot);

    iniFile.SetValue("Techniques", ss.str(), "", sectionRoot);
    iniFile.SetBool("AllowAllTechniques", _allowAllTechniques, "", sectionRoot);
    iniFile.SetBool("TechniqueExceptions", _hasTechniqueExceptions, "", sectionRoot);

    iniFile.SetBool("ProvideTextureBinding", _isProvidingTextureBinding, "", sectionRoot);
    iniFile.SetValue("TextureBindingName", _textureBindingName, "", sectionRoot);
    iniFile.SetBool("ClearTextureBindings", _clearBindings, "", sectionRoot);
    iniFile.SetBool("CopyTextureBinding", _copyTextureBinding, "", sectionRoot);
    iniFile.SetBool("FlipBufferBinding", _flipBufferBinding, "", sectionRoot);

    iniFile.SetBool("ExtractConstants", _extractConstants, "", sectionRoot);
    iniFile.SetUInt("ConstantPipelineSlot", _cbSlotIndex, "", sectionRoot);
    iniFile.SetUInt("ConstantDescriptorIndex", _cbDescIndex, "", sectionRoot);
    iniFile.SetBool("ConstantPushMode", _cbModePush, "", sectionRoot);
    iniFile.SetUInt("ConstantShaderStage", _cbShaderStage, "", sectionRoot);

    iniFile.SetBool("ExtractSRVs", _extractResourceViews, "", sectionRoot);
    iniFile.SetUInt("SRVPipelineSlot", _bindingSrvSlotIndex, "", sectionRoot);
    iniFile.SetUInt("SRVDescriptorIndex", _bindingSrvDescIndex, "", sectionRoot);
    iniFile.SetUInt("SRVShaderStage", _bindingSrvShaderStage, "", sectionRoot);
    iniFile.SetUInt("BindingRenderTargetIndex", _bindingRTIndex, "", sectionRoot);
    iniFile.SetUInt("BindingInvocationLocation", _bindingInvocationLocation, "", sectionRoot);
    iniFile.SetUInt("BindingMatchSwapchainResolutionOnly", _bindingMatchSwapchainResolution, "", sectionRoot);
    iniFile.SetBool("ClearPreviewAlpha", _previewClearAlpha, "", sectionRoot);
    iniFile.SetBool("TonemapHDRtoSDRtoHDR", _tonemapHDRtoSDRtoHDR, "", sectionRoot);
    iniFile.SetBool("PreserveTargetAlphaChannel", _preserveAlpha, "", sectionRoot);
    iniFile.SetBool("FlipBuffer", _flipBuffer, "", sectionRoot);

    // Per-shader draw suppression, keyed by (stage << 32 | hash). Only non-default entries are
    // written so a configuration without suppression stays as small as before.
    const string drawSettingsCategory = sectionRoot + "_ShaderDrawSettings";
    int drawSettingIndex = 0;
    for (const auto& [key, settings] : _shaderDrawSettings) {
        if (settings.isDefault())
            continue;

        const string prefix = "Entry" + std::to_string(drawSettingIndex);
        iniFile.SetUInt(prefix + "Stage", static_cast<uint32_t>(key >> 32), "", drawSettingsCategory);
        iniFile.SetUInt(prefix + "Hash", static_cast<uint32_t>(key & 0xFFFFFFFFu), "", drawSettingsCategory);
        iniFile.SetBool(prefix + "Suppress", settings.suppressDrawCall, "", drawSettingsCategory);
        iniFile.SetBool(prefix + "MatchIndex", settings.matchIndexCount, "", drawSettingsCategory);
        // Older builds can read the first range. Use an empty interval if the
        // legacy format cannot represent it without broadening the match.
        const IndexCountRange first = settings.indexCountRanges.empty() ? IndexCountRange{} : settings.indexCountRanges.front();
        const bool legacyEmpty = first.max == 0 || first.min == UINT32_MAX;
        iniFile.SetUInt(prefix + "IndexMin", legacyEmpty ? 2 : first.min, "", drawSettingsCategory);
        iniFile.SetUInt(prefix + "IndexMax", legacyEmpty ? 1 : first.max == UINT32_MAX ? 0 : first.max, "", drawSettingsCategory);
        iniFile.SetUInt(prefix + "IndexRangeCount", static_cast<uint32_t>(settings.indexCountRanges.size()), "", drawSettingsCategory);
        for (size_t i = 0; i < settings.indexCountRanges.size(); ++i) {
            const string rangePrefix = prefix + "IndexRange" + std::to_string(i);
            iniFile.SetUInt(rangePrefix + "Min", settings.indexCountRanges[i].min, "", drawSettingsCategory);
            iniFile.SetUInt(rangePrefix + "Max", settings.indexCountRanges[i].max, "", drawSettingsCategory);
        }
        iniFile.SetBool(prefix + "MatchVertexBuffer", settings.matchVertexBuffer, "", drawSettingsCategory);
        iniFile.SetUInt(prefix + "VertexBufferCount", static_cast<uint32_t>(settings.vertexBufferFilters.size()), "", drawSettingsCategory);
        for (size_t i = 0; i < settings.vertexBufferFilters.size(); ++i)
            iniFile.SetValue(prefix + "VertexBuffer" + std::to_string(i), std::to_string(settings.vertexBufferFilters[i]), "", drawSettingsCategory);
        iniFile.SetBool(prefix + "MatchVertex", settings.matchVertexCount, "", drawSettingsCategory);
        iniFile.SetUInt(prefix + "VertexMin", settings.vertexCountMin, "", drawSettingsCategory);
        iniFile.SetUInt(prefix + "VertexMax", settings.vertexCountMax, "", drawSettingsCategory);
        iniFile.SetBool(prefix + "MatchInstance", settings.matchInstanceCount, "", drawSettingsCategory);
        iniFile.SetUInt(prefix + "InstanceMin", settings.instanceCountMin, "", drawSettingsCategory);
        iniFile.SetUInt(prefix + "InstanceMax", settings.instanceCountMax, "", drawSettingsCategory);
        iniFile.SetBool(prefix + "SpatialFilter", settings.spatialFilterEnabled, "", drawSettingsCategory);
        const uint32_t regionCount = static_cast<uint32_t>(std::min<size_t>(settings.spatialRegions.size(), 64));
        iniFile.SetUInt(prefix + "RegionCount", regionCount, "", drawSettingsCategory);
        for (uint32_t regionIndex = 0; regionIndex < regionCount; ++regionIndex) {
            const ScreenRegion& region = settings.spatialRegions[regionIndex];
            const std::string regionPrefix = prefix + "Region" + std::to_string(regionIndex);
            iniFile.SetValue(regionPrefix + "MinX", std::format("{:.9g}", region.minX), "", drawSettingsCategory);
            iniFile.SetValue(regionPrefix + "MinY", std::format("{:.9g}", region.minY), "", drawSettingsCategory);
            iniFile.SetValue(regionPrefix + "MaxX", std::format("{:.9g}", region.maxX), "", drawSettingsCategory);
            iniFile.SetValue(regionPrefix + "MaxY", std::format("{:.9g}", region.maxY), "", drawSettingsCategory);
        }
        ++drawSettingIndex;
    }
    if (drawSettingIndex != 0)
        iniFile.SetUInt("Amount", drawSettingIndex, "", drawSettingsCategory);
}

static std::vector<std::string> split(std::string& str, char delimiter) {
    std::vector<std::string> ret;

    if (str.size() > 0) {
        stringstream ss(str);

        while (ss.good()) {
            string substr;
            getline(ss, substr, delimiter);

            ret.push_back(substr);
        }
    }

    return ret;
}

void ToggleGroup::loadState(CDataFile& iniFile, int groupCounter) {
    if (groupCounter < 0) {
        int amount = iniFile.GetInt("AmountHashes", "PixelShaders");
        for (int i = 0; i < amount; i++) {
            uint32_t hash = iniFile.GetUInt("ShaderHash" + std::to_string(i), "PixelShaders");
            if (hash != UINT_MAX) {
                _pixelShaderHashes.emplace(hash);
            }
        }
        amount = iniFile.GetInt("AmountHashes", "VertexShaders");
        for (int i = 0; i < amount; i++) {
            uint32_t hash = iniFile.GetUInt("ShaderHash" + std::to_string(i), "VertexShaders");
            if (hash != UINT_MAX) {
                _vertexShaderHashes.emplace(hash);
            }
        }
        amount = iniFile.GetInt("AmountHashes", "ComputeShaders");
        for (int i = 0; i < amount; i++) {
            uint32_t hash = iniFile.GetUInt("ShaderHash" + std::to_string(i), "ComputeShaders");
            if (hash != UINT_MAX) {
                _computeShaderHashes.emplace(hash);
            }
        }

        // done
        return;
    }

    const string sectionRoot = "Group" + std::to_string(groupCounter);
    const string vertexHashesCategory = sectionRoot + "_VertexShaders";
    const string pixelHashesCategory = sectionRoot + "_PixelShaders";
    const string computeHashesCategory = sectionRoot + "_ComputeShaders";
    const string constantsCategory = sectionRoot + "_Constants";

    int amountShaders = iniFile.GetInt("AmountHashes", vertexHashesCategory);
    for (int i = 0; i < amountShaders; i++) {
        uint32_t hash = iniFile.GetUInt("ShaderHash" + std::to_string(i), vertexHashesCategory);
        if (hash != UINT_MAX) {
            _vertexShaderHashes.emplace(hash);
        }
    }

    amountShaders = iniFile.GetInt("AmountHashes", pixelHashesCategory);
    for (int i = 0; i < amountShaders; i++) {
        uint32_t hash = iniFile.GetUInt("ShaderHash" + std::to_string(i), pixelHashesCategory);
        if (hash != UINT_MAX) {
            _pixelShaderHashes.emplace(hash);
        }
    }

    amountShaders = iniFile.GetInt("AmountHashes", computeHashesCategory);
    for (int i = 0; i < amountShaders; i++) {
        uint32_t hash = iniFile.GetUInt("ShaderHash" + std::to_string(i), computeHashesCategory);
        if (hash != UINT_MAX) {
            _computeShaderHashes.emplace(hash);
        }
    }

    int amountConstants = iniFile.GetInt("AmountConstants", constantsCategory);
    for (int i = 0; i < amountConstants; i++) {
        uint32_t offset = iniFile.GetUInt("Offset" + std::to_string(i), constantsCategory);
        string varName = iniFile.GetString("Variable" + std::to_string(i), constantsCategory);
        bool prevValue = iniFile.GetBool("UsePreviousValue" + std::to_string(i), constantsCategory);
        if (offset != UINT_MAX && varName.size() > 0) {
            _varOffsetMapping.emplace(varName, make_tuple(offset, prevValue));
        }
    }

    _name = iniFile.GetValue("Name", sectionRoot);
    if (_name.size() <= 0) {
        _name = "Default";
    }
    const uint32_t toggleKeyValue = iniFile.GetUInt("ToggleKey", sectionRoot);
    if (toggleKeyValue == UINT_MAX) {
        _keybind = VK_CAPITAL;
    } else {
        _keybind = toggleKeyValue;
    }

    _isActive = iniFile.GetBool("Active", sectionRoot);

    const int32_t invocationLocation = iniFile.GetUInt("InvocationLocation", sectionRoot);
    if (invocationLocation != UINT_MAX) {
        _invocationLocation = invocationLocation;
    } else {
        _invocationLocation = 0;
    }

    _matchSwapchainResolution = iniFile.GetUInt("MatchSwapchainResolutionOnly", sectionRoot);
    if (_matchSwapchainResolution == UINT_MAX) {
        _matchSwapchainResolution = SWAPCHAIN_MATCH_MODE_RESOLUTION; // fallback to effect invocation location when loading old configs
    }

    _requeueAfterRTMatchingFailure = iniFile.GetBoolOrDefault("RequeueAfterRTMatchingFailure", sectionRoot, false);

    string techniques = iniFile.GetString("Techniques", sectionRoot);

    vector<string> splitTech = split(techniques, ',');
    if (splitTech.size() > 0) {
        for (auto& tech : splitTech) {
            _preferredTechniques.emplace(tech);
        }
    }

    _allowAllTechniques = iniFile.GetBool("AllowAllTechniques", sectionRoot);
    _hasTechniqueExceptions = iniFile.GetBool("TechniqueExceptions", sectionRoot);

    _isProvidingTextureBinding = iniFile.GetBool("ProvideTextureBinding", sectionRoot);
    _clearBindings = iniFile.GetBoolOrDefault("ClearTextureBindings", sectionRoot, true);
    _textureBindingName = iniFile.GetString("TextureBindingName", sectionRoot);
    _copyTextureBinding = iniFile.GetBoolOrDefault("CopyTextureBinding", sectionRoot, true);

    _extractConstants = iniFile.GetBool("ExtractConstants", sectionRoot);

    uint32_t slotIndex = iniFile.GetUInt("ConstantPipelineSlot", sectionRoot);
    if (slotIndex != UINT_MAX) {
        _cbSlotIndex = slotIndex;
    } else {
        _cbSlotIndex = 2;
    }

    uint32_t descIndex = iniFile.GetUInt("ConstantDescriptorIndex", sectionRoot);
    if (descIndex != UINT_MAX) {
        _cbDescIndex = descIndex;
    } else {
        _cbDescIndex = 0;
    }

    bool _cbPushMode = iniFile.GetBoolOrDefault("ConstantPushMode", sectionRoot, false);

    uint32_t shaderStage = iniFile.GetUInt("ConstantShaderStage", sectionRoot);
    if (shaderStage != UINT_MAX && shaderStage < 3) {
        _cbShaderStage = shaderStage;
    } else {
        _cbShaderStage = 0;
    }

    _extractResourceViews = iniFile.GetBool("ExtractSRVs", sectionRoot);

    uint32_t srvSlotIndex = iniFile.GetUInt("SRVPipelineSlot", sectionRoot);
    if (srvSlotIndex != UINT_MAX) {
        _bindingSrvSlotIndex = srvSlotIndex;
    } else {
        _bindingSrvSlotIndex = 1;
    }

    uint32_t srvDescIndex = iniFile.GetUInt("SRVDescriptorIndex", sectionRoot);
    if (srvDescIndex != UINT_MAX) {
        _bindingSrvDescIndex = srvDescIndex;
    } else {
        _bindingSrvDescIndex = 0;
    }

    uint32_t srvShaderStage = iniFile.GetUInt("SRVShaderStage", sectionRoot);
    if (srvShaderStage != UINT_MAX && srvShaderStage < 3) {
        _bindingSrvShaderStage = srvShaderStage;
    } else {
        _bindingSrvShaderStage = 0;
    }

    _renderToResourceViews = iniFile.GetBoolOrDefault("RenderToSRVs", sectionRoot, false);
    _autoRenderSRV = iniFile.GetBoolOrDefault("AutoRenderSRV", sectionRoot, false);
    _hideMarkedShaders = iniFile.GetBoolOrDefault("HideMarkedShaders", sectionRoot, false);

    uint32_t renderSrvSlotIndex = iniFile.GetUInt("RenderSRVPipelineSlot", sectionRoot);
    if (renderSrvSlotIndex != UINT_MAX) {
        _renderSrvSlotIndex = renderSrvSlotIndex;
    } else {
        _renderSrvSlotIndex = 1;
    }

    uint32_t renderSrvDescIndex = iniFile.GetUInt("RenderSRVDescriptorIndex", sectionRoot);
    if (renderSrvDescIndex != UINT_MAX) {
        _renderSrvDescIndex = renderSrvDescIndex;
    } else {
        _renderSrvDescIndex = 0;
    }

    uint32_t renderSrvShaderStage = iniFile.GetUInt("RenderSRVShaderStage", sectionRoot);
    if (renderSrvShaderStage != UINT_MAX && renderSrvShaderStage < 3) {
        _renderSrvShaderStage = renderSrvShaderStage;
    } else {
        _renderSrvShaderStage = 0;
    }

    uint32_t rtvIndex = iniFile.GetUInt("RenderTargetIndex", sectionRoot);
    if (rtvIndex != UINT_MAX) {
        _rtIndex = rtvIndex;
    } else {
        _rtIndex = _cbDescIndex; // rt index and cb descriptor index were previously shared. Fallback for older configs
    }

    uint32_t bindingRTIndex = iniFile.GetUInt("BindingRenderTargetIndex", sectionRoot);
    if (bindingRTIndex != UINT_MAX) {
        _bindingRTIndex = bindingRTIndex;
    } else {
        _bindingRTIndex = _cbDescIndex; // rt index and cb descriptor index were previously shared. Fallback for older configs
    }

    uint32_t bindingInvocationLocation = iniFile.GetUInt("BindingInvocationLocation", sectionRoot);
    if (bindingInvocationLocation != UINT_MAX) {
        _bindingInvocationLocation = bindingInvocationLocation;
    } else {
        _bindingInvocationLocation = _invocationLocation; // fallback to effect invocation location when loading old configs
    }

    _bindingMatchSwapchainResolution = iniFile.GetUInt("BindingMatchSwapchainResolutionOnly", sectionRoot);
    if (_bindingMatchSwapchainResolution == UINT_MAX) {
        _bindingMatchSwapchainResolution = _matchSwapchainResolution; // fallback to effect invocation location when loading old configs
    }

    _previewClearAlpha = iniFile.GetBoolOrDefault("ClearPreviewAlpha", sectionRoot, true);

    _tonemapHDRtoSDRtoHDR = iniFile.GetBoolOrDefault("TonemapHDRtoSDRtoHDR", sectionRoot, false);

    _preserveAlpha = iniFile.GetBoolOrDefault("PreserveTargetAlphaChannel", sectionRoot, false);

    _flipBuffer = iniFile.GetBoolOrDefault("FlipBuffer", sectionRoot, false);

    _flipBufferBinding = iniFile.GetBoolOrDefault("FlipBufferBinding", sectionRoot, false);
    _shaderDrawSettings.clear();
    const string loadDrawSettingsCategory = sectionRoot + "_ShaderDrawSettings";
    const uint32_t drawSettingCount = iniFile.GetUInt("Amount", loadDrawSettingsCategory);
    if (drawSettingCount != UINT_MAX) {
        for (uint32_t i = 0; i < std::min(drawSettingCount, 65536u); ++i) {
            const string prefix = "Entry" + std::to_string(i);
            const uint32_t stage = iniFile.GetUInt(prefix + "Stage", loadDrawSettingsCategory);
            const uint32_t hash = iniFile.GetUInt(prefix + "Hash", loadDrawSettingsCategory);
            if (stage == UINT_MAX || stage > 2 || hash == UINT_MAX || hash == 0 || !(stage == 0 ? _pixelShaderHashes : stage == 1 ? _vertexShaderHashes : _computeShaderHashes).contains(hash))
                continue;

            auto readCount = [&iniFile, &loadDrawSettingsCategory](const string& key) {
                const uint32_t value = iniFile.GetUInt(key, loadDrawSettingsCategory);
                return value == UINT_MAX ? 0u : value;
            };

            ShaderDrawSettings settings;
            settings.suppressDrawCall = iniFile.GetBoolOrDefault(prefix + "Suppress", loadDrawSettingsCategory, false);
            settings.matchIndexCount = iniFile.GetBoolOrDefault(prefix + "MatchIndex", loadDrawSettingsCategory, false);
            const uint32_t indexRangeCount = iniFile.GetUInt(prefix + "IndexRangeCount", loadDrawSettingsCategory);
            if (indexRangeCount == UINT_MAX) {
                const uint32_t min = readCount(prefix + "IndexMin");
                const uint32_t max = readCount(prefix + "IndexMax");
                if (settings.matchIndexCount || min != 0 || max != 0)
                    settings.indexCountRanges.push_back({min, max == 0 ? UINT32_MAX : max});
            } else {
                auto readBound = [&](const string& key, uint32_t& value) {
                    const string text = iniFile.GetValue(key, loadDrawSettingsCategory);
                    const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
                    return result.ec == std::errc{} && result.ptr == text.data() + text.size();
                };
                for (uint32_t rangeIndex = 0; rangeIndex < indexRangeCount; ++rangeIndex) {
                    const string rangePrefix = prefix + "IndexRange" + std::to_string(rangeIndex);
                    IndexCountRange range;
                    if (!readBound(rangePrefix + "Min", range.min) || !readBound(rangePrefix + "Max", range.max))
                        break;
                    settings.indexCountRanges.push_back(range);
                }
            }
            settings.matchVertexBuffer = iniFile.GetBoolOrDefault(prefix + "MatchVertexBuffer", loadDrawSettingsCategory, false);
            const uint32_t vbCount = std::min(readCount(prefix + "VertexBufferCount"), 65536u);
            for (uint32_t vb = 0; vb < vbCount; ++vb) {
                const auto text = iniFile.GetString(prefix + "VertexBuffer" + std::to_string(vb), loadDrawSettingsCategory);
                uint64_t identifier = 0;
                const auto parsed = std::from_chars(text.data(), text.data() + text.size(), identifier);
                if (parsed.ec == std::errc{} && parsed.ptr == text.data() + text.size() && identifier != 0 &&
                    std::find(settings.vertexBufferFilters.begin(), settings.vertexBufferFilters.end(), identifier) == settings.vertexBufferFilters.end())
                    settings.vertexBufferFilters.push_back(identifier);
            }
            // Retired experimental filters must remain unmatched when loading their old configuration.
            const auto oldFingerprint = iniFile.GetString(prefix + "GeometryFingerprint", loadDrawSettingsCategory);
            if (iniFile.GetBoolOrDefault(prefix + "MatchVertexBinding", loadDrawSettingsCategory, false) ||
                iniFile.GetBoolOrDefault(prefix + "MatchIndexBinding", loadDrawSettingsCategory, false) ||
                (!oldFingerprint.empty() && oldFingerprint != "0"))
                settings.matchVertexBuffer = true;
            settings.matchVertexCount = iniFile.GetBoolOrDefault(prefix + "MatchVertex", loadDrawSettingsCategory, false);
            settings.vertexCountMin = readCount(prefix + "VertexMin");
            settings.vertexCountMax = readCount(prefix + "VertexMax");
            settings.matchInstanceCount = iniFile.GetBoolOrDefault(prefix + "MatchInstance", loadDrawSettingsCategory, false);
            settings.instanceCountMin = readCount(prefix + "InstanceMin");
            settings.instanceCountMax = readCount(prefix + "InstanceMax");
            settings.spatialFilterEnabled = iniFile.GetBoolOrDefault(prefix + "SpatialFilter", loadDrawSettingsCategory, false);

            auto readFloat = [&iniFile, &loadDrawSettingsCategory](const string& key, float fallback) {
                const float value = iniFile.GetFloat(key, loadDrawSettingsCategory);
                return value == FLT_MIN ? fallback : value;
            };
            const uint32_t regionCount = std::min(readCount(prefix + "RegionCount"), 64u);
            for (uint32_t regionIndex = 0; regionIndex < regionCount; ++regionIndex) {
                const std::string regionPrefix = prefix + "Region" + std::to_string(regionIndex);
                ScreenRegion region;
                region.minX = std::clamp(readFloat(regionPrefix + "MinX", 0.0f), 0.0f, 1.0f);
                region.minY = std::clamp(readFloat(regionPrefix + "MinY", 0.0f), 0.0f, 1.0f);
                region.maxX = std::clamp(readFloat(regionPrefix + "MaxX", 0.0f), 0.0f, 1.0f);
                region.maxY = std::clamp(readFloat(regionPrefix + "MaxY", 0.0f), 0.0f, 1.0f);
                addScreenRegion(settings.spatialRegions, region);
            }

            if (!settings.isDefault())
                _shaderDrawSettings[shaderDrawSettingsKey(stage, hash)] = settings;
        }
    }
    RebuildShaderDrawSnapshot();
}
}
