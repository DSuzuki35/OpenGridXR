#pragma once
#include <string>
#include <vector>
#include <stdexcept>
#include <cstring>
#include "openxr/openxr.h"
#include "openxr/openxr_loader_negotiation.h"

namespace lmuqv::autoload {
inline bool valid_backend(const std::wstring& b) { return b==L"QVFR"||b==L"VectorXR"||b==L"Native"||b==L"VRS"; }
inline bool keep_layer(const std::string& name,const std::string& backend) {
    if(name=="XR_APILAYER_LMU_quadview"||name=="XR_APILAYER_LMU_autostart"||name=="XR_APILAYER_MBUCCHIA_quad_views_foveated")return false;
    if(name=="XR_APILAYER_DIENERTECH_VECTORXR")return backend=="QVFR";
    if(name=="XR_APILAYER_OPENGRIDXR")return backend=="VRS";
    if(name=="XR_APILAYER_MBUCCHIA_toolkit")return backend!="VectorXR"&&backend!="VRS";
    return true;
}
inline std::wstring quote(const std::wstring& value) {
    std::wstring result=L"\"";size_t slashes=0;
    for(auto c:value) {
        if(c==L'\\'){++slashes;continue;}
        result.append(c==L'"'?slashes*2+1:slashes,L'\\');result+=c;slashes=0;
    }
    result.append(slashes*2,L'\\');return result+L'"';
}
struct Entry {std::string name;PFN_xrGetInstanceProcAddr get;PFN_xrCreateApiLayerInstance create;};
struct Chain {
    std::vector<Entry> entries;
    PFN_xrGetInstanceProcAddr terminalGet=nullptr;
    PFN_xrCreateApiLayerInstance terminalCreate=nullptr;
    std::vector<XrApiLayerNextInfo> nodes;
    void read(const XrApiLayerNextInfo* head,const std::string& backend) {
        if(!head)throw std::runtime_error("Missing OpenXR layer chain");
        const auto* previous=head;size_t count=0;
        for(auto* node=head->next;node;node=node->next) {
            if(++count>64||node->structType!=XR_LOADER_INTERFACE_STRUCT_API_LAYER_NEXT_INFO||
               node->structVersion!=XR_API_LAYER_NEXT_INFO_STRUCT_VERSION||node->structSize!=sizeof(*node)||
               !memchr(node->layerName,0,sizeof(node->layerName))||!previous->nextGetInstanceProcAddr||!previous->nextCreateApiLayerInstance)
                throw std::runtime_error("Invalid OpenXR layer chain");
            if(keep_layer(node->layerName,backend))entries.push_back({node->layerName,previous->nextGetInstanceProcAddr,previous->nextCreateApiLayerInstance});
            previous=node;
        }
        terminalGet=previous->nextGetInstanceProcAddr;terminalCreate=previous->nextCreateApiLayerInstance;
        if(!terminalGet||!terminalCreate)throw std::runtime_error("Missing OpenXR runtime entry point");
    }
    void link() {
        nodes.resize(entries.size());
        for(size_t i=0;i<nodes.size();++i) {
            auto& node=nodes[i];node={};node.structType=XR_LOADER_INTERFACE_STRUCT_API_LAYER_NEXT_INFO;
            node.structVersion=XR_API_LAYER_NEXT_INFO_STRUCT_VERSION;node.structSize=sizeof(node);
            if(entries[i].name.size()>=sizeof(node.layerName))throw std::runtime_error("Invalid layer name");
            memcpy(node.layerName,entries[i].name.c_str(),entries[i].name.size()+1);
            node.nextGetInstanceProcAddr=i+1<nodes.size()?entries[i+1].get:terminalGet;
            node.nextCreateApiLayerInstance=i+1<nodes.size()?entries[i+1].create:terminalCreate;
            node.next=i+1<nodes.size()?&nodes[i+1]:nullptr;
        }
    }
    void complete(const Entry& adapter,const Entry* provider=nullptr) {
        // Retained implicit overlays see the original app request, matching
        // the explicit launch path. In particular VectorXR must not intercept
        // the quad extensions the LMU adapter adds for the selected provider.
        entries.push_back(adapter);
        if(provider)entries.push_back(*provider);
        link();
    }
};
}
